#include "smart_speaker_cover.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace smart_speaker::cover;

std::vector<uint8_t> bmp(uint8_t red) {
  std::vector<uint8_t> data(kBmpBytes, 0);
  auto put16 = [&](size_t at, uint16_t v) { data[at]=uint8_t(v); data[at+1]=uint8_t(v>>8); };
  auto put32 = [&](size_t at, uint32_t v) { put16(at,uint16_t(v)); put16(at+2,uint16_t(v>>16)); };
  data[0]='B'; data[1]='M'; put32(2,data.size()); put32(10,54); put32(14,40);
  put32(18,kSide); put32(22,kSide); put16(26,1); put16(28,24); put32(34,kSide*kSide*3);
  for (size_t i=54; i<data.size(); i+=3) { data[i]=20; data[i+1]=30; data[i+2]=red; }
  return data;
}

int main() {
  const auto directory = std::filesystem::temp_directory_path() /
      ("speaker-cover-" + std::to_string(std::random_device{}()));
  std::filesystem::create_directory(directory);
  const auto dir = directory.string();
  const auto track=dir+"/song.mp3";
  const auto sidecar=path_for_track(track);
  assert(sidecar==track+".cover.bmp");
  assert(path_for_track("/music/nope.wav").empty());
  assert(path_for_track("/music/bad\n.mp3").empty());
  auto first=bmp(200), second=bmp(80);
  std::string error;
  assert(valid_bmp(first.data(),first.size()));
  auto broken=first; broken[18]=239; assert(!valid_bmp(broken.data(),broken.size()));
  assert(!exists(track));
  assert(save(track,first.data(),first.size(),error)); assert(error.empty() && exists(track));
  auto decoded=decode(first.data(),first.size()); assert(decoded && decoded->pixels);
  const auto expected=uint16_t((200>>3)<<11) | uint16_t((30>>2)<<5) | uint16_t(20>>3);
  assert(decoded->pixels.get()[0] == expected);
  assert(!save(track,broken.data(),broken.size(),error));
  // Temporary write failure keeps the currently readable image intact.
  std::filesystem::create_directory(sidecar+".upload");
  assert(!save(track,second.data(),second.size(),error));
  std::filesystem::remove(sidecar+".upload");
  auto buffer=allocate_bmp(); assert(buffer);
  assert(read(track,buffer.get(),kBmpBytes,error) && buffer.get()[56]==200);
  assert(save(track,second.data(),second.size(),error));
  assert(read(track,buffer.get(),kBmpBytes,error));
  assert(buffer.get()[56]==80);
  assert(decoded->pixels.get()[0] == expected); // An old LVGL reference remains valid.
  // Simulate power loss between the two FAT renames.
  std::filesystem::rename(sidecar,sidecar+".bak");
  { std::ofstream interrupted(sidecar+".upload"); interrupted << "partial"; }
  assert(exists(track) && read(track,buffer.get(),kBmpBytes,error));
  assert(buffer.get()[56]==80);
  assert(save(track,first.data(),first.size(),error));
  assert(!std::filesystem::exists(sidecar+".bak"));
  { std::ofstream invalid(sidecar,std::ios::binary|std::ios::trunc); invalid << "bad"; }
  assert(!read(track,buffer.get(),kBmpBytes,error) && error=="cover_read_failed");
  assert(save(track,first.data(),first.size(),error));
  assert(remove(track,error) && !exists(track));
  assert(!read(track,buffer.get(),kBmpBytes,error) && error=="cover_not_found");
  assert(!save(dir+"/missing/song.mp3",first.data(),first.size(),error));
  assert(!error.empty());
  std::filesystem::remove_all(directory);
  std::cout << "Cover: fixed BMP validation, RGB565 decode, sidecar replace/read/remove and write failure passed\n";
}
