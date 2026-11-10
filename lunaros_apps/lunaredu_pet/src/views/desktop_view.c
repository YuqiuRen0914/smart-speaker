#include "lunaredu_lunaros/desktop/local/compact_layout.h"
#include "lunaredu_pet_subtitle_policy.h"
#include "lunaredu_pet_desktop_view_api.h"
#include "lunaredu_lunaros/desktop/local/local_view_port.h"
#include "lunaredu_lunaros/desktop/local/view_widgets.h"
#include "lunaredu_lunaros/desktop/local/view_style.h"
#include "lunaredu_lunaros/desktop/local/view_drawing.h"
#include "lunaredu_pet_visual_port.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "view_internal.h"
#include "lunaredu_pet_desktop_view.h"
#define PET_FRAME_SIZE 144
#define ASSISTANT_REPLY_HOLD_MS 3000
#define ASSISTANT_TRANSCRIPT_HOLD_MS 5000
#define ASSISTANT_CONNECTION_NOTICE_DELAY_MS 3000

typedef enum { PAGE_UNKNOWN = -1, PAGE_DESKTOP, PAGE_PET_FULLSCREEN, PAGE_PET_BINDING, PAGE_EYES, PAGE_ONBOARDING } lunaros_page_t;
static lunaros_page_t active_page(void) {
 const char *route = lunaros_view_active_route();
 if(strcmp(route, "desktop")==0) return PAGE_DESKTOP;
 if(strcmp(route, "pet-fullscreen")==0) return PAGE_PET_FULLSCREEN;
 if(strcmp(route, "pet-binding")==0) return PAGE_PET_BINDING;
 if(strcmp(route, "eyes")==0) return PAGE_EYES;
 if(strcmp(route, "onboarding")==0) return PAGE_ONBOARDING;
 return PAGE_UNKNOWN;
}
static bool updating_state;
static bool refresh_requested;
static void show_page(lunaros_page_t page) {
 if(page == PAGE_DESKTOP) {if(updating_state && strcmp(lunaros_view_active_route(), "desktop")==0) {refresh_requested=true;return;} lunaros_view_navigate("desktop"); return;}
 if(page == PAGE_PET_FULLSCREEN) {if(updating_state && strcmp(lunaros_view_active_route(), "pet-fullscreen")==0) {refresh_requested=true;return;} lunaros_view_navigate("pet-fullscreen"); return;}
 if(page == PAGE_PET_BINDING) {if(updating_state && strcmp(lunaros_view_active_route(), "pet-binding")==0) {refresh_requested=true;return;} lunaros_view_navigate("pet-binding"); return;}
 if(page == PAGE_EYES) {if(updating_state && strcmp(lunaros_view_active_route(), "eyes")==0) {refresh_requested=true;return;} lunaros_view_navigate("eyes"); return;}
 if(page == PAGE_ONBOARDING) {if(updating_state && strcmp(lunaros_view_active_route(), "onboarding")==0) {refresh_requested=true;return;} lunaros_view_navigate("onboarding"); return;}
}
#define current_page active_page()
#define ui_initialized lunaros_view_is_initialized()
static lv_obj_t *content_root;
static pet_visual_ports_t visual_ports;
static bool visual_mounted;
static atomic_bool consumes_pet_frames = true;
static bool companion_mode;
static bool presentation_changed;
void pet_view_configure_visual(pet_visual_ports_t ports) {
    visual_ports = ports;
    atomic_store(&consumes_pet_frames, companion_mode || !ports.mount);
}
void pet_view_sync_visual(void) { if(visual_ports.sync) visual_ports.sync(); }
static void set_visual_audio_priority(int enabled) {
    if(visual_ports.set_audio_priority) visual_ports.set_audio_priority(enabled);
}
static void mount_visual(lv_obj_t *parent, uint16_t scale_percent);

