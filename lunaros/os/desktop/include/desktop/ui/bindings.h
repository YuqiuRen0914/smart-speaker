#pragma once
#include "desktop/ui/route.h"
#include <array>
#include <cstring>
#include <string>
namespace lunaredu::app {
// One immutable declaration per UI owner. Concrete Apps supply presentation
// types and handlers; products bind capabilities. No worker or lifecycle owner.
template<class Capture, class Input, class Entry, class Launch, class Payload, size_t Capacity=16>
class UiBindings {
 public:
  using CaptureContext = Capture;
  using InputContext = Input;
  using EntryContext = Entry;
  using LaunchContext = Launch;
  using Presentation = Payload;
  struct Definition {
    const char *id;
    Payload (*capture)(Capture &)=nullptr;
    bool (*input)(Input &)=nullptr;
    void (*entered)(Entry &)=nullptr;
    bool (*launch)(Launch &,std::string &)=nullptr;
  };
  // A cursor is input-owner-local, never mutable state on the shared table.
  // Its lookup key is owned; a caller reusing a char buffer cannot alias it.
  struct InputCursor {
    const UiBindings *table=nullptr;size_t count=0;std::array<char,64> owner{};
    const Definition *entry=nullptr;
  };
  bool add(Definition definition) {
    if(!definition.id||!*definition.id||count_==Capacity){valid_=false;return false;}
    for(size_t i=0;i<count_;++i)if(!std::strcmp(entries_[i].id,definition.id)){valid_=false;return false;}
    entries_[count_++]=definition;return true;
  }
  const Definition *find(const char *id) const {
    if(id)for(size_t i=0;i<count_;++i)if(!std::strcmp(entries_[i].id,id))return &entries_[i];
    return nullptr;
  }
  bool valid() const { return valid_; }
  Payload capture(ViewRoute route,Capture &context) const {
    const auto *entry=find(route.owner);return entry&&entry->capture?entry->capture(context):Payload{};
  }
  bool input(ViewRoute route,Input &context) const {
    const auto *entry=find(route.owner);return entry&&entry->input&&entry->input(context);
  }
  // Entry-only surfaces have no App input handler. Resolve the immutable
  // binding before constructing every selected session's input context.
  template<class Factory> bool input_with(ViewRoute route,Factory make_context) const {
    const auto *entry=find(route.owner);
    if(!entry||!entry->input)return false;
    auto context=make_context();return entry->input(context);
  }
  template<class Factory> bool input_with(ViewRoute route,InputCursor &cursor,Factory make_context) const {
    const char *id=route.owner?route.owner:"";
    const size_t length=std::strlen(id);
    // This bounds the cache, not valid identifiers. Longer owners retain the
    // ordinary lazy lookup path without truncation or heap allocation.
    if(length>=cursor.owner.size()){cursor.table=nullptr;return input_with(route,make_context);}
    if(cursor.table!=this||cursor.count!=count_||std::strcmp(cursor.owner.data(),id)){
      cursor.table=this;cursor.count=count_;std::memcpy(cursor.owner.data(),id,length+1);cursor.entry=find(route.owner);
    }
    if(!cursor.entry||!cursor.entry->input)return false;
    auto context=make_context();return cursor.entry->input(context);
  }
  void entered(const char *id,Entry &context) const {
    const auto *entry=find(id);if(entry&&entry->entered)entry->entered(context);
  }
 private:
  std::array<Definition,Capacity> entries_{};
  size_t count_=0;
  bool valid_=true;
};
}
