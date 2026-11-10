#include "lunaredu_lunaros/desktop/local/boot_view.h"
#include "lunaredu_lunaros/desktop/local/view_style.h"
#include "lunaredu_lunaros/desktop/local/view_drawing.h"
#include <stdio.h>

#define BOOT_STAGE_CAPACITY 8
static uint8_t boot_stage_count = 1;
static LunarOSBootStage boot_stages[BOOT_STAGE_CAPACITY] = {
    {"系", "系统", "正在准备系统", "部分功能稍后可用"},
};
#define BOOT_STAGE_COUNT boot_stage_count
#define BOOT_ALL_STAGES_MASK ((1U << BOOT_STAGE_COUNT) - 1U)
#define BOOT_MINIMUM_VISIBLE_MS 1400U
#define BOOT_COMPLETION_TRANSITION_MS 380U
#define BOOT_SIMULATOR_STAGE_MS 360U

static lv_obj_t * boot_root;
static lv_timer_t * boot_timer;
static uint32_t boot_started_at_ms;
static uint32_t boot_completion_started_at_ms;
static uint16_t boot_visual_progress_milli;
static uint8_t boot_ready_mask;
static uint8_t boot_degraded_mask;
static bool boot_finish_requested;
static bool boot_animation_complete;
static bool boot_completion_transition_active;

static void (*boot_finished)(void);

static uint8_t boot_triangle_wave(uint32_t elapsed, uint32_t period)
{
    if(period == 0) return 0;
    const uint32_t position = elapsed % period;
    const uint32_t half = period / 2U;
    if(half == 0) return 0;
    const uint32_t rising = position <= half ? position : period - position;
    return (uint8_t)LV_MIN(255U, rising * 255U / half);
}

static void draw_boot_logo_motion(lv_layer_t * layer, uint32_t elapsed)
{
    static const int8_t orbit_x[] = {
        0, 19, 33, 38, 33, 19, 0, -19, -33, -38, -33, -19,
    };
    static const int8_t orbit_y[] = {
        -31, -27, -16, 0, 16, 27, 31, 27, 16, 0, -16, -27,
    };
    const uint8_t pulse = boot_triangle_wave(elapsed, 1200U);
    lunaros_ui_draw_rect(layer, 29, 19, 72, 72, lv_color_hex(0x183B70),
        LV_RADIUS_CIRCLE, (lv_opa_t)(18U + pulse / 7U));
    lunaros_ui_draw_rect(layer, 33, 23, 64, 64, lv_color_hex(0x204F91),
        LV_RADIUS_CIRCLE, (lv_opa_t)(10U + pulse / 10U));

    const size_t orbit_count = sizeof(orbit_x) / sizeof(orbit_x[0]);
    const size_t head = (elapsed / 90U) % orbit_count;
    for(size_t index = 0; index < orbit_count; ++index) {
        const size_t distance = (head + orbit_count - index) % orbit_count;
        const int size = distance == 0 ? 5 : distance <= 2 ? 3 : 2;
        const lv_opa_t opacity = distance == 0 ? LV_OPA_COVER :
            distance <= 2 ? LV_OPA_60 : LV_OPA_20;
        lunaros_ui_draw_rect(layer, 65 + orbit_x[index] - size / 2,
            55 + orbit_y[index] - size / 2, size, size,
            lv_color_hex(distance == 0 ? 0xFFD166 : 0x6EA8FF),
            LV_RADIUS_CIRCLE, opacity);
    }
    lunaros_ui_draw_lunar_logo_mark(layer, 29, 19);
}

static void draw_boot_active_spinner(lv_layer_t * layer, int center_x,
    int center_y, uint32_t elapsed)
{
    static const int8_t point_x[] = {0, 18, 25, 18, 0, -18, -25, -18};
    static const int8_t point_y[] = {-25, -18, 0, 18, 25, 18, 0, -18};
    const size_t point_count = sizeof(point_x) / sizeof(point_x[0]);
    const size_t head = (elapsed / 85U) % point_count;
    for(size_t index = 0; index < point_count; ++index) {
        const size_t distance = (head + point_count - index) % point_count;
        const int size = distance == 0 ? 5 : 3;
        const lv_opa_t opacity = distance == 0 ? LV_OPA_COVER :
            distance <= 2 ? LV_OPA_60 : LV_OPA_20;
        lunaros_ui_draw_rect(layer, center_x + point_x[index] - size / 2,
            center_y + point_y[index] - size / 2, size, size,
            lv_color_hex(0x7C8CFF), LV_RADIUS_CIRCLE, opacity);
    }
}

static uint8_t boot_terminal_mask(void)
{
    return boot_ready_mask | boot_degraded_mask;
}