static bool eyes_mode = false;
static pet_view_model_t model = {
    .device_serial_number = "",
    .assistant_enabled = false,
    .assistant_service_enabled = false,
    .assistant_suspended = false,
    .assistant_speaking = false,
    .assistant_microphone_enabled = true,
    .assistant_activation_ready = false,
    .assistant_activation_pending = false,
    .assistant_activation_code = "",
    .assistant_transport_connected = false,
    .assistant_session_state = "inactive",
    .assistant_last_error = "",
    .assistant_transcript_text = "",
    .assistant_transcript_role = "",
    .assistant_transcript_revision = 0,
};

static bool pet_frame_ready;
static lv_image_dsc_t pet_frame_image = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .flags = 0,
        .w = PET_FRAME_SIZE,
        .h = PET_FRAME_SIZE,
        .stride = PET_FRAME_SIZE * 4,
    },
    .data_size = PET_FRAME_SIZE * PET_FRAME_SIZE * 4,
    .data = NULL,
};
static lv_obj_t * pet_frame_parent;
static lv_obj_t * pet_frame_widget;
static lv_obj_t * pet_frame_placeholder;
static int pet_frame_target_size;
static lv_obj_t * assistant_subtitle_label;
static bool assistant_activation_clear_confirmation_pending;
static uint32_t assistant_activation_cleared_at_ms;
static bool assistant_subtitles_enabled = true;
static bool assistant_continuous_conversation;
static bool assistant_reply_hold_active;
static uint32_t assistant_reply_hold_started_at_ms;
static bool assistant_transcript_hold_active;
static uint32_t assistant_transcript_hold_started_at_ms;
static bool assistant_connection_notice_pending;
static uint32_t assistant_connection_notice_started_at_ms;
static lv_obj_t * create_pet_frame_image(lv_obj_t * parent, int target_size);
static void build_desktop(void);
static bool assistant_can_be_stopped(void);
static void stop_active_assistant(void);
static void pet_fullscreen_exit_event(lv_event_t * event);
static void build_pet_fullscreen(lv_obj_t * screen);
static void assistant_activation_clear_event(lv_event_t * event);
static void update_assistant_subtitle_label(void);
static void assistant_reply_hold_timer_event(lv_timer_t * timer);
static void create_ambient_subtitle(lv_obj_t * screen);
static void build_pet_binding(void);
void lunaros_set_assistant_state(
    int activation_ready,
    int activation_pending,
    const char * activation_code,
    int transport_connected,
    const char * session_state,
    const char * last_error,
    int service_enabled,
    int suspended,
    int speaking
);
void lunaros_set_assistant_transcript(
    const char * role, const char * text, uint32_t revision);
void lunaros_set_pet_frame_a8rgb666_u8x4(const uint8_t * pixels, int width, int height);
int lunaros_ui_pet_display_is_companion(void);
void lunaros_set_eyes_mode(int active);
void lunaros_set_pet_preferences(
    int subtitles_enabled,
    int continuous_conversation);
static void assistant_connection_notice_timer_event(lv_timer_t * timer);
static lv_obj_t * create_pet_frame_image(lv_obj_t * parent, int target_size)
{
    if(!pet_frame_ready) return NULL;
    lv_obj_t * image = lv_image_create(parent);
    lv_image_set_src(image, &pet_frame_image);
    lv_image_set_scale(image, (uint32_t)(target_size * LV_SCALE_NONE / PET_FRAME_SIZE));
    lv_obj_center(image);
    return image;
}











