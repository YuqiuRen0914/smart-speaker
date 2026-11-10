// Frozen historical expansion-baseboard description. Do not extend for new boards.
#include "boards/board_descriptor.h"
#include "legacy/expansion_baseboard/pins.h"
#include "sdkconfig.h"

namespace {
using BoardProfileMask = uint8_t;

constexpr BoardProfileMask kBoardProfileMaskBase = 1u << 0;
constexpr BoardProfileMask kBoardProfileMaskCamBundle = 1u << 1;
constexpr BoardProfileMask kBoardProfileMaskMinimal = 1u << 2;
constexpr BoardProfileMask kBoardProfileMaskReuleaux = 1u << 3;
constexpr BoardProfileMask kBoardProfileMaskXiaoxing = 1u << 4;

struct LegacyResourceDescriptor {
  bsp_resource_t id;
  const char *name;
  uint64_t pin_mask;
  BoardProfileMask profile_mask;
};

constexpr uint64_t pin_mask(uint8_t pin) { return 1ULL << pin; }

constexpr BoardProfileMask profile_mask_for(bsp_board_profile_t profile) {
  switch (profile) {
  case BSP_BOARD_PROFILE_BASE:
    return kBoardProfileMaskBase;
  case BSP_BOARD_PROFILE_CAM_BUNDLE:
    return kBoardProfileMaskCamBundle;
  case BSP_BOARD_PROFILE_MINIMAL:
    return kBoardProfileMaskMinimal;
  case BSP_BOARD_PROFILE_XIAOXING:
    return kBoardProfileMaskXiaoxing;
  case BSP_BOARD_PROFILE_REULEAUX_TRIANGLE_V3:
    return kBoardProfileMaskReuleaux;
  default:
    return 0;
  }
}

constexpr uint64_t pins_camera() {
  return pin_mask(lunaredu::bsp::pins::kCameraSiod) |
         pin_mask(lunaredu::bsp::pins::kCameraSioc) |
         pin_mask(lunaredu::bsp::pins::kCameraVsync) |
         pin_mask(lunaredu::bsp::pins::kCameraHref) |
         pin_mask(lunaredu::bsp::pins::kCameraXclk) |
         pin_mask(lunaredu::bsp::pins::kCameraY2) |
         pin_mask(lunaredu::bsp::pins::kCameraY3) |
         pin_mask(lunaredu::bsp::pins::kCameraY4) |
         pin_mask(lunaredu::bsp::pins::kCameraY5) |
         pin_mask(lunaredu::bsp::pins::kCameraY6) |
         pin_mask(lunaredu::bsp::pins::kCameraY7) |
         pin_mask(lunaredu::bsp::pins::kCameraY8) |
         pin_mask(lunaredu::bsp::pins::kCameraY9) |
         pin_mask(lunaredu::bsp::pins::kCameraPclk);
}

constexpr uint64_t pins_tfcard() {
  return pin_mask(lunaredu::bsp::pins::kTfCardCommand) |
         pin_mask(lunaredu::bsp::pins::kTfCardClock) |
         pin_mask(lunaredu::bsp::pins::kTfCardData0);
}

constexpr uint64_t pins_baseboard_i2c() {
  return pin_mask(lunaredu::bsp::pins::kMpuI2cSda) |
         pin_mask(lunaredu::bsp::pins::kMpuI2cScl);
}

constexpr uint64_t pins_expansion_i2c1() {
  return pin_mask(lunaredu::bsp::pins::kExpansionI2c1Sda) |
         pin_mask(lunaredu::bsp::pins::kExpansionI2c1Scl);
}

constexpr uint64_t pins_xiaoxing_i2s_audio() { return 0; }

constexpr uint64_t pins_expansion_spi1() {
  return pin_mask(lunaredu::bsp::pins::kExpansionSpi1SignalA) |
         pin_mask(lunaredu::bsp::pins::kExpansionSpi1SignalB) |
         pin_mask(lunaredu::bsp::pins::kExpansionSpi1SignalC) |
         pin_mask(lunaredu::bsp::pins::kExpansionSpi1Cs);
}

constexpr uint64_t pins_usb_native() {
  return pin_mask(lunaredu::bsp::pins::kUsbDMinus) |
         pin_mask(lunaredu::bsp::pins::kUsbDPlus);
}

constexpr uint64_t pins_pwm_bank() {
  return pin_mask(lunaredu::bsp::pins::kPwmBank0A) |
         pin_mask(lunaredu::bsp::pins::kPwmBank0B) |
         pin_mask(lunaredu::bsp::pins::kPwmBank1A) |
         pin_mask(lunaredu::bsp::pins::kPwmBank1B) |
         pin_mask(lunaredu::bsp::pins::kPwmBank2A) |
         pin_mask(lunaredu::bsp::pins::kPwmBank2B) |
         pin_mask(lunaredu::bsp::pins::kPwmBank3A) |
         pin_mask(lunaredu::bsp::pins::kPwmBank3B);
}

constexpr LegacyResourceDescriptor kResources[] = {
    {BSP_RESOURCE_BASEBOARD_MPU6050_I2C, "baseboard_mpu6050_i2c",
     pins_baseboard_i2c(),
     kBoardProfileMaskBase | kBoardProfileMaskCamBundle |
         kBoardProfileMaskMinimal},
    {BSP_RESOURCE_EXPANSION_I2C1, "expansion_i2c1",
     pins_expansion_i2c1(),
     kBoardProfileMaskBase | kBoardProfileMaskCamBundle |
         kBoardProfileMaskMinimal},
    {BSP_RESOURCE_EXPANSION_SPI1, "expansion_spi1",
     pins_expansion_spi1(),
     kBoardProfileMaskBase | kBoardProfileMaskCamBundle |
         kBoardProfileMaskMinimal},
    {BSP_RESOURCE_ONBOARD_WS2812, "onboard_ws2812",
     pin_mask(lunaredu::bsp::pins::kOnboardWs2812), kBoardProfileMaskBase},
    {BSP_RESOURCE_ONBOARD_STATUS_LED, "onboard_status_led",
     pin_mask(lunaredu::bsp::pins::kStatusLed), kBoardProfileMaskCamBundle},
    {BSP_RESOURCE_CAMERA, "camera", pins_camera(), kBoardProfileMaskCamBundle},
    {BSP_RESOURCE_TFCARD, "tfcard", pins_tfcard(),
     kBoardProfileMaskCamBundle},
    {BSP_RESOURCE_USB_NATIVE, "usb_native", pins_usb_native(),
     kBoardProfileMaskBase | kBoardProfileMaskCamBundle |
         kBoardProfileMaskMinimal},
    {BSP_RESOURCE_I2S_DATA, "i2s_data", pins_usb_native(),
     kBoardProfileMaskBase | kBoardProfileMaskCamBundle |
         kBoardProfileMaskMinimal},
    {BSP_RESOURCE_PWM_SERVO_BANK, "pwm_servo_bank", pins_pwm_bank(),
     kBoardProfileMaskBase | kBoardProfileMaskCamBundle |
         kBoardProfileMaskMinimal},
    {BSP_RESOURCE_PWM_MOTOR_BANK, "pwm_motor_bank", pins_pwm_bank(),
     kBoardProfileMaskBase | kBoardProfileMaskCamBundle |
         kBoardProfileMaskMinimal},
    {BSP_RESOURCE_BAT_ADC, "bat_adc",
     pin_mask(lunaredu::bsp::pins::kBatAdc),
     kBoardProfileMaskBase | kBoardProfileMaskCamBundle |
         kBoardProfileMaskMinimal | kBoardProfileMaskXiaoxing},
    {BSP_RESOURCE_XIAOXING_I2S_AUDIO, "xiaoxing_i2s_audio",
     pins_xiaoxing_i2s_audio(), kBoardProfileMaskXiaoxing},
    {BSP_RESOURCE_REMOTE_CONTROLLER_TOUCH, "remote_controller_touch", 0, 0},
};

static_assert(sizeof(kResources) / sizeof(kResources[0]) ==
                  BSP_RESOURCE_COUNT,
              "resource table must match bsp_resource_t");

constexpr uint8_t kTopSpiCsPins[] = {
    lunaredu::bsp::pins::kTopDc,       lunaredu::bsp::pins::kTopCtrl21,
    lunaredu::bsp::pins::kTopCtrl14,   lunaredu::bsp::pins::kCs1,
    lunaredu::bsp::pins::kCs2,         lunaredu::bsp::pins::kTfCardData0,
    lunaredu::bsp::pins::kTfCardClock, lunaredu::bsp::pins::kTfCardCommand,
};

constexpr uint8_t kCenterI2cAdcPins[] = {
    lunaredu::bsp::pins::kMpuI2cScl,
    lunaredu::bsp::pins::kMpuI2cSda,
    lunaredu::bsp::pins::kBatAdc,
};

constexpr uint8_t kLeftUpperPins[] = {lunaredu::bsp::pins::kLeftUpperIoA,
                                      lunaredu::bsp::pins::kLeftUpperIoB};

constexpr uint8_t kLeftMiddlePins[] = {
    lunaredu::bsp::pins::kPwmBank0A, lunaredu::bsp::pins::kPwmBank0B,
    lunaredu::bsp::pins::kPwmBank1A, lunaredu::bsp::pins::kPwmBank1B,
    lunaredu::bsp::pins::kPwmBank2A, lunaredu::bsp::pins::kPwmBank2B,
    lunaredu::bsp::pins::kPwmBank3A, lunaredu::bsp::pins::kPwmBank3B,
};

constexpr uint8_t kLeftLowerPins[] = {lunaredu::bsp::pins::kBootIo0,
                                      lunaredu::bsp::pins::kLeftLowerIoB};

constexpr uint8_t kRightUpperPins[] = {lunaredu::bsp::pins::kBootIo45,
                                       lunaredu::bsp::pins::kBootIo46};

constexpr uint8_t kRightMiddlePins[] = {
    lunaredu::bsp::pins::kUartTx,        lunaredu::bsp::pins::kUartRx,
    lunaredu::bsp::pins::kCameraY4,      lunaredu::bsp::pins::kCameraY3,
    lunaredu::bsp::pins::kExpansionI2c1Sda,
    lunaredu::bsp::pins::kExpansionI2c1Scl,
    lunaredu::bsp::pins::kMpuI2cSda,
    lunaredu::bsp::pins::kMpuI2cScl,
    lunaredu::bsp::pins::kUsbDMinus, lunaredu::bsp::pins::kUsbDPlus,
};

constexpr uint8_t kRightLowerPins[] = {lunaredu::bsp::pins::kUsbDMinus,
                                       lunaredu::bsp::pins::kUsbDPlus};

constexpr bsp_baseboard_group_t kBaseboardGroups[] = {
    {"top.spi_cs", BSP_BASEBOARD_ZONE_TOP, kTopSpiCsPins,
     sizeof(kTopSpiCsPins) / sizeof(kTopSpiCsPins[0]),
     BSP_PIN_USE_GPIO | BSP_PIN_USE_SPI | BSP_PIN_USE_CS |
         BSP_PIN_USE_PWM | BSP_PIN_USE_TFCARD | BSP_PIN_USE_CAMERA,
     "4x3pin XH2.54",
     "顶部分区：GPIO41/21/14 是 SPI1 候选信号，GPIO42 是对应 CS；"
     "GPIO38/39/40 同时也是可选 TF 卡引脚。"},
    {"center.i2c_adc", BSP_BASEBOARD_ZONE_CENTER, kCenterI2cAdcPins,
     sizeof(kCenterI2cAdcPins) / sizeof(kCenterI2cAdcPins[0]),
     BSP_PIN_USE_I2C | BSP_PIN_USE_ADC | BSP_PIN_USE_BOOT_STRAP |
         BSP_PIN_USE_ONBOARD,
     "4pin 排针 + 1 个焊盘",
     "默认单个 MPU6050 I2C：GPIO48 SDA / GPIO47 SCL；BAT-ADC 使用 GPIO3。"},
    {"left.upper", BSP_BASEBOARD_ZONE_LEFT_UPPER, kLeftUpperPins,
     sizeof(kLeftUpperPins) / sizeof(kLeftUpperPins[0]),
     BSP_PIN_USE_GPIO | BSP_PIN_USE_PWM | BSP_PIN_USE_CS |
         BSP_PIN_USE_KEY | BSP_PIN_USE_CAMERA,
     "2x4pin XH2.54 可选焊接布局",
     "3.3V 信号组。camera 启用时，GPIO4/5 是 SCCB 引脚。"},
    {"left.middle.pwm", BSP_BASEBOARD_ZONE_LEFT_MIDDLE, kLeftMiddlePins,
     sizeof(kLeftMiddlePins) / sizeof(kLeftMiddlePins[0]),
     BSP_PIN_USE_GPIO | BSP_PIN_USE_PWM | BSP_PIN_USE_CAMERA,
     "4x2pin XH 或 4x2x3pin 排针",
     "高密度 PWM 分组，可用于电机或舵机；camera 启用时会占用这些引脚。"},
    {"left.lower", BSP_BASEBOARD_ZONE_LEFT_LOWER, kLeftLowerPins,
     sizeof(kLeftLowerPins) / sizeof(kLeftLowerPins[0]),
     BSP_PIN_USE_GPIO | BSP_PIN_USE_PWM | BSP_PIN_USE_CS |
         BSP_PIN_USE_KEY | BSP_PIN_USE_BOOT_STRAP | BSP_PIN_USE_CAMERA,
     "2x4pin XH2.54 可选焊接布局",
     "GPIO0 是 BOOT 绑带引脚；camera 启用时 GPIO10 是图像数据引脚。"},
    {"right.upper.keys", BSP_BASEBOARD_ZONE_RIGHT_UPPER, kRightUpperPins,
     sizeof(kRightUpperPins) / sizeof(kRightUpperPins[0]),
     BSP_PIN_USE_GPIO | BSP_PIN_USE_KEY | BSP_PIN_USE_BOOT_STRAP,
     "3x2pin XH2.54",
     "按键输入分组，GPIO45/46 都是启动绑带受限引脚。"},
    {"right.middle.bus_pwm_i2s", BSP_BASEBOARD_ZONE_RIGHT_MIDDLE,
     kRightMiddlePins, sizeof(kRightMiddlePins) / sizeof(kRightMiddlePins[0]),
     BSP_PIN_USE_GPIO | BSP_PIN_USE_PWM | BSP_PIN_USE_I2C |
         BSP_PIN_USE_UART | BSP_PIN_USE_I2S | BSP_PIN_USE_CS |
         BSP_PIN_USE_USB | BSP_PIN_USE_CAMERA | BSP_PIN_USE_ONBOARD,
     "4x2pin + 2x3pin XH/排针可选布局",
     "混合总线/PWM 区域：GPIO1/2 是扩展 I2C1；GPIO43/44 是默认 UART；"
     "GPIO19/20 可作为 USB、I2S 数据或片选。"},
    {"right.lower.usb_type_a", BSP_BASEBOARD_ZONE_RIGHT_LOWER, kRightLowerPins,
     sizeof(kRightLowerPins) / sizeof(kRightLowerPins[0]),
     BSP_PIN_USE_USB | BSP_PIN_USE_I2S | BSP_PIN_USE_CS,
     "USB Type-A",
     "GPIO19/20 是 USB D-/D+，也可选择作为 I2S 数据/片选。"},
};

} // namespace

