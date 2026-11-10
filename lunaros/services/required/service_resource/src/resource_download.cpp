#include "resource_download.h"
#include "download_stream.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

#include "lunaros_digest.h"
#include <mbedtls/base64.h>

#include "resource_storage_internal.h"
#include "resource_policy.h"
#include "lunaredu_download.h"
#include "lunaros_storage.h"

namespace lunaredu::lunaros::internal {
namespace hashing = ::lunaredu::lunaros::digest;
namespace {

struct DownloadContext {
  DownloadReader *reader = nullptr;
  size_t maximum_bytes = 0;
  size_t total = 0;
  hashing::Sha256 sha_context;
  ResourceDownloadCancellationCallback cancellation_callback = nullptr;
  const void *cancellation_context = nullptr;
};

bool cancellation_requested(const DownloadContext &context) {
  return context.cancellation_callback != nullptr &&
         context.cancellation_callback(context.cancellation_context);
}

std::string sha256_text(const std::string &text) {
  return hashing::sha256_hex(reinterpret_cast<const uint8_t *>(text.data()),text.size());
}

std::string scope_identity(const ResourceCacheScope &scope) {
  return std::to_string(scope.binding_epoch) + ":" +
      std::to_string(scope.account_id.size()) + ":" + scope.account_id;
}

std::string hash_token(const std::string &text, size_t bytes) {
  std::array<uint8_t, 32> digest{};
  if(bytes>digest.size())return {};
  if (!hashing::sha256(reinterpret_cast<const uint8_t *>(text.data()),text.size(),digest)) return {};
  std::array<unsigned char,45> encoded{};size_t written=0;
  if(mbedtls_base64_encode(encoded.data(),encoded.size(),&written,digest.data(),bytes)!=0)return {};
  while(written&&encoded[written-1]=='=')--written;
  std::string result(reinterpret_cast<const char *>(encoded.data()),written);
  for(auto &c:result){if(c=='+')c='-';else if(c=='/')c='_';}
  return result;
}

std::string cache_path(const ResourceDescriptor &descriptor,
                       const ResourceCacheScope &scope) {
  if (descriptor.scope != ResourceScope::Public) {
    const std::string root = private_resource_cache_root(scope);
    if (root.empty()) return {};
    // The full scope also enters the 160-bit file key: a short directory-tag
    // collision can at worst retain garbage, never alias private content.
    const std::string key = hash_token(scope_identity(scope) + "\n" +
        std::to_string(static_cast<unsigned>(descriptor.scope)) + "\n" +
        descriptor.id + "\n" + descriptor.revision + "\n" + descriptor.format, 20);
    return key.empty() ? std::string{} : root + "/" + key;
  }
  const std::string key = sha256_text(descriptor.id + "\n" +
                                      descriptor.revision + "\n" +
                                      descriptor.format);
  if (key.empty()) {
    return {};
  }
  // LittleFS applies its maximum path length to the temporary `.part` path
  // as well as the installed file.  A full 64-character hex digest made the
  // private-resource path exceed that limit.  160 bits still gives a
  // collision-resistant cache key while keeping the temporary path bounded.
  constexpr size_t kCacheKeyHexLength = 40;
  return std::string(kPublicResourceRoot) + "/" + key.substr(0, kCacheKeyHexLength);
}

std::string absolute_url(const std::string &url,
                         const ResourceTransportConfig &transport) {
  if (url.rfind("https://", 0) == 0) {
    return url;
  }
  if (url.empty() || url.front() != '/' ||
      transport.base_url.rfind("https://", 0) != 0) {
    return {};
  }
  return transport.base_url + url;
}

bool write_download_chunk(StorageFileChunkWriter writer, void *target,
                          void *opaque, std::string &error) {
  auto &context = *static_cast<DownloadContext *>(opaque);
  DownloadStream stream{context.reader, context.maximum_bytes, 0,
      &context.sha_context, context.cancellation_callback, context.cancellation_context};
  const bool ok = copy_download_stream(writer, target, &stream, error);
  context.total = stream.total;
  return ok;
}

ResourceDownloadResult failed(std::string error) {
  ResourceDownloadResult result;
  result.error = std::move(error);
  return result;
}

} // namespace

std::string private_resource_cache_root(const ResourceCacheScope &scope) {
  if (scope.account_id.empty()) return {};
  const std::string tag = hash_token(scope_identity(scope), 8);
  return tag.empty() ? std::string{} : std::string(kPrivateResourceRoot) + "/" + tag;
}

std::string resource_cache_path(const ResourceDescriptor &descriptor,
                                const ResourceCacheScope &scope) {
  return cache_path(descriptor, scope);
}

ResourceDownloadResult download_resource_transaction(
    const ResourceDescriptor &descriptor,
    const ResourceTransportConfig &transport,
    ResourceDownloadProgressCallback progress_callback,
    const void *progress_context,
    ResourceDownloadCancellationCallback cancellation_callback,
    const void *cancellation_context, const ResourceCacheScope &scope) {
  if (cancellation_callback != nullptr &&
      cancellation_callback(cancellation_context)) {
    return failed("download_cancelled");
  }
  const std::string url = absolute_url(descriptor.url, transport);
  if (url.empty()) {
    return failed("resource_url_invalid");
  }

  const size_t maximum =
      std::min(descriptor.maximum_bytes, kAbsoluteResourceMaximumBytes);
  DownloadRequest request;
  request.url = url;
  request.maximum_bytes = maximum;
  // This is background delivery. Bound a blocked read so an OTA cancellation
  // cannot be hidden behind a 30-second socket wait.
  request.timeout_ms = 5000;
  request.transport_security = DownloadTransportSecurity::RequireTls;
  if (!transport.device_serial.empty()) {
    request.headers.push_back({"X-Hardware-Serial", transport.device_serial});
  }
  if (!transport.bearer_token.empty()) {
    request.headers.push_back(
        {"Authorization", "Bearer " + transport.bearer_token});
  }
  request.progress_callback = [progress_callback, progress_context](
                                  const DownloadProgress &progress) {
    if (progress_callback != nullptr) {
      progress_callback(progress.progress, progress_context);
    }
  };

  DownloadFailure failure;
  std::unique_ptr<DownloadReader> reader =
      download_start(std::move(request), &failure);
  if (!reader) {
    return failed(failure.detail == "download_size_limit_exceeded" ||
                  failure.detail == "download_content_length_unavailable"
        ? "resource_size_rejected" : "download_open_failed");
  }

  if (reader->content_length() == 0 || reader->content_length() > maximum) {
    reader->Close();
    return failed("resource_size_rejected");
  }
  const size_t declared_length = reader->content_length();
  const size_t pixel_bytes = resource_cached_size_if_valid(descriptor);
  if ((descriptor.format == "rgb666-u8x3" ||
       descriptor.format == "a8rgb666-u8x4") &&
      (pixel_bytes == 0 || declared_length != pixel_bytes)) {
    reader->Close();
    return failed("resource_pixel_size_mismatch");
  }

  const std::string destination = resource_cache_path(descriptor, scope);
  if (destination.empty()) {
    reader->Close();
    return failed("resource_cache_key_failed");
  }
  std::string capacity_error;
  if (descriptor.scope == ResourceScope::AccountPrivate &&
      !ensure_private_resource_capacity(reader->content_length(),
                                        capacity_error)) {
    reader->Close();
    return failed(capacity_error);
  }
  const std::string temporary = destination + ".part";
  storage_delete_file(temporary.c_str());

  DownloadContext context;
  context.reader = reader.get();
  context.maximum_bytes = declared_length;
  context.cancellation_callback = cancellation_callback;
  context.cancellation_context = cancellation_context;
  if (!context.sha_context.begin()) {
    reader->Close();
    return failed("sha256_init_failed");
  }

  size_t written = 0;
  std::string write_error;
  const bool stored = storage_write_streamed_file(
      temporary.c_str(), write_download_chunk, &context, &written,
      &write_error);
  reader->Close();

  std::array<uint8_t, 32> digest{};
  const bool digest_ready =
      stored &&
      context.sha_context.finish(digest);
  if (!digest_ready) {
    (void)context.sha_context.abort();
  }
  if (!stored || !digest_ready || written != context.total ||
      written != declared_length) {
    storage_delete_file(temporary.c_str());
    return failed(write_error.empty() || write_error == "download_length_mismatch"
        ? "download_incomplete" : write_error);
  }

  if (cancellation_requested(context)) {
    storage_delete_file(temporary.c_str());
    return failed("download_cancelled");
  }

  const std::string actual_sha = hashing::hex(digest.data(), digest.size());
  if (!descriptor.sha256.empty() && descriptor.sha256 != actual_sha) {
    storage_delete_file(temporary.c_str());
    return failed("resource_checksum_mismatch");
  }

  if (cancellation_requested(context)) {
    storage_delete_file(temporary.c_str());
    return failed("download_cancelled");
  }

  storage_delete_file(destination.c_str());
  std::string move_error;
  if (!storage_move_file(temporary.c_str(), destination.c_str(), &move_error)) {
    storage_delete_file(temporary.c_str());
    return failed(move_error.empty() ? "resource_install_failed" : move_error);
  }

  ResourceDownloadResult result;
  result.success = true;
  result.path = destination;
  result.sha256 = actual_sha;
  result.size_bytes = written;
  return result;
}

} // namespace lunaredu::lunaros::internal
