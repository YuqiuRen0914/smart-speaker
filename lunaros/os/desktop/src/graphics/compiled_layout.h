#pragma once
#include "desktop/ui/content_layout.h"
#include <array>
namespace lunaredu::desktop::compiled_layout {
struct Grid {
  std::array<int16_t,3> x{},y{};
  int16_t width=0,height=0,diameter=0,top=0;
};
constexpr Grid compile_grid(unsigned columns,unsigned rows,bool hidden,bool labels) {
  namespace m=lunaros::graphics::metrics;
  const ContentLayout area(hidden);
  Grid g{};g.width=area.width/int(columns);g.height=area.height()/int(rows);g.top=area.top;
  const int label=labels?m::font(12)+m::gap:0;
  g.diameter=std::min(columns==1&&rows==1?m::um(18000):m::launcher_icon,
    std::min(int(g.width)-m::gap*2,int(g.height)-label-m::gap*2));
  for(unsigned i=0;i<columns;++i)g.x[i]=area.inset+int(i)*g.width+g.width/2;
  for(unsigned i=0;i<rows;++i)g.y[i]=area.top+int(i)*g.height+g.height/2-label/2;
  return g;
}
constexpr auto compile_grids() {
  std::array<Grid,36> result{};
  for(unsigned hidden=0;hidden<2;++hidden)for(unsigned labels=0;labels<2;++labels)
    for(unsigned rows=1;rows<=3;++rows)for(unsigned columns=1;columns<=3;++columns)
      result[((hidden*2+labels)*3+rows-1)*3+columns-1]=compile_grid(columns,rows,hidden,labels);
  return result;
}
inline constexpr auto grids=compile_grids();
struct Point {float x,y;};
constexpr Point compile_point(size_t index){
  if(!index)return {};
  size_t emitted=1;
  constexpr int dx[]={0,-1,-1,0,1,1},dy[]={-1,0,1,1,0,-1};
  for(int radius=1;radius<8;++radius){
    const size_t ring=size_t(6*radius);
    if(index<emitted+ring){
      const size_t target=(index-emitted+size_t(radius/2))%ring;
      int q=radius,r=0;
      for(size_t n=0;n<target;++n){const int side=int(n)/radius;q+=dx[side];r+=dy[side];}
      constexpr float spacing=lunaros::graphics::metrics::native?
        float(std::max(lunaros::graphics::metrics::min_touch,lunaros::graphics::metrics::launcher_icon)+lunaros::graphics::metrics::gap*2):68.f;
      return {spacing*(q+r*.5f),spacing*.866025404f*r};
    }
    emitted+=ring;
  }
  return {};
}
constexpr auto compile_points(){
  std::array<Point,169> points{};
  for(size_t i=0;i<points.size();++i)points[i]=compile_point(i);
  return points;
}
inline constexpr auto honeycomb_points=compile_points();
inline const Grid &grid(unsigned columns,unsigned rows,bool hidden,bool labels) {
  return grids[((unsigned(hidden)*2+unsigned(labels))*3+std::clamp(rows,1u,3u)-1)*3+std::clamp(columns,1u,3u)-1];
}
}
