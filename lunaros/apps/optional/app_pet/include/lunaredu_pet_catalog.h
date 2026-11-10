#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace lunaredu::pet::catalog {

// Keep this table in renderer state order: the animation configs use these
// indices. Groups describe meaning; they are derived instead of maintained as
// separate lists. Task states require a real activity phase.
enum class CharacterStateGroup : uint8_t {
  Conversation, Task, System, Notice, Reaction, Cue, InteractionDemo
};
struct CharacterStateEntry {
  const char *id;
  CharacterStateGroup group;
};
#include "lunaredu_pet_builtin_expression_catalog.inc"

constexpr auto character_state_names() {
  std::array<const char *, std::size(kCharacterStateEntries)> result{};
  for (size_t i = 0; i < result.size(); ++i)
    result[i] = kCharacterStateEntries[i].id;
  return result;
}

template <CharacterStateGroup Group>
constexpr auto character_state_group() {
  constexpr size_t count = [] {
    size_t result = 0;
    for (const auto &entry : kCharacterStateEntries)
      if (entry.group == Group) ++result;
    return result;
  }();
  std::array<const char *, count> result{};
  size_t index = 0;
  for (const auto &entry : kCharacterStateEntries)
    if (entry.group == Group) result[index++] = entry.id;
  return result;
}

inline constexpr auto kCharacterStates = character_state_names();
inline constexpr auto kCharacterConversationStates =
    character_state_group<CharacterStateGroup::Conversation>();
inline constexpr auto kCharacterTaskStates =
    character_state_group<CharacterStateGroup::Task>();
inline constexpr auto kCharacterSystemStates =
    character_state_group<CharacterStateGroup::System>();
inline constexpr auto kCharacterNoticeStates =
    character_state_group<CharacterStateGroup::Notice>();
inline constexpr auto kCharacterReactionStates =
    character_state_group<CharacterStateGroup::Reaction>();
inline constexpr auto kCharacterCueStates =
    character_state_group<CharacterStateGroup::Cue>();
inline constexpr auto kCharacterInteractionDemoStates =
    character_state_group<CharacterStateGroup::InteractionDemo>();
inline constexpr std::array<const char *, 19> kCharacterShapes = {
    "soft-pet", "blob", "pebble", "bean", "egg", "squircle", "tablet", "capsule",
    "cylinder", "hex", "gem", "crystal", "wedge", "shield", "dome", "arch",
    "cloud", "teardrop", "leaf"};
inline constexpr std::array<const char *, 13> kCharacterColors = {
    "cyan", "cream", "gold", "magenta", "black", "brown", "red", "orange",
    "yellow", "green", "blue", "violet", "gray"};

template <size_t Size>
constexpr bool character_catalog_contains(const std::array<const char *, Size> &values,
                                          const char *candidate) {
  if (candidate == nullptr) return false;
  for (const char *value : values) {
    const char *left = value;
    const char *right = candidate;
    while (*left != '\0' && *left == *right) {
      ++left;
      ++right;
    }
    if (*left == '\0' && *right == '\0') return true;
  }
  return false;
}

static_assert(character_catalog_contains(kCharacterStates, "orbit"));
static_assert(character_catalog_contains(kCharacterStates, "powering-down"));

} // namespace lunaredu::pet::catalog
