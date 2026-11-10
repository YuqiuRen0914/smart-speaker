#include "realtime/output_cadence.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
  lunaredu::audio::OutputCadence cadence;
  uint64_t now=0;
  unsigned reads=0;
  const auto clock=[&]{++reads;return now;};
  cadence.observe(false,1,clock);assert(reads==0);
  assert(cadence.observe(true,7,clock)==0);
  now=12000;assert(cadence.observe(true,7,clock)==12000);
  now=9000000;assert(cadence.observe(true,8,clock)==0);
  assert(cadence.observe(false,8,clock)==0);
  now=0;cadence={};
  cadence.observe(true,1,clock);now=8000;cadence.observe(true,1,clock);
  assert(cadence.take_maximum()==8000);
  now=48000;cadence.observe(true,1,clock);assert(cadence.take_maximum()==40000);
  now=9000000;cadence.observe(true,2,clock);assert(cadence.take_maximum()==0);
  now+=4000;cadence.observe(true,2,clock);assert(cadence.take_maximum()==4000);
  cadence.observe(false,2,clock);const auto stopped_reads=reads;
  cadence.observe(false,2,clock);assert(reads==stopped_reads);
  now+=9000000;cadence.observe(true,2,clock);assert(cadence.take_maximum()==0);
  now=1;cadence.observe(true,2,clock);assert(cadence.take_maximum()==0);
  std::puts("Output cadence: no disabled clock reads, window maxima, generation and idle reset passed");
}
