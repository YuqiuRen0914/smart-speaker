#include "bsp/lunaredu_bsp.h"

#include <stdio.h>
#include <string.h>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "sdkconfig.h"

namespace {

using BoardProfileMask = uint8_t;

constexpr BoardProfileMask kBoardProfileMaskBase = 1u << 0;
constexpr BoardProfileMask kBoardProfileMaskCamBundle = 1u << 1;
constexpr BoardProfileMask kBoardProfileMaskMinimal = 1u << 2;
constexpr BoardProfileMask kBoardProfileMaskReuleaux = 1u << 3;

struct ResourceDescriptor {
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

constexpr ResourceDescriptor kResources[] = {
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
     pin_mask(lunaredu::bsp::pins::kBootIo3BatAdc),
     kBoardProfileMaskBase | kBoardProfileMaskCamBundle |
         kBoardProfileMaskMinimal},
};

static_assert(sizeof(kResources) / sizeof(kResources[0]) ==
                  BSP_RESOURCE_COUNT,
              "resource table must match bsp_resource_t");

const ResourceDescriptor *descriptor_for(bsp_resource_t resource) {
  if (resource < 0 || resource >= BSP_RESOURCE_COUNT) {
    return nullptr;
  }
  return &kResources[resource];
}

bool resource_available_for_profile(const ResourceDescriptor &descriptor) {
  return (descriptor.profile_mask & profile_mask_for(bsp_board_profile())) != 0;
}

const char *pin_name(uint64_t pin) {
  switch (pin) {
  case pin_mask(0):
    return "GPIO0";
  case pin_mask(1):
    return "GPIO1";
  case pin_mask(2):
    return "GPIO2";
  case pin_mask(3):
    return "GPIO3";
  case pin_mask(4):
    return "GPIO4";
  case pin_mask(5):
    return "GPIO5";
  case pin_mask(6):
    return "GPIO6";
  case pin_mask(7):
    return "GPIO7";
  case pin_mask(8):
    return "GPIO8";
  case pin_mask(9):
    return "GPIO9";
  case pin_mask(10):
    return "GPIO10";
  case pin_mask(11):
    return "GPIO11";
  case pin_mask(12):
    return "GPIO12";
  case pin_mask(13):
    return "GPIO13";
  case pin_mask(14):
    return "GPIO14";
  case pin_mask(15):
    return "GPIO15";
  case pin_mask(16):
    return "GPIO16";
  case pin_mask(17):
    return "GPIO17";
  case pin_mask(18):
    return "GPIO18";
  case pin_mask(19):
    return "GPIO19";
  case pin_mask(20):
    return "GPIO20";
  case pin_mask(21):
    return "GPIO21";
  case pin_mask(38):
    return "GPIO38";
  case pin_mask(39):
    return "GPIO39";
  case pin_mask(40):
    return "GPIO40";
  case pin_mask(41):
    return "GPIO41";
  case pin_mask(42):
    return "GPIO42";
  case pin_mask(43):
    return "GPIO43";
  case pin_mask(44):
    return "GPIO44";
  case pin_mask(45):
    return "GPIO45";
  case pin_mask(46):
    return "GPIO46";
  case pin_mask(47):
    return "GPIO47";
  case pin_mask(48):
    return "GPIO48";
  default:
    return "GPIO?";
  }
}

constexpr size_t kOwnerNameMax = BSP_RESOURCE_CLAIM_OWNER_MAX;
constexpr size_t kClaimPinMax = BSP_RESOURCE_CLAIM_PIN_MAX;

const char *g_owners[BSP_RESOURCE_COUNT] = {};
char g_owner_storage[BSP_RESOURCE_COUNT][kOwnerNameMax] = {};
char g_last_claim_owner[kOwnerNameMax] = {};
bsp_resource_claim_t g_last_claim = {};

SemaphoreHandle_t resource_mutex() {
  static StaticSemaphore_t mutex_buffer;
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&mutex_buffer);
  return mutex;
}

class ResourceLock {
public:
  ResourceLock() : mutex_(resource_mutex()) {
    if (mutex_ != nullptr) {
      xSemaphoreTake(mutex_, portMAX_DELAY);
    }
  }

  ~ResourceLock() {
    if (mutex_ != nullptr) {
      xSemaphoreGive(mutex_);
    }
  }

private:
  SemaphoreHandle_t mutex_;
};

void copy_owner_name(char *target, size_t target_size, const char *owner) {
  if (target == nullptr || target_size == 0) {
    return;
  }
  snprintf(target, target_size, "%s", owner == nullptr ? "(unknown)" : owner);
}

void copy_optional_claim_text(char *target, size_t target_size,
                              const char *text) {
  if (target == nullptr || target_size == 0) {
    return;
  }
  snprintf(target, target_size, "%s", text == nullptr ? "" : text);
}

bsp_resource_claim_snapshot_t last_claim_snapshot_unlocked() {
  bsp_resource_claim_snapshot_t snapshot = {};
  snapshot.ok = g_last_claim.ok;
  copy_optional_claim_text(snapshot.owner, sizeof(snapshot.owner),
                           g_last_claim.owner);
  copy_optional_claim_text(snapshot.conflict_owner,
                           sizeof(snapshot.conflict_owner),
                           g_last_claim.conflict_owner);
  copy_optional_claim_text(snapshot.conflict_pin, kClaimPinMax,
                           g_last_claim.conflict_pin);
  return snapshot;
}

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

} // 匿名命名空间

