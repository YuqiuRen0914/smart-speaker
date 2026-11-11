"""Run the production foreground-focus state transitions with host doubles."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "main/smart_speaker/smart_speaker_runtime.cpp").read_text()
retry = source[source.index("void retry_assistant_focus_resume()"):
               source.index("void apply_assistant_focus_release_request()")]
release = source[source.index("void apply_assistant_focus_release_request()"):
                 source.index("void sync_game_audio_focus()")]
game = source[source.index("void sync_game_audio_focus()"):
              source.index("uint32_t now_ms()")]
command_cases = source[source.index("  case CommandType::kAssistantPause:"):
                       source.index("  case CommandType::kWhiteNoise:")]

acquire = source[source.index("bool assistant_playback_focus_acquire("):
                 source.index("void assistant_playback_focus_release()")]

fixture = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include "smart_speaker_focus_policy.h"

namespace smart_speaker::product {
enum class Page { Desktop, Feature };
enum class Module { Music, Games };
struct NavigationSnapshot { Page page; Module module; };
NavigationSnapshot navigation{Page::Desktop, Module::Music};
NavigationSnapshot product_navigation_snapshot() { return navigation; }
}

namespace smart_speaker::detail {
enum class CommandType { kAssistantPause, kAssistantResume, kWhiteNoise };
struct Command {
  CommandType type{};
  int value = 0;
  uint32_t playlist_revision = 0;
  uint32_t focus_generation = 0;
  uint32_t intent_revision = 0;
};
struct Runtime {
  std::mutex mutex;
  bool assistant_focus_active = false;
  bool game_focus_active = false;
  bool game_focus_failed = false;
  bool assistant_focus_was_playing = false;
  bool assistant_focus_resume_pending = false;
  bool assistant_focus_resume_queued = false;
  uint8_t assistant_focus_resume_attempts = 0;
  uint32_t assistant_focus_resume_retry_at_ms = 0;
  uint32_t assistant_focus_generation = 0;
  uint32_t assistant_focus_intent_revision = 0;
  bool playing = false;
  bool paused = false;
  bool paused_by_volume = false;
  int volume = 10;
  std::string last_error;
};
Runtime g_runtime;
std::atomic<uint32_t> g_playback_intent_revision{0};
std::atomic<bool> g_assistant_focus_release_requested{false};
std::vector<Command> queued;
bool queue_accepts = true;
uint32_t clock_ms = 1000;
uint32_t now_ms() { return clock_ms; }
#define pdMS_TO_TICKS(ms) (ms)
void vTaskDelay(uint32_t ms){clock_ms+=ms;}
void set_error_locked(const char *error) { g_runtime.last_error = error; }
constexpr int ESP_OK = 0;
int pause_result = ESP_OK, resume_result = ESP_OK;
int pause_calls = 0, resume_calls = 0, fade_mutes = 0, fade_ups = 0;
int audio_player_pause() { ++pause_calls; return pause_result; }
int audio_player_resume() { ++resume_calls; return resume_result; }
int dual_i2s_audio_fade_to_mute(int) { ++fade_mutes; return ESP_OK; }
void dual_i2s_audio_fade_up() { ++fade_ups; }
void update_play_state(bool playing, bool paused, bool) {
  g_runtime.playing = playing; g_runtime.paused = paused;
}
bool enqueue_command(CommandType type, int = 0, uint32_t = 0,
                     uint32_t focus = 0, uint32_t intent = 0) {
  if (!queue_accepts) return false;
  queued.push_back({type, 0, 0, focus, intent});
  return true;
}
''' + retry + release + game + r'''
void execute_focus_command(const Command &command) {
  switch (command.type) {
''' + command_cases + r'''
  default: break;
  }
}
void enter_game() {
  smart_speaker::product::navigation = {
      smart_speaker::product::Page::Feature,
      smart_speaker::product::Module::Games};
  sync_game_audio_focus();
}
void leave_game() {
  smart_speaker::product::navigation = {
      smart_speaker::product::Page::Desktop,
      smart_speaker::product::Module::Music};
  sync_game_audio_focus();
}
}  // namespace smart_speaker::detail
using namespace smart_speaker::detail;
namespace smart_speaker {
using namespace detail;
''' + acquire + r'''
}
int main() {
  // A stale deferred release cannot revoke a new turn's focus.
  g_runtime.assistant_focus_active=true;
  g_assistant_focus_release_requested=true;
  assert(!smart_speaker::assistant_playback_focus_acquire(0));
  assert(g_runtime.last_error=="assistant_playback_release_pending");
  apply_assistant_focus_release_request();
  assert(!g_runtime.assistant_focus_active);
  assert(smart_speaker::assistant_playback_focus_acquire(0));
  assert(g_runtime.assistant_focus_active);
  apply_assistant_focus_release_request();
  assert(g_runtime.assistant_focus_active); // No pending release remains.
  g_runtime.assistant_focus_active=false;
  // Game takes the original playing intent and submits one pause. Exit releases
  // it and queues a resume with the captured generation and revision.
  g_runtime.playing = true;
  g_playback_intent_revision = 7;
  enter_game();
  assert(g_runtime.game_focus_active && g_runtime.assistant_focus_was_playing);
  assert(queued.size() == 1 && queued[0].type == CommandType::kAssistantPause);
  const auto generation = g_runtime.assistant_focus_generation;
  assert(queued[0].focus_generation == generation && queued[0].intent_revision == 7);
  g_runtime.playing = false;
  g_runtime.paused = true;
  leave_game();
  assert(g_assistant_focus_release_requested.load());
  apply_assistant_focus_release_request();
  assert(g_runtime.assistant_focus_resume_pending && g_runtime.assistant_focus_resume_queued);
  assert(queued.back().type == CommandType::kAssistantResume);

  // A newly entered game cancels an already queued resume and inherits the
  // same paused playback snapshot; leaving later queues a fresh resume.
  queued.clear();
  enter_game();
  assert(g_runtime.game_focus_active && !g_runtime.assistant_focus_resume_pending);
  assert(g_runtime.assistant_focus_generation == generation);
  assert(g_runtime.assistant_focus_was_playing && queued.empty());
  leave_game();
  apply_assistant_focus_release_request();
  assert(g_runtime.assistant_focus_resume_pending && queued.size() == 1);

  // Assistant and game are nested owners. Releasing the assistant alone must
  // keep playback paused; the game exit is the final release.
  queued.clear();
  g_runtime.assistant_focus_resume_pending = false;
  g_runtime.assistant_focus_resume_queued = false;
  g_runtime.game_focus_active = false;
  g_runtime.assistant_focus_active = false;
  g_runtime.assistant_focus_was_playing = false;
  g_runtime.playing = true;
  g_runtime.paused = false;
  enter_game();
  g_runtime.playing = false;
  g_runtime.paused = true;
  g_runtime.assistant_focus_active = true;
  g_assistant_focus_release_requested = true;
  apply_assistant_focus_release_request();
  assert(!g_runtime.assistant_focus_resume_pending && queued.size() == 1);
  assert(g_runtime.game_focus_active);
  leave_game();
  apply_assistant_focus_release_request();
  assert(g_runtime.assistant_focus_resume_pending && queued.size() == 2);

  // Queue pressure fails closed and leaves a visible runtime error.
  queued.clear();
  g_runtime.assistant_focus_resume_pending = false;
  g_runtime.assistant_focus_resume_queued = false;
  g_runtime.assistant_focus_active = false;
  g_runtime.game_focus_active = false;
  g_runtime.assistant_focus_was_playing = false;
  g_runtime.playing = true;
  g_runtime.paused = false;
  queue_accepts = false;
  enter_game();
  assert(!g_runtime.game_focus_active);
  assert(g_runtime.last_error == "game_playback_pause_queue_full");
  const auto retry_count = queued.size();
  sync_game_audio_focus();
  assert(queued.size() == retry_count);  // No retry storm while staying in game.
  leave_game();
  queue_accepts = true;
  enter_game();
  assert(g_runtime.game_focus_active && queued.size() == retry_count + 1);

  // The production decoder command cases must acknowledge only the current
  // generation and must never restore playback after a newer user/AI intent.
  queued.clear();
  pause_calls = resume_calls = fade_mutes = fade_ups = 0;
  g_runtime.assistant_focus_active = true;
  g_runtime.game_focus_active = false;
  g_runtime.assistant_focus_generation = 41;
  g_runtime.assistant_focus_was_playing = true;
  g_runtime.playing = true;
  g_runtime.paused = false;
  execute_focus_command({CommandType::kAssistantPause, 0, 0, 40, 7});
  assert(pause_calls == 0 && fade_mutes == 0);
  execute_focus_command({CommandType::kAssistantPause, 0, 0, 41, 7});
  assert(pause_calls == 1 && fade_mutes == 1 && !g_runtime.playing && g_runtime.paused);

  g_runtime.assistant_focus_generation = 42;
  g_runtime.playing = true;
  g_runtime.paused = false;
  pause_result = 1;
  execute_focus_command({CommandType::kAssistantPause, 0, 0, 42, 7});
  assert(pause_calls == 2 && g_runtime.playing && !g_runtime.paused &&
         g_runtime.last_error == "assistant_playback_pause_failed");
  pause_result = ESP_OK;
  g_runtime.assistant_focus_generation = 41;

  g_runtime.assistant_focus_resume_pending = true;
  g_runtime.assistant_focus_resume_queued = true;
  g_runtime.assistant_focus_was_playing = true;
  g_runtime.assistant_focus_intent_revision = 7;
  g_runtime.volume = 10;
  g_playback_intent_revision = 8;  // AI changed the playback intent during focus.
  execute_focus_command({CommandType::kAssistantResume, 0, 0, 41, 7});
  assert(resume_calls == 0 && !g_runtime.assistant_focus_resume_pending);

  g_runtime.assistant_focus_resume_pending = true;
  g_runtime.assistant_focus_resume_queued = true;
  g_runtime.assistant_focus_was_playing = true;
  g_runtime.assistant_focus_intent_revision = 9;
  g_playback_intent_revision = 9;
  g_runtime.playing = false;
  g_runtime.paused = true;
  resume_result = 1;
  execute_focus_command({CommandType::kAssistantResume, 0, 0, 41, 9});
  assert(resume_calls == 1 && g_runtime.assistant_focus_resume_pending &&
         !g_runtime.assistant_focus_resume_queued &&
         g_runtime.assistant_focus_resume_attempts == 1 &&
         g_runtime.assistant_focus_resume_retry_at_ms == clock_ms + 100);
  const auto prior_queue = queued.size();
  retry_assistant_focus_resume();
  assert(queued.size() == prior_queue);
  clock_ms += 100;
  retry_assistant_focus_resume();
  assert(queued.size() == prior_queue + 1 && g_runtime.assistant_focus_resume_queued);
  resume_result = ESP_OK;
  execute_focus_command(queued.back());
  assert(resume_calls == 2 && fade_ups == 1 && g_runtime.playing &&
         !g_runtime.paused && !g_runtime.assistant_focus_resume_pending);

  queued.clear();
  g_runtime.assistant_focus_resume_pending = true;
  g_runtime.assistant_focus_resume_queued = false;
  g_runtime.assistant_focus_resume_attempts = 0;
  g_runtime.assistant_focus_resume_retry_at_ms = 0;
  queue_accepts = false;
  retry_assistant_focus_resume();
  assert(!g_runtime.assistant_focus_resume_queued &&
         g_runtime.assistant_focus_resume_attempts == 1);
  queue_accepts = true;

  queued.clear();
  g_runtime.playing = false;
  g_runtime.paused = true;
  g_runtime.assistant_focus_resume_pending = true;
  g_runtime.assistant_focus_resume_queued = false;
  g_runtime.assistant_focus_was_playing = true;
  g_runtime.assistant_focus_resume_attempts = 0;
  g_runtime.assistant_focus_resume_retry_at_ms = 0;
  resume_result = 1;
  for (int attempt = 0; attempt < 3; ++attempt) {
    retry_assistant_focus_resume();
    assert(g_runtime.assistant_focus_resume_queued && !queued.empty());
    execute_focus_command(queued.back());
    queued.clear();
    assert(g_runtime.assistant_focus_resume_pending &&
           g_runtime.assistant_focus_resume_attempts == attempt + 1);
    clock_ms += 100;
  }
  retry_assistant_focus_resume();
  assert(!g_runtime.assistant_focus_resume_pending &&
         g_runtime.last_error == "assistant_playback_restore_failed");
  return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="speaker-focus-state-") as tmp:
    cpp = Path(tmp) / "test.cpp"
    binary = Path(tmp) / "test"
    cpp.write_text(fixture)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "main/smart_speaker"), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Audio focus: production game/assistant nesting, resume cancellation and queue failure passed")