static unsigned boot_completed_stage_count(void)
{
    uint8_t value = boot_terminal_mask() & BOOT_ALL_STAGES_MASK;
    unsigned count = 0;
    while(value != 0) {
        count += value & 1U;
        value >>= 1U;
    }
    return count;
}

static void boot_draw_event(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;
    lv_layer_t * layer = lv_event_get_layer(event);
    const uint32_t elapsed = lv_tick_elaps(boot_started_at_ms);
    const uint8_t terminal = boot_terminal_mask();
    int active_stage = BOOT_STAGE_COUNT - 1;
    for(int index = 0; index < BOOT_STAGE_COUNT; ++index) {
        if((terminal & (1U << index)) == 0) {
            active_stage = index;
            break;
        }
    }

    if(LUNAROS_VIEW_SCREEN_WIDTH < 360) {
        lunaros_ui_draw_text(layer, "LunarOS", lunaros_font_runtime_24(), lv_color_hex(LUNAROS_VIEW_COLOR_TEXT),
            LV_TEXT_ALIGN_CENTER, 12, 35, LUNAROS_VIEW_SCREEN_WIDTH - 24, 34);
        lunaros_ui_draw_text(layer, boot_stages[active_stage].label, lunaros_font_runtime_16(),
            lv_color_hex(LUNAROS_VIEW_COLOR_MUTED), LV_TEXT_ALIGN_CENTER, 12, 100, LUNAROS_VIEW_SCREEN_WIDTH - 24, 28);
        const int width = LUNAROS_VIEW_SCREEN_WIDTH - 40;
        const int progress = width * boot_visual_progress_milli / (BOOT_STAGE_COUNT * 1000U);
        lunaros_ui_draw_rect(layer, 20, 156, width, 6, lv_color_hex(LUNAROS_VIEW_COLOR_BORDER), 3, LV_OPA_COVER);
        if(progress > 0) lunaros_ui_draw_rect(layer, 20, 156, progress, 6,
            lv_color_hex(boot_degraded_mask ? LUNAROS_VIEW_COLOR_WARNING : LUNAROS_VIEW_COLOR_PRIMARY), 3, LV_OPA_COVER);
        return;
    }
    draw_boot_logo_motion(layer, elapsed);
    lunaros_ui_draw_text(layer, "明月 LunarEDU", lunaros_font_runtime_24(), lv_color_hex(LUNAROS_VIEW_COLOR_TEXT),
        LV_TEXT_ALIGN_LEFT, 112, 32, 300, 32);
    lunaros_ui_draw_text(layer, "LunarOS 正在启动", lunaros_font_runtime_14(),
        lv_color_hex(LUNAROS_VIEW_COLOR_MUTED), LV_TEXT_ALIGN_LEFT, 113, 67, 280, 24);

    for(int index = 0; index < BOOT_STAGE_COUNT; ++index) {
        const uint8_t mask = (uint8_t)(1U << index);
        const bool ready = (boot_ready_mask & mask) != 0;
        const bool degraded = (boot_degraded_mask & mask) != 0;
        const bool active = !ready && !degraded && index == active_stage;
        const int stage_spacing = (LUNAROS_VIEW_SCREEN_WIDTH - 32) / BOOT_STAGE_COUNT;
        const int x = 16 + index * stage_spacing + (stage_spacing - 40) / 2;
        if(index + 1 < BOOT_STAGE_COUNT) {
            const bool next_reached = (terminal & (1U << (index + 1))) != 0;
            lunaros_ui_draw_rect(layer, x + 40, 169, LV_MAX(0, stage_spacing - 40), 2,
                lv_color_hex(next_reached ? LUNAROS_VIEW_COLOR_PRIMARY_DARK : LUNAROS_VIEW_COLOR_BORDER),
                1, LV_OPA_COVER);
            if((ready || degraded) && !next_reached) {
                const int trail_x = x + 40 + (int)((elapsed / 45U) % 27U);
                lunaros_ui_draw_rect(layer, trail_x, 168, 8, 4,
                    lv_color_hex(degraded ? LUNAROS_VIEW_COLOR_WARNING : LUNAROS_VIEW_COLOR_PRIMARY),
                    2, LV_OPA_70);
            }
        }
        lv_color_t background = lv_color_hex(LUNAROS_VIEW_COLOR_SURFACE);
        lv_color_t border = lv_color_hex(LUNAROS_VIEW_COLOR_BORDER);
        lv_color_t text = lv_color_hex(LUNAROS_VIEW_COLOR_MUTED);
        if(ready) {
            background = lv_color_hex(LUNAROS_VIEW_COLOR_PRIMARY_DARK);
            border = lv_color_hex(LUNAROS_VIEW_COLOR_PRIMARY);
            text = lv_color_hex(LUNAROS_VIEW_COLOR_TEXT);
        } else if(degraded) {
            background = lv_color_hex(0x3A2D12);
            border = lv_color_hex(LUNAROS_VIEW_COLOR_WARNING);
            text = lv_color_hex(LUNAROS_VIEW_COLOR_WARNING);
        } else if(active) {
            border = lv_color_hex(LUNAROS_VIEW_COLOR_PRIMARY);
            text = lv_color_hex(LUNAROS_VIEW_COLOR_PRIMARY);
            if(((elapsed / 320U) & 1U) != 0) {
                background = lv_color_hex(LUNAROS_VIEW_COLOR_SURFACE_MUTED);
            }
        }
        lunaros_ui_draw_bordered_rect(layer, x, 150, 40, 40, background, border, 10);
        lunaros_ui_draw_text(layer, boot_stages[index].glyph, lunaros_font_runtime_16(), text,
            LV_TEXT_ALIGN_CENTER, x, 160, 40, 22);
        lunaros_ui_draw_text(layer, boot_stages[index].label, lunaros_font_runtime_12(),
            ready ? lv_color_hex(LUNAROS_VIEW_COLOR_TEXT) : text,
            LV_TEXT_ALIGN_CENTER, x - 8, 200, 56, 20);
        if(active) {
            draw_boot_active_spinner(layer, x + 20, 170, elapsed);
        } else if(ready || degraded) {
            lunaros_ui_draw_rect(layer, x + 30, 153, 7, 7,
                lv_color_hex(ready ? LUNAROS_VIEW_COLOR_SUCCESS : LUNAROS_VIEW_COLOR_WARNING),
                LV_RADIUS_CIRCLE, LV_OPA_COVER);
        }
    }

    const unsigned completed = boot_completed_stage_count();
    const bool all_terminal = completed == BOOT_STAGE_COUNT;
    const bool has_degraded = boot_degraded_mask != 0;
    const char * detail = boot_stages[active_stage].pending_detail;
    if(all_terminal) {
        detail = "所有服务已就绪";
        for(unsigned index = 0; index < BOOT_STAGE_COUNT; ++index) {
            if((boot_degraded_mask & (1U << index)) != 0) {
                detail = boot_stages[index].degraded_detail;
                break;
            }
        }
    }
    char animated_detail[96];
    if(!all_terminal) {
        const int dot_count = (int)((elapsed / 320U) % 4U);
        snprintf(animated_detail, sizeof(animated_detail), "%s%.*s", detail,
            dot_count, "...");
        detail = animated_detail;
    }
    lunaros_ui_draw_text(layer, detail, lunaros_font_runtime_14(),
        lv_color_hex(has_degraded ? LUNAROS_VIEW_COLOR_WARNING : LUNAROS_VIEW_COLOR_MUTED),
        LV_TEXT_ALIGN_CENTER, 40, 235, 400, 22);
    lunaros_ui_draw_rect(layer, 48, 275, 384, 6, lv_color_hex(LUNAROS_VIEW_COLOR_BORDER), 3,
        LV_OPA_COVER);
    const int progress_width = (int)(384U * boot_visual_progress_milli /
        (BOOT_STAGE_COUNT * 1000U));
    if(progress_width > 0) {
        lunaros_ui_draw_rect(layer, 48, 275, progress_width, 6,
            lv_color_hex(has_degraded ? LUNAROS_VIEW_COLOR_WARNING : LUNAROS_VIEW_COLOR_PRIMARY),
            3, LV_OPA_COVER);
        const uint8_t progress_pulse = boot_triangle_wave(elapsed, 700U);
        lunaros_ui_draw_rect(layer, 48 + LV_MAX(0, progress_width - 10), 274,
            LV_MIN(progress_width, 12), 8,
            lv_color_hex(has_degraded ? 0xFFE19A : 0xB7C0FF), 4,
            (lv_opa_t)(90U + progress_pulse / 2U));
    }
    if(boot_completion_transition_active) {
        const uint32_t completion_elapsed =
            lv_tick_elaps(boot_completion_started_at_ms);
        const uint8_t completion_pulse = boot_triangle_wave(
            completion_elapsed, BOOT_COMPLETION_TRANSITION_MS);
        lunaros_ui_draw_rect(layer, 24, 136, 432, 76, lv_color_hex(0x31406F), 20,
            (lv_opa_t)(completion_pulse / 7U));
    }
}

