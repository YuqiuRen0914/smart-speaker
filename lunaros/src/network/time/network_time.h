#pragma once

#include <cstdint>

#include <esp_err.h>

// 设置进程级 POSIX 时区（例如中国标准时间使用 "CST-8"）。调用后，
// localtime/localtime_r 会统一返回对应的本地时间。
esp_err_t network_set_system_timezone(const char *timezone_spec);

// 非阻塞启动 SNTP。适合在网络连接事件中调用；同步完成后会自动记录
// UTC、本地时间与当前时区，并由 lwIP 继续周期校时。
esp_err_t network_start_system_time_sync();

// Stops LunarOS-owned SNTP state during Network Service shutdown. The system
// clock value itself is intentionally preserved.
void network_stop_system_time_sync();

// 当前系统时间是否已经达到可用于 TLS 证书有效期校验的范围。
bool network_system_time_valid();

// 启动 SNTP（首次调用时）并等待系统时间同步。SNTP 会保持运行，后续由
// lwIP 定期校准系统时间。该函数可以由多个网络服务安全地重复调用。
esp_err_t network_sync_system_time(uint32_t timeout_ms, bool force = false);