static void refresh_pet_frame(void) {
    if(!pet_frame_parent) return;
    if(pet_frame_ready) {
        if(pet_frame_placeholder) {lv_obj_delete(pet_frame_placeholder);pet_frame_placeholder=NULL;}
        if(!pet_frame_widget) pet_frame_widget=create_pet_frame_image(pet_frame_parent,pet_frame_target_size);
        else {
            lv_image_set_src(pet_frame_widget,&pet_frame_image);
            lv_obj_invalidate(pet_frame_widget);
        }
    } else {
        if(pet_frame_widget) {lv_obj_delete(pet_frame_widget);pet_frame_widget=NULL;}
        if(!pet_frame_placeholder) {
            pet_frame_placeholder=lunaros_view_create_label(pet_frame_parent,"宠物",lunaros_font_runtime_24(),lv_color_hex(LUNAROS_VIEW_COLOR_TEXT));
            lv_obj_center(pet_frame_placeholder);
        }
    }
}
static void mount_visual(lv_obj_t *parent, uint16_t scale_percent) {
    if(!companion_mode && visual_ports.mount) {
        visual_ports.mount(parent, scale_percent);
        visual_mounted = true;
    } else {
        pet_frame_parent=parent;
        lv_obj_update_layout(parent);
        const int available=LV_MIN(lv_obj_get_content_width(parent),lv_obj_get_content_height(parent));
        const int desired=current_page==PAGE_PET_FULLSCREEN ? 240 : 144;
        pet_frame_target_size=LV_MAX(1,LV_MIN(desired,available));
        refresh_pet_frame();
    }
}

