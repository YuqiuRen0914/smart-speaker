#include "smart_speaker_focus_policy.h"

#include <cassert>

using smart_speaker::assistant_focus_resume_allowed;
using smart_speaker::assistant_focus_release_after_acquire_timeout;
using smart_speaker::playback_focus_group_unowned;

int main() {
  assert(assistant_focus_release_after_acquire_timeout(true));
  assert(!assistant_focus_release_after_acquire_timeout(false));
  assert(!playback_focus_group_unowned(true, false));
  assert(!playback_focus_group_unowned(false, true));
  assert(!playback_focus_group_unowned(true, true));
  assert(playback_focus_group_unowned(false, false));
  assert(assistant_focus_resume_allowed(true, true, true, false, 30));
  assert(!assistant_focus_resume_allowed(false, true, true, false, 30));
  assert(!assistant_focus_resume_allowed(true, false, true, false, 30));
  assert(!assistant_focus_resume_allowed(true, true, false, false, 30));
  assert(!assistant_focus_resume_allowed(true, true, true, true, 30));
  assert(!assistant_focus_resume_allowed(true, true, true, false, 0));
}
