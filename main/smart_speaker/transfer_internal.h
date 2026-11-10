#pragma once
#include "smart_speaker_transfer.h"
#include "lunaredu_lunaros/device_api/device_projection.h"
#include <mutex>
namespace smart_speaker::detail {
using lunaredu::lunaros::DeviceBulkRequest;
extern TransferStatus g_transfer;
extern std::mutex g_transfer_mutex;
uint32_t now_ms();
std::string lower_copy(std::string text);
bool ends_with_mp3(const std::string &name);
bool safe_upload_name(const std::string &name);
bool stream_upload_to_file(const DeviceBulkRequest &request,
                           const std::string &path, size_t &written);
}
