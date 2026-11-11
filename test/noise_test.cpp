#include "smart_speaker_noise.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

int main() {
  using smart_speaker::noise::Stream;
  Stream stream;
  std::array<char, 44> header{};
  assert(stream.read(header.data(), header.size()) == header.size());
  assert(std::memcmp(header.data(), "RIFF", 4) == 0);
  assert(std::memcmp(header.data() + 8, "WAVEfmt ", 8) == 0);
  assert(header[20] == 1 && header[22] == 2 && header[34] == 16);
  assert(std::memcmp(header.data() + 36, "data", 4) == 0);
  std::vector<char> pcm(22050 * 4), replay(pcm.size());
  assert(stream.read(pcm.data(), pcm.size()) == pcm.size());
  assert(stream.seek(44, SEEK_SET) == 44);
  // Unaligned short reads must produce exactly the same samples.
  for (size_t offset = 0; offset < replay.size();) {
    const size_t size = std::min<size_t>(137, replay.size() - offset);
    assert(stream.read(replay.data() + offset, size) == size); offset += size;
  }
  assert(pcm == replay);
  double sum = 0, square = 0, correlation = 0;
  int previous = 0;
  for (size_t offset = 0; offset < pcm.size(); offset += 2) {
    const int16_t sample = static_cast<int16_t>(uint16_t(uint8_t(pcm[offset])) |
        uint16_t(uint8_t(pcm[offset + 1])) << 8);
    assert(sample >= -4096 && sample <= 4095);
    sum += sample; square += double(sample) * sample;
    correlation += double(sample) * previous; previous = sample;
  }
  const double count = pcm.size() / 2;
  assert(std::abs(sum / count) < 40);
  assert(square / count > 5e6 && square / count < 6.2e6);
  assert(std::abs(correlation / square) < 0.02);
  assert(stream.seek(-3, SEEK_END) == Stream::kFileBytes - 3);
  assert(stream.read(replay.data(), replay.size()) == 3);
  assert(stream.read(replay.data(), replay.size()) == 0);
  assert(stream.seek(-1, SEEK_SET) == -1);
  assert(stream.seek(1, SEEK_END) == -1);
  assert(stream.seek(INT64_MAX, SEEK_CUR) == -1);
  assert(stream.seek(0, 123) == -1);
#ifdef __APPLE__
  FILE *file = smart_speaker::noise::open(); assert(file);
  assert(fread(replay.data(), 1, 44, file) == 44);
  assert(std::memcmp(replay.data(), header.data(), 44) == 0);
  assert(fseek(file, 44, SEEK_SET) == 0);
  assert(fread(replay.data(), 1, pcm.size(), file) == pcm.size());
  assert(replay == pcm); assert(fclose(file) == 0);
#endif
  std::cout << "Noise: canonical WAV, deterministic seek/short reads, bounded amplitude, distribution, EOF and stream ownership passed\n";
}
