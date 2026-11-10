#pragma once

#include <stddef.h>
#include <stdint.h>

#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <esp_err.h>



typedef struct {
  const char *id;
  const char *name;
  const char *board_type;
  const char *model;
  const char *hardware_revision;
} bsp_board_info_t;

typedef enum {
  BSP_BOARD_PROFILE_NONE = -1,
  BSP_BOARD_PROFILE_BASE = 0,
  BSP_BOARD_PROFILE_CAM_BUNDLE,
  BSP_BOARD_PROFILE_MINIMAL,
  BSP_BOARD_PROFILE_XIAOXING,
  BSP_BOARD_PROFILE_REMOTE_CONTROLLER,
  BSP_BOARD_PROFILE_REULEAUX_TRIANGLE_V3,
} bsp_board_profile_t;

// 历史扩展底板资源名称与分区契约；只有显式启用相应封存 profile 才可使用。
// 新板型不能从这些名称推导接线。运行时可用性以所选板级描述为准。
//
// 默认分组：
// - BSP_RESOURCE_BASEBOARD_MPU6050_I2C：中心分区，GPIO48 SDA / GPIO47 SCL。
// - BSP_RESOURCE_EXPANSION_I2C1：右侧中部分区，GPIO1/GPIO2。
// - BSP_RESOURCE_EXPANSION_SPI1：顶部分区，GPIO41/GPIO21/GPIO14 + CS GPIO42。
//
// 板载资源 profile：
// - BSP_RESOURCE_ONBOARD_WS2812 只存在于 base profile，和默认
//   MPU6050 I2C 分组共用 GPIO48。
// - BSP_RESOURCE_ONBOARD_STATUS_LED 只存在于 cam bundle profile，和扩展 I2C1/IO
//   共用 GPIO2。
// - BSP_RESOURCE_CAMERA 和 BSP_RESOURCE_TFCARD 只存在于 cam bundle profile，并且
//   都是可选资源；启用后对应引脚不能再给扩展底板其它功能使用。
//
// 高风险或强互斥选择：
// - GPIO0/GPIO3/GPIO45/GPIO46 是启动/绑带受限引脚。
// - GPIO19/GPIO20 是 USB D-/D+，也参与可选 I2S 数据/片选分组。
// - Camera 与左侧大量 PWM/普通 IO 扩展互斥。
// - TF 卡与顶部分区 GPIO38/GPIO39/GPIO40 扩展互斥。
typedef enum {
  BSP_RESOURCE_BASEBOARD_MPU6050_I2C = 0,
  BSP_RESOURCE_EXPANSION_I2C1,
  BSP_RESOURCE_EXPANSION_SPI1,
  BSP_RESOURCE_ONBOARD_WS2812,
  BSP_RESOURCE_ONBOARD_STATUS_LED,
  BSP_RESOURCE_CAMERA,
  BSP_RESOURCE_TFCARD,
  BSP_RESOURCE_USB_NATIVE,
  BSP_RESOURCE_I2S_DATA,
  BSP_RESOURCE_PWM_SERVO_BANK,
  BSP_RESOURCE_PWM_MOTOR_BANK,
  BSP_RESOURCE_BAT_ADC,
  // XiaoXing uses a fixed product wiring contract rather than the expansion
  // baseboard's coarse I2C/SPI groups.
  BSP_RESOURCE_XIAOXING_I2S_AUDIO,
  BSP_RESOURCE_REMOTE_CONTROLLER_TOUCH,
  BSP_RESOURCE_COUNT,
} bsp_resource_t;

typedef struct {
  bool ok;
  const char *owner;
  const char *conflict_owner;
  const char *conflict_pin;
} bsp_resource_claim_t;

#define BSP_RESOURCE_CLAIM_OWNER_MAX 32
#define BSP_RESOURCE_CLAIM_PIN_MAX 16

typedef struct {
  bool ok;
  char owner[BSP_RESOURCE_CLAIM_OWNER_MAX];
  char conflict_owner[BSP_RESOURCE_CLAIM_OWNER_MAX];
  char conflict_pin[BSP_RESOURCE_CLAIM_PIN_MAX];
} bsp_resource_claim_snapshot_t;

typedef enum {
  BSP_PIN_USE_GPIO = 1u << 0,
  BSP_PIN_USE_PWM = 1u << 1,
  BSP_PIN_USE_KEY = 1u << 2,
  BSP_PIN_USE_I2C = 1u << 3,
  BSP_PIN_USE_UART = 1u << 4,
  BSP_PIN_USE_SPI = 1u << 5,
  BSP_PIN_USE_I2S = 1u << 6,
  BSP_PIN_USE_USB = 1u << 7,
  BSP_PIN_USE_ADC = 1u << 8,
  BSP_PIN_USE_CS = 1u << 9,
  BSP_PIN_USE_CAMERA = 1u << 10,
  BSP_PIN_USE_TFCARD = 1u << 11,
  BSP_PIN_USE_BOOT_STRAP = 1u << 12,
  BSP_PIN_USE_ONBOARD = 1u << 13,
} bsp_pin_use_flags_t;

typedef enum {
  BSP_BASEBOARD_ZONE_TOP = 0,
  BSP_BASEBOARD_ZONE_CENTER,
  BSP_BASEBOARD_ZONE_LEFT_UPPER,
  BSP_BASEBOARD_ZONE_LEFT_MIDDLE,
  BSP_BASEBOARD_ZONE_LEFT_LOWER,
  BSP_BASEBOARD_ZONE_RIGHT_UPPER,
  BSP_BASEBOARD_ZONE_RIGHT_MIDDLE,
  BSP_BASEBOARD_ZONE_RIGHT_LOWER,
} bsp_baseboard_zone_t;

