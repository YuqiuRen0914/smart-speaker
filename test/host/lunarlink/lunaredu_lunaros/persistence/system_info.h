#pragma once
#include <string>
namespace lunaredu::system {
struct SystemInfo {
 static int GetFlashSize(){return 16*1024*1024;}
 static int GetMinimumFreeHeapSize(){return 10000;}
 static std::string GetChipModelName(){return "ESP32-S3";}
 static std::string GetMacAddress(){return "00:11:22:33:44:55";}
};
}
