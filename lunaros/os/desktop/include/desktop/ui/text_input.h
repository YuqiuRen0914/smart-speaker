#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace lunaredu::desktop {
enum class TextInputMode : uint8_t { Letters, Numbers, Symbols };
enum class TextInputAction : uint8_t { Character, Group, Done, Case, Letters, Numbers, Symbols, NextSymbols, Delete, Groups, Reveal };
struct TextInputKey {
  TextInputAction action=TextInputAction::Character;
  char value=0;
  char label[12]{};
};
// Exact, bounded printable-ASCII editor; no prediction or normalization.
// Immutable render snapshots own their own copy; each destructor wipes it.
struct TextInputState {
  char value[65]{};
  uint8_t length=0,focus=0,symbol_page=0;
  int8_t group=-1;
  TextInputMode mode=TextInputMode::Letters;
  bool upper=false,revealed=false,last_visible=false;
  uint32_t last_character_ms=0;
  ~TextInputState();
  void clear();
};
using TextInputKeys=std::array<TextInputKey,16>;
size_t text_input_keys(const TextInputState &,TextInputKeys &);
unsigned text_input_columns(const TextInputState &);
bool text_input_key(TextInputState &,const TextInputKey &,uint32_t now_ms);
bool text_input_move(TextInputState &,int direction,int step); // -1: sequential
bool text_input_tick(TextInputState &,uint32_t now_ms);
bool text_input_back(TextInputState &);
void wipe_text(void *bytes,size_t length);
} // namespace lunaredu::desktop
