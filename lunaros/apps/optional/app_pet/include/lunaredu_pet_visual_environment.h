#pragma once

namespace lunaredu::pet {
// Companion presents a local pet independently of passive network/activation
// work. PairingDevice adds guidance until local binding is committed.
enum class VisualPolicy { Standard, PairingDevice, Companion };
enum class VisualLinkState { Unspecified, Offline, OnlineUnbound, AwaitingPairing, Bound };
struct VisualEnvironment {
    bool provisioning = false;
    VisualLinkState link = VisualLinkState::Unspecified;
    float pairing_remaining_fraction = -1.0f;
    // Borrowed only while the display callback returns and Pet applies the frame.
    const char *expression_override = nullptr;
    // Optional presentation fact; no audio handles or playback ownership.
    bool media_playing=false;
    bool binding_committed = false;
};
// Optional platform-backed reader for PairingDevice. Pass it directly to
// Ports::read_visual_environment; custom/offline products supply their own reader.
VisualEnvironment read_platform_visual_environment(void *);
} // namespace lunaredu::pet
