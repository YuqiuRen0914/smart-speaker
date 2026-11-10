#pragma once
#include "lunaros_network_wifi_analysis.h"
#include "lunaros_network_scan.h"
#include "lunaros_network_jobs.h"
#include "kernel/static_mutex.h"
#include "platform/system.h"
#include "network/network_service_internal.h"
#include "network/wifi/wifi_manager.h"
#include <algorithm>
#include <cstdio>

namespace lunaredu::lunaros::internal {
// Shared bounded lifecycle for ordinary SSID discovery and per-AP survey.
// SDK I/O remains on the existing job runner, outside this view lock.
class WifiScanJob {
public:
  explicit WifiScanJob(NetworkWifiSurveySnapshot &view) : view_(view) {}
  bool start(NetworkJobRunner runner, bool survey, uint32_t &generation, std::string &error) {
    error.clear();
    if (!admit_network_work(error)) return false;
    const auto network = network_fast_snapshot();
    if (!network.station_enabled) { error="network_station_disabled"; return false; }
    if (!network.service_started) { error="network_service_not_started"; return false; }
    {
      StaticLockGuard lock(mutex_); reconcile_locked();
      if (view_.scan.state == NetworkWifiScanState::Scanning) { generation=generation_; return true; }
      if (++generation_ == 0) ++generation_;
      generation=generation_;
      const auto sequence=view_.scan.sequence+1;
      view_={}; view_.scan.sequence=sequence; view_.scan.state=NetworkWifiScanState::Scanning;
      handle_={}; submitting_=true;
    }
    uint32_t deadline=system_monotonic_ms()+15000;
    if (!deadline) deadline=1;
    NetworkJobDefinition definition;
    definition.owner_service_id=survey?"network.wifi.survey":"network.wifi.scan";
    definition.kind=survey?"wifi.survey":"wifi.scan";
    definition.priority=kNetworkPriorityInteractive; definition.maximum_attempts=60;
    definition.deadline_ms=deadline; definition.runner=runner;
    definition.context=reinterpret_cast<void *>(static_cast<uintptr_t>(generation));
    definition.retry_backoff_initial_ms=definition.retry_backoff_maximum_ms=250;
    NetworkJobHandle handle;
    const bool submitted=submit_network_job(definition,handle,error);
    {
      StaticLockGuard lock(mutex_);
      if (generation==generation_) {
        submitting_=false; handle_=handle;
        if (!submitted) fail_locked(error.empty()?"wifi_scan_queue_failed":error.c_str());
        return submitted;
      }
    }
    if (submitted) { std::string ignored; (void)cancel_network_job(handle,ignored); }
    error="wifi_scan_superseded"; return false;
  }
  template<bool Survey>
  NetworkJobRunResult run(NetworkJobHandle handle, uint32_t generation) {
    {
      StaticLockGuard lock(mutex_);
      if (generation!=generation_) return {false,false,"wifi_scan_superseded"};
    }
    std::vector<WifiScanApRecord> records;
    uint16_t total=0;
    std::string error;
    bool ok=!network_job_cancellation_requested(handle);
    if (ok) {
      if constexpr (Survey) ok=wifi_survey_access_points(records,total,error);
      else ok=wifi_scan_access_points(records,error);
    }
    if (network_job_cancellation_requested(handle)) { ok=false; error="wifi_scan_cancelled"; }
    StaticLockGuard lock(mutex_);
    if (generation!=generation_) return {false,false,"wifi_scan_superseded"};
    if (!ok && error=="wifi_radio_busy") return {false,true,error};
    if (!ok) { fail_locked(error.empty()?"wifi_scan_failed":error.c_str()); return {false,false,view_.scan.error.data()}; }
    view_.scan.access_points={};
    view_.scan.access_point_count=std::min(records.size(),view_.scan.access_points.size());
    for (size_t i=0;i<view_.scan.access_point_count;++i) {
      auto &ap=view_.scan.access_points[i]; const auto &record=records[i];
      std::snprintf(ap.ssid.data(),ap.ssid.size(),"%s",record.ssid.c_str());
      ap.bssid=record.bssid; ap.rssi=record.rssi; ap.channel=record.channel;
      ap.authmode=record.authmode; ap.secure=record.secure;
    }
    view_.total_count=Survey?total:static_cast<uint16_t>(records.size());
    view_.scan.state=NetworkWifiScanState::Ready; view_.scan.error[0]=0; ++view_.scan.sequence;
    return {true,false,Survey?"wifi_survey_ready":"wifi_scan_ready"};
  }
  void snapshot_into(NetworkWifiSurveySnapshot &output) {
    StaticLockGuard lock(mutex_); reconcile_locked(); output=view_;
  }
  void snapshot_into(NetworkWifiScanSnapshot &output) {
    StaticLockGuard lock(mutex_); reconcile_locked(); output=view_.scan;
  }
  bool observe(uint32_t generation, NetworkWifiScanSnapshot &output, std::string &error) {
    StaticLockGuard lock(mutex_);
    if (generation!=generation_) { error="wifi_scan_superseded"; return false; }
    reconcile_locked();
    output.state=view_.scan.state;
    if (output.state!=NetworkWifiScanState::Scanning) output=view_.scan;
    return true;
  }
  void reset() {
    StaticLockGuard lock(mutex_);
    if (++generation_==0) ++generation_;
    view_={}; handle_={}; submitting_=false;
  }
private:
  void fail_locked(const char *error) {
    view_.scan.state=NetworkWifiScanState::Failed; view_.scan.access_point_count=0;
    view_.scan.access_points={}; view_.total_count=0;
    std::snprintf(view_.scan.error.data(),view_.scan.error.size(),"%s",error);
    ++view_.scan.sequence;
  }
  void reconcile_locked() {
    if (view_.scan.state==NetworkWifiScanState::Scanning && !submitting_ && handle_ && !network_job_active(handle_))
      fail_locked("wifi_scan_cancelled_or_expired");
  }
  StaticMutex mutex_;
  NetworkWifiSurveySnapshot &view_;
  NetworkJobHandle handle_;
  uint32_t generation_=0;
  bool submitting_=false;
};
} // namespace lunaredu::lunaros::internal
