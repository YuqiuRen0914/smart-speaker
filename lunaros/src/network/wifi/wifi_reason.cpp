#include "wifi_reason.h"

#include <esp_wifi.h>

// ESP-IDF 6 adjusted a few Wi-Fi disconnect reason constant names.
#ifndef WIFI_REASON_ASSOC_EXPIRE
#define WIFI_REASON_ASSOC_EXPIRE WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY
#endif

#ifndef WIFI_REASON_NOT_AUTHED
#define WIFI_REASON_NOT_AUTHED WIFI_REASON_CLASS2_FRAME_FROM_NONAUTH_STA
#endif

#ifndef WIFI_REASON_NOT_ASSOCED
#define WIFI_REASON_NOT_ASSOCED WIFI_REASON_CLASS3_FRAME_FROM_NONASSOC_STA
#endif

const char *wifi_disconnect_reason_name(int reason) {
  switch (reason) {
  case WIFI_REASON_UNSPECIFIED:
    return "未指定";
  case WIFI_REASON_AUTH_EXPIRE:
    return "认证超时";
  case WIFI_REASON_AUTH_LEAVE:
    return "认证主动离开";
  case WIFI_REASON_ASSOC_EXPIRE:
    return "关联超时";
  case WIFI_REASON_ASSOC_TOOMANY:
    return "关联数过多";
  case WIFI_REASON_NOT_AUTHED:
    return "尚未认证";
  case WIFI_REASON_NOT_ASSOCED:
    return "尚未关联";
  case WIFI_REASON_ASSOC_LEAVE:
    return "关联主动离开";
  case WIFI_REASON_ASSOC_NOT_AUTHED:
    return "关联但未认证";
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    return "四次握手超时";
#ifdef WIFI_REASON_STA_LEAVING
  case WIFI_REASON_STA_LEAVING:
    return "STA 主动离开";
#endif
  case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
    return "组密钥更新超时";
  case WIFI_REASON_IE_IN_4WAY_DIFFERS:
    return "四次握手 IE 不一致";
  case WIFI_REASON_HANDSHAKE_TIMEOUT:
    return "握手超时";
  case WIFI_REASON_NO_AP_FOUND:
    return "未找到接入点";
  case WIFI_REASON_AUTH_FAIL:
    return "认证失败";
  case WIFI_REASON_ASSOC_FAIL:
    return "关联失败";
  case WIFI_REASON_CONNECTION_FAIL:
    return "连接失败";
  case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    return "未找到兼容安全模式的接入点";
  case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    return "未找到满足认证门槛的接入点";
  case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
    return "未找到满足信号强度门槛的接入点";
  default:
    return "未知原因";
  }
}