static void build_desktop(void)
{
    // Use the real expression renderer inside the layout from
    // design-017763fc8622. This preserves emotion and audio animation instead
    // of copying the design simulator's two static white rectangles.
    lv_obj_t * eyes_viewport = lv_obj_create(content_root);
    lunaros_ui_reset_object(eyes_viewport);
    lv_obj_set_pos(eyes_viewport, lunaros_view_is_compact() ? 4 : 72, lunaros_view_is_compact() ? 6 : 38);
    lv_obj_set_size(eyes_viewport, lunaros_view_is_compact() ? LUNAROS_VIEW_SCREEN_WIDTH - 8 : 336, 126);
    lv_obj_set_style_bg_opa(eyes_viewport, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(eyes_viewport, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(eyes_viewport, LV_OBJ_FLAG_SCROLLABLE);
    mount_visual(eyes_viewport, lunaros_view_is_compact() ? 80 : 116);

    lv_obj_t * message = lv_obj_create(content_root);
    lunaros_ui_reset_object(message);
    lv_obj_set_pos(message, lunaros_view_is_compact() ? 8 : 32, lunaros_view_is_compact() ? 140 : 190);
    lv_obj_set_size(message, LUNAROS_VIEW_SCREEN_WIDTH - (lunaros_view_is_compact() ? 16 : 64), lunaros_view_is_compact() ? 64 : 82);
    lunaros_view_style_surface(message, lv_color_hex(0x17181D), 14);
    lv_obj_set_style_border_width(message, 0, 0);
    lv_obj_remove_flag(message, LV_OBJ_FLAG_SCROLLABLE);

    assistant_subtitle_label = lunaros_view_create_label(message, "", lunaros_font_runtime_14(),
        lv_color_hex(LUNAROS_VIEW_COLOR_TEXT));
    lv_obj_set_size(assistant_subtitle_label, LUNAROS_VIEW_SCREEN_WIDTH - (lunaros_view_is_compact() ? 32 : 100), 54);
    lv_label_set_long_mode(assistant_subtitle_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(assistant_subtitle_label,
        LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(assistant_subtitle_label);
    update_assistant_subtitle_label();
}

static bool assistant_can_be_stopped(void)
{
    return model.assistant_speaking ||
        strcmp(model.assistant_session_state, "listening") == 0 ||
        strcmp(model.assistant_session_state, "speaking") == 0;
}

static void stop_active_assistant(void)
{
    if(assistant_can_be_stopped()) {
        lunaros_view_emit_intent("assistant.response.stop", "{}");
    }
}

static void pet_fullscreen_exit_event(lv_event_t * event)
{
    (void)event;
    show_page(PAGE_EYES);
}

static void build_pet_fullscreen(lv_obj_t * screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(LUNAROS_VIEW_COLOR_BACKGROUND), 0);
    // The content switch is the single source of truth. `eyes_mode` only
    // describes the current presentation (window/fullscreen); letting it
    // select the content as well made a stale fullscreen flag override the
    // user's expression/companion choice.
    {
        // Reuse the exact window viewport and only enlarge its contents. The
        // viewport itself is centered on the physical 480x320 screen, so
        // fullscreen and window mode share the same coordinate system.
        lv_obj_t * viewport = lv_obj_create(screen);
        lunaros_ui_reset_object(viewport);
        lv_obj_set_size(viewport, LV_MIN(300, LUNAROS_VIEW_SCREEN_WIDTH), LV_MIN(208, LUNAROS_VIEW_SCREEN_HEIGHT));
        lv_obj_center(viewport);
        lv_obj_set_style_bg_opa(viewport, LV_OPA_TRANSP, 0);
        mount_visual(viewport, lunaros_view_is_compact() ? 100 : 140);
    }
    lunaros_view_create_status_bar(screen);
    create_ambient_subtitle(screen);
    lv_obj_t * exit = lv_button_create(screen);
    lunaros_ui_reset_object(exit);
    lv_obj_set_pos(exit, LUNAROS_VIEW_SCREEN_WIDTH - 74, 28);
    lv_obj_set_size(exit, 62, 40);
    lunaros_view_style_surface(exit, lv_color_hex(LUNAROS_VIEW_COLOR_SURFACE_MUTED), 10);
    lunaros_view_style_pressable(exit);
    lv_obj_add_event_cb(exit, pet_fullscreen_exit_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t * exit_label = lunaros_view_create_label(exit, "退出", lunaros_font_runtime_12(),
        lv_color_hex(LUNAROS_VIEW_COLOR_TEXT));
    lv_obj_center(exit_label);
}

static void assistant_activation_clear_event(lv_event_t * event)
{
    (void)event;
    if(!model.assistant_activation_ready) {
        // The clear action rebuilds this page with a new button at the same
        // coordinates. Ignore release jitter/bounce from the clear gesture so
        // it cannot immediately re-enable the former XiaoZhi account.
        if(assistant_activation_cleared_at_ms != 0 &&
           lv_tick_elaps(assistant_activation_cleared_at_ms) < 1500) {
            return;
        }
        assistant_activation_cleared_at_ms = 0;
        assistant_activation_clear_confirmation_pending = false;
        lunaros_view_emit_intent("assistant.activation.start", "{}");
        show_page(PAGE_PET_BINDING);
        return;
    }
    if(!assistant_activation_clear_confirmation_pending) {
        assistant_activation_clear_confirmation_pending = true;
        show_page(current_page);
        return;
    }
    assistant_activation_clear_confirmation_pending = false;
    assistant_activation_cleared_at_ms = lv_tick_get();
    lunaros_view_emit_intent("assistant.activation.clear", "{}");
    show_page(PAGE_PET_BINDING);
}



static void update_assistant_subtitle_label(void)
{
    if(assistant_subtitle_label == NULL) return;
    const bool full_screen_pet = current_page == PAGE_DESKTOP ||
        current_page == PAGE_EYES || current_page == PAGE_PET_FULLSCREEN;
    const bool reply_hold = assistant_reply_hold_active &&
        lv_tick_elaps(assistant_reply_hold_started_at_ms) <
            ASSISTANT_REPLY_HOLD_MS;
    const bool transcript_hold = assistant_transcript_hold_active &&
        lv_tick_elaps(assistant_transcript_hold_started_at_ms) <
            ASSISTANT_TRANSCRIPT_HOLD_MS;
    const bool connection_notice_due = assistant_connection_notice_pending &&
        lv_tick_elaps(assistant_connection_notice_started_at_ms) >=
            ASSISTANT_CONNECTION_NOTICE_DELAY_MS;
    const lunaros_home_assistant_state_t state = {
        .network_provisioning = model.network_provisioning,
        .network_connecting = model.network_connecting,
        .network_connected = model.network_connected,
        .assistant_enabled = model.assistant_enabled,
        .assistant_service_enabled = model.assistant_service_enabled,
        .assistant_suspended = model.assistant_suspended,
        .assistant_activation_ready = model.assistant_activation_ready,
        .assistant_activation_pending = model.assistant_activation_pending,
        .assistant_transport_connected = model.assistant_transport_connected,
        .assistant_speaking = model.assistant_speaking,
        .assistant_microphone_enabled = model.assistant_microphone_enabled,
        .connection_notice_due = connection_notice_due,
        .subtitles_enabled = assistant_subtitles_enabled,
        .full_screen_pet = full_screen_pet,
        .desktop_page = current_page == PAGE_DESKTOP,
        .reply_hold = reply_hold,
        .transcript_hold = transcript_hold,
        .session_state = model.assistant_session_state,
        .transcript_role = model.assistant_transcript_role,
        .transcript_text = model.assistant_transcript_text,
    };
    // Connectivity and setup problems stay visible even when conversational
    // subtitles are disabled: they explain why the user cannot talk to XiaoZhi.
    const char * subtitle = lunaros_home_assistant_subtitle_text(&state);
    if(subtitle[0] != '\0') {
        lunaros_view_set_label_text(assistant_subtitle_label, subtitle,
            lv_obj_get_style_text_font(assistant_subtitle_label,
                LV_PART_MAIN));
        lv_obj_remove_flag(assistant_subtitle_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(assistant_subtitle_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void assistant_reply_hold_timer_event(lv_timer_t * timer)
{
    (void)timer;
    bool refresh_subtitle = false;
    if(assistant_reply_hold_active &&
       lv_tick_elaps(assistant_reply_hold_started_at_ms) >=
           ASSISTANT_REPLY_HOLD_MS) {
        assistant_reply_hold_active = false;
        refresh_subtitle = true;
        if(strcmp(model.assistant_session_state, "speaking") != 0) {
            set_visual_audio_priority(false);
        }
    }
    if(assistant_transcript_hold_active &&
       lv_tick_elaps(assistant_transcript_hold_started_at_ms) >=
           ASSISTANT_TRANSCRIPT_HOLD_MS) {
        assistant_transcript_hold_active = false;
        refresh_subtitle = true;
    }
    if(refresh_subtitle) update_assistant_subtitle_label();
}

static void create_ambient_subtitle(lv_obj_t * screen)
{
    assistant_subtitle_label = lunaros_view_create_label(screen, "", lunaros_font_runtime_14(),
        lv_color_hex(0xFFFFFF));
    lv_obj_set_size(assistant_subtitle_label, 420, 62);
    lv_label_set_long_mode(assistant_subtitle_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(assistant_subtitle_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(assistant_subtitle_label,
        lv_color_hex(LUNAROS_VIEW_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(assistant_subtitle_label, LV_OPA_70, 0);
    lv_obj_set_style_radius(assistant_subtitle_label, 16, 0);
    lv_obj_set_style_pad_left(assistant_subtitle_label, 14, 0);
    lv_obj_set_style_pad_right(assistant_subtitle_label, 14, 0);
    lv_obj_set_style_pad_top(assistant_subtitle_label, 10, 0);
    lv_obj_set_style_pad_bottom(assistant_subtitle_label, 10, 0);
    lv_obj_align(assistant_subtitle_label, LV_ALIGN_BOTTOM_MID, 0, -14);
    update_assistant_subtitle_label();
}











static void build_pet_binding(void)
{
    const bool remote_binding_still_active = strcmp(model.assistant_last_error,
        "xiaozhi_remote_binding_still_active") == 0;
    const char * detail = model.assistant_activation_ready ? "已绑定并可对话" :
        remote_binding_still_active ? "激活后台仍返回旧授权" :
        model.assistant_activation_pending ? "等待上位机确认配对" :
        model.assistant_last_error[0] != '\0' ? "配对暂不可用" : "点击开始绑定";
    lunaros_view_create_page_title(
        lunaros_view_setup_active() ? "第3步 · 绑定小智" : "绑定小智",
        lunaros_view_setup_active()
            ? "请让老师或家长打开 xiaozhi.me 输入配对码"
            : detail);

    lv_obj_t * status = lv_obj_create(content_root);
    lunaros_ui_reset_object(status);
    lv_obj_set_pos(status, 14, 72);
    lv_obj_set_size(status, 452, 94);
    lv_obj_set_style_border_color(status, lv_color_hex(LUNAROS_VIEW_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(status, 1, 0);
    lv_obj_t * status_title = lunaros_view_create_label(status,
        model.assistant_activation_ready ? "小智已绑定" :
            remote_binding_still_active ? "小智后台未释放旧授权" :
            "等待绑定小智",
        lunaros_font_runtime_16(), lv_color_hex(model.assistant_activation_ready
            ? LUNAROS_VIEW_COLOR_SUCCESS : LUNAROS_VIEW_COLOR_TEXT));
    lv_obj_set_pos(status_title, 14, 12);
    const char * code = remote_binding_still_active
        ? "控制台虽已删除，激活服务仍返回旧账号"
        : model.assistant_activation_code[0] != '\0'
            ? model.assistant_activation_code
            : "配对码将在服务准备好后显示";
    lv_obj_t * code_label = lunaros_view_create_label(status, code, lunaros_font_runtime_14(),
        lv_color_hex(LUNAROS_VIEW_COLOR_PRIMARY));
    lv_obj_set_pos(code_label, 14, 42);
    lv_obj_set_width(code_label, 420);
    lv_label_set_long_mode(code_label, LV_LABEL_LONG_MODE_DOTS);

    const char * action_text = model.assistant_activation_ready
        ? (assistant_activation_clear_confirmation_pending
            ? "再次确认解除绑定" : "解除绑定")
        : model.assistant_activation_pending ? "正在绑定" :
            remote_binding_still_active ? "重新检查" : "开始绑定";
    if(remote_binding_still_active) {
        char support_text[112];
        snprintf(support_text, sizeof(support_text), "设备标识 %s · 持续出现请联系小智官方",
            model.device_serial_number[0] == '\0' ? "暂不可用" : model.device_serial_number);
        lv_obj_t * guide = lunaros_view_create_label(content_root, support_text,
            lunaros_font_runtime_12(), lv_color_hex(LUNAROS_VIEW_COLOR_PRIMARY));
        lv_obj_set_pos(guide, 14, 176);
        lv_obj_set_width(guide, 452);
        lv_label_set_long_mode(guide, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(guide, LV_TEXT_ALIGN_CENTER, 0);
    } else if(lunaros_view_setup_active()) {
        lv_obj_t * guide = lunaros_view_create_label(content_root,
            "在 xiaozhi.me 完成后，设备会自动进入下一步",
            lunaros_font_runtime_12(), lv_color_hex(LUNAROS_VIEW_COLOR_PRIMARY));
        lv_obj_set_pos(guide, 14, 176);
        lv_obj_set_width(guide, 452);
        lv_obj_set_style_text_align(guide, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_t * action = lunaros_view_create_primary_button(content_root, action_text,
        14, (lunaros_view_setup_active() || remote_binding_still_active) ? 210 : 184, 190);
    if(model.assistant_activation_pending) {
        lv_obj_add_state(action, LV_STATE_DISABLED);
    }
    lv_obj_add_event_cb(action, assistant_activation_clear_event,
        LV_EVENT_CLICKED, NULL);
}





void lunaros_set_assistant_state(
    int activation_ready,
    int activation_pending,
    const char * activation_code,
    int transport_connected,
    const char * session_state,
    const char * last_error,
    int service_enabled,
    int suspended,
    int speaking
)
{
    const bool was_speaking = model.assistant_speaking ||
        strcmp(model.assistant_session_state, "speaking") == 0;
    const char * next_code = activation_code == NULL ? "" : activation_code;
    const char * next_session = session_state == NULL ? "inactive" : session_state;
    const char * next_error = last_error == NULL ? "" : last_error;
    const bool next_speaking = speaking != 0 ||
        strcmp(next_session, "speaking") == 0;
    const bool was_waiting_for_connection =
        strcmp(model.assistant_session_state, "opening") == 0 ||
        !model.assistant_transport_connected;
    const bool next_waiting_for_connection =
        strcmp(next_session, "opening") == 0 || transport_connected == 0;
    const bool changed =
        model.assistant_activation_ready != (activation_ready != 0) ||
        model.assistant_activation_pending != (activation_pending != 0) ||
        strcmp(model.assistant_activation_code, next_code) != 0 ||
        model.assistant_transport_connected != (transport_connected != 0) ||
        strcmp(model.assistant_session_state, next_session) != 0 ||
        strcmp(model.assistant_last_error, next_error) != 0 ||
        model.assistant_service_enabled != (service_enabled != 0) ||
        model.assistant_suspended != (suspended != 0) ||
        model.assistant_speaking != next_speaking;

    model.assistant_activation_ready = activation_ready != 0;
    model.assistant_activation_pending = activation_pending != 0;
    snprintf(model.assistant_activation_code,
        sizeof(model.assistant_activation_code), "%s", next_code);
    model.assistant_transport_connected = transport_connected != 0;
    model.assistant_service_enabled = service_enabled != 0;
    model.assistant_suspended = suspended != 0;
    model.assistant_speaking = next_speaking;
    snprintf(model.assistant_session_state,
        sizeof(model.assistant_session_state), "%s", next_session);
    snprintf(model.assistant_last_error,
        sizeof(model.assistant_last_error), "%s", next_error);

    if(ui_initialized && changed && lunaros_view_setup_active() &&
       current_page == PAGE_PET_BINDING &&
       model.assistant_activation_ready) {
        lunaros_view_setup_advance(false);
        return;
    }

    if(next_waiting_for_connection &&
       (!was_waiting_for_connection || !assistant_connection_notice_pending)) {
        assistant_connection_notice_pending = true;
        assistant_connection_notice_started_at_ms = lv_tick_get();
    } else if(!next_waiting_for_connection) {
        assistant_connection_notice_pending = false;
    }

    if(next_speaking) {
        assistant_reply_hold_active = false;
        set_visual_audio_priority(true);
    } else if(was_speaking) {
        // The session state can settle a little before the final PCM buffer is
        // physically drained. Keep the final reply and frozen eye frame long
        // enough to avoid a visible flash or a display/audio race.
        assistant_reply_hold_active = true;
        assistant_reply_hold_started_at_ms = lv_tick_get();
    } else if(strcmp(next_session, "listening") == 0 ||
              strcmp(next_session, "opening") == 0 ||
              strcmp(next_session, "failed") == 0) {
        assistant_reply_hold_active = false;
        set_visual_audio_priority(false);
    }

    if(ui_initialized && changed) {
        if(current_page == PAGE_ONBOARDING) {
            show_page(current_page);
        }
        else update_assistant_subtitle_label();
    }
}

void lunaros_set_assistant_transcript(
    const char * role, const char * text, uint32_t revision)
{
    const char * next_role = role == NULL ? "" : role;
    const char * next_text = text == NULL ? "" : text;
    const bool changed = model.assistant_transcript_revision != revision ||
        strcmp(model.assistant_transcript_role, next_role) != 0 ||
        strcmp(model.assistant_transcript_text, next_text) != 0;
    if(!changed) return;
    snprintf(model.assistant_transcript_role,
        sizeof(model.assistant_transcript_role), "%s", next_role);
    snprintf(model.assistant_transcript_text,
        sizeof(model.assistant_transcript_text), "%s", next_text);
    model.assistant_transcript_revision = revision;
    assistant_transcript_hold_active = next_text[0] != '\0';
    assistant_transcript_hold_started_at_ms = lv_tick_get();
    if(ui_initialized) update_assistant_subtitle_label();
}

void lunaros_set_pet_frame_a8rgb666_u8x4(const uint8_t * pixels, int width, int height)
{
    if(ui_initialized) lv_image_cache_drop(&pet_frame_image);
    if(pixels == NULL || width != PET_FRAME_SIZE || height != PET_FRAME_SIZE) {
        pet_frame_ready = false;
        pet_frame_image.data = NULL;
    } else {
        pet_frame_image.data = pixels;
        pet_frame_ready = true;
    }

    if(ui_initialized) refresh_pet_frame();
}

int lunaros_ui_pet_display_is_companion(void)
{
    return atomic_load(&consumes_pet_frames);
}
void lunaros_set_pet_companion_mode(int enabled) {
    const bool next=enabled!=0;
    if(companion_mode==next) return;
    companion_mode=next;
    atomic_store(&consumes_pet_frames, companion_mode || !visual_ports.mount);
    presentation_changed=true;
}





void lunaros_set_eyes_mode(int active)
{
    const bool next = active != 0;
    if(eyes_mode == next) return;
    eyes_mode = next;
    if(!ui_initialized) return;
    if(next) {
        show_page(PAGE_PET_FULLSCREEN);
    } else if(!next && current_page == PAGE_PET_FULLSCREEN &&
              !companion_mode) {
        // The pet's ambient scene is always full-screen. Clearing an explicit
        // expression takeover returns to the same quiet full-screen home.
        show_page(PAGE_EYES);
    }
}

void lunaros_set_pet_preferences(
    int subtitles_enabled,
    int continuous_conversation)
{
    const bool next_subtitles = subtitles_enabled != 0;
    const bool next_continuous = continuous_conversation != 0;
    assistant_subtitles_enabled = next_subtitles;
    assistant_continuous_conversation = next_continuous;
    update_assistant_subtitle_label();
}

static void assistant_connection_notice_timer_event(lv_timer_t * timer)
{
    (void)timer;
    if(!assistant_connection_notice_pending ||
       lv_tick_elaps(assistant_connection_notice_started_at_ms) <
           ASSISTANT_CONNECTION_NOTICE_DELAY_MS) return;
    update_assistant_subtitle_label();
}
pet_view_model_t *pet_view_model(void) {return &model;}


void lunaros_pet_prepare_route(const char *next_route) {
 (void)next_route;
 if(visual_mounted && visual_ports.unmount) visual_ports.unmount();
 visual_mounted=false;
 pet_frame_parent=NULL;pet_frame_widget=NULL;pet_frame_placeholder=NULL;assistant_subtitle_label=NULL;
}
void lunaros_pet_refresh_subtitle(void) {update_assistant_subtitle_label();}
void lunaros_pet_interrupt(void) {stop_active_assistant();}
void lunaros_pet_view_init(void) {
 lv_timer_create(assistant_reply_hold_timer_event,250,NULL);
 lv_timer_create(assistant_connection_notice_timer_event,250,NULL);
}
void pet_view_leave(const char *next_route) {lunaros_pet_prepare_route(next_route);content_root=NULL;}

void pet_view_mount(lv_obj_t *parent, const char *route, bool entering) {
 (void)entering;content_root=parent;
 if(strcmp(route,"desktop")==0) {build_desktop();}
 if(strcmp(route,"eyes")==0) {mount_visual(parent, 145);}
 if(strcmp(route,"pet-fullscreen")==0) {build_pet_fullscreen(parent);}
 if(strcmp(route,"pet-binding")==0) {build_pet_binding();lunaros_view_column_page(parent, 50);}
 if(strcmp(route,"eyes")==0) {
  lunaros_view_create_status_bar(parent);create_ambient_subtitle(parent);
 }
}

void pet_view_begin_update(void) {updating_state=true;refresh_requested=false;}
bool pet_view_end_update(void) {updating_state=false;return refresh_requested;}
bool pet_view_take_change(void) {bool changed=presentation_changed;presentation_changed=false;return changed;}
