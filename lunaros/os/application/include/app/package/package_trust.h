#pragma once
#include "app/package/application_package.h"
#include "app/package/package_signature.h"
namespace lunaredu::lunaros::apps {
inline bool verify_aot_package_signature(const Application &package, std::string &error) {
  return package.entry_format != "aot" || verify_aot_signature(
      package.sha256, package.signer_id, package.package_signature, error);
}
}
