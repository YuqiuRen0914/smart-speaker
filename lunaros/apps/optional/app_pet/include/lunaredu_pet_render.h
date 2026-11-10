#pragma once
#include "lunaredu_pet_visual_environment.h"
#include "lunaredu_pet_frame_region.h"
#include "lunaredu_pet_connections.h"
#include "lunaredu_pet_scene.h"
#include <cstdint>
#include <string>
namespace lunaredu::lunaros { class LocalFactoryMode; }
namespace lunaredu::pet {
enum class TouchPhase : uint8_t { Down, Move, Up, Cancel };
enum class TouchGesture : uint8_t { None, Tap, Hold, Stroke };
struct TouchEvent {
    TouchPhase phase=TouchPhase::Cancel;
    uint16_t x=0,y=0,width=0,height=0;
    uint32_t time_ms=0;
    // Retained/asynchronous hosts stamp the revision of the presented frame.
    // Zero is only for synchronous hosts which already fence physical input.
    uint64_t input_revision=0;
};
struct TouchResult {
    bool captured=false;
    TouchGesture gesture=TouchGesture::None;
    const char *emotion=nullptr;
};
struct ImuSample {float accel[3]{},gyro[3]{};}; // g and bias-corrected degrees/s
struct FramePresentation {
    FrameRegion damage{},visible{};
    // Outside visible, identical signatures promise identical App pixels.
    // Hosts union their previous visible area, including skipped producer frames.
    uint64_t content_signature=0,input_revision=0;
};
struct RenderTelemetry {
    uint32_t fps_x100=0;
    uint32_t raster_us=0;
    uint32_t body_us=0;
    uint32_t eyes_us=0;
    uint32_t present_us=0;
    uint32_t max_frame_us=0;
    uint32_t slow_frames=0;
    uint32_t window_frames=0;
};
struct Ports {
    void *context=nullptr;
    ConnectionPorts connections;
    bool supports_voice=false,supports_agent=false;
    uint32_t target_fps=30;
    uint32_t animation_hz=30;
    VisualPolicy visual_policy=VisualPolicy::Standard;
    bool (*ready)(void *)=nullptr;
    void (*initial_gravity)(void *,float *)=nullptr;
    bool (*read_imu)(void *,ImuSample &)=nullptr;
    VisualEnvironment (*read_visual_environment)(void *)=nullptr;
    bool (*on_start)(void *,std::string &)=nullptr;
    void (*on_stop)(void *,const char *)=nullptr;
    // Native compositors draw a semantic scene on their single render owner.
    // Pixel-frame ports below remain for direct panels and pixel producers.
    void (*present_scene)(void *,Scene)=nullptr;
    // Optional compositor backpressure. Input, UI and animation projection
    // still progress; defer only capture/publication until a slot is available.
    bool (*accept_scene)(void *)=nullptr;
    uint16_t scene_width=0,scene_height=0;
    uint16_t (*scene_content_top)(void *)=nullptr;
    // RGB565 defaults to display wire order; native_rgb565 selects compositor
    // order. begin_frame returns an exclusively writable product surface;
    // present must finish or retain it until the next begin_frame.
    PetFrameBuffer (*begin_frame)(void *)=nullptr;
    // The owner keeps each returned surface unchanged between exclusive
    // acquisitions. Enables bounded decoration retention; never implies a
    // successful panel transfer. Fresh/externally overwritten surfaces omit it.
    bool retained_frame_pixels=false;
    // Region includes old/new visible bounds, so disappearance clears correctly.
    // Empty regions require no transfer.
    void (*present)(void *,FrameRegion)=nullptr;
    void (*present_frame)(void *,const FramePresentation &)=nullptr;
    void (*clear)(void *)=nullptr;
};
// One bounded instance per firmware, explicitly configured during composition.
bool configure(const Ports &,std::string &error);
// Called on the foreground owner. Color/shape/state commands do not change sensor input.
bool command(const char *text);
// Current solver state, for local diagnostics. Null when the pet is inactive.
const char *current_expression();
RenderTelemetry render_telemetry();
struct FrameRateDiagnostic {
    uint32_t target_fps=0;
    uint32_t remaining_ms=0;
};
// Session-scoped diagnostic override; the configured product rate resumes on
// timeout or reset and is never persisted across a reboot.
bool set_diagnostic_frame_rate(uint32_t target_fps,uint32_t duration_ms,
                               std::string &error);
void reset_diagnostic_frame_rate();
FrameRateDiagnostic diagnostic_frame_rate();
bool register_factory_check(lunaros::LocalFactoryMode &mode, std::string &error);
// Called on the foreground owner with native display coordinates. A gesture is
// emitted once, on release; press and movement update the local tactile pose.
bool handle_touch(const TouchEvent &event,TouchResult &result);
const char *touch_gesture_name(TouchGesture gesture);
}
