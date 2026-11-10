#include "platform_link_ota_retry.h"
#include "configuration/settings.h"
#include <algorithm>
#include <cctype>

namespace lunaredu::lunaros::platform_link {
namespace {
constexpr uint32_t kRetryMs = 15 * 60 * 1000;
constexpr uint32_t kWindowMs = 24 * 60 * 60 * 1000;
constexpr int64_t kValidEpoch = 1735689600;
constexpr size_t kMaximumKey = 768;
constexpr size_t kMaximumRecord = 4096;
constexpr const char *kNamespace = "lunar_link";
constexpr const char *kRecord = "ota_retry";
}

std::string automatic_ota_key(const std::string &origin, const std::string &account,
    const std::string &assignment, const std::string &sha256) {
  if (sha256.size() != 64 || !std::all_of(sha256.begin(),sha256.end(),[](unsigned char c){return std::isxdigit(c);}) ||
      origin.empty() || origin.size()>256 || account.empty() || account.size()>256 || assignment.size()>128) return {};
  // Length-prefix variable identifiers: separators inside an assignment or
  // origin cannot alias another assignment. Digest is the first fixed field.
  std::string key = sha256;
  std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
  for (const auto *part : {&origin,&account,&assignment}) key += "|" + std::to_string(part->size()) + ":" + *part;
  return key.size() <= kMaximumKey ? key : std::string{};
}

bool AutomaticOtaRetryGuard::load(uint32_t now) {
  if (loaded_) return true;
  std::string text, error;
  const auto read = settings_read_string(kNamespace,kRecord,kMaximumRecord,text,error);
  if (read == SettingsReadResult::Failed) { error_ = "auto_ota_retry_read_failed:" + error; return false; }
  std::array<Entry,4> restored{};
  if (read == SettingsReadResult::Found) {
    JsonDocument doc;
    if (deserializeJson(doc,text) || doc["schema"].as<int>() != 1 || !doc["entries"].is<JsonArrayConst>() || doc["entries"].size()>restored.size()) {
      error_ = "auto_ota_retry_record_invalid"; return false;
    }
    size_t index=0;
    for (JsonVariantConst item : doc["entries"].as<JsonArrayConst>()) {
      Entry &entry = restored[index++];
      entry.key = item["key"].as<std::string>();
      entry.attempts = item["attempts"] | 0U;
      entry.window = item["window"] | int64_t{0};
      entry.last = item["last"] | int64_t{0};
      entry.observed = now;
      if (entry.key.size()<65 || entry.key.size()>kMaximumKey || entry.key[64]!='|' ||
          !std::all_of(entry.key.begin(),entry.key.begin()+64,[](unsigned char c){return std::isxdigit(c);}) ||
          entry.attempts==0 || entry.attempts>3 || entry.window<0 || entry.last<0) {
        error_ = "auto_ota_retry_record_invalid"; return false;
      }
      for (size_t previous=0; previous+1<index; ++previous) if (restored[previous].key==entry.key) {
        error_ = "auto_ota_retry_record_invalid"; return false;
      }
    }
  }
  entries_=std::move(restored); loaded_=true; error_.clear(); return true;
}

bool AutomaticOtaRetryGuard::save() {
  JsonDocument doc; doc["schema"]=1;
  auto array=doc["entries"].to<JsonArray>();
  for (const auto &entry : entries_) if (!entry.key.empty()) {
    auto item=array.add<JsonObject>(); item["key"]=entry.key; item["attempts"]=entry.attempts;
    item["window"]=entry.window; item["last"]=entry.last;
  }
  std::string text; serializeJson(doc,text);
  if (text.size()>kMaximumRecord || !settings_set_string(kNamespace,kRecord,text)) {
    loaded_=false; error_="auto_ota_retry_write_failed"; return false;
  }
  return true;
}

bool AutomaticOtaRetryGuard::allowed(const std::string &key,int64_t epoch,uint32_t now) {
  selected_attempts_=0;
  if (key.empty() || key.size()>kMaximumKey) { error_="auto_ota_retry_identity_invalid"; return false; }
  if (!load(now)) return false;
  for (auto &entry : entries_) if (entry.key==key) {
    selected_attempts_=entry.attempts;
    const bool elapsed = (epoch>=kValidEpoch && entry.window>=kValidEpoch && epoch>=entry.window && epoch-entry.window>=kWindowMs/1000) ||
        static_cast<uint32_t>(now-entry.observed)>=kWindowMs;
    if (elapsed) { error_.clear(); return true; }
    if (entry.attempts>=3) { error_="auto_ota_retry_budget_exhausted"; return false; }
    const bool clock_elapsed = epoch>=kValidEpoch && entry.last>=kValidEpoch && epoch>=entry.last && epoch-entry.last>=kRetryMs/1000;
    const uint32_t reference=entry.local_attempt ? entry.attempted : entry.observed;
    if (!clock_elapsed && static_cast<uint32_t>(now-reference)<kRetryMs) { error_="auto_ota_retry_backoff"; return false; }
    error_.clear(); return true;
  }
  if (std::none_of(entries_.begin(),entries_.end(),[](const auto &entry){return entry.key.empty();})) {
    // Do not evict failed packages to let an alternating server assignment
    // evade the bounded budget. Entries expire after a stable day.
    for (auto &entry : entries_) {
      if ((epoch>=kValidEpoch && entry.window>=kValidEpoch && epoch>=entry.window && epoch-entry.window>=kWindowMs/1000) || static_cast<uint32_t>(now-entry.observed)>=kWindowMs) {
        error_.clear(); return true;
      }
    }
    error_="auto_ota_retry_history_full"; return false;
  }
  error_.clear(); return true;
}

bool AutomaticOtaRetryGuard::reserve(const std::string &key,int64_t epoch,uint32_t now) {
  if (!allowed(key,epoch,now)) return false;
  Entry *selected=nullptr;
  for (auto &entry : entries_) if (entry.key==key) {selected=&entry;break;}
  if (!selected) for (auto &entry : entries_) if (entry.key.empty()) {selected=&entry;break;}
  if (!selected) for (auto &entry : entries_) if ((epoch>=kValidEpoch && entry.window>=kValidEpoch && epoch>=entry.window && epoch-entry.window>=kWindowMs/1000) || static_cast<uint32_t>(now-entry.observed)>=kWindowMs) {selected=&entry;break;}
  if (!selected) {error_="auto_ota_retry_history_full";return false;}
  const bool fresh=selected->key!=key || (epoch>=kValidEpoch && selected->window>=kValidEpoch && epoch>=selected->window && epoch-selected->window>=kWindowMs/1000) || static_cast<uint32_t>(now-selected->observed)>=kWindowMs;
  if (fresh) *selected={key,0,epoch>=kValidEpoch?epoch:0,0,now,0,false};
  ++selected->attempts; selected->last=epoch>=kValidEpoch?epoch:0;
  selected->attempted=now; selected->local_attempt=true;
  selected_attempts_=selected->attempts;
  return save();
}

bool AutomaticOtaRetryGuard::reset() {
  // Erase only after an explicit operator request, never on a failed read.
  std::string error;
  if (!settings_erase(kNamespace,kRecord,error)) {loaded_=false;error_="auto_ota_retry_reset_failed:"+error;return false;}
  entries_={};loaded_=true;selected_attempts_=0;error_.clear();return true;
}

bool AutomaticOtaRetryGuard::confirmed(const std::string &context_key,uint32_t now) {
  if (context_key.size()<68 || context_key.compare(context_key.size()-3,3,"|0:")!=0) return true;
  if (!load(now)) return false;
  const std::string prefix=context_key.substr(0,context_key.size()-2);
  bool changed=false;
  for (auto &entry : entries_) if (entry.key.compare(0,prefix.size(),prefix)==0) {entry={};changed=true;}
  if (changed && !save()) return false;
  if (changed) {selected_attempts_=0;error_.clear();}
  return true;
}

void AutomaticOtaRetryGuard::fill_status(JsonObject target) const {
  target["attempts"]=selected_attempts_;target["maximum_attempts"]=3;
  target["retry_after_seconds"]=kRetryMs/1000;target["window_seconds"]=kWindowMs/1000;
  target["error"]=error_;
}
} // namespace lunaredu::lunaros::platform_link
