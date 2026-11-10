#pragma once

#include <stdint.h>

#include "lunaredu_remote_control_protocol.h"
#include "lunaredu_remote_control_sequence.h"

namespace lunaredu::remote_control {

// Caller-owned live link identity. This is not a serialized storage record.
struct ReceiverBinding {
  bool paired = false;
  uint8_t channel = 1;
  uint8_t peer_mac[6] = {};
  uint64_t nonce = 0;

  bool valid() const {
    if (!paired || channel < 1 || channel > 14 || nonce == 0 ||
        (peer_mac[0] & 1U) != 0) return false;
    for (const auto byte : peer_mac) if (byte != 0) return true;
    return false;
  }
};

struct ReceiverCoreConfig {
  int64_t pairing_window_us = 60000000;
  int64_t link_timeout_us = 500000;
  int64_t sequence_restart_us = 500000;
};

enum class InputDisposition : uint8_t {
  Rejected = 0,
  Accepted = 1,
  CandidateNeedsCommit = 2,
};

struct ReceiverSnapshot {
  LinkState link_state = LinkState::Unpaired;
  ReceiverBinding binding;
  ReceiverBinding candidate;
  InputFrame input;
  int64_t pairing_remaining_us = 0;
  int64_t last_input_age_us = -1;
  uint32_t accepted_frames = 0;
  uint32_t rejected_frames = 0;
  int last_rssi = 0;
};

class ReceiverCore {
 public:
  explicit ReceiverCore(ReceiverCoreConfig config = {});

  void restore_binding(const ReceiverBinding &binding);
  void clear_binding();
  void begin_pairing(int64_t now_us, uint8_t channel, bool preserve_binding = false);
  bool pairing_open(int64_t now_us) const;

  bool observe_pairing_request(const uint8_t source_mac[6],
                               const PairingFrame &request, int64_t now_us);
  bool recognizes_binding_request(const uint8_t source_mac[6],
                                  const PairingFrame &request) const;
  bool update_binding_channel(uint8_t channel);
  InputDisposition accept_input(const uint8_t source_mac[6],
                                const InputFrame &frame, int rssi,
                                int64_t now_us);
  bool commit_candidate();
  void clear_candidate();

  ReceiverSnapshot snapshot(int64_t now_us) const;
  const ReceiverBinding &binding() const;
  const ReceiverBinding &candidate() const;

 private:
  LinkState link_state(int64_t now_us) const;

  ReceiverCoreConfig config_;
  ReceiverBinding binding_;
  ReceiverBinding candidate_;
  InputFrame input_;
  int64_t pairing_open_until_us_ = 0;
  int64_t last_input_us_ = 0;
  uint32_t accepted_frames_ = 0;
  uint32_t rejected_frames_ = 0;
  int last_rssi_ = 0;
  FrameSequence input_sequence_,candidate_sequence_;
};

}  // namespace lunaredu::remote_control
