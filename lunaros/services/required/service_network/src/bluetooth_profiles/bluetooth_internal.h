#pragma once
#include "lunaros_bluetooth.h"
#include "lunaros_network_lifecycle.h"
#include <mutex>
namespace lunaredu::lunaros::bluetooth_service_internal {
struct Request { BluetoothAction action;uint16_t value;std::array<char,161> data{};uint32_t id=0; };
extern std::mutex mutex;
extern BluetoothSnapshot state;
extern KernelRuntime *runtime;
extern std::array<char,27> device_name;
bool take_request(Request &);
void set_request_admission(bool);
void report(const std::string &);
bool configure_backend(std::string &);
void observe_device(const BluetoothDevice &);
void receive(const uint8_t *,size_t);
}
