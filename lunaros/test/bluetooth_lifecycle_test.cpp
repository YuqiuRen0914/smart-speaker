#include "bluetooth_stub.h"
#include "network/bluetooth/bluetooth.h"
#include <cassert>
#include <iostream>
BleConfig ble_hs_cfg;
int starts=0,deinits=0,installs=0,syncs=0,resets=0;
bool fail_install=true,fail_stop=false,fail_deinit=false;
int stops=0;
const char *esp_err_to_name(int) { return "fake_error"; }
int nimble_port_init() { ++starts; return 0; }
int nimble_port_deinit() { ++deinits; return fail_deinit ? 9 : 0; }
int nimble_port_stop() { ++stops;return fail_stop ? 7 : 0; }
void nimble_port_run() {}
int esp_nimble_enable(void *) { assert(installs == starts); return 0; }
void nimble_port_freertos_deinit() {}
void ble_svc_gap_init() {}
void ble_svc_gatt_init() {}
int ble_store_util_status_rr(int,void *) { return 0; }
extern "C" void ble_store_config_init() {}
bool install(void *,std::string &error) {
 ++installs;if(fail_install) {error="gatt_failed";return false;} return true;
}
int main() {
 using namespace lunaredu::lunaros;
 std::string error;
 BluetoothProfile profile{"test",install,[](void*){++syncs;},[](void*){++resets;}};
 assert(register_bluetooth_profile(profile,error));
 assert(register_bluetooth_profile(profile,error));
 assert(!start_bluetooth(error) && error=="gatt_failed" && deinits==1);
 fail_install=false;
 assert(start_bluetooth(error));
 assert(start_bluetooth(error) && starts==2);
 assert(!bluetooth_synchronized());
 ble_hs_cfg.sync_cb();assert(bluetooth_synchronized() && syncs==1);
 auto late=profile;late.id="late";assert(!register_bluetooth_profile(late,error));
 assert(register_bluetooth_profile(profile,error));
 fail_stop=true;assert(!stop_bluetooth(error));assert(bluetooth_synchronized());
 fail_stop=false;assert(stop_bluetooth(error));assert(!bluetooth_synchronized() && resets==1);
 assert(stop_bluetooth(error) && deinits==2);
 assert(start_bluetooth(error) && starts==3 && installs==3);
 ble_hs_cfg.sync_cb();assert(syncs==2);
 ble_hs_cfg.reset_cb(1);assert(!bluetooth_synchronized() && resets==2);
 fail_deinit=true;assert(!stop_bluetooth(error));
 const int stopped_count=stops;
 assert(!start_bluetooth(error)); // Retry cleanup without starting a second host.
 fail_deinit=false;assert(stop_bluetooth(error));assert(stops==stopped_count);
 assert(start_bluetooth(error));assert(stop_bluetooth(error));
 std::cout<<"Bluetooth single host, GATT-before-start, rollback, sync, restart and stop passed\n";
}
