#include "../src/delivery/update/ota_http_pull.cpp"
#include <algorithm>
#include <cassert>
#include <iostream>

namespace {
std::string body = "abc", message;
size_t length = 3, capacity = 1024, closes = 0, installs = 0, opens = 0;
bool open_failed = false, read_failed = false;
int response_status = 200;
class Reader final : public lunaredu::lunaros::NetworkDownloadReader {
  size_t offset = 0;
public:
  int Read(char *data, size_t count) override {
    if (read_failed) return -1;
    count = std::min(count, body.size() - offset);
    std::memcpy(data, body.data() + offset, count); offset += count;
    return static_cast<int>(count);
  }
  size_t content_length() const override { return length; }
  size_t total_read() const override { return offset; }
  int GetLastError() const override { return read_failed ? 7 : 0; }
  std::string error_detail() const override { return read_failed ? "incomplete" : ""; }
  void Close() override { ++closes; }
};
std::string checksum() {
  std::array<uint8_t, 32> digest{}; size_t count = 0;
  psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const uint8_t *>(body.data()),
      body.size(), digest.data(), digest.size(), &count);
  std::string result;
  for (auto byte : digest) { char hex[3]; std::snprintf(hex, sizeof(hex), "%02x", byte); result += hex; }
  return result;
}
}
namespace lunaredu::lunaros {
std::unique_ptr<NetworkDownloadReader> network_start_download(NetworkDownloadRequest request,
                                                            NetworkDownloadFailure *failure) {
  ++opens;
  assert(request.timeout_ms == 120000 && request.keep_alive);
  if (open_failed) { *failure = {-1, 7, "connect_failed"}; return {}; }
  if (response_status != 200) { *failure = {response_status, 0, ""}; return {}; }
  return std::make_unique<Reader>();
}
OtaImageMetadataSnapshot ota_service_image_metadata_snapshot() {
  OtaImageMetadataSnapshot result; result.next.size = capacity; return result;
}
void ota_service_report_failure(const char *error) { message = error; }
bool ota_service_install_stream(const OtaFirmwareStream &stream) {
  ++installs;
  uint8_t data[2]; size_t received = 0;
  while (received < stream.content_length) {
    int count = stream.read_chunk(data, std::min(sizeof(data), stream.content_length - received), stream.context);
    if (count <= 0) { message = stream.read_error(stream.context); return false; }
    received += static_cast<size_t>(count);
  }
  return stream.validate(stream.context);
}
NetworkJobManagerSnapshot network_job_manager_snapshot() { return {}; }
NetworkMaintenanceState network_maintenance_state() { return NetworkMaintenanceState::OtaExclusive; }
bool submit_network_job(const NetworkJobDefinition &, NetworkJobHandle &, std::string &) { return false; }
bool ota_service_upload_active() { return false; }
bool ota_service_restart_pending() { return false; }
OtaServiceStatus ota_service_status_snapshot() { OtaServiceStatus result; result.message = message; return result; }
void ota_service_report_pull_queued(uint32_t) {}
void ota_service_report_pull_started(uint32_t) {}
void ota_service_report_pull_finished(uint32_t) {}
}
int main() {
  using namespace lunaredu::lunaros;
  const std::string url = "https://example.invalid/image.bin";
  const std::string hash = checksum();
  assert(install_from_url(url, hash, 3));
  assert(closes == 1 && installs == 1);
  assert(!install_from_url(url, std::string(64, '0'), 3)); // Digest rejection.
  assert(closes == 2);
  read_failed = true;
  assert(!install_from_url(url, hash, 3));
  assert(message.find("ota_read_failed:") == 0 && closes == 3);
  read_failed = false; length = 4;
  const auto writers = installs;
  assert(!install_from_url(url, hash, 3));
  assert(installs == writers && closes == 4);
  length = 3; body = "ab";
  assert(!install_from_url(url, hash, 3)); // Early EOF cannot validate a partial image.
  assert(closes == 5); body = "abc";
  response_status = 404;
  assert(!install_from_url(url, hash, 3));
  assert(message.find("ota_http_status_failed:404:") == 0);
  response_status = 200; open_failed = true;
  assert(!install_from_url(url, hash, 3));
  assert(message.find("ota_http_open_failed:") == 0);
  open_failed = false;
  auto before = opens;
  assert(!install_from_url(url, "bad-hash", 3));
  assert(!install_from_url("http://example.invalid/image.bin", hash, 3));
  assert(!install_from_url(url, hash, capacity + 1));
  assert(opens == before);
  std::cout << "OTA shared transport: success, digest, EOF, read/status/open failures, size/policy admission and cleanup passed\n";
}
