#pragma once
#include <string>
// Private contract: called only through the lunar.python service executor.
bool acquire_micropython_application_on_owner(std::string &);
void release_micropython_application_on_owner();
