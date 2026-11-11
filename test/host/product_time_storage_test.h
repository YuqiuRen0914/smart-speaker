#pragma once

#include <string>
#include <functional>

namespace test_storage {
extern std::string record;
extern bool ready;
extern bool fail_read;
extern bool fail_write;
extern size_t writes;
extern std::function<void()> before_write;
void reset();
}
