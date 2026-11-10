#pragma once
#include "lunaros_download_service_id.h"
#include "lunaredu_download.h"

namespace lunaredu::lunaros {

// Download-owned adapter from the Network streaming transport.
DownloadTransportResult network_open_download_stream(DownloadRequest request);
struct ServiceNode;
struct DownloadServicePorts { DownloadTransport transport = nullptr; };
// Borrowed for the service registration lifetime; default OS composition owns it.
ServiceNode download_service_node(DownloadServicePorts &);
}  // namespace lunaredu::lunaros

