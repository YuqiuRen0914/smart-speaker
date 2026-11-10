#pragma once

#include <cstddef>
#include <cstdint>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <esp_err.h>

// Candidate general-board hardware v2 within the single bsp component.
// Explicit opt-in excludes existing board profiles. The disabled backend
// has no bus/resource state. These revision APIs are for contract validation;
// current product drivers retain their existing BSP contracts.
enum class bsp_v2_display_t { none, lcd_spi, amoled_qspi };
enum class bsp_v2_i2c_t { common = 0, expansion = 1 };
enum bsp_v2_resource_t {
  BSP_V2_RESOURCE_ZONE_A=0, BSP_V2_RESOURCE_ZONE_B, BSP_V2_RESOURCE_ZONE_C,
  BSP_V2_RESOURCE_AUDIO, BSP_V2_RESOURCE_INTERFACE_AUX,
  BSP_V2_RESOURCE_COMMON_I2C, BSP_V2_RESOURCE_INTERFACE_I2C,
  BSP_V2_RESOURCE_SPI_BUS, BSP_V2_RESOURCE_SCREEN, BSP_V2_RESOURCE_TOUCH_INT,
  BSP_V2_RESOURCE_BOOT, BSP_V2_RESOURCE_RGB, BSP_V2_RESOURCE_USB,
  BSP_V2_RESOURCE_CAMERA, BSP_V2_RESOURCE_TFCARD, BSP_V2_RESOURCE_COUNT,
};
enum bsp_v2_pin_constraints_t : uint32_t {
  BSP_V2_PIN_CAMERA_SHARED=1u<<0, BSP_V2_PIN_JTAG_STRAP=1u<<1,
  BSP_V2_PIN_BOOT_BUTTON=1u<<2, BSP_V2_PIN_ROM_OUTPUT=1u<<3,
  BSP_V2_PIN_VOLTAGE_STRAP=1u<<4, BSP_V2_PIN_DOWNLOAD_STRAP=1u<<5,
  BSP_V2_PIN_I2C_CONNECTED=1u<<6, BSP_V2_PIN_DISPLAY_CARD_SHARED=1u<<7,
  BSP_V2_PIN_TE_OUTPUT=1u<<8, BSP_V2_PIN_RGB_CONNECTED=1u<<9,
  BSP_V2_PIN_USB_EXCLUSIVE=1u<<10,
};
struct bsp_v2_pin_t {
  int gpio; const char *role;
  int adc_unit,adc_channel,touch_channel; // unit0/channel-1 = unsupported.
  bool rtc; uint32_t constraints;
};
struct bsp_v2_group_t {
  const char *id; int pins[6]; size_t pin_count;
  int direct_servo_pin; bool exposed; const char *layout;
};
struct bsp_v2_i2c_bus_t {
  const char *id; uint8_t sda_pin,scl_pin; uint32_t clock_hz;
  bsp_v2_resource_t resource;
};
struct bsp_v2_spi_bus_t {
  const char *id; spi_host_device_t host;
  int mosi_pin,miso_pin,sclk_pin,max_transfer_sz; bsp_v2_resource_t resource;
};
struct bsp_v2_screen_t {
  bsp_v2_display_t mode; int cs,data2,data3,te,touch_interrupt;
  uint32_t write_clock_hz; int reset;
};
struct bsp_v2_audio_t {int bclk,ws,dout,din,mclk;};
struct bsp_v2_camera_t {
  int pin_pwdn=-1,pin_reset=-1,pin_xclk=-1,pin_sccb_sda=-1,pin_sccb_scl=-1;
  int pin_d0=-1,pin_d1=-1,pin_d2=-1,pin_d3=-1,pin_d4=-1,pin_d5=-1,pin_d6=-1,pin_d7=-1;
  int pin_vsync=-1,pin_href=-1,pin_pclk=-1;
  uint32_t xclk_freq_hz=0; // Product must confirm actual module/timing.
};
struct bsp_v2_board_t {
  const char *revision; // "v2" target, not a detected PCB revision.
  const char *support_status; // "prototype", not hardware acceptance.
  const bsp_v2_pin_t *pins; size_t pin_count;
  const bsp_v2_group_t *groups; size_t group_count;
  bsp_v2_i2c_bus_t common_i2c,interface_i2c;
  bsp_v2_spi_bus_t spi; bool spi_available;
  bsp_v2_screen_t screen; bsp_v2_audio_t audio;
  bsp_v2_camera_t camera; bool camera_enabled,tf_enabled;
  int tf_cs; uint32_t tf_clock_hz; int rgb,boot,usb_minus,usb_plus;
  bool power_board_protocol_known,touch_model_confirmed,camera_module_confirmed;
};

