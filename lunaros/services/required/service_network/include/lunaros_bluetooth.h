#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace lunaredu::lunaros {

enum class BluetoothAction:uint8_t { ScanBle, ScanClassic, Pair, ConfirmPair, RejectPair,
  ArmKeyboard, DisarmKeyboard, Key, Media, SendData, MusicOn, MusicOff, MusicMedia };
struct BluetoothDevice {
  std::array<uint8_t,6> address{};
  std::array<char,40> name{};
  int8_t rssi=-127;
  bool classic=false;
  uint32_t seen_ms=0;
};
struct BluetoothSnapshot {
  bool ble_supported=false,classic_supported=false,music_supported=false;
  bool ble_scan_supported=false,pairing_supported=false,hid_supported=false,data_supported=false;
  bool available=false,enabled=false,discovering=false,pairing=false;
  bool ble_connected=false,ble_encrypted=false,spp_connected=false,music_connected=false;
  bool keyboard_armed=false,music_enabled=false,confirm_pending=false;
  bool music_playing=false,music_control_available=false;
  uint32_t revision=0,request_id=0,pairing_code=0;
  uint32_t received_bytes=0,sent_bytes=0,dropped_bytes=0,audio_packets=0,audio_drops=0;
  size_t device_count=0;
  std::array<BluetoothDevice,24> devices{};
  std::array<char,161> received{};
  std::array<char,96> error{};
};
// Fixed queue; commands execute on Network owner. Key=USB HID usage byte,
// Media=consumer usage (play/pause/next/previous/volume), Data<=160 bytes.
bool request_bluetooth(BluetoothAction,uint16_t value,const std::string &data,uint32_t &request_id,std::string &error);
BluetoothSnapshot bluetooth_snapshot();
// Fill owner-provided storage without materializing a large return value.
void bluetooth_snapshot_into(BluetoothSnapshot &);
// Music frontends do not capture the discovery/data arrays. MusicMedia is
// AVRCP-only and must never fall back to the keyboard profile.
struct BluetoothMusicSnapshot {
  bool available=false,enabled=false,receiving=false,connected=false,playing=false,controllable=false;
  std::array<char,96> error{};
  bool supported=false;
  bool operator==(const BluetoothMusicSnapshot &o)const{
    return available==o.available&&enabled==o.enabled&&receiving==o.receiving&&connected==o.connected&&
      playing==o.playing&&controllable==o.controllable&&error==o.error&&supported==o.supported;
  }
  bool operator!=(const BluetoothMusicSnapshot &o)const{return !(*this==o);}
};
BluetoothMusicSnapshot bluetooth_music_snapshot();

} // namespace lunaredu::lunaros
