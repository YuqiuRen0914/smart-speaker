#pragma once

namespace smart_speaker {

constexpr bool assistant_focus_release_after_acquire_timeout(bool was_playing) {
  // A queued pause may finish after its caller's bounded wait expires. Always
  // release that lease so the runtime can cancel it or restore the old intent.
  return was_playing;
}

constexpr bool playback_focus_group_unowned(bool assistant_active,
                                            bool game_active) {
  return !assistant_active && !game_active;
}

constexpr bool assistant_focus_resume_allowed(bool was_playing,
                                              bool intent_unchanged,
                                              bool currently_paused,
                                              bool paused_by_volume,
                                              int volume) {
  return was_playing && intent_unchanged && currently_paused &&
         !paused_by_volume && volume > 0;
}

}  // namespace smart_speaker
