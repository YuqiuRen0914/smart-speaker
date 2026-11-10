#pragma once

#include <stdint.h>

#include "sdkconfig.h"

namespace lunaredu::bsp::pins {

// LunarEdu BSP 描述的是“一块共用 ESP32-S3 扩展底板 + 一个统一 N16R8 核心板 +
// 两种板载资源 profile”的硬件组合：
//
// - base profile:
//   基础装配。对外引脚定义与 cam bundle profile 一致。
//   板载 WS2812 接在 GPIO48 上，因此只有在底板 GPIO47/GPIO48 I2C/MPU6050
//   分组没有被选择时才可以使用。
//
// - cam bundle profile:
//   带摄像头扩展装配。GPIO2 小灯、camera 接口、TF 卡接口都视为可选资源，
//   不是永久占用；如果扩展底板选择了同一组引脚，对应板载功能就不能同时使用。
//
// 扩展底板的复用程度很高。下面每个分组都应当看作“可选择资源”，代码在配置
// 引脚前需要先通过 BSP 资源接口申请占用。典型互斥关系包括：
// camera 与 PWM/普通 IO、TF 卡与顶部分区 GPIO38/39/40、GPIO48 WS2812 与
// MPU6050 I2C、USB 与 I2S。
#if defined(CONFIG_LUNAREDU_BSP_PROFILE_YUEZAI)
constexpr const char *kBoardFamilyId = "yuezai";
constexpr const char *kBoardFamilyName = "LunarEdu Yuezai";
constexpr const char *kBoardId = "yuezai-202608-v1";
constexpr const char *kBoardName = "月仔";
constexpr const char *kBoardModel = "yuezai";
constexpr const char *kBoardType = "yuezai-202608-v1";
#elif defined(CONFIG_LUNAREDU_BSP_PROFILE_REULEAUX_TRIANGLE_V3)
constexpr const char *kBoardFamilyId = "reuleaux_triangle_v3";
constexpr const char *kBoardFamilyName = "Reuleaux Triangle V3";
constexpr const char *kBoardId = "reuleaux_triangle_v3";
constexpr const char *kBoardName = "LunarEdu Reuleaux Triangle V3";
constexpr const char *kBoardModel = "esp32-wroom-32-reuleaux-v3";
constexpr const char *kBoardType = "reuleaux-triangle-v3";
#else
constexpr const char *kBoardFamilyId = "lunaredu_s3_expansion_baseboard";
constexpr const char *kBoardFamilyName =
    "LunarEdu ESP32-S3 Expansion Baseboard";

#if defined(CONFIG_LUNAREDU_BSP_PROFILE_MINIMAL)
constexpr const char *kBoardId = "lunaredu_s3_n16r8_minimal";
constexpr const char *kBoardName = "LunarEdu ESP32-S3-N16R8 Minimal";
constexpr const char *kBoardModel = "esp32-s3-n16r8-minimal";
#elif defined(CONFIG_LUNAREDU_BSP_PROFILE_BASE)
constexpr const char *kBoardId = "lunaredu_s3_n16r8_baseboard";
constexpr const char *kBoardName =
    "LunarEdu ESP32-S3-N16R8 Expansion Baseboard";
constexpr const char *kBoardModel = "esp32-s3-n16r8-baseboard";
#else
constexpr const char *kBoardId = "lunaredu_s3_n16r8cam_baseboard";
constexpr const char *kBoardName =
    "LunarEdu ESP32-S3-N16R8-CAM Expansion Baseboard";
constexpr const char *kBoardModel = "esp32-s3-n16r8cam-baseboard";
#endif

constexpr const char *kBoardType = "lunaredu-hardware";
#endif

// 启动/绑带受限引脚。复位期间，外部电路应保持高阻、弱上下拉、按键、跳线或
// 开漏状态，避免影响 ESP32-S3 启动模式。
constexpr uint8_t kBootIo0 = 0;
constexpr uint8_t kBatAdc = 3;
constexpr uint8_t kBootIo3BatAdc = kBatAdc;
constexpr uint8_t kBootIo45 = 45;
constexpr uint8_t kBootIo46 = 46;

// 右侧中部分区的默认调试/通信 UART。
constexpr uint8_t kUartTx = 43;
constexpr uint8_t kUartRx = 44;

// 原生 USB 引脚。这两个引脚也参与可选的 I2S 数据/片选复用。
constexpr uint8_t kUsbDMinus = 19;
constexpr uint8_t kUsbDPlus = 20;

// 中心分区：默认单个 MPU6050 使用的 I2C 分组。
constexpr uint8_t kMpuI2cSda = 48;
constexpr uint8_t kMpuI2cScl = 47;
constexpr uint32_t kMpuI2cClockHz = 400000;

// 右侧中部分区：扩展 I2C1 候选分组，当前按 GPIO1=SDA、GPIO2=SCL 记录。
// GPIO2 与 cam 核心板板载小灯互斥。若最终丝印/原理图确认方向相反，只需要
// 在这里交换两个常量，使用该分组的业务代码不用跟着改。
constexpr uint8_t kExpansionI2c1Sda = 1;
constexpr uint8_t kExpansionI2c1Scl = 2;
// Touch panels are often wired through loose jumpers during validation.
// Keep the expansion touch bus conservative until the hardware is finalized.
constexpr uint32_t kExpansionI2c1ClockHz = 100000;

constexpr int kMainI2cPort = 0;
constexpr uint8_t kMainI2cSda = kMpuI2cSda;
constexpr uint8_t kMainI2cScl = kMpuI2cScl;
constexpr uint32_t kMainI2cClockHz = kMpuI2cClockHz;

// 两种核心板上的板载指示资源。
constexpr uint8_t kStatusLed = 2;
constexpr bool kStatusLedInverted = false;
constexpr uint8_t kOnboardWs2812 = 48;
constexpr uint16_t kOnboardWs2812Count = 1;

// cam bundle profile 的可选 camera 资源。启用后会占用大量原本可作为 GPIO/PWM/SPI
// 类信号暴露到底板的引脚。
constexpr int8_t kCameraPowerDown = -1;
constexpr int8_t kCameraReset = -1;
constexpr uint8_t kCameraSiod = 4;
constexpr uint8_t kCameraSioc = 5;
constexpr uint8_t kCameraVsync = 6;
constexpr uint8_t kCameraHref = 7;
constexpr uint8_t kCameraXclk = 15;
constexpr uint8_t kCameraY2 = 11;
constexpr uint8_t kCameraY3 = 9;
constexpr uint8_t kCameraY4 = 8;
constexpr uint8_t kCameraY5 = 10;
constexpr uint8_t kCameraY6 = 12;
constexpr uint8_t kCameraY7 = 18;
constexpr uint8_t kCameraY8 = 17;
constexpr uint8_t kCameraY9 = 16;
constexpr uint8_t kCameraPclk = 13;

// cam bundle profile 的可选 1bit TF 卡资源。启用后 GPIO38/39/40 不再能作为顶部分区
// 扩展 IO 使用。
constexpr uint8_t kTfCardData0 = 40;
constexpr uint8_t kTfCardClock = 39;
constexpr uint8_t kTfCardCommand = 38;
constexpr uint8_t kTfCardBusWidth = 1;

// 顶部分区 SPI1 候选分组。这里先故意使用中性的 SignalA/B/C 命名，等原理图/
// 丝印最终确认 SCK/MOSI/MISO 方向后再定名；GPIO42 是对应片选。
constexpr uint8_t kExpansionSpi1SignalA = 41;
constexpr uint8_t kExpansionSpi1SignalB = 21;
constexpr uint8_t kExpansionSpi1SignalC = 14;
constexpr uint8_t kExpansionSpi1Cs = 42;

// 显示屏类接线/控制信号的别名。保留这些名字是为了让用途更直观。
constexpr uint8_t kTopDc = kExpansionSpi1SignalA;
constexpr uint8_t kTopCtrl21 = kExpansionSpi1SignalB;
constexpr uint8_t kTopCtrl14 = kExpansionSpi1SignalC;
constexpr uint8_t kCs1 = kExpansionSpi1Cs;
constexpr uint8_t kCs2 = 13;

// 左侧分区通用 IO/PWM 分组。
constexpr uint8_t kLeftUpperIoA = 4;
constexpr uint8_t kLeftUpperIoB = 5;
constexpr uint8_t kLeftLowerIoA = 0;
constexpr uint8_t kLeftLowerIoB = 10;

constexpr uint8_t kPwmBank0A = 6;
constexpr uint8_t kPwmBank0B = 7;
constexpr uint8_t kPwmBank1A = 15;
constexpr uint8_t kPwmBank1B = 16;
constexpr uint8_t kPwmBank2A = 17;
constexpr uint8_t kPwmBank2B = 18;
constexpr uint8_t kPwmBank3A = 11;
constexpr uint8_t kPwmBank3B = 12;

} // 命名空间 lunaredu::bsp::pins
