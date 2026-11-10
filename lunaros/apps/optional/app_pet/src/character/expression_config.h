#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
#include "lunaredu_pet_catalog.h"
namespace lunaredu::pet::character {
struct ExpressionConfig { uint8_t count=0,eyes[6]{}; float hold[2]{},blink[2]{}; };
inline constexpr size_t kExpressionCount=catalog::kCharacterStates.size();
using ExpressionTable=std::array<ExpressionConfig,kExpressionCount>;
using ExpressionOverrides=std::array<bool,kExpressionCount>;
void publish_expression_configs(const ExpressionTable &,const ExpressionOverrides &);
ExpressionConfig expression_config(size_t state,const ExpressionConfig &builtin);
uint32_t active_generation();
uint32_t rendered_generation();
void mark_rendered(uint32_t generation);
}
