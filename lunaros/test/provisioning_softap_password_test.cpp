#include <cassert>
#include <iostream>

#include "network/provisioning/provisioning_softap_password.h"

using namespace lunaredu::lunaros;

namespace {

void test_generated_password_is_eight_numeric_digits() {
  assert(make_provisioning_softap_password(0) == "00000000");
  assert(make_provisioning_softap_password(7) == "00000007");
  assert(make_provisioning_softap_password(123456789) == "23456789");
  assert(is_provisioning_softap_password(
      make_provisioning_softap_password(0xFFFFFFFFu)));
}

void test_only_eight_numeric_digits_are_reused() {
  assert(is_provisioning_softap_password("12345678"));
  assert(is_provisioning_softap_password("00000000"));
  assert(!is_provisioning_softap_password("1234567"));
  assert(!is_provisioning_softap_password("123456789"));
  assert(!is_provisioning_softap_password("1234abcd"));
  assert(!is_provisioning_softap_password("0123456789abcdef01234567"));
}

} // namespace

int main() {
  test_generated_password_is_eight_numeric_digits();
  test_only_eight_numeric_digits_are_reused();
  std::cout << "provisioning SoftAP password tests passed: 2\n";
  return 0;
}
