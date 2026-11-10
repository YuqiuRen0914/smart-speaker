#include "smart_speaker_audio.h"
#include "sdkconfig.h"
#include <esp_log.h>
namespace smart_speaker {
constexpr const char *kTag="speaker.board";
DualI2sConfig speaker_audio_config(){
  // Existing board routes I2S0 to the physical right and I2S1 to the left.
  return {{0,CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_BCLK,CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_WS,CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_DOUT},
          {1,CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_BCLK,CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_WS,CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_DOUT},
          CONFIG_LUNAREDU_SMART_SPEAKER_FADE_MS,"spkr_dsp","speaker_i2s","speaker.audio"};
}
bool setup_speaker_audio_output(lunaredu::lunaros::KernelRuntime &runtime){return setup_dual_i2s_audio(runtime,speaker_audio_config());}
void smart_speaker_quiet_audio_pins_early(){dual_i2s_quiet_audio_pins(speaker_audio_config());}
bool smart_speaker_validate_and_log_gpio_ownership() {
  struct Pin { const char *owner; int gpio; };
  const Pin pins[] = {
      {"lcd.cs", CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_CS},
      {"lcd.dc", CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_DC},
      {"lcd.mosi", CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_MOSI},
      {"lcd.sclk", CONFIG_LUNAREDU_DEVICE_DISPLAY_SPI_LCD_SCLK},
      {"touch.sda", CONFIG_LUNAREDU_SMART_SPEAKER_TOUCH_SDA_GPIO},
      {"touch.scl", CONFIG_LUNAREDU_SMART_SPEAKER_TOUCH_SCL_GPIO},
      {"touch.int", CONFIG_LUNAREDU_SMART_SPEAKER_TOUCH_INT_GPIO},
      {"mpu.sda", CONFIG_LUNAREDU_SMART_SPEAKER_MPU6050_SDA_GPIO},
      {"mpu.scl", CONFIG_LUNAREDU_SMART_SPEAKER_MPU6050_SCL_GPIO},
      {"encoder.a", CONFIG_LUNAREDU_SMART_SPEAKER_ENCODER_A_GPIO},
      {"encoder.b", CONFIG_LUNAREDU_SMART_SPEAKER_ENCODER_B_GPIO},
      {"button.left", CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_BUTTON_GPIO},
      {"button.right", CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_BUTTON_GPIO},
      {"i2s0.right.ws", CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_WS},
      {"i2s0.right.bclk", CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_BCLK},
      {"i2s0.right.dout", CONFIG_LUNAREDU_SMART_SPEAKER_LEFT_I2S_DOUT},
      {"i2s1.left.ws", CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_WS},
      {"i2s1.left.bclk", CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_BCLK},
      {"i2s1.left.dout", CONFIG_LUNAREDU_SMART_SPEAKER_RIGHT_I2S_DOUT},
      {"sd.cmd", 38}, {"sd.clk", 39}, {"sd.d0", 40},
  };
  bool unique = true;
  for (size_t i = 0; i < std::size(pins); ++i) {
    ESP_LOGI(kTag, "GPIO owner %-14s -> %d", pins[i].owner, pins[i].gpio);
    for (size_t j = i + 1; j < std::size(pins); ++j) {
      if (pins[i].gpio == pins[j].gpio) {
        ESP_LOGE(kTag, "GPIO conflict: %s and %s on %d", pins[i].owner,
                 pins[j].owner, pins[i].gpio);
        unique = false;
      }
    }
  }
  return unique;
}

}
