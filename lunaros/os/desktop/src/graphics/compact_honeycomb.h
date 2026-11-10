#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace lunaredu::desktop::compact_honeycomb {
// Integer vertex coordinates make every separator a shared regular hex edge.
// The origin is a three-cell junction: upper-left, upper-right and lower sectors.
inline constexpr float spacing=64.f, x_unit=spacing/2.f, y_unit=spacing*0.288675134595f;
// Keep positions and shared sector edges on the same regular hex lattice.
// Only icon size changes with distance from the moving viewport center.
inline float icon_diameter(float radius) {
  const float t=std::clamp((radius-24.f)/120.f,0.f,1.f);
  return 60.f*(1.f-.72f*t*t*(3.f-2.f*t));
}
inline constexpr size_t capacity=32;
struct Vertex {int16_t u=0,v=0;};
struct Cell {int16_t q=0,r=0;};
struct Segment {Vertex a,b;};
constexpr int abs(int x){return x<0?-x:x;}
constexpr Vertex center(Cell c){return {int16_t(2*c.q+c.r-1),int16_t(3*c.r-1)};}
constexpr unsigned sector(Cell c){
  const auto p=center(c);
  return p.v>=abs(p.u)?2u:p.u<0?0u:1u;
}
constexpr int score(Cell c){const auto p=center(c);return 3*p.u*p.u+p.v*p.v;}
constexpr auto compile_cells(){
  std::array<std::array<Cell,capacity>,3> result{};
  for(unsigned group=0;group<3;++group){
    std::array<bool,225> used{};
    for(size_t slot=0;slot<capacity;++slot){
      int best=100000;size_t chosen=0;Cell cell{};
      for(int r=-7;r<=7;++r)for(int q=-7;q<=7;++q){
        const Cell c{int16_t(q),int16_t(r)};const size_t index=size_t((r+7)*15+q+7);
        if(!used[index]&&sector(c)==group&&score(c)<best){best=score(c);chosen=index;cell=c;}
      }
      used[chosen]=true;result[group][slot]=cell;
    }
  }
  return result;
}
inline constexpr auto cells=compile_cells();
inline constexpr std::array<Vertex,6> corners{{{1,1},{0,2},{-1,1},{-1,-1},{0,-2},{1,-1}}};
inline constexpr std::array<Cell,6> neighbors{{{0,1},{-1,1},{-1,0},{0,-1},{1,-1},{1,0}}};
constexpr Vertex vertex(Cell c,unsigned edge){const auto p=center(c),v=corners[edge%6];return {int16_t(p.u+v.u),int16_t(p.v+v.v)};}
struct Borders {std::array<Segment,256> segments{};size_t count=0;};
constexpr auto compile_borders(){
  Borders out{};
  // A bounded immutable map covers all 32 slots of every sector plus the viewport.
  for(int r=-16;r<=16;++r)for(int q=-16;q<=16;++q){
    const Cell c{int16_t(q),int16_t(r)};
    for(unsigned edge=0;edge<6;++edge){
      const auto d=neighbors[edge];const Cell n{int16_t(q+d.q),int16_t(r+d.r)};
      if(sector(c)>=sector(n))continue;
      out.segments[out.count++]={vertex(c,edge),vertex(c,edge+1)};
    }
  }
  return out;
}
inline constexpr auto borders=compile_borders();
static_assert(borders.count<256);
struct Point {float x=0,y=0;};
struct SoftSegment {Point a,b;};
struct SoftBorders {
  std::array<SoftSegment,borders.count*5> segments{};
  size_t count=0,source_edges=0;
};
constexpr bool equal(Vertex a,Vertex b){return a.u==b.u&&a.v==b.v;}
constexpr Point position(Vertex p){return {p.u*x_unit,p.v*y_unit};}
constexpr Point mix(Point a,Point b,float t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};}
// Compile the three shared boundaries once. Corner trimming remains inside
// the gap between hex cells, without changing category/app coordinates.
constexpr auto compile_soft_borders(){
  SoftBorders out{};std::array<bool,256> used{};
  constexpr float trim=6.f/(spacing*.57735026919f);
  for(unsigned arm=0;arm<3;++arm){
    Vertex current{};Point last{};
    for(size_t step=0;step<borders.count;++step){
      size_t chosen=borders.count;Vertex next{};
      for(size_t i=0;i<borders.count;++i)if(!used[i]){
        const auto edge=borders.segments[i];
        if(equal(edge.a,current)||equal(edge.b,current)){
          chosen=i;next=equal(edge.a,current)?edge.b:edge.a;break;
        }
      }
      if(chosen==borders.count)break;
      used[chosen]=true;++out.source_edges;
      const Point corner=position(next);Vertex following{};bool has_next=false;
      for(size_t i=0;i<borders.count;++i)if(!used[i]){
        const auto edge=borders.segments[i];
        if(equal(edge.a,next)||equal(edge.b,next)){
          following=equal(edge.a,next)?edge.b:edge.a;has_next=true;break;
        }
      }
      if(!has_next){out.segments[out.count++]={last,corner};break;}
      const auto before=mix(corner,position(current),trim);
      const auto after=mix(corner,position(following),trim);
      out.segments[out.count++]={last,before};
      Point previous=before;
      for(unsigned sample=1;sample<=4;++sample){
        const float t=sample/4.f;
        const Point curve=mix(mix(before,corner,t),mix(corner,after,t),t);
        out.segments[out.count++]={previous,curve};previous=curve;
      }
      last=after;current=next;
    }
  }
  return out;
}
inline constexpr auto soft_borders=compile_soft_borders();
static_assert(soft_borders.source_edges==borders.count);
static_assert(soft_borders.count<=soft_borders.segments.size());
constexpr auto compile_edge_fade(){
  std::array<uint8_t,240> result{};
  for(int i=0;i<240;++i){const int margin=i<120?i:239-i;result[size_t(i)]=uint8_t(margin>=24?255:margin*255/24);}
  return result;
}
inline constexpr auto edge_fade=compile_edge_fade();
}
