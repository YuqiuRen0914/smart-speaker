#pragma once
#include <ArduinoJson.h>
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace lunaredu::lunaros::local_web {
inline constexpr char kMaintenanceIssuer[]="https://passport.lunaredu.cn";
inline constexpr char kMaintenanceJwks[]="https://passport.lunaredu.cn/.well-known/jwks.json";
inline constexpr char kMaintenanceAudience[]="urn:lunaredu:device-maintenance";
inline constexpr char kMaintenanceType[]="lunaredu-device-maintenance+jwt";

// This is a central administrator's capability for a fresh local challenge,
// not hardware attestation, enrollment or business-platform identity.
class MaintenanceAuthority {
 public:
  // The transport owner calls this only after fixed-origin SYSTEM TLS.
  bool install_trusted_jwks(const std::string &,uint32_t now,std::string &error);
  bool keys_ready(uint32_t now) const;
  bool issue(const std::string &serial,const std::string &uuid,
      const std::string &state,const std::string &callback,const std::string &opener,
      const std::array<uint8_t,32> &entropy,uint32_t now,int64_t epoch,
      JsonObject response,std::string &error);
  bool exchange(const std::string &jwt,const std::string &state,
      const std::array<uint8_t,32> &entropy,uint32_t now,int64_t epoch,
      JsonObject response,std::string &error);
  bool authorized(const char *session,uint32_t now,int64_t epoch) const;
  void revoke(const char *session);
 private:
  struct Key {std::string kid;std::vector<uint8_t> der;size_t bits=0;};
  struct Challenge {
    std::string serial,uuid,state,callback,opener,nonce;
    uint32_t issued=0; bool active=false;
  };
  struct Session {std::string value;uint32_t issued=0,duration=0;int64_t expires=0;};
  mutable std::mutex mutex_;
  std::vector<Key> keys_;
  uint32_t keys_at_=0;
  std::array<Challenge,4> challenges_{};
  std::array<Session,4> sessions_{};
};
}
