#pragma once
namespace lunaredu::bsp::xiaoxing_power {
// Electrical facts of the current XiaoXing and remote-controller boards.
// Products select these boards; this description selects no service or App.
inline constexpr int battery_adc_pin=2;
inline constexpr int divider_numerator=57,divider_denominator=10;
inline constexpr float divider_ratio=float(divider_numerator)/divider_denominator;
inline constexpr int empty_mv=6400,full_mv=8400;
}
