#include "diagnostics/current_task_runtime.h"
#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
namespace lunaredu::system {
uint32_t settled_current_task_runtime_us() {
#if defined(ESP_PLATFORM) && configUSE_TRACE_FACILITY && configGENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
  taskYIELD();
  return current_task_runtime_us(true);
#else
  return 0;
#endif
}
uint32_t current_task_runtime_us(bool enabled) {
#if defined(ESP_PLATFORM) && configUSE_TRACE_FACILITY && configGENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
  if(enabled){
    // nullptr selects this currently running task; avoid another state query.
    TaskStatus_t task{};
    vTaskGetInfo(nullptr,&task,pdFALSE,eRunning);
    return static_cast<uint32_t>(task.ulRunTimeCounter);
  }
#else
  (void)enabled;
#endif
  return 0;
}
}

#if defined(ESP_PLATFORM) && defined(LUNAROS_DIAGNOSTIC_PROBES)
#include "diagnostics/function_probe.h"
#include "diagnostics/cpu_monitor.h"
#include <esp_timer.h>
#include <atomic>
extern "C" uint32_t lunar_diag_current_task_us();
namespace lunaredu::system::probe {
namespace {
struct Metric {std::atomic<uint32_t> calls{0},cpu{0},wall{0},maximum{0},attempts{0};};
Metric metrics[static_cast<unsigned>(Point::Count)];
static_assert(sizeof(metrics)<=2304,"diagnostic internal storage budget");
std::atomic<uint32_t> mask{0},divisor{1};
constexpr const char *names[]={"empty","main","health","foreground","runtime","receiver","assistant_restart","product_state",
  "desktop","model_copy","ui_capture","frame_capture","publish","projection","visual_environment",
  "scene","clear","clear_mount","clear_cache_before","clear_wait","clear_cache_after",
  "character","body","eyes","effects","polygon","ui_paint",
  "transfer","window","pack","submit","recycle","drain","pixel_poll","damage","render_job",
  "runtime_applications","runtime_lifecycle","runtime_diagnostics","runtime_maintenance_io","runtime_usb","runtime_health","runtime_ota","input_begin","input_safety","input_navigation","input_shell","input_pointer","input_application","input_session_hooks","input_snapshot","input_policy","input_controls","input_destination","input_command","input_widgets","input_widget_selection","input_shell_observe","input_model_observation","input_synchronize","input_persistence","input_observers","input_redraw","input_ota_snapshot","pet_preferences","pet_pairing","pet_connections","pet_decoration","pet_admission","pet_overrides","pet_projection","pet_visual_snapshot","pet_platform_environment","pet_policy","pet_scene_acceptance","pet_scene_snapshot","pet_publication","pet_tick","ui_execute","ui_chrome","calibration_loop",
  "afe_process","afe_feed","afe_fetch","afe_output_copy","io_loop","io_pump","io_job","io_applications","io_delay","io_retire","io_wait","cpu_projection","cpu_snapshot","cpu_heap","http_reply"};
static_assert(sizeof(names)/sizeof(*names)==static_cast<unsigned>(Point::Count));
}
uint32_t task_clock(){return lunar_diag_current_task_us();}
uint32_t wall_clock(){return uint32_t(esp_timer_get_time());}
uint32_t sampling_divisor(){return divisor.load(std::memory_order_relaxed);}
uint32_t selected(){return cpu_monitor_active()?mask.load(std::memory_order_relaxed):0;}
bool active(Point p){
  const auto value=mask.load(std::memory_order_relaxed);if(!value)return false;
  uint32_t family=0;
  if(p==Point::Empty)family=511;
  else if(p==Point::Main)family=1|8|16|32;
  else if(p<=Point::VisualEnvironment)family=1;
  else if(p<=Point::UiPaint||p==Point::Damage||p==Point::RenderJob||p==Point::UiExecute||p==Point::UiChrome)family=2;
  else if(p<=Point::PixelPoll)family=4;
  else if(p>=Point::RuntimeApplications&&p<=Point::RuntimeOta)family=8;
  else if(p>=Point::InputBegin&&p<=Point::InputOtaSnapshot)family=16;
  else if(p>=Point::PetPreferences&&p<=Point::PetTick)family=32;
  else if(p>=Point::AfeProcess&&p<=Point::AfeOutputCopy)family=64;
  else if(p>=Point::IoLoop&&p<=Point::IoWait)family=128;
  else if(p>=Point::CpuProjection&&p<=Point::HttpReply)family=256;
  if(!(value&family)||!cpu_monitor_probe_active())return false;
  auto &m=metrics[unsigned(p)];const auto number=m.attempts.fetch_add(1,std::memory_order_relaxed)+1;
  const auto d=divisor.load(std::memory_order_relaxed);if(d==1)return true;
  // Mix the already-counted invocation index. No second shared atomic state,
  // and no every-sixteenth-call alias with periodic animation/audio work.
  uint32_t next=number^(0x261018U^((unsigned(p)+1)*0x9e3779b9U));
  next^=next>>16;next*=0x7feb352dU;next^=next>>15;next*=0x846ca68bU;next^=next>>16;
  return (next&(d-1))==0;
}
void select(uint32_t value,uint32_t requested_divisor){
  // Cumulative modulo-32-bit observations avoid racing resets with live scopes.
  // The host subtracts two snapshots over a bounded observation window.
  divisor.store(requested_divisor==16?16:1,std::memory_order_relaxed);
  mask.store(value&511,std::memory_order_relaxed);
  if(value){const auto wall=wall_clock(),cpu=task_clock();
    for(unsigned i=0;i<1000;++i){Scope empty(Point::Empty);}
    const auto end_cpu=task_clock(),end_wall=wall_clock();record(Point::CalibrationLoop,end_cpu-cpu,end_wall-wall);}
}
void record(Point p,uint32_t cpu,uint32_t wall){auto &m=metrics[unsigned(p)];
  if(p==Point::CalibrationLoop)m.attempts.fetch_add(1,std::memory_order_relaxed);
  m.cpu.fetch_add(cpu,std::memory_order_relaxed);m.wall.fetch_add(wall,std::memory_order_relaxed);
  m.calls.fetch_add(1,std::memory_order_relaxed);
  auto old=m.maximum.load(std::memory_order_relaxed);
  while(old<wall&&!m.maximum.compare_exchange_weak(old,wall,std::memory_order_relaxed)){}
}
Reading read(Point p){auto &m=metrics[unsigned(p)];return {names[unsigned(p)],m.calls.load(std::memory_order_relaxed),m.cpu.load(std::memory_order_relaxed),m.wall.load(std::memory_order_relaxed),m.maximum.load(std::memory_order_relaxed),m.attempts.load(std::memory_order_relaxed)};}
}
#endif
