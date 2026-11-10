#pragma once
#include <cstdint>
namespace lunaredu::presentation {
enum class Expression : uint8_t {
  Boot,
  Idle,
  Listening,
  Speaking,
  Happy,
  Celebrate,
  Curious,
  Sleepy,
  Error,
  Moving,
};

inline const char *expression_name(Expression expression) {
  switch (expression) {
  case Expression::Boot:
    return "boot";
  case Expression::Idle:
    return "idle";
  case Expression::Listening:
    return "listening";
  case Expression::Speaking:
    return "speaking";
  case Expression::Happy:
    return "happy";
  case Expression::Celebrate:
    return "celebrate";
  case Expression::Curious:
    return "curious";
  case Expression::Sleepy:
    return "sleepy";
  case Expression::Error:
    return "error";
  case Expression::Moving:
    return "moving";
  }
  return "idle";
}

}
