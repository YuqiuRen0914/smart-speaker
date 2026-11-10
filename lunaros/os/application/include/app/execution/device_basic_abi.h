#pragma once
#ifndef LUNAROS_DEVICE_BASIC_ABI_H
#define LUNAROS_DEVICE_BASIC_ABI_H
#include <stdint.h>

/* Canonical device.basic ABI 1. SDK's self-contained header mirrors this
 * block through tools/assets/generate-device-basic-sdk.mjs. No guest pointers,
 * GPIOs, bus handles, caller identities or allocation cross this boundary. */
enum LunaDeviceOperation {
  LUNA_DEVICE_QUERY=0, LUNA_DEVICE_RGB=1, LUNA_DEVICE_SERVO=2,
  LUNA_DEVICE_POSE_MDEG=3, LUNA_DEVICE_LIGHT_MV=4, LUNA_DEVICE_SOIL_PERMILLE=5
};
enum LunaDeviceQuery {
  LUNA_DEVICE_CAPABILITIES=0, LUNA_DEVICE_CONTRACT=1,
  LUNA_DEVICE_LAST_RESULT=2, LUNA_DEVICE_PENDING=3
};
enum LunaDeviceResult {
  LUNA_DEVICE_OK=0,
  LUNA_DEVICE_INVALID=INT32_MIN, LUNA_DEVICE_UNSUPPORTED=INT32_MIN+1,
  LUNA_DEVICE_DENIED=INT32_MIN+2, LUNA_DEVICE_REVOKED=INT32_MIN+3,
  LUNA_DEVICE_BUSY=INT32_MIN+4, LUNA_DEVICE_STALE=INT32_MIN+5,
  LUNA_DEVICE_PROVIDER_FAILED=INT32_MIN+6
};
#define LUNA_DEVICE_CONTRACT_VERSION 1
#define LUNA_DEVICE_CALLBACK_BUDGET 64
#define LUNA_DEVICE_CACHE_MS 250
#define LUNA_DEVICE_CAPABILITY(op) (1u << (op))
#define LUNA_DEVICE_VALUE_VALID(value) ((int32_t)(value) > (INT32_MIN + 1024))

/* X(operation, capability id, unit, minimum, maximum, writes output,
 *   cached scalar channels). Describes the contract, never product readiness. */
#define LUNA_DEVICE_CONTRACTS(X) \
  X(LUNA_DEVICE_RGB,"device.rgb","channel_u8",0,255,1,0) \
  X(LUNA_DEVICE_SERVO,"device.servo","microseconds",500,2500,1,0) \
  X(LUNA_DEVICE_POSE_MDEG,"device.pose","millidegrees",-360000,360000,0,3) \
  X(LUNA_DEVICE_LIGHT_MV,"sensor.light.voltage","millivolts",0,100000,0,1) \
  X(LUNA_DEVICE_SOIL_PERMILLE,"sensor.soil.moisture","permille",0,1000,0,1)
#define LUNA_DEVICE_SAMPLE_TOTAL(op,id,unit,lo,hi,writes,channels) + channels
enum { LUNA_DEVICE_SAMPLE_COUNT = 0 LUNA_DEVICE_CONTRACTS(LUNA_DEVICE_SAMPLE_TOTAL) };
#undef LUNA_DEVICE_SAMPLE_TOTAL
#define LUNA_DEVICE_KNOWN_BIT(op,id,unit,lo,hi,writes,channels) | LUNA_DEVICE_CAPABILITY(op)
enum { LUNA_DEVICE_KNOWN_CAPABILITIES = 0 LUNA_DEVICE_CONTRACTS(LUNA_DEVICE_KNOWN_BIT) };
#undef LUNA_DEVICE_KNOWN_BIT

static inline int luna_device_request_valid(int op,int a,int b,int c) {
  switch(op) {
  case LUNA_DEVICE_QUERY:return a>=0&&a<=3&&b==0&&c==0;
  case LUNA_DEVICE_RGB:return a>=0&&a<=255&&b>=0&&b<=255&&c>=0&&c<=255;
  case LUNA_DEVICE_SERVO:return (a==-1||(a>=500&&a<=2500))&&(b==-1||(b>=500&&b<=2500))&&c==0;
  case LUNA_DEVICE_POSE_MDEG:return a>=0&&a<3&&b==0&&c==0;
  case LUNA_DEVICE_LIGHT_MV:case LUNA_DEVICE_SOIL_PERMILLE:return a==0&&b==0&&c==0;
  default:return 0;
  }
}
#endif
