#include "smart_speaker_frame_delta.h"
#include "smart_speaker_character.h"
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
  using namespace smart_speaker;
  using namespace character;
  std::vector<uint16_t> previous(kSize*kSize), screen(previous.size()), expected(previous.size());
  std::array<uint16_t,kSize*kTileRows> tile{};
  Controller controller; Renderer renderer; Input input; input.playing=true;
  uint64_t sent=0, full=0;
  bool retry=false;
  for (uint32_t frame=0; frame<3000; ++frame) {
    if (frame%240==0) { ++input.action_sequence; input.action=Action::Play; }
    input.feedback=frame%900>800 ? Feedback::Radar : frame%900>700 ? Feedback::Hold : Feedback::None;
    input.hold_percent=frame%101;
    auto pose=controller.update(0xffff0000U+frame*33,input);
    renderer.prepare(pose);
    const bool force=retry || frame%91==0 || frame%900==0 || frame%900==701 || frame%900==801;
    retry=false;
    if (force) std::fill(screen.begin(),screen.end(),0xffff); // External LVGL damage.
    for (int y=0; y<kSize; y+=kTileRows) {
      if (frame%173==81 && y==64) { retry=true; break; } // Mid-frame transfer failure.
      assert(renderer.render_tile(y,kTileRows,tile.data(),tile.size()));
      std::copy(tile.begin(),tile.end(),expected.begin()+y*kSize);
      const auto regions=frame_delta(previous.data()+y*kSize,tile.data(),kSize,kTileRows,force);
      assert(regions.count<=regions.items.size());
      for (unsigned i=0;i<regions.count;++i) {
        auto r=regions.items[i];
        assert(r.x>=0 && r.y>=0 && r.width>0 && r.height>0 && r.x+r.width<=kSize && r.y+r.height<=kTileRows);
        for (int row=0;row<r.height;++row) {
          std::copy_n(tile.data()+(r.y+row)*kSize+r.x,r.width,screen.data()+(y+r.y+row)*kSize+r.x);
          std::copy_n(tile.data()+(r.y+row)*kSize+r.x,r.width,previous.data()+(y+r.y+row)*kSize+r.x);
        }
        sent+=r.width*r.height;
      }
    }
    if (!retry) assert(screen==expected); // Includes removal of eyes/particles, morphology and wrap.
    full+=kSize*kSize;
  }
  assert(sent<full*0.65);
  auto same=frame_delta(tile.data(),tile.data(),kSize,kTileRows,false);
  assert(same.count==0);
  // Replay the packed 96-pixel Radar viewport, then return to a full Pebble.
  bool was_radar=false;
  for (uint32_t f=0; f<400; ++f) {
    const bool radar=f>=80 && f<280;
    const bool force=f==0 || radar!=was_radar;
    was_radar=radar;
    input.feedback=radar ? Feedback::Radar : Feedback::None;
    renderer.prepare(controller.update(100000+f*33,input));
    const int inset=radar ? 32 : 0, width=kSize-2*inset;
    if (force) std::fill(screen.begin(),screen.end(),0);
    std::fill(expected.begin(),expected.end(),0);
    for (int y=inset;y<kSize-inset;y+=kTileRows) {
      assert(renderer.render_tile(y,kTileRows,tile.data(),tile.size()));
      for (int row=0;row<kTileRows;++row) {
        std::copy_n(tile.data()+row*kSize+inset,width,expected.data()+(y+row)*kSize+inset);
        std::move(tile.begin()+row*kSize+inset,tile.begin()+row*kSize+inset+width,tile.begin()+row*width);
      }
      auto regions=frame_delta(previous.data()+y*kSize,tile.data(),width,kTileRows,force);
      for (unsigned i=0;i<regions.count;++i) {
        auto r=regions.items[i];
        for (int row=0;row<r.height;++row) {
          std::copy_n(tile.data()+(r.y+row)*width+r.x,r.width,screen.data()+(y+r.y+row)*kSize+inset+r.x);
          std::copy_n(tile.data()+(r.y+row)*width+r.x,r.width,previous.data()+y*kSize+(r.y+row)*width+r.x);
        }
      }
    }
    assert(screen==expected);
  }
  // Enter/leave the lyric crop repeatedly, including its final eight-row tile.
  bool was_compact=false;
  input.feedback=Feedback::None;
  for (uint32_t f=0; f<500; ++f) {
    const bool compact=f%200>=40 && f%200<160;
    const bool force=f==0 || compact!=was_compact;
    was_compact=compact;
    auto pose=controller.update(200000+f*33,input);
    if (compact) fit_lyrics_layout(pose);
    renderer.prepare(pose);
    if (force) std::fill(screen.begin(),screen.end(),0);
    std::fill(expected.begin(),expected.end(),0);
    const int bottom=compact?136:kSize;
    for (int y=0;y<bottom;y+=kTileRows) {
      const int rows=std::min(kTileRows,bottom-y);
      assert(renderer.render_tile(y,rows,tile.data(),tile.size()));
      std::copy_n(tile.data(),rows*kSize,expected.data()+y*kSize);
      const auto regions=frame_delta(previous.data()+y*kSize,tile.data(),kSize,rows,force);
      for (unsigned i=0;i<regions.count;++i) {
        auto r=regions.items[i];
        for (int row=0;row<r.height;++row) {
          std::copy_n(tile.data()+(r.y+row)*kSize+r.x,r.width,screen.data()+(y+r.y+row)*kSize+r.x);
          std::copy_n(tile.data()+(r.y+row)*kSize+r.x,r.width,previous.data()+(y+r.y+row)*kSize+r.x);
        }
      }
    }
    assert(screen==expected);
  }
  std::printf("frame delta: exact full-render replay across 3000 frames, damage recovery, %.1f%% pixels transferred\n",100.0*sent/full);
}
