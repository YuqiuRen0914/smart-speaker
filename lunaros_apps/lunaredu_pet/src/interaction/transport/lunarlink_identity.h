#pragma once
#include <string>
#include "lunaredu_lunaros/device_link/device_link.h"
#include "lunaredu_lunaros/persistence/settings.h"
#include "lunaredu_lunaros/persistence/identity.h"

struct LunarLinkIdentity {
  std::string serial, credential, environment, account;
  int epoch = 0;
  bool ready() const {return epoch > 0 && !serial.empty() && !credential.empty() && !account.empty();}
  bool operator==(const LunarLinkIdentity &) const = default;
};
inline LunarLinkIdentity lunarlink_identity() {
  const auto config = lunaredu::lunaros::device_config();
  const auto pairing = lunaredu::lunaros::device_link_pairing_snapshot();
  return {lunaredu::lunaros::hardware_serial_number(), config.cloud.credential,
          config.cloud.environment, pairing.account_id, pairing.state == lunaredu::lunaros::DeviceLinkBindingState::Bound ? pairing.binding_epoch : 0};
}
