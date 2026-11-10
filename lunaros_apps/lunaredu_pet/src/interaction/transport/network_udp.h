#pragma once

#include <functional>
#include <memory>
#include <string>

class NetworkUdpChannel {
 public:
  virtual ~NetworkUdpChannel() = default;

  virtual bool Connect(const std::string &host, int port) = 0;
  virtual void Disconnect() = 0;
  virtual int Send(const std::string &data) = 0;
  virtual void OnMessage(
      std::function<void(const std::string &data)> callback) = 0;
  virtual bool IsConnected() const = 0;
  virtual int GetLastError() = 0;
};

std::unique_ptr<NetworkUdpChannel> network_open_udp_channel();

