#include "diagnostics/function_probe.h"
#include "device_api/core_projections.h"
#include "device_api/device_operation_builders.h"
#include "kernel/runtime.h"
#include "model/device_model.h"
#include "device_api/device_api_context.h"
#include <ArduinoJson.h>

#include "device_api/device_projection.h"
#include <esp_log.h>
#include "kernel/service_executor.h"
#include "kernel/managed_task.h"
#include "kernel/thread_observation.h"
#include "diagnostics/runtime_diagnostics.h"
#include "diagnostics/memory_pressure.h"
#include "diagnostics/product_loop_timing.h"
#include <memory>
#include <cstring>
#if defined(LUNAROS_DIAGNOSTIC_PROBES)
extern "C" void lunar_diag_idle_snapshot(uint32_t *,uint32_t *,uint32_t *);
#endif

namespace lunaredu::lunaros {
namespace {

constexpr const char *kModuleId = "runtime";
void fill_read_cost(JsonObject value,const system::DiagnosticReadCost &cost) {
  value["samples"]=cost.samples;value["wait_max_us"]=cost.wait_max_us;value["held_max_us"]=cost.held_max_us;
  value["wait_total_us"]=cost.wait_total_us;value["held_total_us"]=cost.held_total_us;
  value["scope"]="query_lock_elapsed_not_cpu";
}

void fill_task_status([[maybe_unused]] DeviceApiContext &device, JsonObject payload) {
  const ManagedTaskRegistrySnapshot snapshot = device.runtime.task_snapshot();
  fill_read_cost(payload["read_cost"].to<JsonObject>(),snapshot.read_cost);
  payload["revision"] = snapshot.revision;
  payload["active_tasks"] = snapshot.active_tasks;
  payload["complete"] = snapshot.complete;
  payload["captured_tasks"] = snapshot.tasks.size();
  payload["resident_tasks"] = snapshot.resident_tasks;
  payload["feature_tasks"] = snapshot.feature_tasks;
  payload["transient_tasks"] = snapshot.transient_tasks;
  payload["internal_stack_bytes"] = snapshot.internal_stack_bytes;
  payload["external_stack_bytes"] = snapshot.external_stack_bytes;
  payload["count_scope"] = "managed_tasks_only";
  payload["ownership_enforced"] = device.runtime.task_ownership_enforced();
  JsonObject storage = payload["storage"].to<JsonObject>();
  storage["memory"] = snapshot.storage.memory;
  storage["capacity"] = snapshot.storage.capacity;
  storage["storage_bytes"] = snapshot.storage.storage_bytes;

  payload["allocation_policy"] = "physical_memory_and_execution_admission";

  JsonArray tasks = payload["tasks"].to<JsonArray>();
  for (const ManagedTaskEntrySnapshot &task : snapshot.tasks) {
    if (!task.active) {
      continue;
    }
    JsonObject item = tasks.add<JsonObject>();
    item["id"] = task.id.data();
    item["owner_id"] = task.owner_service_id.data();
    item["owner_kind"] = managed_task_owner_kind_name(task.owner_kind);
    if (task.owner_kind == ManagedTaskOwnerKind::Service)
      item["owner_service"] = task.owner_service_id.data();
    ManagedTaskDefinition definition;
    definition.owner_service_id = task.owner_service_id.data(); definition.owner_kind = task.owner_kind;
    std::string owner_error;
    item["owner_registered"] = device.runtime.validate_task_owner(definition, owner_error);
    item["role"] = task.role.data();
    item["stack_bytes"] = task.stack_bytes;
    item["priority"] = task.priority;
    item["core_affinity"] = task.core_affinity;
    item["lifetime"] = managed_task_lifetime_name(task.lifetime);
    item["memory"] = managed_task_memory_name(task.memory);
    item["internal_fallback"] = task.allow_internal_fallback;
    item["started"] = task.native_handle != 0;
    item["minimum_free_stack_bytes"] = task.minimum_free_stack_bytes;
  }

  ServiceExecutorSnapshot executor_snapshots[4] = {};
  const size_t executor_count = device.runtime.executor_snapshot(
      executor_snapshots,
      sizeof(executor_snapshots) / sizeof(executor_snapshots[0]));
  JsonArray executors = payload["executors"].to<JsonArray>();
  const size_t visible_executor_count =
      executor_count < 4 ? executor_count : 4;
  payload["executor_count"] = executor_count;
  payload["captured_executors"] = visible_executor_count;
  payload["executors_complete"] = executor_count == visible_executor_count;
  for (size_t index = 0; index < visible_executor_count; ++index) {
    const ServiceExecutorSnapshot &executor =
        executor_snapshots[index];
    JsonObject item = executors.add<JsonObject>();
    item["id"] = executor.id;
    item["owner_service"] = executor.owner_service_id;
    item["queue_depth"] = executor.queue_depth;
    item["bound"] = executor.bound;
  }
}

void fill_thread_status(DeviceApiContext &device, JsonObject payload) {
  // Diagnostic snapshots must not abort or steal internal DMA/stack SRAM
  // when a PSRAM device is under pressure. This storage is fallible and uses
  // one explicit memory class; no-PSRAM devices retain the internal path.
  RegistrationArray<NativeThreadRegistrySnapshot> storage(true);
  if(!storage.resize(1)){
    payload["available"]=false;payload["complete"]=false;
    payload["error"]="thread_snapshot_allocation_failed";return;
  }
  auto *native=storage.data();
  const auto managed = device.runtime.task_metadata();
  capture_native_threads(*native);
  const auto managed_revision = device.runtime.task_summary().revision;
  const bool ownership_consistent = managed.complete && managed.revision == managed_revision;
  payload["available"] = native->available; payload["complete"] = native->complete;
  payload["observed_threads"] = native->observed_threads;
  payload["captured_threads"] = native->captured_threads;
  payload["capacity"] = kNativeThreadSnapshotCapacity;
  payload["count_scope"] = "freertos_point_in_time";
  const auto cpu=system::read_runtime_diagnostics_snapshot();
  payload["cpu_usage_available"] = cpu.cpu_usage_available;
  payload["cpu_sample_interval_ms"] = cpu.cpu_sample_interval_ms;
  payload["cpu_sampled_at_us"] = cpu.cpu_sampled_at_us;
  payload["cpu_percent_basis"] = "one_core_100_percent";
  payload["core_affinity_available"] = native->core_affinity_available;
  if (!native->error.empty()) payload["error"] = native->error;
  payload["managed_metadata_revision"] = managed.revision;
  payload["managed_metadata_complete"] = managed.complete;
  payload["managed_metadata_consistent"] = ownership_consistent;
  JsonArray threads = payload["threads"].to<JsonArray>();
  for (size_t i = 0; i < native->captured_threads; ++i) {
    const auto &thread = native->threads[i];
    JsonObject item = threads.add<JsonObject>();
    item["name"] = thread.name.data(); item["number"] = thread.number;
    item["state"] = thread.state; item["priority"] = thread.priority;
    if (native->core_affinity_available) item["core_affinity"] = thread.core_affinity;
    else item["core_affinity"] = nullptr;
    item["minimum_free_stack_bytes"] = thread.minimum_free_stack_bytes;
    if(thread.stack_span_bytes)item["stack_span_bytes"]=thread.stack_span_bytes;
    else item["stack_span_bytes"]=nullptr;
    item["stack_bytes"]=nullptr;
    item["stack_memory"]=thread.stack_memory;
    if(thread.stack_start)item["stack_start"]=thread.stack_start;
    else item["stack_start"]=nullptr;
    uint16_t usage=0;
    if(cpu.cpu_usage_available&&system::cpu_thread_usage(thread.number,usage))item["cpu_percent"]=usage/10.0;
    else item["cpu_percent"]=nullptr;
    if (ownership_consistent) item["managed"] = false;
    else item["managed"] = nullptr;
    if (const auto *owned = managed.find(thread.handle, managed_revision)) {
      const auto &task = *owned;
      item["managed"] = true; item["id"] = task.id.data();
      item["stack_bytes"] = task.stack_bytes;
      item["owner_id"] = task.owner_service_id.data();
      item["owner_kind"] = managed_task_owner_kind_name(task.owner_kind);
      item["role"] = task.role.data(); item["lifetime"] = managed_task_lifetime_name(task.lifetime);
    }
  }
}

void fill_cpu_status(DeviceApiContext &,JsonObject payload) {
  system::probe::Scope diagnostic_projection(system::probe::Point::CpuProjection);
#if defined(LUNAROS_DIAGNOSTIC_PROBES)
  uint32_t idle0,idle1,at;lunar_diag_idle_snapshot(&idle0,&idle1,&at);
  payload["idle_counter_at_us_u32"]=at;
  auto idle=payload["idle_task_runtime_us_u32"].to<JsonArray>();idle.add(idle0);idle.add(idle1);
  payload["idle_counter_basis"]="scheduler_idle_tasks_including_live_slice_and_charged_isr; modulo_u32; window_less_than_71_minutes";
  payload["function_profile"]=system::probe::selected();
  payload["function_sampling"]=system::probe::selected()?system::probe::sampling_divisor():0;
  payload["function_estimator"]="sample_mean_cpu_us_times_total_calls; sampled_max_is_not_worst_case";
  payload["function_basis"]="inclusive_scheduled_task_us_including_charged_isr_modulo_u32";
  payload["function_snapshot_atomic"]=false;
  auto probes=payload["function_probes"].to<JsonArray>();
  for(unsigned i=0;i<unsigned(system::probe::Point::Count);++i){
    const auto r=system::probe::read(system::probe::Point(i));
    auto item=probes.add<JsonObject>();item["name"]=r.name;item["calls"]=r.calls;item["total_calls"]=r.total_calls;
    item["cpu_us"]=r.cpu_us;item["wall_us"]=r.wall_us;item["max_wall_us"]=r.max_wall_us;
  }
#endif
  const auto cpu=system::read_runtime_diagnostics_snapshot();
  payload["supported"]=system::cpu_monitor_supported();
  payload["main_work_profile"]=system::main_work_profiling_active();
  payload["worker_work_profile"]=system::worker_work_profiling_active();
  payload["main_stage_attribution"]="elapsed_only";
  fill_read_cost(payload["read_cost"].to<JsonObject>(),cpu.read_cost);
  payload["enabled"]=cpu.cpu_monitor_active;
  payload["start_on_boot"]=cpu.cpu_monitor_start_on_boot;
  payload["remaining_ms"]=cpu.cpu_monitor_remaining_ms;
  payload["sampling_interval_ms"]=1000;
  payload["available"]=cpu.cpu_usage_available;
  payload["sample_interval_ms"]=cpu.cpu_sample_interval_ms;
  payload["sampled_at_us"]=cpu.cpu_sampled_at_us;
  payload["capture_us"]=cpu.cpu_capture_us;
  payload["capture_kind"]=cpu.cpu_capture_fallback?"full_thread_fallback":"runtime_counters";
  payload["core_count"]=cpu.cpu_core_count;
  payload["runtime_accounting_enabled"]=cpu.task_runtime_stats_enabled;
  payload["thread_count"]=cpu.freertos_task_count;
  payload["sampled_thread_count"]=cpu.sampled_task_count;
  payload["task_percent_basis"]="one_core_100_percent";
  payload["new_task_usage"]="unavailable_until_next_interval";
  const auto timing=system::read_product_loop_timing();
  auto loop=payload["product_loop"].to<JsonObject>();loop["available"]=cpu.cpu_monitor_active&&timing.available;
  loop["basis"]="elapsed_including_preemption";
  if(cpu.cpu_monitor_active&&timing.available){
    loop["samples"]=timing.samples;loop["period_us"]=timing.period_us;loop["over_budget"]=timing.over_budget;
    loop["maximum_us"]=timing.maximum_us;
    auto phases=loop["average_us"].to<JsonObject>();
    const char *names[]={"health","maintenance","radio","product"};
    for(unsigned i=0;i<4;++i)phases[names[i]]=timing.average_us[i];
  }
  payload["internal_minimum_free_bytes"]=cpu.internal_minimum_free_bytes;
  payload["internal_largest_free_bytes"]=cpu.internal_largest_free_bytes;
  payload["psram_largest_free_bytes"]=cpu.psram_largest_free_bytes;
  if(!cpu.cpu_monitor_error.empty())payload["error"]=cpu.cpu_monitor_error;
  JsonArray cores=payload["core_busy_percent"].to<JsonArray>();
  for(uint32_t core=0;core<cpu.cpu_core_count;++core) {
    if(cpu.cpu_usage_available)cores.add((core?cpu.core1_busy_percent_tenths:cpu.core0_busy_percent_tenths)/10.0);
    else cores.add(nullptr);
  }
  if(cpu.cpu_usage_available&&cpu.cpu_core_count)
    payload["total_busy_percent"]=(cpu.core0_busy_percent_tenths+(cpu.cpu_core_count>1?cpu.core1_busy_percent_tenths:0))/(10.0*cpu.cpu_core_count);
  else payload["total_busy_percent"]=nullptr;
  JsonArray top=payload["top_tasks"].to<JsonArray>();
  for(size_t i=0;i<cpu.top_task_count;++i) {
    const auto &task=cpu.top_tasks[i];auto item=top.add<JsonObject>();
    item["name"]=task.name.data();item["number"]=task.task_number;
    item["cpu_percent"]=task.core_percent_tenths/10.0;
    item["priority"]=task.priority;item["minimum_free_stack_bytes"]=nullptr;
    if(task.core_id>=0)item["core_affinity"]=task.core_id;else item["core_affinity"]=nullptr;
  }
}
DeviceOperationResult set_cpu_status(DeviceApiContext &device,JsonVariantConst request,JsonObject payload) {
  if(!request["enabled"].is<bool>()||(!request["start_on_boot"].isNull()&&!request["start_on_boot"].is<bool>()))
    return device_operation_error("cpu_monitor_invalid_switch",400);
  for(JsonPairConst item:request.as<JsonObjectConst>()) {
#if defined(LUNAROS_DIAGNOSTIC_PROBES)
    if(!std::strcmp(item.key().c_str(),"function_sampling")){
      if(!item.value().is<unsigned>()||(item.value().as<unsigned>()!=1&&item.value().as<unsigned>()!=16))return device_operation_error("cpu_monitor_invalid_switch",400);
      continue;
    }
    if(!std::strcmp(item.key().c_str(),"function_profile")){
      if(!item.value().is<unsigned>()||item.value().as<unsigned>()>511)return device_operation_error("cpu_monitor_invalid_switch",400);
      continue;
    }
#endif
    if(!std::strcmp(item.key().c_str(),"start_on_boot")&&!item.value().is<bool>())return device_operation_error("cpu_monitor_invalid_switch",400);
    if((!std::strcmp(item.key().c_str(),"main_work_profile")||!std::strcmp(item.key().c_str(),"worker_work_profile"))&&!item.value().is<bool>())return device_operation_error("cpu_monitor_invalid_switch",400);
    if(std::strcmp(item.key().c_str(),"enabled")&&std::strcmp(item.key().c_str(),"start_on_boot")&&std::strcmp(item.key().c_str(),"main_work_profile")&&std::strcmp(item.key().c_str(),"worker_work_profile"))return device_operation_error("cpu_monitor_invalid_switch",400);
  }
  std::optional<bool> boot;
  if(!request["start_on_boot"].isNull())boot=request["start_on_boot"].as<bool>();
  std::string error;
  if(!system::configure_cpu_monitor(request["enabled"].as<bool>(),boot,error))
    return device_operation_error(error.c_str(),error=="cpu_monitor_unsupported"?503:500);
  system::set_main_work_profiling(request["enabled"].as<bool>()&&request["main_work_profile"].as<bool>());
  system::set_worker_work_profiling(request["enabled"].as<bool>()&&request["worker_work_profile"].as<bool>());
#if defined(LUNAROS_DIAGNOSTIC_PROBES)
  system::probe::select(request["enabled"].as<bool>()?request["function_profile"].as<unsigned>():0,request["function_sampling"].isNull()?16:request["function_sampling"].as<unsigned>());
#endif
  fill_cpu_status(device,payload);return device_operation_ack();
}

void fill_contract(JsonObject contract) {
  contract["registry_version"] = 2;
  contract["admission_control"] = true;
  contract["registration_storage"] = "dynamic";
  contract["stack_memory_accounting"] = true;
  contract["typed_owners"] = true;
  contract["native_threads_operation"] = "runtime.threads.get";
  contract["cpu_monitor_operation"] = "runtime.cpu.get";
  contract["cpu_monitor_switch_operation"] = "runtime.cpu.set";
  contract["cpu_monitor_supported"] = system::cpu_monitor_supported();
}

const DeviceCapabilityDescriptor kTaskRuntimeCapability = []() {
  DeviceCapabilityDescriptor descriptor;
  descriptor.id = "runtime_tasks";
  return descriptor;
}();

void fill_runtime_memory_history(JsonObject history) {
  std::array<system::RuntimeMemorySample,8> samples{};
  const auto view=system::read_runtime_memory_history(samples);
  history["available"]=view.available;
  if(!view.available){history["error"]="memory_history_busy";return;}
  history["interval_ms"]=system::kRuntimeMemoryIntervalMs;
  history["capacity"]=system::kRuntimeMemoryCapacity;
  history["retained"]=view.retained;history["returned"]=view.returned;
  history["latest_sequence"]=view.latest_sequence;history["oldest_sequence"]=view.oldest_sequence;
  history["maximum_sample_gap_ms"]=view.maximum_sample_gap_ms;
  history["maximum_heap_capture_us"]=view.maximum_capture_us;
  history["skipped_records"]=view.skipped_records;
  history["sampling_started"]=view.latest_sequence!=0;
  history["scope"]="sampled_on_existing_product_owner_not_peak_or_atomic";
  history["minimum_scope"]="per_activity_observed_free_and_largest_since_boot";
  history["pool_minimum_scope"]="sdk_per_heap_minima_since_boot";
  history["capture_cost_scope"]="heap_queries_wall_us_excludes_activity_observation";
  history["activity_bits"]["audio"]=system::kMemoryActivityAudio;
  history["activity_bits"]["display"]=system::kMemoryActivityDisplay;
  history["activity_bits"]["motion"]=system::kMemoryActivityMotion;
  history["activity_scope"]="audio_demand_display_frame_residency_motion_lease_or_retirement";
  const auto fill=[](JsonObject row,const system::MemoryPoolFreeSnapshot &pool){
    row["free_bytes"]=pool.free_bytes;row["largest_free_bytes"]=pool.largest_free_bytes;
    row["minimum_free_bytes"]=pool.minimum_free_bytes;
  };
  auto records=history["samples"].to<JsonArray>();
  for(uint32_t i=0;i<view.returned;++i){const auto &sample=samples[i];auto row=records.add<JsonObject>();
    row["sequence"]=sample.sequence;row["uptime_ms"]=sample.uptime_ms;row["activity_bits"]=sample.activity_bits;
    row["allocation_failures"]=sample.allocation_failures;row["heap_capture_us"]=sample.capture_us;
    fill(row["internal"].to<JsonObject>(),sample.internal);fill(row["internal_dma"].to<JsonObject>(),sample.dma);
    fill(row["psram"].to<JsonObject>(),sample.psram);
  }
  auto minima=history["activity_minima"].to<JsonArray>();
  for(uint32_t bits=0;bits<view.activity_minima.size();++bits){const auto &m=view.activity_minima[bits];
    if(!m.samples)continue;
    auto row=minima.add<JsonObject>();row["activity_bits"]=bits;row["samples"]=m.samples;
    row["first_ms"]=m.first_ms;row["last_ms"]=m.last_ms;
    row["internal_free_bytes"]=m.internal_free;row["internal_largest_free_bytes"]=m.internal_largest;
    row["dma_free_bytes"]=m.dma_free;row["dma_largest_free_bytes"]=m.dma_largest;
    row["psram_free_bytes"]=m.psram_free;row["psram_largest_free_bytes"]=m.psram_largest;
  }
}
void fill_memory_status(DeviceApiContext &,JsonObject payload) {
  const auto memory=system::read_memory_pressure_snapshot();
  const auto fill=[&](const char *name,const system::MemoryPoolSnapshot &value) {
    auto pool=payload[name].to<JsonObject>();
    pool["total_bytes"]=value.total_bytes;pool["free_bytes"]=value.free_bytes;
    pool["minimum_free_bytes"]=value.minimum_free_bytes;pool["largest_free_bytes"]=value.largest_free_bytes;
  };
  fill("internal",memory.internal);fill("internal_dma",memory.dma);fill("psram",memory.psram);
  payload["pools_overlap"]=true;
  payload["minimum_scope"]="sum_of_per_heap_minima_since_boot";
  payload["total_bytes_scope"]="registered_heap_spans_may_overlap";
  const auto layout=system::read_internal_memory_layout();
  auto accounting=payload["internal_accounting"].to<JsonObject>();
  const auto &a=layout.accounting;
  accounting["physical_span_bytes"]=a.physical_span_bytes;
  accounting["registered_span_bytes"]=a.registered_span_bytes;
  accounting["backing_container_bytes"]=a.backing_container_bytes;
  accounting["walk_free_bytes"]=a.walk_free_bytes;
  accounting["spans_complete"]=a.spans_complete;
  accounting["payload_complete"]=a.payload_complete;
  accounting["atomic_across_regions"]=false;
  accounting["scope"]="internal_8bit_heap_address_union";
  if(a.payload_complete) {
    accounting["live_payload_bytes"]=a.live_payload_bytes;
    accounting["allocator_overhead_bytes"]=a.allocator_overhead_bytes;
  }
  auto failure=payload["allocation_failures"].to<JsonObject>();
  failure["capture_available"]=memory.failure_capture_available;
  failure["count_scope"]="captured_events";
  failure["count"]=memory.failure.count;failure["sample_available"]=memory.failure.sample_available;
  if(memory.failure.sample_available) {
    failure["requested_bytes"]=memory.failure.requested_bytes;
    failure["capabilities"]=memory.failure.capabilities;
    failure["uptime_ms"]=memory.failure.uptime_ms;
    failure["allocator"]=memory.failure.allocator.data();
  }
  auto checkpoints=payload["boot_checkpoints"].to<JsonArray>();
  payload["checkpoint_scope"]="first_bootstrap_since_boot";
  for(uint32_t i=0;i<system::kBootMemoryCheckpointCapacity;++i) {
    system::MemoryCheckpoint value;
    if(!system::read_boot_memory_checkpoint(i,value))break;
    auto entry=checkpoints.add<JsonObject>();
    entry["stage"]=value.name.data();entry["uptime_ms"]=value.uptime_ms;
    entry["internal_free_bytes"]=value.internal_free_bytes;entry["dma_free_bytes"]=value.dma_free_bytes;
    entry["internal_largest_free_bytes"]=value.internal_largest_free_bytes;
    entry["psram_free_bytes"]=value.psram_free_bytes;
  }
  fill_runtime_memory_history(payload["runtime_history"].to<JsonObject>());
}

constexpr DeviceOperationDefinition kTaskOperations[] = {
    device_operation_on_caller(make_device_json_provider("memory.layout.get",[](DeviceApiContext &,JsonObject payload){
      const auto layout=system::read_internal_memory_layout();
      payload["complete"]=layout.complete;payload["scope"]="internal_8bit_regions";
      payload["sizes_scope"]="sdk_block_walk";payload["atomic_across_regions"]=false;
      payload["allocations_complete"]=layout.allocations_complete;
      payload["allocation_minimum_bytes"]=system::InternalMemoryLayout::allocation_minimum_bytes;
      payload["physical_span_bytes"]=layout.accounting.physical_span_bytes;
      payload["registered_span_bytes"]=layout.accounting.registered_span_bytes;
      payload["backing_container_bytes"]=layout.accounting.backing_container_bytes;
      auto allocations=payload["large_allocations"].to<JsonArray>();
      for(uint32_t i=0;i<layout.allocation_count;++i){const auto &a=layout.large_allocations[i];auto row=allocations.add<JsonObject>();
        row["heap_start"]=a.heap_start;row["start"]=a.start;row["bytes"]=a.bytes;}
      auto regions=payload["regions"].to<JsonArray>();
      for(uint32_t i=0;i<layout.count;++i){const auto &r=layout.regions[i];auto row=regions.add<JsonObject>();
        row["start"]=r.start;row["end"]=r.end;row["free_bytes"]=r.free_bytes;
        row["used_bytes"]=r.used_bytes;row["largest_free_bytes"]=r.largest_free_bytes;
        row["free_blocks"]=r.free_blocks;row["used_blocks"]=r.used_blocks;}
    })),
    device_operation_on_caller(make_device_json_provider("memory.get",fill_memory_status)),
    make_device_json_provider("tasks.get", fill_task_status),
    make_device_json_provider("threads.get", fill_thread_status),
    device_operation_on_caller(make_device_json_provider("cpu.get",fill_cpu_status)),
    device_operation_on_caller(device_operation_schemas(make_device_json_command("cpu.set",set_cpu_status),
      #if defined(LUNAROS_DIAGNOSTIC_PROBES)
      R"({"type":"object","required":["enabled"],"properties":{"function_sampling":{"type":"integer","enum":[1,16]},"function_profile":{"type":"integer","minimum":0,"maximum":511},"enabled":{"type":"boolean"},"start_on_boot":{"type":"boolean"},"main_work_profile":{"type":"boolean"},"worker_work_profile":{"type":"boolean"}},"additionalProperties":false})",nullptr)),
#else
      R"({"type":"object","required":["enabled"],"properties":{"enabled":{"type":"boolean"},"start_on_boot":{"type":"boolean"},"main_work_profile":{"type":"boolean"},"worker_work_profile":{"type":"boolean"}},"additionalProperties":false})",nullptr)),
#endif
};

const DeviceModuleProjectionMetadata kTaskModule = {
    kModuleId,
    "runtime",
    fill_contract,
};

const DeviceModuleProjection kTaskProjection = {
    "runtime_tasks",
    &kTaskModule,
    kTaskOperations,
    sizeof(kTaskOperations) / sizeof(kTaskOperations[0]),
};

}  // namespace

static bool install_tasks_projection(DeviceProjectionRegistry &registry, std::string &error) {
  return registry.register_projection(
      {&kTaskRuntimeCapability, &kTaskProjection, nullptr, true}, &error);
}

const StandardProjectionContribution standard_projection::tasks{"runtime", install_tasks_projection};

}  // namespace lunaredu::lunaros