static void complete_boot_view(void)
{
    if(boot_timer != NULL) {
        lv_timer_delete(boot_timer);
        boot_timer = NULL;
    }
    if(boot_root != NULL) {
        lv_obj_del(boot_root);
        boot_root = NULL;
    }
    boot_animation_complete = true;
    if(boot_finished != NULL) boot_finished();
}

static void boot_timer_event(lv_timer_t * timer)
{
    (void)timer;
    if(boot_root == NULL) return;
    const uint32_t elapsed = lv_tick_elaps(boot_started_at_ms);
#if defined(__EMSCRIPTEN__)
    // The browser has no product service host to report hardware readiness.
    // Reveal the configured stages at a readable pace so the preview can be used
    // to review the real startup animation instead of jumping to the desktop
    // while the WASM runtime is still becoming visible.
    const unsigned simulated_completed = LV_MIN(
        elapsed / BOOT_SIMULATOR_STAGE_MS, BOOT_STAGE_COUNT);
    boot_ready_mask = simulated_completed == 0 ? 0 :
        (uint8_t)((1U << simulated_completed) - 1U);
    boot_finish_requested = simulated_completed == BOOT_STAGE_COUNT;
#endif
    const unsigned completed = boot_completed_stage_count();
    const bool all_terminal =
        (boot_terminal_mask() & BOOT_ALL_STAGES_MASK) == BOOT_ALL_STAGES_MASK;
    const uint16_t target_progress = (uint16_t)LV_MIN(
        BOOT_STAGE_COUNT * 1000U,
        completed * 1000U + (all_terminal ? 0U : 700U));
    if(boot_visual_progress_milli < target_progress) {
        uint16_t step = (uint16_t)((target_progress -
            boot_visual_progress_milli + 5U) / 6U);
        if(step < 12U) step = 12U;
        boot_visual_progress_milli = (uint16_t)LV_MIN(target_progress,
            boot_visual_progress_milli + step);
    }

    const bool completion_ready = boot_finish_requested && all_terminal;
    if(completion_ready && !boot_completion_transition_active) {
        boot_completion_transition_active = true;
        boot_completion_started_at_ms = lv_tick_get();
    } else if(!completion_ready) {
        boot_completion_transition_active = false;
    }
    if(completion_ready && elapsed >= BOOT_MINIMUM_VISIBLE_MS &&
       boot_visual_progress_milli >= BOOT_STAGE_COUNT * 990U &&
       lv_tick_elaps(boot_completion_started_at_ms) >=
           BOOT_COMPLETION_TRANSITION_MS) {
        complete_boot_view();
        return;
    }
    lv_obj_invalidate(boot_root);
}

