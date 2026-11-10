#include <cassert>

#include "device_api/access/device_api_access_policy.h"

int main() {
  using Surface = DeviceApiAccessSurface;

  assert(device_api_access_policy_allows(false, Surface::Metadata));
  assert(device_api_access_policy_allows(false, Surface::Operation,
                                        "desktop_pet.motion.set"));

  assert(!device_api_access_policy_allows(true, Surface::Metadata));
  assert(!device_api_access_policy_allows(true, Surface::Operation, nullptr));
  assert(!device_api_access_policy_allows(true, Surface::Operation,
                                         "desktop_pet.state.get"));
  assert(device_api_access_policy_allows(true, Surface::Operation,
                                        "network.provision.status"));
  assert(device_api_access_policy_allows(true, Surface::Operation,
                                        "network.provision.scan"));
  assert(device_api_access_policy_allows(true, Surface::Operation,
                                        "network.provision.submit"));


  assert(device_api_origin_allowed("http://192.168.1.23", "192.168.1.23"));
  assert(device_api_origin_allowed("http://192.168.1.23:80", "192.168.1.23"));
  assert(!device_api_origin_allowed("http://192.168.1.24", "192.168.1.23"));
  assert(!device_api_origin_allowed("http://192.168.1.23:3000", "192.168.1.23"));
  assert(!device_api_origin_allowed("http://192.168.1.23.evil.test", "192.168.1.23"));
  assert(!device_api_origin_allowed("http://0.0.0.0", "0.0.0.0"));
  assert(!device_api_origin_allowed("http://evil.test", "evil.test"));
  assert(device_api_origin_allowed(nullptr));
  assert(device_api_origin_allowed(""));
  assert(device_api_origin_allowed("https://tools.lunaredu.cn"));
  assert(device_api_origin_allowed("https://board.lunaredu.cn"));
  assert(device_api_origin_allowed("http://board.lunaredu.localhost:3000"));
  assert(device_api_origin_allowed("http://board.lunaredu.192.168.31.10.nip.io"));
  assert(device_api_origin_allowed("http://board.lunaredu.10.0.0.5.nip.io:3000"));
  assert(!device_api_origin_allowed("https://board.lunaredu.cn.evil.test"));
  assert(!device_api_origin_allowed("http://board.lunaredu.999.1.1.1.nip.io"));
  assert(!device_api_origin_allowed("http://board.lunaredu.192.168.31.10.nip.io/path"));
  assert(device_api_origin_allowed("http://localhost:3000"));
  assert(device_api_origin_allowed("http://127.0.0.1:15555"));
  assert(device_api_origin_allowed("http://[::1]:3000"));
  assert(device_api_origin_allowed("http://tools.lunaredu.localhost:3000"));
  assert(device_api_origin_allowed("http://192.168.4.1"));
  assert(device_api_origin_allowed("http://192.168.4.1:80"));
  assert(device_api_origin_allowed(
      "http://tools.lunaredu.192.168.31.10.nip.io"));
  assert(device_api_origin_allowed(
      "http://tools.lunaredu.10.0.0.5.nip.io:3000"));
  assert(!device_api_origin_allowed("https://evil.example"));
  assert(!device_api_origin_allowed("https://localhost:3000"));
  assert(!device_api_origin_allowed("http://localhost.evil.example"));
  assert(!device_api_origin_allowed("http://192.168.4.1:3000"));
  assert(!device_api_origin_allowed("http://192.168.4.10"));
  assert(!device_api_origin_allowed(
      "http://tools.lunaredu.999.1.1.1.nip.io"));
  assert(!device_api_origin_allowed(
      "http://tools.lunaredu.192.168.31.10.nip.io/path"));
  assert(!device_api_origin_allowed("http://127.0.0.1:99999"));
  return 0;
}