typedef struct {
  const char *id;
  bsp_baseboard_zone_t zone;
  const uint8_t *pins;
  size_t pin_count;
  uint32_t use_flags;
  const char *layout;
  const char *notes;
} bsp_baseboard_group_t;

typedef struct {
  const char *id;
  uint8_t sda_pin;
  uint8_t scl_pin;
  uint32_t clock_hz;
  bsp_resource_t resource;
} bsp_i2c_bus_t;

typedef enum {
  BSP_SHARED_PIN_MODE_NONE = 0,
  BSP_SHARED_PIN_MODE_PWM3,
  BSP_SHARED_PIN_MODE_I2S,
  BSP_SHARED_PIN_MODE_BASEBOARD_MPU6050_I2C,
} bsp_shared_pin_mode_t;

typedef struct {
  bool mounted;
  const char *mount_path;
  uint64_t capacity_bytes;
  const char *last_error;
} bsp_sdcard_status_t;

esp_err_t bsp_board_init();
const bsp_board_info_t *bsp_board_get_info();
bsp_board_profile_t bsp_board_profile();
const char *bsp_board_profile_name();

esp_err_t bsp_resource_acquire(bsp_resource_t resource, const char *owner);
esp_err_t bsp_resource_release(bsp_resource_t resource, const char *owner);
bool bsp_resource_is_acquired(bsp_resource_t resource);
const char *bsp_resource_name(bsp_resource_t resource);
const char *bsp_resource_owner(bsp_resource_t resource);
bool bsp_resource_contains_pin(bsp_resource_t resource, int pin);
typedef struct {
  const char *name; // Immutable board descriptor storage.
  bool configured_available;
  uint64_t pin_mask;
  char owner[BSP_RESOURCE_CLAIM_OWNER_MAX];
} bsp_resource_observation_t;
bool bsp_resource_observe(bsp_resource_t resource, bsp_resource_observation_t *out);
typedef struct {
  const char *role;
  int gpio;
} bsp_signal_observation_t;
// Roles declared by the selected board/bus contract, never electrical probing.
size_t bsp_resource_signals(bsp_resource_t resource, bsp_signal_observation_t *out, size_t capacity);
const bsp_resource_claim_t *bsp_resource_last_claim();
bsp_resource_claim_snapshot_t bsp_resource_last_claim_snapshot();
void bsp_resource_reset_all();
const bsp_baseboard_group_t *bsp_baseboard_groups(size_t *count);

const bsp_i2c_bus_t *bsp_mpu_i2c_bus();
const bsp_i2c_bus_t *bsp_expansion_i2c1_bus();
// BSP I2C 是共享单例总线；init/deinit/read/write 在组件内串行化。
// 单个设备模块不应在其他模块仍可能使用总线时调用 deinit。
esp_err_t bsp_i2c_init(const bsp_i2c_bus_t *bus);
esp_err_t bsp_i2c_deinit();
bool bsp_i2c_ready();
i2c_master_bus_handle_t bsp_i2c_master_bus_handle();
bool bsp_i2c_write_bytes(uint8_t address, const uint8_t *data, size_t size,
                         bool send_stop);
bool bsp_i2c_read_bytes(uint8_t address, uint8_t *data, size_t size);
bool bsp_i2c_write_read_bytes(uint8_t address, const uint8_t *write_data,
                              size_t write_size, uint8_t *read_data,
                              size_t read_size);

typedef struct {
  const char *id;
  spi_host_device_t host;
  int mosi_pin;
  int miso_pin;
  int sclk_pin;
  int max_transfer_sz;
  bsp_resource_t resource;
} bsp_spi_bus_t;

esp_err_t bsp_spi_init(const bsp_spi_bus_t *bus);
esp_err_t bsp_spi_deinit();
bool bsp_spi_ready();

typedef struct {
  int pin;
  bool inverted;
} bsp_status_led_board_config_t;
esp_err_t bsp_status_led_get_board_config(bsp_status_led_board_config_t *config);
// Selected board pin, or -1 when no battery ADC has been declared.
int bsp_battery_adc_pin();
// "legacy" / "product" / "unspecified"; does not assert hardware validation.
const char *bsp_board_support_status();
esp_err_t bsp_status_led_init();
esp_err_t bsp_status_led_set(bool on);
esp_err_t bsp_status_led_deinit();

esp_err_t bsp_sdcard_mount(const char *mount_path);
esp_err_t bsp_sdcard_unmount();
bsp_sdcard_status_t bsp_sdcard_get_status();

esp_err_t bsp_shared_pins_enter(bsp_shared_pin_mode_t mode);
esp_err_t bsp_shared_pins_leave(bsp_shared_pin_mode_t mode);
bsp_shared_pin_mode_t bsp_shared_pins_get_mode();

// Zero-initialize before use. Copying transfers release responsibility; callers
// must clear the source and must not release two copies of the same lease.
typedef struct {
  uint64_t lease_id;
  bool shared_i2s_entered;
  bool expansion_spi1_claimed;
  bool expansion_i2c1_claimed;
  bool baseboard_i2c_claimed;
  bool pwm_bank_claimed;
  bool xiaoxing_i2s_audio_claimed;
} bsp_i2s_resource_claim_t;

esp_err_t bsp_i2s_claim_pins(const int *pins, size_t pin_count,
                             bsp_i2s_resource_claim_t *claim);
esp_err_t bsp_i2s_release_pins(bsp_i2s_resource_claim_t *claim);
bool bsp_i2s_uses_shared_pins(const int *pins, size_t pin_count);
