#include "kernel/service.h"
#include "sdkconfig.h"
#include "lunaros_network_service_id.h"
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
#include "lunaros_storage_service_id.h"
#endif
#include "lunaros_diagnostics.h"
#include "capture_control.h"
#include "espnow_diagnostics.h"
#include "official_tools.h"
#include "heap_tools.h"
#include "gprof_tools.h"
#include "diagnostic_outbox.h"
#include "insights_tools.h"
#include "coredump_tools.h"
#include "log_stream/lifecycle.h"
#include "session.h"
#include "kernel/service_executor.h"
#include "diagnostics/log.h"
#include "diagnostics/runtime_diagnostics.h"
namespace lunaredu::lunaros {
namespace {
ServiceStartResult start(void *) {
 if(!internal::start_espnow_diagnostics())return {ServiceStartDisposition::Failed,"espnow_diagnostics_provider_conflict"};
 std::string error_for_output;
 if(!internal::bind_log_capture_backend() || !diagnostics::start_sessions())return {ServiceStartDisposition::Failed,"diagnostics_backend_conflict"};
 if(!system::bind_cpu_monitor_reporter(system::poll_serial_diagnostics)){diagnostics::stop_sessions();return {ServiceStartDisposition::Failed,"diagnostics_reporter_conflict"};}
 if(!start_diagnostics_log_stream(error_for_output)) {
   system::unbind_cpu_monitor_reporter(system::poll_serial_diagnostics);
   diagnostics::stop_sessions();return {ServiceStartDisposition::Failed,error_for_output};
 }
 internal::official_tools::resume();
 internal::heap_tools::resume();
 internal::gprof_tools::resume();
 internal::diagnostic_outbox::resume();
 internal::insights_tools::resume();
 internal::coredump_tools::resume();
 internal::resume_debug_log_collector();return {ServiceStartDisposition::Ready,"diagnostics_ready"};
}
bool stop(void *,std::string &error) {
 // Revoke start authorization immediately, even when I/O or SDK retirement
 // needs retries. Stopping a sampler does not detach its borrowed buffers.
 diagnostics::stop_sessions();
 internal::stop_espnow_diagnostics();
 // The I/O job borrows the capture owners. Close admission and prove its
 // retirement before releasing SDK records, the encoder or crash state.
 if(!internal::diagnostic_outbox::shutdown(error))return false;
 if(!internal::official_tools::shutdown(error) || !internal::heap_tools::shutdown(error) || !internal::gprof_tools::shutdown(error))return false;
 internal::coredump_tools::shutdown();internal::insights_tools::shutdown();
 stop_diagnostics_log_stream();
 system::unbind_cpu_monitor_reporter(system::poll_serial_diagnostics);
 uninstall_debug_log_collector();error.clear();return true;
}
}
ServiceNode diagnostics_service_node(){
 static constexpr ServiceDependency dependencies[]={{kKernelReadyServiceId,true},{kNetworkRuntimeServiceId,true}
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
 ,{kStorageServiceId,true}
#endif
 };
#if CONFIG_LUNAROS_DIAGNOSTIC_FIELD
 constexpr size_t dependency_count=3;
#else
 const size_t dependency_count=diagnostics_log_stream_selected()?2u:1u;
#endif
 return {{kDiagnosticsServiceId,"Diagnostics",kMainServiceExecutor,ServiceCriticality::Optional,dependencies,dependency_count},start,stop,nullptr};
}
}
