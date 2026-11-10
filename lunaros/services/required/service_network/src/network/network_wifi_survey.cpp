#include "lunaros_network_wifi_analysis.h"
#include "network/network_scan_internal.h"
#include "network/network_service_internal.h"
#include "network/wifi_scan_job.h"
#include "platform/cold_storage.h"
namespace lunaredu::lunaros {
namespace {
LUNAROS_COLD_BSS NetworkWifiSurveySnapshot survey_view;
internal::WifiScanJob survey(survey_view);
NetworkJobRunResult run_survey(NetworkJobHandle handle, void *context) {
  return survey.run<true>(handle,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context)));
}
}
bool request_network_wifi_survey(KernelRuntime &runtime, std::string &error) {
  if (!require_network_runtime(runtime,error)) return false;
  uint32_t generation;
  return survey.start(run_survey,true,generation,error);
}
void network_wifi_survey_snapshot_into(NetworkWifiSurveySnapshot &output) { survey.snapshot_into(output); }
void reset_network_wifi_survey_internal() { survey.reset(); }
} // namespace lunaredu::lunaros