namespace lunaredu::bsp {
ResourceTable legacy_baseboard_resources() {
  ResourceTable table{};
  const auto mask = profile_mask_for(bsp_board_profile());
  for (const auto &resource : kResources) {
    table[resource.id] = {resource.id, resource.name, resource.pin_mask,
                          (resource.profile_mask & mask) != 0};
  }
  return table;
}
const bsp_baseboard_group_t *legacy_baseboard_groups(size_t *count) {
  if (count) *count = sizeof(kBaseboardGroups) / sizeof(kBaseboardGroups[0]);
  return kBaseboardGroups;
}
const bsp_i2c_bus_t *legacy_mpu_i2c_bus() {
  static constexpr bsp_i2c_bus_t bus{"i2c.mpu", pins::kMpuI2cSda,
    pins::kMpuI2cScl, pins::kMpuI2cClockHz, BSP_RESOURCE_BASEBOARD_MPU6050_I2C};
  return &bus;
}
const bsp_i2c_bus_t *legacy_expansion_i2c1_bus() {
  static constexpr bsp_i2c_bus_t bus{"i2c.expansion1", pins::kExpansionI2c1Sda,
    pins::kExpansionI2c1Scl, pins::kExpansionI2c1ClockHz, BSP_RESOURCE_EXPANSION_I2C1};
  return &bus;
}
} // namespace lunaredu::bsp