bsp_board_profile_t bsp_board_profile() {
#if defined(CONFIG_LUNAREDU_BSP_PROFILE_REULEAUX_TRIANGLE_V3)
  return BSP_BOARD_PROFILE_REULEAUX_TRIANGLE_V3;
#elif defined(CONFIG_LUNAREDU_BSP_PROFILE_MINIMAL) || \
    defined(CONFIG_LUNAREDU_BSP_PROFILE_YUEZAI)
  return BSP_BOARD_PROFILE_MINIMAL;
#elif defined(CONFIG_LUNAREDU_BSP_PROFILE_BASE)
  return BSP_BOARD_PROFILE_BASE;
#else
  return BSP_BOARD_PROFILE_CAM_BUNDLE;
#endif
}

const char *bsp_board_profile_name() {
  switch (bsp_board_profile()) {
  case BSP_BOARD_PROFILE_BASE:
    return "base";
  case BSP_BOARD_PROFILE_CAM_BUNDLE:
    return "cam_bundle";
  case BSP_BOARD_PROFILE_MINIMAL:
#if defined(CONFIG_LUNAREDU_BSP_PROFILE_YUEZAI)
    return "yuezai";
#else
    return "minimal";
#endif
  case BSP_BOARD_PROFILE_REULEAUX_TRIANGLE_V3:
    return "reuleaux_triangle_v3";
  default:
    return "unknown";
  }
}

esp_err_t bsp_resource_acquire(bsp_resource_t resource, const char *owner) {
  ResourceLock lock;

  if (owner == nullptr || owner[0] == '\0' ||
      std::strlen(owner) >= kOwnerNameMax) return ESP_ERR_INVALID_ARG;
  const ResourceDescriptor *descriptor = descriptor_for(resource);
  copy_owner_name(g_last_claim_owner, sizeof(g_last_claim_owner), owner);
  const char *claim_owner = g_last_claim_owner;

  g_last_claim = {
      .ok = false,
      .owner = claim_owner,
      .conflict_owner = nullptr,
      .conflict_pin = nullptr,
  };

  if (descriptor == nullptr) {
    g_last_claim.conflict_owner = "invalid_resource";
    return ESP_ERR_INVALID_ARG;
  }

  if (!resource_available_for_profile(*descriptor)) {
    g_last_claim.conflict_owner = "board_profile_unavailable";
    return ESP_ERR_NOT_SUPPORTED;
  }

  const char *current_owner = g_owners[resource];
  if (current_owner != nullptr) {
    if (strcmp(current_owner, claim_owner) == 0) {
      g_last_claim.ok = true;
      return ESP_OK;
    }
    g_last_claim.conflict_owner = current_owner;
    return ESP_ERR_INVALID_STATE;
  }

  for (const ResourceDescriptor &other : kResources) {
    const char *other_owner = g_owners[other.id];
    if (other_owner == nullptr) {
      continue;
    }

    const uint64_t conflict = descriptor->pin_mask & other.pin_mask;
    if (conflict != 0) {
      g_last_claim.conflict_owner = other_owner;
      g_last_claim.conflict_pin = pin_name(conflict & (~conflict + 1));
      return ESP_ERR_INVALID_STATE;
    }
  }

  copy_owner_name(g_owner_storage[resource], sizeof(g_owner_storage[resource]),
                  claim_owner);
  g_owners[resource] = g_owner_storage[resource];
  g_last_claim.ok = true;
  return ESP_OK;
}

esp_err_t bsp_resource_release(bsp_resource_t resource, const char *owner) {
  ResourceLock lock;

  if (resource < 0 || resource >= BSP_RESOURCE_COUNT) {
    return ESP_ERR_INVALID_ARG;
  }

  const char *current_owner = g_owners[resource];
  if (current_owner == nullptr) {
    return ESP_OK;
  }

  if (owner != nullptr && strcmp(current_owner, owner) != 0) {
    return ESP_ERR_INVALID_STATE;
  }

  g_owners[resource] = nullptr;
  g_owner_storage[resource][0] = '\0';
  return ESP_OK;
}

bool bsp_resource_is_acquired(bsp_resource_t resource) {
  ResourceLock lock;

  if (resource < 0 || resource >= BSP_RESOURCE_COUNT) {
    return false;
  }
  return g_owners[resource] != nullptr;
}

const char *bsp_resource_name(bsp_resource_t resource) {
  const ResourceDescriptor *descriptor = descriptor_for(resource);
  return descriptor == nullptr ? "unknown" : descriptor->name;
}

const char *bsp_resource_owner(bsp_resource_t resource) {
  ResourceLock lock;

  if (resource < 0 || resource >= BSP_RESOURCE_COUNT) {
    return nullptr;
  }
  return g_owners[resource];
}

bool bsp_resource_contains_pin(bsp_resource_t resource, int pin) {
  const ResourceDescriptor *descriptor = descriptor_for(resource);
  if (descriptor == nullptr || pin < 0 || pin >= 64) {
    return false;
  }
  const uint64_t mask = 1ULL << static_cast<uint8_t>(pin);
  return (descriptor->pin_mask & mask) != 0;
}

const bsp_resource_claim_t *bsp_resource_last_claim() {
  ResourceLock lock;
  return &g_last_claim;
}

bsp_resource_claim_snapshot_t bsp_resource_last_claim_snapshot() {
  ResourceLock lock;
  return last_claim_snapshot_unlocked();
}

void bsp_resource_reset_all() {
  ResourceLock lock;

  for (const ResourceDescriptor &resource : kResources) {
    g_owners[resource.id] = nullptr;
    g_owner_storage[resource.id][0] = '\0';
  }
  g_last_claim_owner[0] = '\0';
  g_last_claim = {};
}

const bsp_baseboard_group_t *bsp_baseboard_groups(size_t *count) {
  if (count != nullptr) {
    *count = sizeof(kBaseboardGroups) / sizeof(kBaseboardGroups[0]);
  }
  return kBaseboardGroups;
}
