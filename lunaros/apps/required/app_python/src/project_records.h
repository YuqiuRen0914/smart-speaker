#pragma once
#include <string>
namespace lunaredu::python {
// Resolve tagged project autorun and legacy project selections from their own
// records. found=false leaves an ordinary standalone script to its owner.
bool project_boot_source(const std::string &selection,std::string &source,
    std::string &program,bool &found,std::string &error);
}
