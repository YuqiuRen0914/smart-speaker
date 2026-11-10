#include "diagnostics/function_probe.h"
#include "st7789_3wire_panel.h"
#include <climits>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_interface.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/spi_ll.h>

namespace lunaredu::device {
namespace {

constexpr const char *kTag = "st7789_3wire";
constexpr uint8_t kCmdSoftwareReset = 0x01;
constexpr uint8_t kCmdSleepIn = 0x10;
constexpr uint8_t kCmdSleepOut = 0x11;
constexpr uint8_t kCmdNormalOn = 0x13;
constexpr uint8_t kCmdInversionOff = 0x20;
constexpr uint8_t kCmdInversionOn = 0x21;
constexpr uint8_t kCmdDisplayOff = 0x28;
constexpr uint8_t kCmdDisplayOn = 0x29;
constexpr uint8_t kCmdColumnAddress = 0x2A;
constexpr uint8_t kCmdRowAddress = 0x2B;
constexpr uint8_t kCmdMemoryWrite = 0x2C;
constexpr uint8_t kCmdMemoryAccess = 0x36;
constexpr uint8_t kCmdPixelFormat = 0x3A;

constexpr uint8_t kMadctlMirrorY = 0x80;
constexpr uint8_t kMadctlMirrorX = 0x40;
constexpr uint8_t kMadctlSwapXy = 0x20;
constexpr uint8_t kMadctlBgr = 0x08;

// Configurable bounded DMA chunks leave SRAM available for TLS and the
// Workbench HTTP task while amortizing SPI transaction setup.
constexpr size_t kRawChunkBytes = kSt7789ThreeWireRawChunkBytes;
constexpr size_t kPackedChunkBytes = kSt7789ThreeWirePackedChunkBytes;
static_assert(kRawChunkBytes % 8 == 0);
// Each full chunk ends on a 9-bit word and a PSRAM DMA cache line. Two chunks
// are enough to overlap packing with the previous SPI transfer.
static_assert(kPackedChunkBytes % 9 == 0 && kPackedChunkBytes % 64 == 0);
static_assert(kPackedChunkBytes * 8 <= SPI_LL_DMA_MAX_BIT_LEN);

struct St7789ThreeWirePanel {
  esp_lcd_panel_t base = {};
  spi_device_handle_t spi = nullptr;
  uint8_t *packed = nullptr;
  uint8_t *next_packed = nullptr;
  uint8_t *psram_packed = nullptr;
  DeviceSpiLcdThreeWireDmaMode dma_mode =
      DeviceSpiLcdThreeWireDmaMode::InternalQueued;
  int reset_gpio = -1;
  int x_gap = 0;
  int y_gap = 0;
  bool mirror_x = false;
  bool mirror_y = false;
  bool swap_xy = false;
  bool rgb_order_bgr = false;
  uint64_t frame_pack_us = 0;
  uint64_t frame_spi_us = 0;
  uint64_t frame_draw_us = 0;
  uint64_t frame_submit_us = 0,frame_recycle_us = 0,frame_drain_us = 0,frame_window_us = 0;
  uint64_t frame_wire_bytes = 0;
  uint32_t frame_chunks = 0,frame_poll_chunks = 0,frame_max_draw_us = 0;
  uint64_t frame_pixels = 0;
  uint32_t frame_count = 0;
  int64_t report_started_us = 0;
};

St7789ThreeWirePanel *to_panel(esp_lcd_panel_t *base) {
  return reinterpret_cast<St7789ThreeWirePanel *>(base);
}

bool valid_gpio(int gpio) { return gpio >= 0 && gpio < GPIO_NUM_MAX; }

template<bool SwapRgb565=false,bool Data=true>
size_t pack_9bit(const uint8_t *source, size_t source_size,
                 uint8_t *target) {
  const size_t packed_bytes = (source_size * 9 + 7) / 8;

  constexpr uint8_t dc = Data ? 1 : 0;
  size_t source_index = 0;
  size_t target_index = 0;

  // Eight input bytes become exactly nine output bytes. Keeping this hot path
  // byte based is substantially cheaper than setting every individual bit.
  while (source_index + 8 <= source_size) {

    const uint8_t b0 = source[source_index + (0 ^ unsigned(SwapRgb565))];
    const uint8_t b1 = source[source_index + (1 ^ unsigned(SwapRgb565))];
    const uint8_t b2 = source[source_index + (2 ^ unsigned(SwapRgb565))];
    const uint8_t b3 = source[source_index + (3 ^ unsigned(SwapRgb565))];
    const uint8_t b4 = source[source_index + (4 ^ unsigned(SwapRgb565))];
    const uint8_t b5 = source[source_index + (5 ^ unsigned(SwapRgb565))];
    const uint8_t b6 = source[source_index + (6 ^ unsigned(SwapRgb565))];
    const uint8_t b7 = source[source_index + (7 ^ unsigned(SwapRgb565))];
    target[target_index + 0] = static_cast<uint8_t>((dc << 7) | (b0 >> 1));
    target[target_index + 1] = static_cast<uint8_t>(
        ((b0 & 0x01) << 7) | (dc << 6) | (b1 >> 2));
    target[target_index + 2] = static_cast<uint8_t>(
        ((b1 & 0x03) << 6) | (dc << 5) | (b2 >> 3));
    target[target_index + 3] = static_cast<uint8_t>(
        ((b2 & 0x07) << 5) | (dc << 4) | (b3 >> 4));
    target[target_index + 4] = static_cast<uint8_t>(
        ((b3 & 0x0F) << 4) | (dc << 3) | (b4 >> 5));
    target[target_index + 5] = static_cast<uint8_t>(
        ((b4 & 0x1F) << 3) | (dc << 2) | (b5 >> 6));
    target[target_index + 6] = static_cast<uint8_t>(
        ((b5 & 0x3F) << 2) | (dc << 1) | (b6 >> 7));
    target[target_index + 7] =
        static_cast<uint8_t>(((b6 & 0x7F) << 1) | dc);
    target[target_index + 8] = b7;
    source_index += 8;
    target_index += 9;
  }

  // The eight-byte fast path overwrites every output byte. Only the final
  // partial group needs clearing before its bitwise OR writes.
  if (target_index < packed_bytes) {
    std::memset(target + target_index, 0, packed_bytes - target_index);
  }
  size_t target_bit = target_index * 8;
  for (; source_index < source_size; ++source_index) {
    const uint16_t word = static_cast<uint16_t>(
        (Data ? 0x100U : 0x000U) | source[source_index ^ unsigned(SwapRgb565)]);
    for (int bit = 8; bit >= 0; --bit) {
      if ((word & (1U << bit)) != 0) {
        target[target_bit / 8] |=
            static_cast<uint8_t>(0x80U >> (target_bit % 8));
      }
      ++target_bit;
    }
  }

  return packed_bytes;
}

esp_err_t write_9bit(St7789ThreeWirePanel *panel, const uint8_t *source,
                     size_t source_size, bool data) {
  if (source == nullptr || source_size == 0) {
    return ESP_OK;
  }

  // Commands stay synchronous. Pixel transfers can pack the next DMA chunk
  // while the SPI controller transmits the previous one.
  const bool psram_dma =
      panel->dma_mode == DeviceSpiLcdThreeWireDmaMode::PsramRegion;
  const bool queued_dma =
      panel->dma_mode == DeviceSpiLcdThreeWireDmaMode::InternalQueued;
  if (data && source_size > kRawChunkBytes &&
      ((psram_dma && panel->psram_packed != nullptr) ||
       (queued_dma && panel->next_packed != nullptr))) {
    uint8_t *buffers[2] = {
        psram_dma ? panel->psram_packed : panel->packed,
        psram_dma ? panel->psram_packed + kPackedChunkBytes
                  : panel->next_packed};
    spi_transaction_t transactions[2] = {};
    bool pending[2] = {false, false};
    size_t offset = 0;
    unsigned slot = 0;
    esp_err_t result = ESP_OK;
    // PSRAM DMA requires a cache-line aligned length. Send its last partial
    // chunk from the existing internal buffer after the queued transfers.
    const size_t queued_size = psram_dma
        ? source_size / kRawChunkBytes * kRawChunkBytes : source_size;
    while (offset < queued_size) {
      if (pending[slot]) {
        spi_transaction_t *completed = nullptr;
        const int64_t wait_started_us = esp_timer_get_time();
        {lunaredu::system::probe::Scope diagnostic_recycle(lunaredu::system::probe::Point::Recycle);
      result = spi_device_get_trans_result(panel->spi, &completed, portMAX_DELAY);}
        panel->frame_spi_us += esp_timer_get_time() - wait_started_us;
        if (completed == &transactions[slot]) {
          pending[slot] = false;
          if (result != ESP_OK ||
              (completed->flags & SPI_TRANS_DMA_TX_FAIL) != 0) {
            if (result == ESP_OK) result = ESP_ERR_INVALID_STATE;
            break;
          }
        } else {
          if (result == ESP_OK) result = ESP_FAIL;
          break;
        }
      }
      const size_t chunk = std::min(kRawChunkBytes, source_size - offset);
      const int64_t pack_started_us = esp_timer_get_time();
      const size_t packed_bytes = pack_9bit(source + offset,chunk,buffers[slot]);
      panel->frame_pack_us += esp_timer_get_time() - pack_started_us;
      if (packed_bytes > kPackedChunkBytes) {
        result = ESP_ERR_INVALID_SIZE;
        break;
      }
      auto &transaction = transactions[slot];
      transaction = {};
      // The panel discards padding bits when CS rises between chunks.
      transaction.length = packed_bytes * 8;
      transaction.tx_buffer = buffers[slot];
      if (psram_dma) {
        transaction.flags = SPI_TRANS_DMA_USE_PSRAM |
                            SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL;
      }
      {lunaredu::system::probe::Scope diagnostic_submit(lunaredu::system::probe::Point::Submit);
      result = spi_device_queue_trans(panel->spi, &transaction, portMAX_DELAY);}
      if (result != ESP_OK) break;
      pending[slot] = true;
      offset += chunk;
      slot ^= 1U;
    }
    // Complete every queued transfer before returning or sending a command.
    // This also keeps the stack descriptors and both DMA buffers owned by SPI
    // until the controller has finished reading them.
    for (unsigned turn = 0; turn < 2; ++turn) {
      // The next slot to be reused contains the oldest queued transfer.
      const unsigned index = (slot + turn) & 1U;
      if (!pending[index]) continue;
      spi_transaction_t *completed = nullptr;
      const int64_t wait_started_us = esp_timer_get_time();
      const esp_err_t wait_result =
          spi_device_get_trans_result(panel->spi, &completed, portMAX_DELAY);
      panel->frame_spi_us += esp_timer_get_time() - wait_started_us;
      if (completed == &transactions[index]) pending[index] = false;
      if (result == ESP_OK &&
          (wait_result != ESP_OK || completed != &transactions[index] ||
           (completed->flags & SPI_TRANS_DMA_TX_FAIL) != 0)) {
        result = wait_result != ESP_OK ? wait_result :
                 completed != &transactions[index] ? ESP_FAIL :
                 ESP_ERR_INVALID_STATE;
      }
    }
    if (result == ESP_OK && psram_dma && offset < source_size) {
      const size_t tail = source_size - offset;
      const int64_t pack_started_us = esp_timer_get_time();
      const size_t packed_bytes =
          pack_9bit(source + offset, tail, panel->packed);
      panel->frame_pack_us += esp_timer_get_time() - pack_started_us;
      spi_transaction_t transaction = {};
      transaction.length = packed_bytes * 8;
      transaction.tx_buffer = panel->packed;
      const int64_t wait_started_us = esp_timer_get_time();
      {lunaredu::system::probe::Scope diagnostic_poll(lunaredu::system::probe::Point::PixelPoll);
      result = spi_device_polling_transmit(panel->spi, &transaction);}
      panel->frame_spi_us += esp_timer_get_time() - wait_started_us;
    }
    return result;
  }

  size_t offset = 0;
  while (offset < source_size) {
    const size_t chunk =
        std::min(kRawChunkBytes, static_cast<size_t>(source_size - offset));
    const int64_t pack_started_us = esp_timer_get_time();
    const size_t packed_bytes =
        data?pack_9bit(source + offset,chunk,panel->packed):
             pack_9bit<false,false>(source + offset,chunk,panel->packed);
    if (source_size > 1024) {
      panel->frame_pack_us += esp_timer_get_time() - pack_started_us;
    }
    if (packed_bytes > kPackedChunkBytes) {
      return ESP_ERR_INVALID_SIZE;
    }

    spi_transaction_t transaction = {};
    // ESP32-S3's SPI master rejects non-byte-aligned transfer lengths. The
    // panel samples complete 9-bit words while CS is active and discards the
    // final padding bits when CS rises, matching the known-good board driver.
    transaction.length = packed_bytes * 8;
    transaction.tx_buffer = panel->packed;
    const int64_t spi_started_us = esp_timer_get_time();
    const esp_err_t result =
        spi_device_polling_transmit(panel->spi, &transaction);
    if (source_size > 1024) {
      panel->frame_spi_us += esp_timer_get_time() - spi_started_us;
    }
    if (result != ESP_OK) {
      return result;
    }
    offset += chunk;
  }
  return ESP_OK;
}

// Read a rectangle from a larger RGB565 canvas without first compacting it
// into another PSRAM framebuffer. A multiple-of-four pixel width keeps every
// source row and its 9-bit representation byte-aligned.
esp_err_t write_9bit_rows(St7789ThreeWirePanel *panel, const uint8_t *source,
                          size_t row_bytes, size_t stride_bytes, size_t rows,
                          bool native_rgb565=false) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::Transfer);
  if (row_bytes == 0 || row_bytes > kRawChunkBytes ||
      row_bytes % 8 != 0 || stride_bytes < row_bytes) {
    return ESP_ERR_INVALID_ARG;
  }
  const size_t rows_per_chunk = kRawChunkBytes / row_bytes;
  const size_t packed_row_bytes = row_bytes * 9 / 8;
  uint8_t *buffers[2] = {panel->packed, panel->next_packed};
  spi_transaction_t transactions[2] = {};
  bool pending[2] = {false, false};
  const bool queued = panel->dma_mode ==
          DeviceSpiLcdThreeWireDmaMode::InternalQueued &&
      panel->next_packed != nullptr;
  size_t row = 0;
  unsigned slot = 0;
  esp_err_t result = ESP_OK;
  while (row < rows) {
    if (queued && pending[slot]) {
      spi_transaction_t *completed = nullptr;
      const int64_t started = esp_timer_get_time();
      {lunaredu::system::probe::Scope diagnostic_recycle(lunaredu::system::probe::Point::Recycle);
      result = spi_device_get_trans_result(panel->spi, &completed, portMAX_DELAY);}
      const auto elapsed=esp_timer_get_time()-started;
      panel->frame_spi_us += elapsed;
      panel->frame_recycle_us += elapsed;
      if (result != ESP_OK || completed != &transactions[slot] ||
          (completed->flags & SPI_TRANS_DMA_TX_FAIL) != 0) {
        result = result == ESP_OK ? ESP_FAIL : result;
        break;
      }
      pending[slot] = false;
    }
    const size_t count = std::min(rows_per_chunk, rows - row);
    uint8_t *target = buffers[queued ? slot : 0];
    const int64_t pack_started = esp_timer_get_time();
    {lunaredu::system::probe::Scope diagnostic_pack(lunaredu::system::probe::Point::Pack);
    for (size_t index = 0; index < count; ++index) {
      const auto *input=source + (row + index) * stride_bytes;
      auto *output=target + index * packed_row_bytes;
      if(native_rgb565)pack_9bit<true>(input,row_bytes,output);
      else pack_9bit(input,row_bytes,output);
    }
    }
    panel->frame_pack_us += esp_timer_get_time() - pack_started;
    auto &transaction = transactions[slot];
    transaction = {};
    transaction.length = count * packed_row_bytes * 8;
    transaction.tx_buffer = target;
    if (queued) {
      const int64_t submit_started=esp_timer_get_time();
      {lunaredu::system::probe::Scope diagnostic_submit(lunaredu::system::probe::Point::Submit);
      result = spi_device_queue_trans(panel->spi, &transaction, portMAX_DELAY);}
      panel->frame_submit_us+=esp_timer_get_time()-submit_started;
      if (result != ESP_OK) break;
      pending[slot] = true;
      slot ^= 1U;
    } else {
      ++panel->frame_poll_chunks;
      const int64_t started = esp_timer_get_time();
      {lunaredu::system::probe::Scope diagnostic_poll(lunaredu::system::probe::Point::PixelPoll);
      result = spi_device_polling_transmit(panel->spi, &transaction);}
      panel->frame_spi_us += esp_timer_get_time() - started;
      if (result != ESP_OK) break;
    }
    ++panel->frame_chunks;
    panel->frame_wire_bytes+=count*packed_row_bytes;
    row += count;
  }
  if (queued) {
    for (unsigned turn = 0; turn < 2; ++turn) {
      const unsigned index = (slot + turn) & 1U;
      if (!pending[index]) continue;
      spi_transaction_t *completed = nullptr;
      const int64_t started = esp_timer_get_time();
      esp_err_t waited;
      {lunaredu::system::probe::Scope diagnostic_drain(lunaredu::system::probe::Point::Drain);
      waited = spi_device_get_trans_result(
          panel->spi, &completed, portMAX_DELAY); }
      const auto elapsed=esp_timer_get_time()-started;
      panel->frame_spi_us += elapsed;
      panel->frame_drain_us += elapsed;
      pending[index] = false;
      if (result == ESP_OK &&
          (waited != ESP_OK || completed != &transactions[index] ||
           (completed->flags & SPI_TRANS_DMA_TX_FAIL) != 0)) {
        result = waited == ESP_OK ? ESP_FAIL : waited;
      }
    }
  }
  return result;
}

