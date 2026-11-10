#pragma once
#include "lunaros_network_http_client.h"
#include <string>
namespace lunaredu::lunaros {
// Uses the current binding credential, origin and Network TLS policy. Call only
// on an admitted Network owner; borrowed stream lifetime ends on return.
bool platform_link_diagnostic_policy(bool &enabled,int &binding_epoch);
bool platform_link_upload_diagnostic(const char *kind,const char *elf_sha256,
    const char *sha256,NetworkBodyStream body,NetworkResponse &response);
}