static void build_boot_page(lv_obj_t * screen)
{
    lv_obj_clean(screen);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(LUNAROS_VIEW_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    boot_root = lv_obj_create(screen);
    lunaros_ui_reset_object(boot_root);
    lv_obj_remove_flag(boot_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(boot_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(boot_root, 0, 0);
    lv_obj_set_size(boot_root, LUNAROS_VIEW_SCREEN_WIDTH, LUNAROS_VIEW_SCREEN_HEIGHT);
    lv_obj_add_event_cb(boot_root, boot_draw_event, LV_EVENT_DRAW_MAIN, NULL);
    boot_started_at_ms = lv_tick_get();
    boot_completion_started_at_ms = 0;
    boot_visual_progress_milli = 0;
    boot_completion_transition_active = false;
    boot_timer = lv_timer_create(boot_timer_event, 40, NULL);
}

void lunaros_set_boot_status(
    uint8_t ready_mask,
    uint8_t degraded_mask,
    int finish_requested)
{
    if(boot_animation_complete) return;
    const uint8_t bounded_ready = ready_mask & BOOT_ALL_STAGES_MASK;
    const uint8_t bounded_degraded =
        (degraded_mask & BOOT_ALL_STAGES_MASK) & (uint8_t)~bounded_ready;
    const bool changed = boot_ready_mask != bounded_ready ||
        boot_degraded_mask != bounded_degraded ||
        boot_finish_requested != (finish_requested != 0);
    boot_ready_mask = bounded_ready;
    boot_degraded_mask = bounded_degraded;
    boot_finish_requested = finish_requested != 0;
    if(changed && boot_root != NULL) lv_obj_invalidate(boot_root);
}
int lunaros_ui_boot_animation_complete(void)
{
    return boot_animation_complete ? 1 : 0;
}

bool lunaros_boot_view_active(void) { return boot_root != NULL && !boot_animation_complete; }
void lunaros_boot_view_start(lv_obj_t *screen, void (*finished)(void)) {
    boot_finished = finished;
    boot_ready_mask = 0;
    boot_degraded_mask = 0;
    boot_finish_requested = false;
    boot_animation_complete = false;
    build_boot_page(screen);
}

bool lunaros_configure_boot_stages(const LunarOSBootStage *stages, uint8_t count) {
    if(boot_root != NULL || stages == NULL || count == 0 || count > BOOT_STAGE_CAPACITY) return false;
    for(uint8_t index = 0; index < count; ++index) {
        if(!stages[index].glyph || !stages[index].label || !stages[index].pending_detail || !stages[index].degraded_detail) return false;
    }
    for(uint8_t index = 0; index < count; ++index) boot_stages[index] = stages[index];
    boot_stage_count = count;
    return true;
}