esp_err_t send_command(St7789ThreeWirePanel *panel, uint8_t command,
                       const uint8_t *parameters = nullptr,
                       size_t parameter_count = 0) {
  esp_err_t result = write_9bit(panel, &command, 1, false);
  if (result == ESP_OK && parameters != nullptr && parameter_count > 0) {
    result = write_9bit(panel, parameters, parameter_count, true);
  }
  return result;
}

uint8_t madctl(const St7789ThreeWirePanel *panel) {
  uint8_t value = panel->rgb_order_bgr ? kMadctlBgr : 0;
  if (panel->mirror_x) {
    value |= kMadctlMirrorX;
  }
  if (panel->mirror_y) {
    value |= kMadctlMirrorY;
  }
  if (panel->swap_xy) {
    value |= kMadctlSwapXy;
  }
  return value;
}

esp_err_t apply_madctl(St7789ThreeWirePanel *panel) {
  const uint8_t value = madctl(panel);
  return send_command(panel, kCmdMemoryAccess, &value, 1);
}

esp_err_t set_window(St7789ThreeWirePanel *panel, int x_start, int y_start,
                     int x_end, int y_end) {
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::Window);
  x_start += panel->x_gap;
  x_end += panel->x_gap;
  y_start += panel->y_gap;
  y_end += panel->y_gap;

  const uint8_t columns[] = {
      static_cast<uint8_t>(x_start >> 8), static_cast<uint8_t>(x_start),
      static_cast<uint8_t>((x_end - 1) >> 8),
      static_cast<uint8_t>(x_end - 1),
  };
  esp_err_t result =
      send_command(panel, kCmdColumnAddress, columns, sizeof(columns));
  if (result != ESP_OK) {
    return result;
  }

  const uint8_t rows[] = {
      static_cast<uint8_t>(y_start >> 8), static_cast<uint8_t>(y_start),
      static_cast<uint8_t>((y_end - 1) >> 8),
      static_cast<uint8_t>(y_end - 1),
  };
  result = send_command(panel, kCmdRowAddress, rows, sizeof(rows));
  if (result != ESP_OK) {
    return result;
  }
  return send_command(panel, kCmdMemoryWrite);
}

