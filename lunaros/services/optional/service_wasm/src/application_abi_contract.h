#pragma once
#include "app/execution/application_abi.h"
#include <cstddef>
namespace lunaredu::lunaros::apps::abi {
struct Import {const char *name;unsigned parameters;bool device;};
struct Export {const char *name;unsigned kind,parameters;bool required;};
#define LUNA_IMPORT_RECORD(name,method,signature,parameters,device,args) {#name,parameters,bool(device)},
inline constexpr Import imports[]{LUNAROS_APP_ABI_IMPORTS(LUNA_IMPORT_RECORD)};
#undef LUNA_IMPORT_RECORD
#define LUNA_EXPORT_RECORD(name,kind,parameters,required) {#name,kind,parameters,bool(required)},
inline constexpr Export exports[]{LUNAROS_APP_ABI_EXPORTS(LUNA_EXPORT_RECORD)};
#undef LUNA_EXPORT_RECORD
inline constexpr size_t import_count=sizeof(imports)/sizeof(imports[0]);
inline constexpr size_t export_count=sizeof(exports)/sizeof(exports[0]);
}
