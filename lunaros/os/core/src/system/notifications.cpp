#include "system/notifications.h"
#include "kernel/static_mutex.h"
#include <cstring>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#include "sdkconfig.h"
#endif

#if defined(ESP_PLATFORM) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#define NOTIFICATION_RECORD_DATA EXT_RAM_BSS_ATTR
#else
#define NOTIFICATION_RECORD_DATA
#endif

namespace lunaredu::lunaros::notifications {
namespace {
struct Record {
  char app[48]{}, id[48]{}, title[96]{};
  Kind kind = Kind::Message;
  uint64_t expires = 0;
  bool acknowledged = false;
};
// Records are accessed only by normal tasks under the internal control lock.
// They do not participate in cache-disabled flash operations or interrupts.
NOTIFICATION_RECORD_DATA std::array<Record, kCapacity> records{};
StaticMutex mutex;
uint64_t sequence=0;
SnapshotRevision revision_locked(uint64_t now){SnapshotRevision r{sequence,0};for(size_t i=0;i<records.size();++i)if(*records[i].app && !(records[i].expires && records[i].expires<=now))r.live_mask|=uint32_t(1)<<i;return r;}

bool valid(const char *value, size_t capacity) {
  return value && *value && std::strlen(value) < capacity;
}
bool expired(const Record &record, uint64_t now) {
  return record.expires && record.expires <= now;
}
Record *find(const char *app, const char *id) {
  if (!valid(app, 48) || !valid(id, 48)) return nullptr;
  for (auto &record : records)
    if (!std::strcmp(record.app, app) && !std::strcmp(record.id, id)) return &record;
  return nullptr;
}
} // namespace
void reset() {
  StaticLockGuard lock(mutex);
  for (auto &record : records) record = {};
  ++sequence;
}

namespace {
bool publish_locked(const Notice &notice, uint64_t now) {
  if (!valid(notice.app_id, 48) || !valid(notice.id, 48) || !valid(notice.title, 96) ||
      unsigned(notice.kind) > unsigned(Kind::Reminder) ||
      (notice.expires_ms && notice.expires_ms <= now)) return false;
  for (auto &record : records) if (expired(record, now)) record = {};
  Record *record = find(notice.app_id, notice.id);
  if (!record) for (auto &candidate : records) if (!*candidate.app) { record = &candidate; break; }
  if (!record) for (auto &candidate : records)
    if (candidate.kind == Kind::Message && candidate.acknowledged) { record = &candidate; *record = {}; break; }
  if (!record) return false;
  if (*record->app && record->kind==notice.kind && record->expires==notice.expires_ms && !std::strcmp(record->title,notice.title)) return true;
  const bool acknowledged = *record->app && record->kind == notice.kind && record->acknowledged;
  std::strcpy(record->app, notice.app_id);
  std::strcpy(record->id, notice.id);
  std::strcpy(record->title, notice.title);
  record->kind = notice.kind;
  record->expires = notice.expires_ms;
  record->acknowledged = acknowledged;
  ++sequence;
  return true;
}
Summary summary_locked(const char *app, uint64_t now) {
  Summary result;
  if (app && !valid(app, 48)) return result;
  for (const auto &record : records) {
    if (expired(record, now) || !*record.app || (app && std::strcmp(record.app, app))) continue;
    if (record.kind == Kind::Reminder) ++result.pending;
    else if (!record.acknowledged) ++result.unread;
  }
  return result;
}
} // namespace

bool publish(const Notice &notice, uint64_t now) {
  StaticLockGuard lock(mutex);
  return publish_locked(notice, now);
}
bool withdraw(const char *app, const char *id) {
  StaticLockGuard lock(mutex);
  if (!valid(app, 48) || !valid(id, 48)) return false;
  if (auto *record = find(app, id)) {*record = {};++sequence;}
  return true;
}
Summary summary(const char *app, uint64_t now) {
  StaticLockGuard lock(mutex);
  return summary_locked(app, now);
}
size_t withdraw_source(const char *app) {
  StaticLockGuard lock(mutex);
  if (!valid(app, 48)) return 0;
  size_t removed = 0;
  for (auto &record : records) if (!std::strcmp(record.app, app)) {
    record = {}; ++removed;
  }
  if (removed) ++sequence;
  return removed;
}
Summary synchronize(const Notice &notice, bool present, uint64_t now) {
  StaticLockGuard lock(mutex);
  if (present) (void)publish_locked(notice, now);
  else {
    if (auto *record = find(notice.app_id, notice.id)) {
      *record = {}; ++sequence;
    }
  }
  return summary_locked(notice.app_id, now);
}
SnapshotRevision revision(uint64_t now){StaticLockGuard lock(mutex);return revision_locked(now);}
void snapshot(Snapshot &out,uint64_t now){
 StaticLockGuard lock(mutex);out.revision=revision_locked(now);out.count=0;
 for(auto &entry:out.entries)entry={};
 for(const auto &record:records)if(*record.app && !expired(record,now)){
  auto &entry=out.entries[out.count++];std::strcpy(entry.app_id.data(),record.app);std::strcpy(entry.id.data(),record.id);std::strcpy(entry.title.data(),record.title);
  entry.kind=record.kind;entry.acknowledged=record.acknowledged;entry.expires_ms=record.expires;
 }
}
bool acknowledge(const char *app,const char *id,SnapshotRevision expected,uint64_t now){
 StaticLockGuard lock(mutex);if(!(revision_locked(now)==expected))return false;
 auto *record=find(app,id);if(!record || expired(*record,now))return false;
 if(!record->acknowledged){record->acknowledged=true;++sequence;}return true;
}
}  // namespace lunaredu::lunaros::notifications