esp_err_t panel_reset(esp_lcd_panel_t *base) {
  auto *panel = to_panel(base);
  if (valid_gpio(panel->reset_gpio)) {
    gpio_set_level(static_cast<gpio_num_t>(panel->reset_gpio), 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(static_cast<gpio_num_t>(panel->reset_gpio), 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
  }
  const esp_err_t result = send_command(panel, kCmdSoftwareReset);
  vTaskDelay(pdMS_TO_TICKS(120));
  return result;
}

esp_err_t panel_init(esp_lcd_panel_t *base) {
  auto *panel = to_panel(base);
  esp_err_t result = send_command(panel, kCmdSoftwareReset);
  if (result != ESP_OK) {
    return result;
  }
  vTaskDelay(pdMS_TO_TICKS(120));

  result = send_command(panel, kCmdSleepOut);
  if (result != ESP_OK) {
    return result;
  }
  vTaskDelay(pdMS_TO_TICKS(120));

  const uint8_t pixel_format = 0x55;
  result = send_command(panel, kCmdPixelFormat, &pixel_format, 1);
  if (result == ESP_OK) {
    result = apply_madctl(panel);
  }
  if (result == ESP_OK) {
    result = send_command(panel, kCmdInversionOff);
  }
  if (result == ESP_OK) {
    result = send_command(panel, kCmdNormalOn);
  }
  if (result == ESP_OK) {
    result = send_command(panel, kCmdDisplayOn);
  }
  vTaskDelay(pdMS_TO_TICKS(60));
  return result;
}

esp_err_t panel_del(esp_lcd_panel_t *base) {
  auto *panel = to_panel(base);
  if (panel->spi != nullptr) {
    (void)send_command(panel, kCmdDisplayOff);
    (void)spi_bus_remove_device(panel->spi);
    panel->spi = nullptr;
  }
  if (panel->packed != nullptr) {
    heap_caps_free(panel->packed);
    panel->packed = nullptr;
  }
  heap_caps_free(panel->next_packed);
  heap_caps_free(panel->psram_packed);
  if (valid_gpio(panel->reset_gpio)) {
    gpio_reset_pin(static_cast<gpio_num_t>(panel->reset_gpio));
  }
  std::free(panel);
  return ESP_OK;
}

void record_panel_draw(St7789ThreeWirePanel *panel, size_t pixels,
                       int64_t draw_started_us) {
  ++panel->frame_count;
  panel->frame_pixels += pixels;
  const int64_t now_us = esp_timer_get_time();
  panel->frame_draw_us += now_us - draw_started_us;
  panel->frame_max_draw_us=std::max(panel->frame_max_draw_us,uint32_t(std::min<int64_t>(now_us-draw_started_us,UINT32_MAX)));
  if (panel->report_started_us == 0) panel->report_started_us = now_us;
  if (now_us - panel->report_started_us >= 2000000) {
    ESP_LOGI(kTag, "draws=%u pixels=%llu pack_ms=%.2f dma_wait_ms=%.2f draw_ms=%.2f",
             static_cast<unsigned>(panel->frame_count),
             static_cast<unsigned long long>(panel->frame_pixels),
             panel->frame_pack_us / (1000.0 * panel->frame_count),
             panel->frame_spi_us / (1000.0 * panel->frame_count),
             panel->frame_draw_us / (1000.0 * panel->frame_count));
    ESP_LOGI(kTag,"PANEL_PROFILE window_us=%llu draws=%u pixels=%llu wire_bytes=%llu chunks=%u poll_chunks=%u window_command_us=%llu pack_us=%llu submit_us=%llu recycle_us=%llu drain_us=%llu draw_us=%llu max_draw_us=%u",
        static_cast<unsigned long long>(now_us-panel->report_started_us),unsigned(panel->frame_count),static_cast<unsigned long long>(panel->frame_pixels),
        static_cast<unsigned long long>(panel->frame_wire_bytes),unsigned(panel->frame_chunks),unsigned(panel->frame_poll_chunks),
        static_cast<unsigned long long>(panel->frame_window_us),static_cast<unsigned long long>(panel->frame_pack_us),static_cast<unsigned long long>(panel->frame_submit_us),
        static_cast<unsigned long long>(panel->frame_recycle_us),static_cast<unsigned long long>(panel->frame_drain_us),static_cast<unsigned long long>(panel->frame_draw_us),unsigned(panel->frame_max_draw_us));
    panel->frame_pack_us = panel->frame_spi_us = panel->frame_draw_us = 0;
    panel->frame_pixels = 0;
    panel->frame_count = 0;
    panel->frame_submit_us=panel->frame_recycle_us=panel->frame_drain_us=panel->frame_window_us=panel->frame_wire_bytes=0;
    panel->frame_chunks=panel->frame_poll_chunks=panel->frame_max_draw_us=0;
    panel->report_started_us = now_us;
  }
}

esp_err_t panel_draw_bitmap(esp_lcd_panel_t *base, int x_start, int y_start,
                            int x_end, int y_end, const void *color_data) {
  auto *panel = to_panel(base);
  const int64_t draw_started_us = esp_timer_get_time();
  if (color_data == nullptr || x_start < 0 || y_start < 0 ||
      x_end <= x_start || y_end <= y_start) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t result = set_window(panel, x_start, y_start, x_end, y_end);
  panel->frame_window_us+=esp_timer_get_time()-draw_started_us;
  if (result != ESP_OK) {
    return result;
  }

  const size_t width = static_cast<size_t>(x_end - x_start);
  const size_t height = static_cast<size_t>(y_end - y_start);
  if (width > SIZE_MAX / height || width * height > SIZE_MAX / 2) {
    return ESP_ERR_INVALID_SIZE;
  }
  const size_t byte_count = width * height * 2;

  // The shared LCD surface supplies RGB565 as an MSB-first byte stream. Pack
  // those bytes directly so each one receives its own in-band D/C=1 bit.
  result = write_9bit(panel, static_cast<const uint8_t *>(color_data),
                      byte_count, true);
  if (result == ESP_OK) record_panel_draw(panel, width * height, draw_started_us);
  return result;
}

esp_err_t draw_bitmap_2d_order(esp_lcd_panel_t *base, int x_start, int y_start,
                                int x_end, int y_end, const void *color_data,
                                size_t src_x_size, size_t src_y_size,
                                int src_x_start, int src_y_start,
                                int src_x_end, int src_y_end, bool native_rgb565) {
  auto *panel = to_panel(base);
  const int64_t started = esp_timer_get_time();
  if (base == nullptr || color_data == nullptr || x_start < 0 || y_start < 0 ||
      x_end <= x_start || y_end <= y_start || src_x_start < 0 ||
      src_y_start < 0 || src_x_end <= src_x_start ||
      src_y_end <= src_y_start ||
      static_cast<size_t>(src_x_end) > src_x_size ||
      static_cast<size_t>(src_y_end) > src_y_size ||
      x_end - x_start != src_x_end - src_x_start ||
      y_end - y_start != src_y_end - src_y_start) {
    return ESP_ERR_INVALID_ARG;
  }
  const size_t width = static_cast<size_t>(x_end - x_start);
  const size_t height = static_cast<size_t>(y_end - y_start);
  if (src_x_size > SIZE_MAX / src_y_size ||
      src_x_size * src_y_size > SIZE_MAX / sizeof(uint16_t)) {
    return ESP_ERR_INVALID_SIZE;
  }
  // The compact 9-bit stream must not gain padding bits at row boundaries.
  // Products align these regions to four RGB565 pixels before submission.
  if (width % 4 != 0) return ESP_ERR_NOT_SUPPORTED;
  esp_err_t result = set_window(panel, x_start, y_start, x_end, y_end);
  if (result != ESP_OK) return result;
  panel->frame_window_us+=esp_timer_get_time()-started;
  const auto *source = static_cast<const uint8_t *>(color_data) +
      (static_cast<size_t>(src_y_start) * src_x_size + src_x_start) * 2;
  result = write_9bit_rows(panel, source, width * 2, src_x_size * 2, height,native_rgb565);
  if (result == ESP_OK) record_panel_draw(panel, width * height, started);
  return result;
}

esp_err_t panel_draw_bitmap_2d(esp_lcd_panel_t *base, int x_start, int y_start,
    int x_end, int y_end, const void *color_data, size_t src_x_size,
    size_t src_y_size, int src_x_start, int src_y_start, int src_x_end, int src_y_end) {
  return draw_bitmap_2d_order(base,x_start,y_start,x_end,y_end,color_data,
      src_x_size,src_y_size,src_x_start,src_y_start,src_x_end,src_y_end,false);
}

esp_err_t panel_mirror(esp_lcd_panel_t *base, bool mirror_x, bool mirror_y) {
  auto *panel = to_panel(base);
  panel->mirror_x = mirror_x;
  panel->mirror_y = mirror_y;
  return apply_madctl(panel);
}

esp_err_t panel_swap_xy(esp_lcd_panel_t *base, bool swap_axes) {
  auto *panel = to_panel(base);
  panel->swap_xy = swap_axes;
  return apply_madctl(panel);
}

esp_err_t panel_set_gap(esp_lcd_panel_t *base, int x_gap, int y_gap) {
  auto *panel = to_panel(base);
  panel->x_gap = x_gap;
  panel->y_gap = y_gap;
  return ESP_OK;
}

esp_err_t panel_invert(esp_lcd_panel_t *base, bool invert) {
  return send_command(to_panel(base),
                      invert ? kCmdInversionOn : kCmdInversionOff);
}

esp_err_t panel_display(esp_lcd_panel_t *base, bool on) {
  return send_command(to_panel(base), on ? kCmdDisplayOn : kCmdDisplayOff);
}

esp_err_t panel_sleep(esp_lcd_panel_t *base, bool sleep) {
  const esp_err_t result =
      send_command(to_panel(base), sleep ? kCmdSleepIn : kCmdSleepOut);
  vTaskDelay(pdMS_TO_TICKS(120));
  return result;
}

} // namespace

esp_err_t device_st7789_3wire_create_panel(
    spi_host_device_t spi_host, const DeviceSpiLcdConfig &config,
    esp_lcd_panel_handle_t *panel_out) {
  if (panel_out == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  *panel_out = nullptr;

  auto *panel = static_cast<St7789ThreeWirePanel *>(
      std::calloc(1, sizeof(St7789ThreeWirePanel)));
  if (panel == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  panel->packed = static_cast<uint8_t *>(heap_caps_malloc(
      kPackedChunkBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (panel->packed == nullptr) {
    std::free(panel);
    return ESP_ERR_NO_MEM;
  }
  // One buffer is enough for the safe synchronous fallback. Allocate the
  // second only when internal DMA memory is available.
  panel->next_packed = static_cast<uint8_t *>(heap_caps_malloc(
      kPackedChunkBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (panel->next_packed == nullptr) {
    ESP_LOGW(kTag, "second DMA buffer unavailable; using polling transfers");
  }

  spi_device_interface_config_t device = {};
  device.mode = config.spi_mode;
  device.clock_speed_hz = static_cast<int>(config.pixel_clock_hz);
  device.spics_io_num = static_cast<gpio_num_t>(config.cs_gpio);
  device.queue_size = 2;
  device.flags = SPI_DEVICE_HALFDUPLEX;
  // ST7789 requires >=15ns CS setup/hold. Match the four-wire path's
  // explicit 20ns guard, rounded up to whole SPI cycles (40MHz: one cycle).
  const auto guard = static_cast<uint8_t>(std::clamp<uint64_t>(
      (uint64_t{config.pixel_clock_hz} + 49999999) / 50000000, 1, 16));
  device.cs_ena_pretrans = guard;
  device.cs_ena_posttrans = guard;
  esp_err_t result = spi_bus_add_device(spi_host, &device, &panel->spi);
  if (result != ESP_OK) {
    heap_caps_free(panel->next_packed);
    heap_caps_free(panel->packed);
    std::free(panel);
    return result;
  }
  int actual_khz = 0;
  ESP_LOGI(kTag, "CS setup/hold guard=%u SPI cycles", guard);
  if (spi_device_get_actual_freq(panel->spi, &actual_khz) == ESP_OK) {
    ESP_LOGI(kTag, "SPI clock requested=%u Hz actual=%d kHz",
             static_cast<unsigned>(config.pixel_clock_hz), actual_khz);
  }

  panel->reset_gpio = config.reset_gpio;
  panel->rgb_order_bgr = config.rgb_order_bgr;
  panel->base.reset = panel_reset;
  panel->base.init = panel_init;
  panel->base.del = panel_del;
  panel->base.draw_bitmap = panel_draw_bitmap;
  panel->base.draw_bitmap_2d = panel_draw_bitmap_2d;
  panel->base.mirror = panel_mirror;
  panel->base.swap_xy = panel_swap_xy;
  panel->base.set_gap = panel_set_gap;
  panel->base.invert_color = panel_invert;
  panel->base.disp_on_off = panel_display;
  panel->base.disp_sleep = panel_sleep;
  panel->base.set_brightness = nullptr;
  panel->base.user_data = nullptr;

  if (valid_gpio(panel->reset_gpio)) {
    gpio_config_t reset = {};
    reset.pin_bit_mask = 1ULL << panel->reset_gpio;
    reset.mode = GPIO_MODE_OUTPUT;
    reset.pull_up_en = GPIO_PULLUP_DISABLE;
    reset.pull_down_en = GPIO_PULLDOWN_DISABLE;
    reset.intr_type = GPIO_INTR_DISABLE;
    result = gpio_config(&reset);
    if (result != ESP_OK) {
      (void)panel_del(&panel->base);
      return result;
    }
  }

  *panel_out = &panel->base;
  ESP_LOGI(kTag,
           "panel facade ready host=%d mode=%d clock=%lu cs=%d rst=%d",
           static_cast<int>(spi_host), config.spi_mode,
           static_cast<unsigned long>(config.pixel_clock_hz), config.cs_gpio,
           config.reset_gpio);
  return ESP_OK;
}

esp_err_t device_st7789_3wire_draw_rgb565_strided(
    esp_lcd_panel_handle_t base,int x,int y,int width,int height,
    const uint16_t *pixels,size_t stride,bool native_rgb565) {
  if(width<=0||height<=0||x<0||y<0||x>INT_MAX-width||y>INT_MAX-height)
    return ESP_ERR_INVALID_ARG;
  return draw_bitmap_2d_order(base,x,y,x+width,y+height,pixels,stride,
      size_t(height),0,0,width,height,native_rgb565);
}

esp_err_t device_st7789_3wire_set_dma_mode(
    esp_lcd_panel_handle_t base, DeviceSpiLcdThreeWireDmaMode mode) {
  if (base == nullptr) return ESP_ERR_INVALID_ARG;
  auto *panel = to_panel(base);
  if (mode == DeviceSpiLcdThreeWireDmaMode::InternalQueued &&
      panel->next_packed == nullptr) {
    panel->next_packed = static_cast<uint8_t *>(heap_caps_malloc(
        kPackedChunkBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA |
                               MALLOC_CAP_8BIT));
    if (panel->next_packed == nullptr) return ESP_ERR_NO_MEM;
  } else if (mode == DeviceSpiLcdThreeWireDmaMode::PsramRegion &&
             panel->psram_packed == nullptr) {
    panel->psram_packed = static_cast<uint8_t *>(heap_caps_aligned_alloc(
        64, 2 * kPackedChunkBytes,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    if (panel->psram_packed == nullptr) return ESP_ERR_NO_MEM;
  }
  panel->dma_mode = mode;
  if (mode != DeviceSpiLcdThreeWireDmaMode::InternalQueued) {
    heap_caps_free(panel->next_packed);
    panel->next_packed = nullptr;
  }
  if (mode != DeviceSpiLcdThreeWireDmaMode::PsramRegion) {
    heap_caps_free(panel->psram_packed);
    panel->psram_packed = nullptr;
  }
  return ESP_OK;
}

} // namespace lunaredu::device