// Null when CONFIG_LUNAROS_BSP_V2_ENABLED is false. No GPIO/eFuse access.
const bsp_v2_board_t *bsp_v2_board();
esp_err_t bsp_v2_camera_config(bsp_v2_camera_t *out);

constexpr size_t BSP_V2_OWNER_MAX=32;
struct bsp_v2_resource_observation_t {
  const char *name; bool configured_available; uint64_t pin_mask;
  char owner[BSP_V2_OWNER_MAX];
};
struct bsp_v2_signal_t {const char *role;int gpio;};
esp_err_t bsp_v2_resource_acquire(bsp_v2_resource_t,const char *owner);
esp_err_t bsp_v2_resource_release(bsp_v2_resource_t,const char *owner);
bool bsp_v2_resource_is_acquired(bsp_v2_resource_t);
bool bsp_v2_resource_contains_pin(bsp_v2_resource_t,int gpio);
bool bsp_v2_resource_observe(bsp_v2_resource_t,bsp_v2_resource_observation_t *out);
// Returns full count; writes at most capacity entries. Does not probe hardware.
size_t bsp_v2_resource_signals(bsp_v2_resource_t,bsp_v2_signal_t *out,size_t capacity);

// Owner initializes/stops the two independent buses. Devices borrow handles
// and remove native devices before teardown. Closing failure retains ownership
// for retry and rejects new transactions. These APIs do not implement ULP.
esp_err_t bsp_v2_i2c_init(bsp_v2_i2c_t);
esp_err_t bsp_v2_i2c_deinit(bsp_v2_i2c_t);
bool bsp_v2_i2c_ready(bsp_v2_i2c_t);
i2c_master_bus_handle_t bsp_v2_i2c_handle(bsp_v2_i2c_t);
esp_err_t bsp_v2_i2c_probe(bsp_v2_i2c_t,uint8_t address,int timeout_ms);
esp_err_t bsp_v2_i2c_transfer(bsp_v2_i2c_t,uint8_t address,const uint8_t *write_data,
                            size_t write_size,uint8_t *read_data,size_t read_size,int timeout_ms);

// Shared SPI owner. Drivers own their SDK device handles and transactions.
// A declared card must be initialized before display communication. Callers
// claim SCREEN before adding display devices; removing them precedes release.
esp_err_t bsp_v2_spi_init();
esp_err_t bsp_v2_spi_deinit();
bool bsp_v2_spi_ready();

struct bsp_v2_audio_claim_t {uint64_t lease_id=0;};
esp_err_t bsp_v2_audio_claim(const int *pins,size_t count,bsp_v2_audio_claim_t *out);
esp_err_t bsp_v2_audio_release(bsp_v2_audio_claim_t *claim);

struct bsp_v2_sdcard_status_t {
  bool mounted;
  char mount_path[128];
  uint64_t capacity_bytes;
  char last_error[64]; // Owned snapshot, valid across subsequent mount/unmount.
};
esp_err_t bsp_v2_sdcard_mount(const char *path);
esp_err_t bsp_v2_sdcard_unmount();
bsp_v2_sdcard_status_t bsp_v2_sdcard_get_status();
