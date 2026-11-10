#include "diagnostics/function_probe.h"
#include "kernel/task_signal.h"
#include "kernel/runtime.h"
#include "lunaredu_pet_application.h"
#include "lunaredu_pet_service.h"
#include "lunaredu_pet_render.h"
#include "app/presentation_projector.h"
#include "app/touch_input.h"
#include "app/ui_owner.h"
#include "app/pairing_guidance.h"
#include "app/foreground_work.h"
#include "app/frame_ui_retention.h"
#include "app/scene_storage.h"
#ifdef LUNAROS_FACTORY_SERVICE_COMPONENT
#include "lunaros_factory.h"
#endif
#include "kernel/managed_task.h"
#include "kernel/service_executor.h"
#include "kernel/rate_schedule.h"
#include "lunaredu_pet_catalog.h"
#include "pet_render_core.h"
#include "display/pipeline/frame_pacer.h"
#include "display/pipeline/frame_cadence.h"
#if defined(LUNAROS_PET_DMA_CLEAR) && LUNAROS_PET_DMA_CLEAR
#include "display/canvas_zero_fill.h"
#endif
#include <atomic>
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <functional>
#include <esp_timer.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include "diagnostics/runtime_work_profile.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#endif
namespace lunaredu::pet {
struct SceneUi {
    UiSnapshot view;
    UiDecoration decoration;
    UiControls controls;
    FrameRegion character{};
    uint64_t key=0,input_revision=0;
    uint16_t content_top=0;
};
struct SceneFrame {
    PetRenderSnapshot animation{};
    std::shared_ptr<const SceneUi> ui;
    uint32_t activation=0;
    uint16_t width=0,height=0;
    int status=0;
    uint32_t samples=0,errors=0,produced=0;
    uint64_t age=0;
};
namespace {
std::mutex raster_mutex;
#if defined(LUNAROS_PET_DMA_CLEAR) && LUNAROS_PET_DMA_CLEAR
display::CanvasZeroFill canvas_zero;
bool canvas_zero_attempted=false;
#endif
system::RuntimeWorkProfile<4> scene_work{true};
std::atomic<uint32_t> scene_activation{0},scene_published{0},scene_pool_busy{0};
std::array<std::shared_ptr<SceneFrame>,3> scene_slots;
std::shared_ptr<const SceneUi> scene_ui;
uint32_t scene_sample=UINT32_MAX;
uint32_t raster_activation=0;
Ports ports;
FrameDamage damage;
display::ScreenFramePacer frame_pacer;
// A steady 30 Hz presentation leaves enough CPU and bus headroom for audio,
// wake-word processing, captions and network work on the XiaoXing product.
uint64_t present_us=0,begin_us=0,max_frame_us=0,pixel_bytes=0;
uint64_t clear_us=0,body_us=0,eyes_us=0,raster_us=0;
uint64_t surface_clear_us=0,ui_paint_us=0;
uint64_t back_ribbon_us=0,effects_us=0,body_shape_us=0,
         eye_shape_us=0,front_ribbon_us=0;
uint32_t slow_frames=0;
struct RasterTaskProfile { uint64_t total=0,body=0,eyes=0,effects=0;uint32_t frames=0; };
#if defined(ESP_PLATFORM) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
EXT_RAM_BSS_ATTR
#endif
RasterTaskProfile raster_task_profile;
std::atomic<uint32_t> telemetry_fps_x100{0},telemetry_raster_us{0},
    telemetry_body_us{0},telemetry_eyes_us{0},
    telemetry_present_us{0},telemetry_max_frame_us{0},
    telemetry_slow_frames{0},telemetry_window_frames{0};
std::mutex mutex;
std::atomic<bool> service_live{false},worker_exited{true},active{false};
lunaros::KernelRuntime *animation_runtime=nullptr;
uint32_t activation_epoch=0;
PetRenderSnapshot latest{};
uint64_t last_sample=0,window=0,render_us=0;
uint64_t reported_missed_slots=0;
uint32_t samples=0,errors=0,frames=0,previous_samples=0;
std::atomic<uint32_t> produced_samples{0};
uint32_t previous_produced_samples=0;
// Cross-owner mutations invalidate Main's no-work admission. They do not
// replace the protected payload or permit reading it without its owner lock.
std::atomic<uint32_t> foreground_inputs_revision{0};
uint32_t handled_foreground_inputs_revision=0;
uint64_t next_frame_override_expiry=0,next_expression_override_expiry=0;
uint32_t active_target_fps=0;
lunaros::RateSchedule scene_projection_schedule;
lunaros::RateSchedule foreground_observation_schedule;
uint64_t foreground_observed_revision=0;
UiDecoration foreground_decoration;
uint64_t projected_ui_revision=0;
std::string projected_factory_override;
uint32_t diagnostic_target_fps=0;
int64_t diagnostic_expires_us=0;
TouchInput touch;
std::string factory_expression;
std::mutex factory_mutex;
const char *benchmark_expression=nullptr;
uint64_t benchmark_expression_until=0;
PresentationProjector presentation_projector;
UiSession ui;
UiRenderer ui_renderer;
FrameUiRetention ui_retention;
std::atomic<bool> animation_visible{true};
UiControls displayed_controls;
FrameRegion displayed_character{};
UiAction held_action=UiAction::None;
uint64_t held_revision=0;
uint16_t held_x=0,held_y=0;
uint64_t painted_input_revision=0;
unsigned observed_decoration_controls=0;
uint64_t painted_ui_key=0;
DisplayMode observed_default_mode=DisplayMode::Window;
bool observed_flexible=false;
uint64_t observed_ui_revision=0,presentation_key=0;
UiDecoration observed_decoration;
uint64_t ui_key(const UiSnapshot &,const UiDecoration &decoration){
    const auto revision=ui.presentation_revision();
    if(!presentation_key||revision!=observed_ui_revision||!(decoration==observed_decoration)){
      observed_ui_revision=revision;observed_decoration=decoration;++presentation_key;
    }
    return presentation_key;
}
bool prepare_scenes(std::string &error){
    if(!ports.present_scene)return true;
    for(auto &slot:scene_slots){
        slot=make_scene_object<SceneFrame>();
        if(!slot){scene_slots={};error="pet_scene_allocation_failed";return false;}
    }
    return true;
}
void publish_scene(const PetRenderSnapshot &animation,const UiSnapshot &view,
                   const UiDecoration &decoration,bool animate,uint64_t age,uint32_t sample,uint32_t count,uint32_t failed){
    const auto top=std::min<uint16_t>(ports.scene_content_top?ports.scene_content_top(ports.context):0,ports.scene_height-1);
    if(scene_ui&&scene_ui->content_top!=top)ui.presentation_changed();
    const auto key=ui_key(view,decoration)^(uint64_t(top)<<40U);
    if(scene_ui&&scene_ui->key==key&&(!animate||sample==scene_sample))return;
    std::shared_ptr<SceneFrame> writable;
    for(const auto &slot:scene_slots)if(slot&&slot.use_count()==1){writable=slot;break;}
    if(!writable){++scene_pool_busy;return;}
    if(!scene_ui||scene_ui->key!=key){
        auto replacement=make_scene_object<SceneUi>();
        if(!replacement){++scene_pool_busy;return;}
        replacement->view=view;replacement->decoration=decoration;
        replacement->key=key;replacement->input_revision=ui.input_revision();
        replacement->content_top=top;
        replacement->controls=ui_controls(view,ports.scene_width,ports.scene_height-top,decoration);
        for(size_t i=0;i<replacement->controls.count;++i)replacement->controls.items[i].bounds.y+=top;
        replacement->character=animate?UiRenderer::character_region(view,ports.scene_width,ports.scene_height-top):FrameRegion{};
        replacement->character.y+=top;
        scene_ui=std::move(replacement);
    }
    writable->animation=animation;writable->ui=scene_ui;
    writable->activation=scene_activation.load();
    writable->width=ports.scene_width;writable->height=ports.scene_height;
    writable->samples=count;writable->errors=failed;writable->produced=sample;writable->age=age;
    writable->status=ports.read_imu&&feature_preferences().imu&&age>=100000?2:0;
    // Main owns input semantics; physical acknowledgements continue to fence
    // these controls until the matching scene actually reaches the panel.
    displayed_controls=scene_ui->controls;displayed_character=scene_ui->character;
    painted_input_revision=scene_ui->input_revision;scene_sample=sample;
    ++scene_published;ports.present_scene(ports.context,std::move(writable));
}

#ifdef LUNAROS_FACTORY_SERVICE_COMPONENT
bool factory_available(void *, std::string &reason) {
    if (active.load()) { reason.clear(); return true; }
    reason = "pet_not_active";
    return false;
}

bool factory_start(void *context, const char *action,
                   lunaros::FactoryStartState &state,
                   std::string &detail, std::string &error) {
    const char *expression = static_cast<const char *>(context);
    if (action != nullptr && action[0] != '\0' &&
        std::strcmp(action, expression) != 0) {
        error = "factory_expression_action_mismatch";
        return false;
    }
    if (!catalog::character_catalog_contains(
            catalog::kCharacterStates, expression)) {
        error = "factory_expression_unknown";
        return false;
    }
    if (!active) {
        error = "pet_not_active";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(factory_mutex);
        factory_expression = expression;
        ++foreground_inputs_revision;
    }
    state = lunaros::FactoryStartState::AwaitingConfirmation;
    detail = std::string("请目视确认表情 ") + expression;
    error.clear();
    return true;
}
void factory_cancel(void *) {
    std::lock_guard<std::mutex> lock(factory_mutex);
    factory_expression.clear();
    ++foreground_inputs_revision;
}
bool factory_confirm(void *, bool, std::string &error) {
    std::lock_guard<std::mutex> lock(factory_mutex);
    factory_expression.clear();
    ++foreground_inputs_revision;
    error.clear();
    return true;
}
#endif

bool prepared( void *,std::string &error){
    if(!setup_runtime()){error="pet_runtime_unavailable";return false;}
    if(!ports.ready||!ports.ready(ports.context)){error="pet_hardware_unavailable";return false;}
    if(!service_live){error="pet_animation_unavailable";return false;}
    error.clear();return true;
}
void worker(void *){
    TickType_t wake=xTaskGetTickCount();uint64_t previous=esp_timer_get_time();
    uint32_t fractional_ticks=0,previous_epoch=0;
    ImuSample held_input;held_input.accel[2]=1.0f;
    for(;;){
        uint32_t epoch=0;
        {std::lock_guard<std::mutex> lock(mutex);
            if(!service_live||!active)break;
            epoch=activation_epoch;
        }
        const bool resumed=epoch!=previous_epoch;
        if(resumed){wake=xTaskGetTickCount();previous=esp_timer_get_time();fractional_ticks=0;previous_epoch=epoch;held_input={};held_input.accel[2]=1.0f;if(ports.initial_gravity)ports.initial_gravity(ports.context,&held_input.accel[0]);}
        if(!animation_visible.load()){vTaskDelay(pdMS_TO_TICKS(50));wake=xTaskGetTickCount();previous=esp_timer_get_time();continue;}
        ImuSample input;input.accel[2]=1.0f;
        const bool use_imu=ports.read_imu&&feature_preferences().imu;
        bool ok=!use_imu||ports.read_imu(ports.context,input);
        if(ok)held_input=input;
        else{
            // Animate through a sensor outage using the last known gravity.
            // An old angular velocity must not invent continued physical motion.
            input=held_input;for(auto &rate:input.gyro)rate=0.0f;
        }
        uint64_t now=esp_timer_get_time();float dt=resumed?1.0f/ports.animation_hz:float(now-previous)*1e-6f;previous=now;
        {std::lock_guard<std::mutex> lock(mutex);
            // A read admitted by the old view must not update a freshly reset
            // animator after a quick leave/re-enter during hardware I/O.
            if(active&&service_live&&epoch==activation_epoch){
                auto s=pet_render_sample(input.accel[0],input.accel[1],input.accel[2],input.gyro[0],input.gyro[1],input.gyro[2],dt);
                if(s.valid){
                    latest=s;++produced_samples;
                    if(ok){last_sample=now;++samples;}
                }
                // Freshness and failures still describe actual sensor reads.
                if(!ok||!s.valid)++errors;
            }
        }
        // Carry fractional FreeRTOS ticks: 30 Hz alternates 33/34 ms with
        // a 1 kHz tick, preserving the absolute cadence without integer drift.
        fractional_ticks += configTICK_RATE_HZ % ports.animation_hz;
        TickType_t delay_ticks = configTICK_RATE_HZ / ports.animation_hz;
        if (fractional_ticks >= ports.animation_hz) {
            fractional_ticks -= ports.animation_hz;
            ++delay_ticks;
        }
        xTaskDelayUntil(&wake,std::max<TickType_t>(1,delay_ticks));
    }
    {std::lock_guard<std::mutex> lock(mutex);worker_exited=true;}
    // The OS task wrapper releases its admission on return.
}
bool start_animation_worker(std::string &error){
    if(!animation_runtime){error="animation_owner_unavailable";return false;}
    const auto *task_id=ports.read_imu?"pet.sensor":"pet.animation";
    if(!worker_exited||animation_runtime->task_active(task_id)){
        error="animation_worker_retiring";return false;
    }
    worker_exited=false;
    lunaros::ManagedTaskDefinition task;
    task.id=ports.read_imu?"pet.sensor":"pet.animation";task.owner_service_id=kAnimationServiceId;
    task.role=ports.read_imu?"sensor-animation":"fixed-animation";
    task.lifetime=lunaredu::lunaros::ManagedTaskLifetime::Feature;
    // The assistant can initialize its AFE concurrently during product boot.
    // Keep this non-DMA animation stack in PSRAM so it cannot fragment the small
    // internal blocks required by ESP-SR, Wi-Fi and TLS.
    task.memory=lunaredu::lunaros::ManagedTaskMemory::External;
    task.stack_bytes=8192;task.priority=5;task.core_affinity=1;
    if(!lunaros::create_managed_task(*animation_runtime,task,worker,nullptr,nullptr,error)){
        worker_exited=true;return false;
    }
    return true;
}
lunaros::ServiceStartResult start_engine(void *context){
    if(service_live)return {lunaros::ServiceStartDisposition::Ready,"already ready"};
    if(!ports.ready||!ports.ready(ports.context))return {lunaros::ServiceStartDisposition::Failed,"hardware unavailable"};
    const auto *task_id=ports.read_imu?"pet.sensor":"pet.animation";
    if(!worker_exited||static_cast<lunaros::KernelRuntime *>(context)->task_active(task_id))
        return {lunaros::ServiceStartDisposition::Failed,"worker still stopping"};
    animation_runtime=static_cast<lunaros::KernelRuntime *>(context);
    service_live=true;
    return {lunaros::ServiceStartDisposition::Ready,"animation_on_demand"};
}
lunaros::ServiceStopResult stop_engine_step(void *context){
    {std::lock_guard<std::mutex> lock(mutex);
        active=false;service_live=false;
    }
    const auto *task_id=ports.read_imu?"pet.sensor":"pet.animation";
    const bool retired=worker_exited&&!static_cast<lunaros::KernelRuntime *>(context)->task_active(task_id);
    return {retired?lunaros::ServiceStopDisposition::Stopped:lunaros::ServiceStopDisposition::Pending,
            retired?"animation retired":"animation retiring"};
}
bool stop_engine(void *context,std::string &error){
    for(int i=0;i<100;++i){
        if(stop_engine_step(context).disposition==lunaros::ServiceStopDisposition::Stopped){error.clear();return true;}
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    error="sensor_stop_timeout";return false;
}
app::ApplicationStartResult start_app(void *, [[maybe_unused]] lunaredu::app::ApplicationEnterReason reason,std::string &error){
    const auto *task_id=ports.read_imu?"pet.sensor":"pet.animation";
    if(!worker_exited||(animation_runtime&&animation_runtime->task_active(task_id))){
        error="animation_worker_retiring";return app::ApplicationStartResult::Failed;
    }
    if(!prepared(nullptr,error))return app::ApplicationStartResult::Failed;
    set_runtime_active(true);
    std::lock_guard<std::mutex> raster_lock(raster_mutex);
    {
        std::lock_guard<std::mutex> lock(mutex);
        float gravity[3]{0.0f,0.0f,1.0f};
        if(ports.initial_gravity)ports.initial_gravity(ports.context,gravity);
        observed_flexible=feature_preferences().flexible;pet_render_set_flexible(observed_flexible);
        pet_render_reset(gravity[0],gravity[1],gravity[2]);
        pet_render_invalidate();
        latest={};last_sample=0;samples=errors=frames=previous_samples=0;produced_samples=previous_produced_samples=0;
        frame_pacer.configure(ports.target_fps);
        active_target_fps=ports.target_fps;
        reported_missed_slots=0;
        window=esp_timer_get_time();render_us=present_us=begin_us=max_frame_us=pixel_bytes=0;
        clear_us=body_us=eyes_us=raster_us=0;surface_clear_us=ui_paint_us=0;
        back_ribbon_us=effects_us=body_shape_us=eye_shape_us=front_ribbon_us=0;
        slow_frames=0;damage.invalidate();touch.reset();presentation_projector.reset();
        scene_projection_schedule.reset();projected_ui_revision=0;projected_factory_override.clear();
        foreground_observation_schedule.reset();foreground_observed_revision=0;foreground_decoration={};
        ui.enter(display_mode());ui.feature_support(1U|(ports.read_imu?2U:0U)|(ports.supports_voice?4U:0U)|(ports.supports_agent?8U:0U));observed_default_mode=display_mode();painted_ui_key=0;ui_retention.reset();animation_visible=true;painted_input_revision=0;observed_decoration_controls=0;held_action=UiAction::None;
    }
    scene_sample=UINT32_MAX;scene_ui.reset();scene_published=0;scene_pool_busy=0;
    if(!prepare_scenes(error))return app::ApplicationStartResult::Failed;
    if(ports.on_start&&!ports.on_start(ports.context,error)){
        if(error.empty())error="pet_screen_acquire_failed";
        return app::ApplicationStartResult::Failed;
    }
    {std::lock_guard<std::mutex> lock(mutex);
        if(++activation_epoch==0)++activation_epoch;
        scene_activation.store(activation_epoch);active=true;
    }
    if(!start_animation_worker(error)){
        active=false;return app::ApplicationStartResult::Failed;
    }
    ESP_LOGI("pet","APP_START id=%s",kApplicationId);
    return app::ApplicationStartResult::Started;
}
bool stop_app(void *,const char *reason,std::string &error){
    {std::lock_guard<std::mutex> lock(mutex);active=false;touch.reset();}
    const auto *task_id=ports.read_imu?"pet.sensor":"pet.animation";
    const auto deadline=esp_timer_get_time()+500000;
    while(!worker_exited||(animation_runtime&&animation_runtime->task_active(task_id))){
        if(esp_timer_get_time()>=deadline){error="animation_retirement_pending";return false;}
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if(!stop_pairing_guidance(error))return false;
    {
        std::lock_guard<std::mutex> lock(factory_mutex);
        factory_expression.clear();
        benchmark_expression=nullptr;benchmark_expression_until=0;
        benchmark_expression=nullptr;benchmark_expression_until=0;
    }
    std::lock_guard<std::mutex> raster_lock(raster_mutex);
    scene_ui.reset();scene_slots={};
#if defined(LUNAROS_PET_DMA_CLEAR) && LUNAROS_PET_DMA_CLEAR
    canvas_zero.release();canvas_zero_attempted=false;
#endif
    scene_projection_schedule.reset();projected_ui_revision=0;
    foreground_observation_schedule.reset();foreground_observed_revision=0;foreground_decoration={};
    std::string{}.swap(projected_factory_override);
    presentation_projector.reset();
    ui.leave();ui_renderer.reset();ui_retention.reset();painted_ui_key=0;detach_ui_presentation();displayed_controls={};displayed_character={};held_action=UiAction::None;
    if(ports.clear)ports.clear(ports.context);
    if(ports.on_stop)ports.on_stop(ports.context,reason);
    set_runtime_active(false);
    ESP_LOGI("pet","APP_STOP reason=%s",reason?reason:"");
    error.clear();return true;
}
void report_window(const PetRenderSnapshot &snapshot,uint64_t done,uint32_t count,uint32_t failed,uint32_t produced,uint64_t age){
    if(done-window>=2000000){
        telemetry_fps_x100.store(uint32_t(frames*1e8/double(done-window)),std::memory_order_relaxed);
        telemetry_raster_us.store(uint32_t(raster_us/frames),std::memory_order_relaxed);
        telemetry_body_us.store(uint32_t(body_us/frames),std::memory_order_relaxed);
        telemetry_eyes_us.store(uint32_t(eyes_us/frames),std::memory_order_relaxed);
        telemetry_present_us.store(uint32_t(present_us/frames),std::memory_order_relaxed);
        telemetry_max_frame_us.store(uint32_t(max_frame_us),std::memory_order_relaxed);
        telemetry_slow_frames.store(slow_frames,std::memory_order_relaxed);
        telemetry_window_frames.store(frames,std::memory_order_relaxed);
        ESP_LOGI("pet","PERF fps=%.2f render_display_ms=%.2f animation_hz=%.1f sensor_hz=%.1f source=%s samples=%u errors=%u age_ms=%.1f heap=%u stack=%u renderer=2d",
            frames*1e6/double(done-window),render_us/(1000.0*frames),(produced-previous_produced_samples)*1e6/double(done-window),(count-previous_samples)*1e6/double(done-window),ports.read_imu&&feature_preferences().imu?"imu":"fixed",unsigned(count),unsigned(failed),age/1000.0,
            unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(uxTaskGetStackHighWaterMark(nullptr)));
        ESP_LOGI("pet","STATE shape=%d color=%06lx custom=%d state=%s tilt=(%.3f,%.3f) turn_rate=%.3f shake=%.3f",
            snapshot.frame.shape_to,(unsigned long)snapshot.frame.body_color,snapshot.frame.color_custom,pet_render_state_name(snapshot.frame.state),
            snapshot.tilt_x,snapshot.tilt_y,snapshot.turn_rate,snapshot.shake);
        const uint64_t missed_slots=ports.present_scene?0:frame_pacer.missed();
        ESP_LOGI("pet","FRAME target=%u present_ms=%.2f bytes=%.0f begin_ms=%.2f max_work_ms=%.2f over_budget=%u/%u missed_slots=%llu",
            unsigned(active_target_fps),present_us/(1000.0*frames),pixel_bytes/double(frames),begin_us/(1000.0*frames),max_frame_us/1000.0,unsigned(slow_frames),unsigned(frames),
            static_cast<unsigned long long>(missed_slots-reported_missed_slots));
        reported_missed_slots=missed_slots;
        ESP_LOGI("pet","RASTER clear_ms=%.2f body_ms=%.2f eyes_ms=%.2f total_ms=%.2f other_ms=%.2f",
            clear_us/(1000.0*frames),body_us/(1000.0*frames),eyes_us/(1000.0*frames),raster_us/(1000.0*frames),
            (render_us-present_us-raster_us)/(1000.0*frames));
        ESP_LOGI("pet","RASTER_STAGES back_ribbon_ms=%.2f effects_ms=%.2f body_shape_ms=%.2f eye_shape_ms=%.2f front_ribbon_ms=%.2f",
            back_ribbon_us/(1000.0*frames),effects_us/(1000.0*frames),
            body_shape_us/(1000.0*frames),eye_shape_us/(1000.0*frames),
            front_ribbon_us/(1000.0*frames));
        ESP_LOGI("pet","UI_STAGES surface_clear_ms=%.2f controls_ms=%.2f",
            surface_clear_us/(1000.0*frames),ui_paint_us/(1000.0*frames));
        if(raster_task_profile.frames) {
          const double divisor=1000.0*raster_task_profile.frames;
          ESP_LOGI("pet","RASTER_TASK frames=%lu total_ms=%.3f body_ms=%.3f eyes_ms=%.3f effects_ms=%.3f",
              (unsigned long)raster_task_profile.frames,raster_task_profile.total/divisor,
              raster_task_profile.body/divisor,raster_task_profile.eyes/divisor,raster_task_profile.effects/divisor);
        }
        raster_task_profile={};
        frames=0;render_us=present_us=begin_us=max_frame_us=pixel_bytes=0;
        clear_us=body_us=eyes_us=raster_us=0;surface_clear_us=ui_paint_us=0;slow_frames=0;window=done;previous_samples=count;previous_produced_samples=produced;
        back_ribbon_us=effects_us=body_shape_us=eye_shape_us=front_ribbon_us=0;
    }
}
void tick(void *){
    system::probe::Scope diagnostic_tick(system::probe::Point::PetTick);
    using foreground_work::Stage;
    foreground_work::begin();
    uint64_t now=esp_timer_get_time();
    if(!active)return;
    const auto preferences=feature_preferences();
    const auto default_mode=display_mode();
    const auto inputs_revision=foreground_inputs_revision.load(std::memory_order_acquire);
    const bool observe=!ports.present_scene||foreground_observed_revision!=ui.presentation_revision()||
        preferences.bits()!=ui.snapshot().features.bits()||observed_default_mode!=default_mode||
        !foreground_observation_schedule.wait_ms(uint32_t(now/1000U),ports.target_fps);
    if(observe){
    foreground_work::measure(Stage::Preferences,[&]{
    const auto flexible=preferences.flexible;
    if(flexible!=observed_flexible){std::lock_guard<std::mutex> animation_lock(mutex);pet_render_set_flexible(flexible);observed_flexible=flexible;}
    if(observed_default_mode!=display_mode()){
        observed_default_mode=display_mode();
        if(ui.snapshot().mode!=observed_default_mode)(void)ui.action(UiAction::ToggleMode,ports.connections);
    }
    });
    foreground_work::measure(Stage::Pairing,[&]{ui.setup_caption(pairing_guidance_caption(ports.visual_policy==VisualPolicy::PairingDevice,now/1000U));});
    foreground_work::measure(Stage::Connections,[&]{ui.poll(ports.connections,now/1000U);});
    const auto &view=ui.snapshot();
    foreground_decoration=foreground_work::measure(Stage::Decoration,[&]{return ui_decoration(view.page);});
    foreground_observed_revision=ui.presentation_revision();
    foreground_observation_schedule.complete(uint32_t(now/1000U),uint32_t(esp_timer_get_time()/1000U),ports.target_fps);
    }
    // No new animation, UI, override or scheduled work: the old path reached
    // the same unchanged-scene rejection only after locks and string work.
    // Keep observation/projection deadlines and all input invalidations live.
    if(ports.present_scene && scene_ui && !observe &&
       ui.presentation_revision()==projected_ui_revision &&
       preferences.bits()==ui.snapshot().features.bits() &&
       inputs_revision==handled_foreground_inputs_revision &&
       (!next_frame_override_expiry || now<next_frame_override_expiry) &&
       (!next_expression_override_expiry || now<next_expression_override_expiry) &&
       scene_projection_schedule.wait_ms(uint32_t(now/1000U),active_target_fps) &&
       produced_samples.load(std::memory_order_acquire)==scene_sample &&
       scene_ui->content_top==std::min<uint16_t>(ports.scene_content_top?ports.scene_content_top(ports.context):0,ports.scene_height-1))return;
    const auto &view=ui.snapshot();
    const auto &decoration=foreground_decoration;
    const unsigned decoration_controls=unsigned(decoration.connection_open)|(unsigned(decoration.connection_busy)<<1U)|(unsigned(decoration.connection_next)<<2U)|(unsigned(decoration.autonomous_appearance_enabled)<<3U);
    if(decoration_controls!=observed_decoration_controls){observed_decoration_controls=decoration_controls;ui.presentation_changed();}
    const bool animate=ui.animate()&&!decoration.connection_open;
    animation_visible.store(animate);
    uint32_t desired_fps=animate?ports.target_fps:2;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(diagnostic_target_fps!=0&&now<static_cast<uint64_t>(diagnostic_expires_us))
            desired_fps=diagnostic_target_fps;
        else if(diagnostic_target_fps!=0){diagnostic_target_fps=0;diagnostic_expires_us=0;}
        next_frame_override_expiry=uint64_t(diagnostic_expires_us);
    }
    if(desired_fps!=active_target_fps){
        frame_pacer.configure(desired_fps);active_target_fps=desired_fps;
    }
    if(!ports.present_scene&&painted_input_revision==ui.input_revision()&&!frame_pacer.due(now))return;
    const uint64_t frame_period_us=frame_pacer.period_us();
    std::string factory_override;
    foreground_work::measure(Stage::Overrides,[&]{
        std::lock_guard<std::mutex> lock(factory_mutex);
        if (!factory_expression.empty()) factory_override = factory_expression;
        else if(benchmark_expression&&now<benchmark_expression_until)factory_override=benchmark_expression;
        else {benchmark_expression=nullptr;benchmark_expression_until=0;}
        next_expression_override_expiry=benchmark_expression_until;
    });
    handled_foreground_inputs_revision=inputs_revision;
    const bool project=foreground_work::measure(Stage::Admission,[&]{
      return !ports.present_scene||projected_ui_revision!=ui.presentation_revision()||
          projected_factory_override!=factory_override||
          !scene_projection_schedule.wait_ms(uint32_t(now/1000U),desired_fps);
    });
    if(project)foreground_work::measure(Stage::Projection,[&]{
      std::lock_guard<std::mutex> animation_lock(mutex);
      presentation_projector.project(ports, static_cast<uint32_t>(now/1000U),factory_override);
      if(ports.present_scene){
        projected_ui_revision=ui.presentation_revision();projected_factory_override=factory_override;
        scene_projection_schedule.complete(uint32_t(now/1000U),uint32_t(esp_timer_get_time()/1000U),desired_fps);
      }
    });
    if(ports.present_scene&&ports.accept_scene&&!foreground_work::measure(Stage::SceneAcceptance,[&]{return ports.accept_scene(ports.context);}))return;
    PetRenderSnapshot snapshot;uint32_t count,failed,produced;uint64_t age;
    const bool snapshot_ready=foreground_work::measure(Stage::SceneSnapshot,[&]{std::lock_guard<std::mutex> lock(mutex);
      count=samples;failed=errors;produced=produced_samples;age=now>=last_sample?now-last_sample:0;
      if(ports.present_scene&&scene_ui){
        const auto top=std::min<uint16_t>(ports.scene_content_top?ports.scene_content_top(ports.context):0,ports.scene_height-1);
        if(scene_ui->content_top==top&&scene_ui->key==(ui_key(view,decoration)^(uint64_t(top)<<40U))&&(!animate||produced==scene_sample))return false;
      }
      snapshot=latest;return true;
    });
    if(!snapshot_ready)return;
    if(ports.present_scene){
      foreground_work::measure(Stage::Publication,[&]{publish_scene(snapshot,view,decoration,animate,age,produced,count,failed);});
      return;
    }
    const uint64_t begin_started=esp_timer_get_time();
    const PetFrameBuffer frame=ports.begin_frame(ports.context);
    if(!frame.pixels||frame.width<=0||frame.height<=0||frame.stride<frame.width)return;
    begin_us+=esp_timer_get_time()-begin_started;
    // The three-dot marker is a sensor-health diagnostic, not part of the character
    // expression. Products without an IMU use a fixed input, so a
    // scheduling delay must not flash a false stale-sensor warning onscreen.
    // One surface and render owner for all pages. Every submitted snapshot is
    // complete, including products whose retained frames rotate in a pool.
    displayed_character=animate?ui_renderer.character_region(view,frame.width,frame.height):FrameRegion{};
    const auto paint_key=ui_key(view,decoration)^(uint64_t(frame.antialias)<<63U)^
      (uint64_t(frame.native_rgb565)<<62U)^(uint64_t(frame.width)<<32U)^uint64_t(frame.height);
    // Window-mode decoration is outside the fixed character rectangle. Reuse
    // it per actual pool surface; UI changes, mode changes, AA and dimensions
    // repaint the whole surface. Fullscreen keeps its overlapping overlays.
    const bool retain=ports.retained_frame_pixels&&view.page==Page::Home&&
      !decoration.connection_open&&animate;
    const auto plan=ui_retention.prepare(frame,paint_key,displayed_character,retain,
      view.mode==DisplayMode::Fullscreen,ui_renderer.fullscreen_overlay_bands(frame.width,frame.height));
    const auto clearing_started=esp_timer_get_time();
    // Black is an all-zero RGB565 word in either byte order. Let the optimized
    // bulk writer handle contiguous canvases; preserve padding on strided ones.
    const auto clear_area=[&](FrameRegion area){
      if(area.width<=0||area.height<=0)return;
      if(area.x==0&&area.width==frame.stride)std::memset(frame.pixels+area.y*frame.stride,0,size_t(area.width)*area.height*sizeof(uint16_t));
      else for(int y=area.y;y<area.y+area.height;++y)std::memset(frame.pixels+y*frame.stride+area.x,0,size_t(area.width)*sizeof(uint16_t));
    };
    clear_area(plan.clear);
    if(plan.clear_overlays)for(const auto band:plan.overlays)clear_area(band);
    surface_clear_us+=esp_timer_get_time()-clearing_started;
    if(displayed_character.width>0){
      auto character=frame;
      character.pixels+=displayed_character.y*frame.stride+displayed_character.x;
      character.width=displayed_character.width;character.height=displayed_character.height;
      character.body_diameter=view.mode==DisplayMode::Fullscreen?std::min(frame.width,frame.height)*2/3:std::min(character.width,character.height)*4/5;
      pet_render_frame_cleared(character,&snapshot,ports.read_imu&&feature_preferences().imu&&age>=100000?2:0);
    }
    const auto painting_started=esp_timer_get_time();
    displayed_controls=ui_controls(view,frame.width,frame.height,decoration);
    if(plan.paint_ui)ui_renderer.paint(frame,view,decoration,&displayed_controls);
    ui_paint_us+=esp_timer_get_time()-painting_started;
    painted_input_revision=ui.input_revision();
    if(paint_key!=painted_ui_key){damage.invalidate();painted_ui_key=paint_key;}
    const PetRenderProfile profile=pet_render_profile();
    if(profile.task_profiled) {
      raster_task_profile.total+=profile.total_task_us;raster_task_profile.body+=profile.body_task_us;
      raster_task_profile.eyes+=profile.eyes_task_us;raster_task_profile.effects+=profile.effects_task_us;
      ++raster_task_profile.frames;
    } else raster_task_profile={};
    clear_us+=profile.clear_us;body_us+=profile.body_us;eyes_us+=profile.eyes_us;raster_us+=profile.total_us;
    back_ribbon_us+=profile.back_ribbon_us;effects_us+=profile.effects_us;
    body_shape_us+=profile.body_shape_us;eye_shape_us+=profile.eye_shape_us;
    front_ribbon_us+=profile.front_ribbon_us;
    auto visible=animate?pet_render_bounds():FrameRegion{};
    visible.x+=displayed_character.x;visible.y+=displayed_character.y;
    ui_retention.rendered(frame,visible);
    FrameRegion region=damage.update(visible,frame.width,frame.height);
    uint64_t submitted=esp_timer_get_time();
    if(ports.present_frame)ports.present_frame(ports.context,{region,visible,paint_key,painted_input_revision});
    else ports.present(ports.context,region);
    present_us+=esp_timer_get_time()-submitted;pixel_bytes+=uint64_t(region.width)*region.height*2;
    uint64_t done=esp_timer_get_time();render_us+=done-now;frames++;max_frame_us=std::max(max_frame_us,done-now);if(done-now>frame_period_us)slow_frames++;
    report_window(snapshot,done,count,failed,produced,age);
}
}
bool paint_scene(const Scene &scene,PetFrameBuffer frame,FramePresentation &presentation,SceneCanvasReuse reuse){
  lunaredu::system::probe::Scope diagnostic_scope(lunaredu::system::probe::Point::Scene);
    std::lock_guard<std::mutex> lock(raster_mutex);
    scene_work.begin();
    if(!scene||!scene->ui||!active.load()||scene->activation!=scene_activation.load()||
       !frame.pixels||frame.width!=scene->width||frame.height!=scene->height||frame.stride<frame.width)return false;
    const uint64_t started=esp_timer_get_time();
    scene_work.mark(0);
    if(raster_activation!=scene->activation){ui_renderer.reset();ui_retention.reset();pet_render_invalidate();raster_activation=scene->activation;}
    const auto &view=scene->ui->view;const auto &decoration=scene->ui->decoration;
    const auto area=scene->ui->character;
    const bool fullscreen=view.mode==DisplayMode::Fullscreen;
    const bool eligible=view.page==Page::Home&&area.width>0&&!decoration.connection_open;
    if(!reuse.retained)ui_retention.reset();
    auto bands=ui_renderer.fullscreen_overlay_bands(frame.width,frame.height-scene->ui->content_top);
    bands[0].height+=scene->ui->content_top;bands[1].y+=scene->ui->content_top;
    const auto plan=ui_retention.prepare(frame,scene->ui->key,area,eligible,fullscreen,
        bands);
    const auto clear=[&](FrameRegion region){
      const int left=std::clamp(region.x,0,frame.width),top=std::clamp(region.y,0,frame.height);
      const int right=int(std::clamp<int64_t>(int64_t(region.x)+region.width,left,frame.width));
      const int bottom=int(std::clamp<int64_t>(int64_t(region.y)+region.height,top,frame.height));
      if(left==0&&right==frame.stride){
        const size_t bytes=size_t(bottom-top)*frame.stride*sizeof(uint16_t);
        auto *pixels=frame.pixels+top*frame.stride;
#if defined(LUNAROS_PET_DMA_CLEAR) && LUNAROS_PET_DMA_CLEAR
        // Only the exact discarded contiguous rows are lent to DMA. Static
        // decoration outside this span stays valid, including on retained slots.
        if(bytes>=4096){
          if(!canvas_zero_attempted){canvas_zero_attempted=true;
            (void)canvas_zero.initialize(size_t(frame.width)*frame.height*sizeof(uint16_t));}
          if(canvas_zero.clear(pixels,bytes))return;
        }
#endif
        std::memset(pixels,0,bytes);
      }
      else for(int row=top;row<bottom;++row)std::memset(frame.pixels+row*frame.stride+left,0,size_t(right-left)*2);
    };
    bool whole_cleared=false;
#if defined(LUNAROS_PET_DMA_CLEAR) && LUNAROS_PET_DMA_CLEAR
    // Only a full discard may use DMA. Reused window decoration remains in
    // the borrowed canvas; clearing it would destroy the retained UI.
    if(plan.clear.x==0&&plan.clear.y==0&&plan.clear.width==frame.width&&
       plan.clear.height==frame.height&&frame.width==frame.stride){
      const size_t bytes=size_t(frame.width)*frame.height*sizeof(uint16_t);
      if(!canvas_zero_attempted){
        canvas_zero_attempted=true;
        const bool ready=canvas_zero.initialize(bytes);
        ESP_LOGI("pet","canvas_zero ready=%d maximum_bytes=%u",ready,unsigned(bytes));
      }
      whole_cleared=canvas_zero.clear(frame.pixels,bytes);
    }
#endif
    if(!whole_cleared){
    clear(plan.clear);
    if(plan.clear_overlays)for(const auto band:plan.overlays)clear(band);
    const auto contains=[](FrameRegion outer,FrameRegion inner){return inner.x>=outer.x&&inner.y>=outer.y&&
        int64_t(inner.x)+inner.width<=int64_t(outer.x)+outer.width&&int64_t(inner.y)+inner.height<=int64_t(outer.y)+outer.height;};
    bool erased=contains(plan.clear,reuse.overwritten);
    if(plan.clear_overlays)for(const auto band:plan.overlays)erased=erased||contains(band,reuse.overwritten);
    if(reuse.retained&&!erased)clear(reuse.overwritten);
    }
    const auto cleared=esp_timer_get_time();surface_clear_us+=cleared-started;
    scene_work.mark(1);
#if defined(LUNAROS_PET_DMA_CLEAR) && LUNAROS_PET_DMA_CLEAR
    if(frames%120==0){const auto zero=canvas_zero.snapshot();
      ESP_LOGI("pet","CANVAS_ZERO ready=%d calls=%u bytes=%llu failures=%u",zero.ready,unsigned(zero.calls),
        static_cast<unsigned long long>(zero.bytes),unsigned(zero.failures));}
#endif
    if(area.width>0){
        auto character=frame;character.pixels+=area.y*frame.stride+area.x;
        character.width=area.width;character.height=area.height;
        character.body_diameter=view.mode==DisplayMode::Fullscreen?std::min(frame.width,frame.height)*2/3:std::min(area.width,area.height)*4/5;
        pet_render_frame_cleared(character,&scene->animation,scene->status);
    }
    const auto ui_started=esp_timer_get_time();
    scene_work.mark(2);
    auto content=frame;content.pixels+=scene->ui->content_top*frame.stride;content.height-=scene->ui->content_top;
    auto controls=scene->ui->controls;
    for(size_t i=0;i<controls.count;++i)controls.items[i].bounds.y-=scene->ui->content_top;
    if(plan.paint_ui)ui_renderer.paint(content,view,decoration,&controls);
    else if(reuse.retained&&reuse.overwritten.width>0&&reuse.overwritten.height>0){
      // The OS lends its exact previous chrome coverage. Repair only those
      // pixels, preserving AA by clearing before painting the clipped UI.
      auto clip=reuse.overwritten;clip.y-=scene->ui->content_top;
      ui_renderer.paint(content,view,decoration,&controls,&clip);
    }
    ui_paint_us+=esp_timer_get_time()-ui_started;
    scene_work.mark(3);
    const auto work=scene_work.finish();
    if(work.calls)ESP_LOGI("pet","SCENE_TASK calls=%u window_us=%llu worker_task_us=%u admission_us=%llu clear_us=%llu character_us=%llu ui_us=%llu admission_task_us=%llu clear_task_us=%llu character_task_us=%llu ui_task_us=%llu",
      unsigned(work.calls),static_cast<unsigned long long>(work.window_us),unsigned(work.worker_task_us),
      static_cast<unsigned long long>(work.wall[0]),static_cast<unsigned long long>(work.wall[1]),static_cast<unsigned long long>(work.wall[2]),static_cast<unsigned long long>(work.wall[3]),
      static_cast<unsigned long long>(work.settled_task[0]),static_cast<unsigned long long>(work.settled_task[1]),static_cast<unsigned long long>(work.settled_task[2]),static_cast<unsigned long long>(work.settled_task[3]));
    auto visible=area.width>0?pet_render_bounds():FrameRegion{};
    visible.x+=area.x;visible.y+=area.y;
    ui_retention.rendered(frame,visible);
    const auto profile=area.width>0?pet_render_profile():PetRenderProfile{};
    clear_us+=profile.clear_us;body_us+=profile.body_us;eyes_us+=profile.eyes_us;raster_us+=profile.total_us;
    back_ribbon_us+=profile.back_ribbon_us;effects_us+=profile.effects_us;body_shape_us+=profile.body_shape_us;
    eye_shape_us+=profile.eye_shape_us;front_ribbon_us+=profile.front_ribbon_us;
    if(profile.task_profiled){raster_task_profile.total+=profile.total_task_us;raster_task_profile.body+=profile.body_task_us;
        raster_task_profile.eyes+=profile.eyes_task_us;raster_task_profile.effects+=profile.effects_task_us;++raster_task_profile.frames;}
    const uint64_t done=esp_timer_get_time(),elapsed=done-started;
    render_us+=elapsed;++frames;max_frame_us=std::max(max_frame_us,elapsed);
    if(elapsed>display::ScreenFrameCadence::period_us)++slow_frames;
    const auto key=scene->ui->key^(uint64_t(frame.antialias)<<63U)^(uint64_t(frame.native_rgb565)<<62U);
    presentation={{0,0,frame.width,frame.height},visible,key,scene->ui->input_revision};
    report_window(scene->animation,done,scene->samples,scene->errors,scene->produced,scene->age);
    return true;
}
SceneStatus scene_status(){return {scene_published.load(),scene_pool_busy.load()};}
bool set_diagnostic_expression(const char *expression,uint32_t duration,std::string &error){
    if(duration<1000||duration>300000||!active.load()||!catalog::character_catalog_contains(catalog::kCharacterStates,expression)){
        error="pet_diagnostic_expression_invalid";return false;
    }
    std::lock_guard<std::mutex> lock(factory_mutex);
    for(const auto *state:catalog::kCharacterStates)if(std::strcmp(state,expression)==0){benchmark_expression=state;break;}
    benchmark_expression_until=uint64_t(esp_timer_get_time())+uint64_t(duration)*1000;
    ++foreground_inputs_revision;
    error.clear();return true;
}
void reset_diagnostic_expression(){std::lock_guard<std::mutex> lock(factory_mutex);benchmark_expression=nullptr;benchmark_expression_until=0;++foreground_inputs_revision;}
RenderTelemetry render_telemetry(){
    return {telemetry_fps_x100.load(std::memory_order_relaxed),
            telemetry_raster_us.load(std::memory_order_relaxed),
            telemetry_body_us.load(std::memory_order_relaxed),
            telemetry_eyes_us.load(std::memory_order_relaxed),
            telemetry_present_us.load(std::memory_order_relaxed),
            telemetry_max_frame_us.load(std::memory_order_relaxed),
            telemetry_slow_frames.load(std::memory_order_relaxed),
            telemetry_window_frames.load(std::memory_order_relaxed)};
}
bool configure(const Ports &p,std::string &error){
    const bool partial_imu=(p.initial_gravity==nullptr)!=(p.read_imu==nullptr);
    if(service_live||!worker_exited||p.target_fps==0||p.target_fps>60||p.animation_hz==0||
       p.animation_hz>200||!p.ready||partial_imu||
       (p.present_scene?(!p.scene_width||!p.scene_height):(!p.begin_frame||(!p.present&&!p.present_frame)||!p.clear))){error="invalid_pet_ports";return false;}
    ports=p;error.clear();return true;
}
static lunaros::ServiceNode make_animation_service(lunaros::KernelRuntime &runtime){
    static constexpr lunaros::ServiceDependency deps[]={{kDisplayServiceId,true}};
    static constexpr lunaros::ServiceDependency sensor_deps[]={{kDisplayServiceId,true},{"imu.motion",true}};
    lunaros::ServiceNode n;n.definition={kAnimationServiceId,"Pet animation",lunaros::kMainServiceExecutor,lunaros::ServiceCriticality::Required,
        ports.read_imu?sensor_deps:deps,ports.read_imu?2U:1U};
    n.start=start_engine;n.stop=stop_engine;n.stop_step=stop_engine_step;n.context=&runtime;return n;
}
static app::ForegroundDefinition make_application_definition(){
    static constexpr app::ApplicationServiceRequirement services[]={
        app::required_service(kDisplayServiceId),
        app::required_service(kAnimationServiceId)};
    static constexpr app::ApplicationServiceRequirement sensor_services[]={
        app::required_service(kDisplayServiceId),
        app::required_service(kAnimationServiceId),
        app::required_service("imu.motion")};
    static constexpr lunaros::ResourceRequirement display_resource[]={{"display.main",lunaros::ResourceAccess::Exclusive}};
    app::ForegroundDefinition a;a.id=kApplicationId;a.manifest.title="宠物";a.manifest.icon="pet";a.manifest.version="1.0.0";
    a.manifest.system_ui=app::ApplicationSystemUiPolicy::Immersive;a.manifest.launcher_accent_rgb=0xe6b35a;
    a.manifest.services=ports.read_imu?sensor_services:services;
    a.manifest.service_count=ports.read_imu?std::size(sensor_services):std::size(services);
    a.manifest.resources=display_resource;
    a.manifest.resource_count=std::size(display_resource);
    a.prepare=prepared;a.start=start_app;a.stop_checked=stop_app;a.tick=tick;a.declare_capabilities=register_application_capabilities;return a;
}
bool command(const char *text){std::lock_guard<std::mutex> lock(mutex);return active&&pet_render_command(text);}
const char *current_expression(){
    std::lock_guard<std::mutex> lock(mutex);
    return active ? pet_render_state_name(latest.frame.state) : nullptr;
}
#ifdef LUNAROS_FACTORY_SERVICE_COMPONENT
bool register_factory_check(lunaros::LocalFactoryMode &mode,
                            std::string &error) {
    static std::array<std::string, catalog::kCharacterStates.size()> ids;
    for (size_t i = 0; i < ids.size(); ++i) {
        const char *expression = catalog::kCharacterStates[i];
        ids[i] = std::string("pet.expression.") + expression;
        if (!mode.register_check(
                {ids[i].c_str(), kApplicationId, expression,
                 lunaros::FactoryCheckKind::Application, factory_available,
                 factory_start, factory_cancel, factory_confirm,
                 const_cast<char *>(expression), "宠物"}, error)) return false;
    }
    return true;
}
#endif
bool handle_touch(const TouchEvent &event,TouchResult &result){
    if(!active.load()){result={};return false;}
    std::unique_lock<std::mutex> animation_lock(mutex,std::defer_lock),raster_lock(raster_mutex,std::defer_lock);
    std::lock(animation_lock,raster_lock);
    if(event.input_revision && event.input_revision!=ui.input_revision()){
        held_action=UiAction::None;touch.reset();result={};return false;
    }
    result={};
    if(event.phase==TouchPhase::Cancel){held_action=UiAction::None;return touch.handle(event,result);}
    if(event.phase==TouchPhase::Down){
      held_action=ui_hit(displayed_controls,event.x,event.y);held_revision=painted_input_revision;held_x=event.x;held_y=event.y;
      if(held_action!=UiAction::None){touch.reset();result.captured=true;return true;}
    }
    if(held_action!=UiAction::None){
      result.captured=true;
      if(event.phase==TouchPhase::Up){
        const auto action=held_action;held_action=UiAction::None;
        if(held_revision==ui.input_revision() && action==ui_hit(displayed_controls,event.x,event.y) &&
           std::abs(int(event.x)-int(held_x))<=event.width/12 && std::abs(int(event.y)-int(held_y))<=event.height/12){
            raster_lock.unlock();animation_lock.unlock();(void)ui_action(action);
        }
      }
      return true;
    }
    if(displayed_character.width<=0 || event.x<displayed_character.x || event.y<displayed_character.y ||
       event.x>=displayed_character.x+displayed_character.width || event.y>=displayed_character.y+displayed_character.height){touch.reset();return true;}
    auto local=event;local.x-=displayed_character.x;local.y-=displayed_character.y;local.width=displayed_character.width;local.height=displayed_character.height;
    return touch.handle(local,result);
}
UiSnapshot ui_snapshot(){return ui.snapshot();}
uint64_t ui_input_revision(){return ui.input_revision();}
bool ui_action(UiAction action){
    if(!active.load())return false;
    if(action==UiAction::ToggleMode&&display_mode_policy()==DisplayModePolicy::FullscreenOnly)return false;
    const bool local=action==UiAction::SaveDefaultMode || action==UiAction::CycleShape || action==UiAction::CycleColor ||
      (action>=UiAction::ToggleFlexible&&action<=UiAction::ToggleAgent) || action==UiAction::AutonomousAppearance || action==UiAction::PairAccount || action==UiAction::ConfigureNetwork || action==UiAction::CloseConnection || action==UiAction::RetryConnection;
    bool okay;
    if(local){if(action==UiAction::PairAccount || action==UiAction::ConfigureNetwork || action==UiAction::RetryConnection || action==UiAction::CloseConnection)ui.action(action,ports.connections);
      std::string message;okay=apply_local_ui_action(action,message);ui.message(std::move(message));}
    else okay=ui.action(action,ports.connections);
    animation_visible.store(ui.animate());
    std::scoped_lock lock(mutex,raster_mutex);
    touch.reset();held_action=UiAction::None;damage.invalidate();pet_render_invalidate();return okay;
}
bool set_diagnostic_frame_rate(uint32_t target_fps,uint32_t duration_ms,
                               std::string &error){
    if(target_fps<20||target_fps>60||duration_ms<20000||duration_ms>900000){
        error="invalid_pet_diagnostic_frame_rate";return false;
    }
    std::lock_guard<std::mutex> lock(mutex);
    diagnostic_expires_us=esp_timer_get_time()+static_cast<int64_t>(duration_ms)*1000;
    diagnostic_target_fps=target_fps;
    ++foreground_inputs_revision;
    error.clear();return true;
}
void reset_diagnostic_frame_rate(){
    std::lock_guard<std::mutex> lock(mutex);
    diagnostic_target_fps=0;diagnostic_expires_us=0;
    ++foreground_inputs_revision;
}
FrameRateDiagnostic diagnostic_frame_rate(){
    std::lock_guard<std::mutex> lock(mutex);
    const int64_t remaining=diagnostic_expires_us-esp_timer_get_time();
    return diagnostic_target_fps!=0&&remaining>0
        ? FrameRateDiagnostic{diagnostic_target_fps,
                              static_cast<uint32_t>(remaining/1000)}
        : FrameRateDiagnostic{};
}
}

namespace lunaredu::pet {
lunaredu::lunaros::ServiceNode animation_service(
    lunaredu::lunaros::KernelRuntime &runtime) {
    return make_animation_service(runtime);
}

lunaredu::app::ForegroundDefinition application() {
    return make_application_definition();
}
} // namespace lunaredu::pet
