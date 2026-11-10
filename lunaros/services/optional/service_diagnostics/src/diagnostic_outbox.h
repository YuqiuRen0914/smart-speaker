#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "sdkconfig.h"
namespace lunaredu::lunaros::internal::diagnostic_outbox {
struct CaptureAuthority {int binding_epoch=0;uint64_t credential_fingerprint=0;};
using CaptureWriter=bool(*)(const uint8_t *,size_t,void *);
using CaptureProducer=bool(*)(CaptureWriter,void *,void *,std::string &);
struct Status {
 bool configured=false,accepting=false,io_active=false,inventory_known=false;
 uint32_t retained_records=0,last_attempt_ms=0;
 std::string last_result;
};
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
Status status();
#else
inline Status status(){return {};}
#endif
void resume();
void poll();
bool shutdown(std::string &error);
// Trusted native capture only, on the retained diagnostic Network I/O job.
// No remote path or credential may enter this API. Success means persisted,
// not server-acknowledged; shutdown retains producers until I/O has retired.
bool enqueue(const char *kind,const char *elf_sha256,const uint8_t *bytes,size_t size,const CaptureAuthority &authority,std::string &error);
// Synchronous, atomic streamed capture. Exact size/hash and capture authority
// are rechecked before publication. The producer/context is never retained.
bool enqueue_stream(const char *kind,const char *elf_sha256,const char *source_sha256,
    size_t size,CaptureProducer producer,void *context,const CaptureAuthority &authority,std::string &error);
}
