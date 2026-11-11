// Run the production resource owner against a temporary SD directory and
// a fault-injectable implementation of the public LittleFS contract.
#include "desktop_resources.h"
#include "lunaredu_lunaros/persistence/storage.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

using namespace smart_speaker::product;
namespace {
std::string sd_root, record;
bool sd_ready = true, storage_ready = true, fail_write = false, fail_read = false;
void le(uint8_t *where, uint32_t value, size_t count = 4) {
  for (size_t i = 0; i < count; ++i) where[i] = (value >> (i * 8)) & 255;
}
WallpaperBmp canonical() {
  WallpaperBmp bmp;
  assert(bmp.resize(kWallpaperBmpBytes));
  std::memset(bmp.data(), 0, bmp.size());
  auto *data = bmp.data(); data[0] = 'B'; data[1] = 'M';
  le(data + 2, bmp.size()); le(data + 10, 54); le(data + 14, 40);
  le(data + 18, 240); le(data + 22, 240); le(data + 26, 1, 2); le(data + 28, 24, 2);
  // BMP is bottom up: bottom-left blue, top-left red, top-right green.
  data[54] = 255;
  data[54 + 239 * 720 + 2] = 255;
  data[54 + 239 * 720 + 239 * 3 + 1] = 255;
  return bmp;
}
}
namespace lunaredu::device {
bool tfcard_resolve_path(const char *path, std::string &resolved) {
  if (!sd_ready) return false;
  resolved = sd_root + path; return true;
}
}
namespace lunaredu::lunaros {
bool storage_ready() { return ::storage_ready; }
bool storage_path_exists(const char *) { return !record.empty(); }
bool storage_write_text_file(const char *, const char *text) {
  if (fail_write || !::storage_ready) return false;
  record = text; return true;
}
bool storage_stream_file(const char *, StorageFileChunkWriter writer, void *context, std::string *) {
  if (fail_read || !::storage_ready) return false;
  return writer(reinterpret_cast<const uint8_t *>(record.data()), record.size(), context);
}
}
int main() {
  const auto root = std::filesystem::temp_directory_path() /
      ("speaker-wallpaper-" + std::to_string(std::random_device{}()));
  std::filesystem::create_directory(root); sd_root = root.string();
  std::filesystem::create_directories(root / "characters/dafeiyu/idle");
  std::filesystem::create_directories(root / "characters/dafeiyu/happy");
  const auto pack_path = root / "characters/dsh-pet/pet-v1.bin";
  pet_animation_request(PetClip::Idle);
  pet_animation_poll(0);
  assert(pet_animation_snapshot().unavailable);
  std::filesystem::create_directories(pack_path.parent_path());
  {
    std::ofstream pack(pack_path, std::ios::binary);
    uint8_t header[kPetPackHeaderBytes] = {'D', 'S', 'H', 'P', 1, 5, 120, 12};
    for (size_t i = 0; i < 5; ++i) header[12 + 2*i] = i == 0 ? 2 : 1;
    pack.write(reinterpret_cast<const char *>(header), sizeof(header));
    for (int index = 0; index < 6; ++index) {
      std::array<uint8_t, kPetSpriteBytes> pixels{};
      pixels[0] = index + 1;
      uint8_t checksum[4]{};
      le(checksum, pet_frame_crc(pixels.data(), pixels.size()));
      pack.write(reinterpret_cast<const char *>(checksum), sizeof(checksum));
      pack.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
    }
  }
  pet_animation_poll(999);
  assert(pet_animation_snapshot().unavailable);
  pet_animation_poll(1000);
  auto first_animation_frame = pet_animation_snapshot();
  assert(first_animation_frame.pixels && (*first_animation_frame.pixels)[0] == 1 &&
         first_animation_frame.frame_count == 2);
  pet_animation_request(PetClip::Idle, 1);
  pet_animation_poll(1001);
  auto second = pet_animation_snapshot();
  assert(second.pixels && second.index == 1 && (*second.pixels)[0] == 2);
  assert((*first_animation_frame.pixels)[0] == 1);
  pet_animation_request(PetClip::Tap);
  pet_animation_poll(1002);
  assert(pet_animation_snapshot().clip == PetClip::Idle); // Both buffers are held.
  first_animation_frame.pixels.reset();
  pet_animation_poll(1003);
  assert(pet_animation_snapshot().clip == PetClip::Tap && (*pet_animation_snapshot().pixels)[0] == 3);
  assert((*second.pixels)[0] == 2);
  second.pixels.reset();
  {
    std::fstream pack(pack_path, std::ios::in | std::ios::out | std::ios::binary);
    pack.seekp(kPetPackHeaderBytes + 4);
    pack.put(char(0x42)); // Same length, bad frame checksum.
  }
  pet_animation_request(PetClip::Idle, 0);
  pet_animation_poll(1004);
  assert(pet_animation_snapshot().unavailable && !pet_animation_snapshot().pixels);
  pet_animation_request(PetClip::Count);
  pet_animation_poll(1005);
  assert(!pet_animation_snapshot().unavailable && !pet_animation_snapshot().pixels);
  assert(!pet_animation_catalog_ready());
  {
    std::ofstream pack(pack_path.parent_path() / "pet-v2.idx", std::ios::binary);
    const uint8_t prefix[kPetPackPrefixBytes] = {'D', 'S', 'H', 'P', 2, kPetClipCount, 120, 12, kPetFramesPerShard};
    pack.write(reinterpret_cast<const char *>(prefix), sizeof(prefix));
    for (size_t clip = 0; clip < kPetClipCount - 1; ++clip) pack.write("\1\0", 2);
    pack.write("\15\0", 2); // A full first shard and one frame in the tail shard.
    const auto directory = pack_path.parent_path() / "frames-v2/113";
    std::filesystem::create_directories(directory);
    std::ofstream shard(directory / "01.bin", std::ios::binary);
    std::array<uint8_t, kPetSpriteBytes> pixels{};
    pixels[0] = kPetClipCount - 1;
    uint8_t checksum[4]{};
    le(checksum, pet_frame_crc(pixels.data(), pixels.size()));
    shard.write(reinterpret_cast<const char *>(checksum), sizeof(checksum));
    shard.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
  }
  pet_animation_request(static_cast<PetClip>(kPetClipCount - 1), 12);
  pet_animation_poll(1006);
  assert(pet_animation_catalog_ready() && pet_animation_snapshot().pixels &&
         (*pet_animation_snapshot().pixels)[0] == kPetClipCount - 1);
  pet_animation_request(PetClip::Count);
  pet_animation_poll(1007);
  assert(pet_animation_catalog_ready() && !pet_animation_snapshot().pixels);
  pet_animation_request(static_cast<PetClip>(kPetClipCount - 1), 11);
  pet_animation_poll(1008);
  assert(!pet_animation_catalog_ready() && pet_animation_snapshot().unavailable);
  pet_animation_request(PetClip::Count);
  pet_animation_poll(1009);
  {
    std::ofstream frame(root / "characters/dafeiyu/idle/00.rgb565", std::ios::binary);
    for (size_t i = 0; i < kPetFramePixels; ++i) {
      const uint8_t pixel[2] = {0x34, 0x12}; frame.write(reinterpret_cast<const char *>(pixel), 2);
    }
    std::ofstream happy(root / "characters/dafeiyu/happy/00.rgb565", std::ios::binary);
    for (size_t i = 0; i < kPetFramePixels; ++i) {
      const uint8_t pixel[2] = {0xCD, 0xAB}; happy.write(reinterpret_cast<const char *>(pixel), 2);
    }
  }
  pet_frame_request(PetFrame::Idle);
  assert(desktop_resources_snapshot().pet_frame_pending);
  pet_frame_poll_request(0);
  auto pet = desktop_resources_snapshot();
  assert(!pet.pet_frame_pending && pet.pet_frame && pet.loaded_pet_frame == PetFrame::Idle);
  assert(pet.pet_frame->size() == kPetFramePixels && (*pet.pet_frame)[0] == 0x1234);
  pet_frame_request(PetFrame::Happy);
  pet_frame_poll_request(1);
  pet = desktop_resources_snapshot();
  assert(pet.loaded_pet_frame == PetFrame::Happy && (*pet.pet_frame)[kPetFramePixels - 1] == 0xABCD);
  pet_frame_request(PetFrame::Poke0);
  pet_frame_poll_request(2);
  pet = desktop_resources_snapshot();
  assert(pet.pet_frame_error == "pet_frame_missing" && pet.loaded_pet_frame == PetFrame::Happy);
  // The old immutable frame stays valid while the requested frame is absent.
  assert((*pet.pet_frame)[0] == 0xABCD);
  std::filesystem::create_directories(root / "characters/dafeiyu/angry");
  { std::ofstream bad(root / "characters/dafeiyu/angry/00.rgb565", std::ios::binary); bad << "short"; }
  pet_frame_request(PetFrame::Angry);
  pet_frame_poll_request(3);
  assert(desktop_resources_snapshot().pet_frame_error == "pet_frame_invalid_size");
  pet_frame_request(PetFrame::Think);
  sd_ready = false;
  pet_frame_poll_request(4);
  sd_ready = true;
  pet = desktop_resources_snapshot();
  assert(pet.pet_frame_error == "pet_sdcard_unavailable" && pet.loaded_pet_frame == PetFrame::Happy);
  PetFrame parsed;
  assert(pet_frame_parse("head_pat", 5, parsed) && parsed == PetFrame::HeadPat5);
  assert(!pet_frame_parse("../idle", 0, parsed));
  WallpaperBmp replacement;
  assert(replacement.resize(kPetFramePixels * 2));
  for (size_t i = 0; i < replacement.size(); i += 2) {
    replacement.data()[i] = 0x56; replacement.data()[i + 1] = 0x78;
  }
  const auto old_pet_pixels = pet.pet_frame;
  std::string pet_error;
  const auto happy_path = root / "characters/dafeiyu/happy/00.rgb565";
  std::filesystem::create_directory(happy_path.string() + ".upload");
  assert(!pet_frame_upload(PetFrame::Happy, replacement.data(), replacement.size(), pet_error));
  assert(pet_error == "pet_frame_write_failed");
  {
    std::ifstream unchanged(happy_path, std::ios::binary);
    uint8_t first[2]{}; unchanged.read(reinterpret_cast<char *>(first), sizeof(first));
    assert(first[0] == 0xCD && first[1] == 0xAB);
  }
  std::filesystem::remove(happy_path.string() + ".upload");
  assert(pet_frame_upload(PetFrame::Happy, replacement.data(), replacement.size(), pet_error));
  assert(!desktop_resources_snapshot().pet_frame && desktop_resources_snapshot().pet_frame_pending);
  pet_frame_poll_request(5);
  pet = desktop_resources_snapshot();
  assert(pet.loaded_pet_frame == PetFrame::Happy && (*pet.pet_frame)[0] == 0x7856);
  assert((*old_pet_pixels)[0] == 0xABCD);
  assert(!pet_frame_upload(PetFrame::Happy, replacement.data(), replacement.size() - 1, pet_error));
  const auto blocked_frame = root / "characters/dafeiyu/poke_react/00.rgb565";
  std::filesystem::create_directories(blocked_frame);
  assert(!pet_frame_upload(PetFrame::Poke0, replacement.data(), replacement.size(), pet_error));
  assert(pet_error == "pet_frame_write_failed");
  assert(!std::filesystem::exists(blocked_frame.string() + ".upload"));
  std::filesystem::remove(blocked_frame);
  assert(pet_frame_upload(PetFrame::Poke0, replacement.data(), replacement.size(), pet_error));
  assert(std::filesystem::file_size(blocked_frame) == replacement.size());
  auto bmp = canonical();
  WallpaperPixels pixels;
  assert(decode_wallpaper_bmp(bmp.data(), bmp.size(), pixels));
  assert(pixels[0] == 0xf800 && pixels[239] == 0x07e0 && pixels[239 * 240] == 0x001f);
  assert(!decode_wallpaper_bmp(bmp.data(), bmp.size() - 1, pixels));
  for (size_t offset : {size_t(10), size_t(14), size_t(18), size_t(22), size_t(26), size_t(28), size_t(30), size_t(46)}) {
    const auto saved = bmp.data()[offset]; bmp.data()[offset] ^= 0xff;
    assert(!decode_wallpaper_bmp(bmp.data(), bmp.size(), pixels)); bmp.data()[offset] = saved;
  }
  assert(!pixels.resize(SIZE_MAX));
  for (const char *name : {"../escape.bmp", "a/b.bmp", "x.png", ".bmp", "x..bmp", "a\\b.bmp"})
    assert(!wallpaper_name_valid(name));
  std::string error;
  std::vector<std::string> files;
  assert(desktop_resources_load());
  assert(!desktop_resources_snapshot().pixels);
  sd_ready = false; assert(!wallpaper_upload("one.bmp", bmp, error)); sd_ready = true;
  assert(wallpaper_upload("one.bmp", bmp, error));
  assert(wallpaper_list(files, error) && files.size() == 1);
  assert(!wallpaper_upload("one.bmp", bmp, error));
  assert(wallpaper_select("one.bmp", error));
  const auto previous = desktop_resources_snapshot().pixels;
  const auto saved = record;
  assert(previous && desktop_resources_snapshot().selected == "one.bmp");
  assert(!wallpaper_remove("one.bmp", error));
  assert(wallpaper_upload("two.bmp", bmp, error));
  fail_write = true;
  const auto before_fail = desktop_resources_snapshot().pixels;
  assert(!wallpaper_select("two.bmp", error));
  assert(desktop_resources_snapshot().pixels == before_fail && desktop_resources_snapshot().selected == "one.bmp");
  fail_write = false;
  assert(wallpaper_remove("two.bmp", error));
  // A failed temp-file creation never publishes a file or changes selection.
  std::filesystem::create_directory(root / "wallpapers" / "blocked.bmp.upload");
  assert(!wallpaper_upload("blocked.bmp", bmp, error));
  assert(!std::filesystem::exists(root / "wallpapers" / "blocked.bmp"));
  assert(desktop_resources_snapshot().selected == "one.bmp");
  // Existing users retain their selected wallpaper regardless of the old
  // home mode. Loading a legacy record doesn't overwrite it.
  for (const char *mode : {"pet", "clock", "wallpaper"}) {
    record = std::string(R"({"schema":1,"mode":")") + mode + R"(","wallpaper":"one.bmp"})";
    const auto legacy = record;
    assert(desktop_resources_load());
    const auto restored = desktop_resources_snapshot();
    assert(restored.selected == "one.bmp" && restored.pixels && (*restored.pixels)[0] == 0xf800);
    assert(record == legacy);
  }
  record = "{bad"; assert(!desktop_resources_load() && record == "{bad");
  record.assign(513, 'x'); assert(!desktop_resources_load() && record.size() == 513);
  record = saved;
  { std::ofstream interrupted(root / "wallpapers" / "partial.bmp.upload", std::ios::binary); interrupted << "partial"; }
  assert(desktop_resources_load());
  assert(!std::filesystem::exists(root / "wallpapers" / "partial.bmp.upload"));
  fail_read = true; assert(!desktop_resources_load()); fail_read = false;
  sd_ready = false;
  assert(!desktop_resources_load());
  assert(!desktop_resources_snapshot().pixels && desktop_resources_snapshot().selected == "one.bmp");
  assert(desktop_resources_snapshot().error == "sdcard_not_ready");
  sd_ready = true; assert(desktop_resources_load() && desktop_resources_snapshot().pixels);
  WallpaperBmp reread;
  assert(wallpaper_read("one.bmp", reread, error) && reread.size() == bmp.size());
  { std::ofstream corrupt(root / "wallpapers" / "one.bmp", std::ios::binary); corrupt << "broken"; }
  const auto held = desktop_resources_snapshot().pixels;
  assert(!wallpaper_select("one.bmp", error) && desktop_resources_snapshot().pixels == held);
  assert(!desktop_resources_load() && !desktop_resources_snapshot().pixels);
  // The UI's old shared image remains valid until it releases its reference.
  assert((*held)[0] == 0xf800 && (*previous)[0] == 0xf800);
  std::filesystem::remove_all(root);
  std::cout << "Wallpaper: BMP orientation/colour, path/size validation, SD faults, duplicate rejection, write rollback, reboot and buffer lifetime passed\n";
}
