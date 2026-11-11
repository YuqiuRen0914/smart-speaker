#pragma once

#include <cstdint>
#include "smart_speaker_pet_frame_policy.h"

namespace smart_speaker {

struct SmartSpeakerDisplayStats {
  uint32_t spectrum_render_count = 0;
  uint32_t spectrum_render_fail_count = 0;
  uint32_t last_spectrum_render_us = 0;
  uint32_t max_spectrum_render_us = 0;
  uint32_t character_render_count = 0;
  uint32_t character_render_fail_count = 0;
  uint32_t last_character_render_us = 0;
  uint32_t max_character_render_us = 0;
  uint32_t last_character_raster_us = 0;
  uint32_t last_character_transfer_us = 0;
  uint64_t character_transferred_pixels = 0;
  uint32_t last_character_pixels = 0;
  uint32_t last_character_regions = 0;
  uint32_t progress_render_count = 0;
  uint32_t last_progress_render_us = 0;
  uint32_t character_over_budget_count = 0;
  uint32_t character_frame_ms = 33;
  uint32_t pet_animation_frames_drawn = 0;
  uint16_t pet_animation_frame = 0;
  uint8_t pet_animation_clip = 0;
  bool pet_animation_visible = false;
  bool pet_animation_unavailable = false;
  bool pet_dragging = false;
  uint8_t character_mood = 0;
  bool character_visible = false;
  bool boot_visible = false;
  // Completed boot-only measurements. Frame intervals include drawing,
  // scheduling and the display task's wait; p95 is a 10ms histogram bound.
  uint32_t boot_frame_count = 0, boot_frame_mean_us = 0;
  uint32_t boot_frame_p95_us = 0, boot_frame_max_us = 0;
  uint32_t boot_frames_over_50ms = 0, boot_max_refresh_us = 0;
  uint32_t boot_refresh_count = 0, boot_full_refresh_count = 0;
  uint64_t boot_flushed_pixels = 0;
  bool lyrics_visible = false;
  bool lyrics_layout = false;
  bool transfer_page = false;
  bool volume_bar_visible = false;
  bool pause_indicator_visible = false;
  uint8_t pause_indicator_percent = 0;
  uint8_t character_edge_contact = 0;
  uint8_t character_edge_impact = 0;
  uint8_t transfer_percent = 0;
  bool account_avatar_ready = false;
  uint32_t account_avatar_generation = 0;
  bool account_avatar_available = false;
  uint32_t lvgl_pool_free_bytes = 0;
  uint32_t lvgl_pool_largest_block = 0;
  uint32_t display_stack_min_free_bytes = 0;
};

bool setup_smart_speaker_display();
void smart_speaker_display_set_assistant_emotion(AssistantVisualState state);
void smart_speaker_display_request_pet_action(int action);
SmartSpeakerDisplayStats smart_speaker_display_stats();

} // namespace smart_speaker
