#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace lunaredu::lunaros::notifications {
inline constexpr size_t kCapacity=32;
enum class Kind : uint8_t { Message, Reminder };
struct Notice {
  const char *app_id;
  const char *id;
  const char *title;
  Kind kind=Kind::Message;
  // Absolute monotonic milliseconds; zero remains until explicitly withdrawn.
  uint64_t expires_ms=0;
};
struct Summary {uint32_t unread=0,pending=0;};
struct SnapshotRevision { uint64_t sequence=0; uint32_t live_mask=0; bool operator==(const SnapshotRevision &b) const {return sequence==b.sequence && live_mask==b.live_mask;} };
struct Entry { std::array<char,48> app_id{},id{}; std::array<char,96> title{}; Kind kind=Kind::Message; bool acknowledged=false; uint64_t expires_ms=0; };
struct Snapshot { SnapshotRevision revision; std::array<Entry,kCapacity> entries{}; size_t count=0;  };
// One notification center per running physical firmware, shared by its Apps.
// Independent KernelRuntime objects do not create independent notification
// domains. Tests reset only after all producers/readers have quiesced.
// Caller supplies bounded storage; the OS store never allocates a snapshot.
SnapshotRevision revision(uint64_t now_ms);
void snapshot(Snapshot &,uint64_t now_ms);
bool acknowledge(const char *app_id,const char *id,SnapshotRevision expected,uint64_t now_ms);
// IDs are scoped to their source App. Acknowledgement means read/seen only.
// Reminders remain pending until their source withdraws the condition.
// Updates preserve acknowledgement;
// withdrawal and re-publication create a new unread/pending notice.
bool publish(const Notice &,uint64_t now_ms);
bool withdraw(const char *app_id,const char *id);
// Retire all volatile notices belonging to one source without copying a full
// center snapshot. Invalid/absent sources leave other Apps and revision intact.
size_t withdraw_source(const char *app_id);
Summary summary(const char *app_id,uint64_t now_ms);
// Synchronize one source-owned condition and read its App summary under one
// lock. Capacity/validation failure leaves existing notices intact; the result
// includes only notices actually admitted. Repeated updates preserve read state.
Summary synchronize(const Notice &,bool present,uint64_t now_ms);
// Reset only at device lifecycle boundaries; UI closure never clears records.
void reset();
}
