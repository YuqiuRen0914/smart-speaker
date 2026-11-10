#include "lunaredu_remote_control_receiver_core.h"

#include <cstring>

namespace lunaredu::remote_control {
namespace {
bool same_mac(const uint8_t *left, const uint8_t *right) {
  return left && right && std::memcmp(left, right, 6) == 0;
}
bool unicast_mac(const uint8_t *mac) {
  if (!mac || (mac[0] & 1U) != 0) return false;
  for (size_t i = 0; i < 6; ++i) if (mac[i] != 0) return true;
  return false;
}
} // namespace

ReceiverCore::ReceiverCore(ReceiverCoreConfig config) : config_(config) {}

void ReceiverCore::restore_binding(const ReceiverBinding &binding) {
  binding_ = binding.valid() ? binding : ReceiverBinding{};
  candidate_ = {};
  pairing_open_until_us_ = 0;
  last_input_us_ = 0;
  input_sequence_.reset();candidate_sequence_.reset();input_={};
}

void ReceiverCore::clear_binding() {
  binding_ = {};
  candidate_ = {};
  input_ = {};
  pairing_open_until_us_ = 0;
  last_input_us_ = 0;
  input_sequence_.reset();candidate_sequence_.reset();
}

void ReceiverCore::begin_pairing(int64_t now_us, uint8_t channel, bool preserve_binding) {
  if (!preserve_binding) clear_binding();
  candidate_ = {};
  candidate_sequence_.reset();
  if (!binding_.valid()) binding_.channel = channel >= 1 && channel <= 14 ? channel : ReceiverBinding{}.channel;
  pairing_open_until_us_ = now_us + config_.pairing_window_us;
}

bool ReceiverCore::pairing_open(int64_t now_us) const {
  return now_us < pairing_open_until_us_;
}

bool ReceiverCore::observe_pairing_request(const uint8_t source_mac[6],
                                           const PairingFrame &request,
                                           int64_t now_us) {
  if (!pairing_open(now_us) || !valid_pairing_frame(request) ||
      request.message != static_cast<uint8_t>(PairingMessage::Request) ||
      request.role != static_cast<uint8_t>(DeviceRole::Controller) ||
      !unicast_mac(source_mac)) {
    rejected_frames_ += 1;
    return false;
  }
  const bool same_candidate=candidate_.valid()&&
      candidate_.nonce==request.binding_nonce&&same_mac(candidate_.peer_mac,source_mac);
  if(!same_candidate)candidate_sequence_.reset();
  candidate_ = {};
  candidate_.paired = 1;
  candidate_.channel = request.channel;
  candidate_.nonce = request.binding_nonce;
  std::memcpy(candidate_.peer_mac, source_mac, sizeof(candidate_.peer_mac));
  return true;
}

bool ReceiverCore::recognizes_binding_request(
    const uint8_t source_mac[6], const PairingFrame &request) const {
  return binding_.valid() && valid_pairing_frame(request) &&
         request.message == static_cast<uint8_t>(PairingMessage::Request) &&
         request.role == static_cast<uint8_t>(DeviceRole::Controller) &&
         same_mac(source_mac, binding_.peer_mac) &&
         request.binding_nonce == binding_.nonce;
}

bool ReceiverCore::update_binding_channel(uint8_t channel) {
  if (!binding_.valid() || channel < 1 || channel > 14) return false;
  binding_.channel = channel;
  return true;
}

InputDisposition ReceiverCore::accept_input(const uint8_t source_mac[6],
                                            const InputFrame &frame, int rssi,
                                            int64_t now_us) {
  if (!valid_input_frame(frame)) {
    rejected_frames_ += 1;
    return InputDisposition::Rejected;
  }
  const bool active_match =
      binding_.valid() && same_mac(source_mac, binding_.peer_mac) &&
      frame.binding_nonce == binding_.nonce;
  const bool candidate_match =
      pairing_open(now_us) && candidate_.valid() &&
      same_mac(source_mac, candidate_.peer_mac) &&
      frame.binding_nonce == candidate_.nonce;
  if (!active_match && !candidate_match) {
    rejected_frames_ += 1;
    return InputDisposition::Rejected;
  }
  auto &sequence=active_match?input_sequence_:candidate_sequence_;
  const bool neutral=frame.left_x==0&&frame.left_y==0&&frame.right_x==0&&frame.right_y==0&&
      frame.buttons==0&&frame.encoder_delta==0;
  if(!sequence.allows(frame.sequence,frame.timestamp_ms,now_us,config_.sequence_restart_us,neutral)) {
    ++rejected_frames_;return InputDisposition::Rejected;
  }
  sequence.record(frame.sequence,frame.timestamp_ms,now_us);
  input_ = frame;
  last_input_us_ = now_us;
  accepted_frames_ += 1;
  last_rssi_ = rssi;
  if (active_match) {
    candidate_ = {};
    candidate_sequence_.reset();
    pairing_open_until_us_ = 0;
    return InputDisposition::Accepted;
  }
  return candidate_match ? InputDisposition::CandidateNeedsCommit
                         : InputDisposition::Accepted;
}

bool ReceiverCore::commit_candidate() {
  if (!candidate_.valid()) return false;
  binding_ = candidate_;
  input_sequence_=candidate_sequence_;candidate_sequence_.reset();
  candidate_ = {};
  pairing_open_until_us_ = 0;
  return true;
}

void ReceiverCore::clear_candidate() { candidate_ = {};candidate_sequence_.reset(); }

LinkState ReceiverCore::link_state(int64_t now_us) const {
  if (pairing_open(now_us)) return LinkState::Pairing;
  if (!binding_.valid()) {
    return pairing_open(now_us) ? LinkState::Pairing : LinkState::Unpaired;
  }
  return last_input_us_ != 0 && now_us - last_input_us_ <= config_.link_timeout_us
             ? LinkState::Linked
             : LinkState::Lost;
}

ReceiverSnapshot ReceiverCore::snapshot(int64_t now_us) const {
  ReceiverSnapshot result;
  result.link_state = link_state(now_us);
  result.binding = binding_;
  result.candidate = candidate_;
  result.input = input_;
  result.pairing_remaining_us = pairing_open(now_us)
                                    ? pairing_open_until_us_ - now_us
                                    : 0;
  result.last_input_age_us =
      last_input_us_ == 0 ? -1 : now_us - last_input_us_;
  result.accepted_frames = accepted_frames_;
  result.rejected_frames = rejected_frames_;
  result.last_rssi = last_rssi_;
  return result;
}

const ReceiverBinding &ReceiverCore::binding() const { return binding_; }
const ReceiverBinding &ReceiverCore::candidate() const { return candidate_; }

}  // namespace lunaredu::remote_control
