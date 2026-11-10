#pragma once
#include <cstdint>
namespace lunaredu::pet::render {
struct BodyColor {const char *name;uint32_t rgb;};
// Light-scheme pigment values from the pinned original GROK_GEO.palette.
// Gold preserves our accepted material; cream adds a light neutral alternative.
inline constexpr BodyColor body_colors[]={
    {"gold",0xe6b35a},{"cream",0xefe8d8},{"black",0x000000},
    {"brown",0xa27952},{"red",0xff3e51},{"orange",0xff781c},
    {"yellow",0xffaf38},{"green",0x00c972},{"cyan",0x1cc3b0},
    {"blue",0x2a92fe},{"violet",0xa97efe},{"magenta",0xff5eb1},{"gray",0x959595}
};
inline constexpr int BodyColorCount=sizeof(body_colors)/sizeof(body_colors[0]);
}
