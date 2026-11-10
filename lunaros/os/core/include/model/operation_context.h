#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <utility>

namespace lunaredu::lunaros {
enum class OperationExecutor:uint8_t { Main,NetworkIo };
// Owned, copyable cooperative budget. No authority, worker, timer or callback.
// A bounded token set rejects excess distinct cancellation ancestry rather than
// silently losing an ancestor at an asynchronous boundary.
struct OperationContext {
  using Clock=std::chrono::steady_clock;
  std::array<std::stop_token,4> cancellations{};
  uint8_t cancellation_count=0;
  Clock::time_point deadline{};
  bool ancestry_exhausted=false;
  void add(std::stop_token token) {
    if(cancellation_count>cancellations.size()){ancestry_exhausted=true;return;}
    if(!token.stop_possible())return;
    for(uint8_t i=0;i<cancellation_count;++i)if(cancellations[i]==token)return;
    if(cancellation_count==cancellations.size()){ancestry_exhausted=true;return;}
    cancellations[cancellation_count++]=std::move(token);
  }
  bool cancelled()const {
    if(ancestry_exhausted||cancellation_count>cancellations.size())return true;
    for(uint8_t i=0;i<cancellation_count;++i)if(cancellations[i].stop_requested())return true;
    return false;
  }
  bool expired()const{return deadline!=Clock::time_point{}&&Clock::now()>=deadline;}
  uint32_t remaining(uint32_t ceiling)const {
    if(cancelled()||expired())return 0;
    if(deadline==Clock::time_point{})return ceiling;
    const auto us=std::chrono::duration_cast<std::chrono::microseconds>(deadline-Clock::now()).count();
    return us<=0?0:uint32_t(std::min<uint64_t>(ceiling,(uint64_t(us)+999)/1000));
  }
  void inherit(const OperationContext &parent) {
    ancestry_exhausted=ancestry_exhausted||parent.ancestry_exhausted||parent.cancellation_count>parent.cancellations.size();
    for(std::size_t i=0;i<std::min<std::size_t>(parent.cancellation_count,parent.cancellations.size());++i)add(parent.cancellations[i]);
    if(parent.deadline!=Clock::time_point{}&&(deadline==Clock::time_point{}||parent.deadline<deadline))deadline=parent.deadline;
  }
};
inline thread_local const OperationContext *active_operation_context=nullptr;
inline OperationContext capture_operation_context(OperationContext local={}) {
  if(local.cancellation_count>local.cancellations.size())local.ancestry_exhausted=true;
  if(active_operation_context)local.inherit(*active_operation_context);
  return local;
}
class OperationContextScope {
  OperationContext context_;
  const OperationContext *previous_;
public:
  explicit OperationContextScope(OperationContext context,bool inherit=true):context_(inherit?capture_operation_context(std::move(context)):std::move(context)),previous_(active_operation_context){active_operation_context=&context_;}
  ~OperationContextScope(){active_operation_context=previous_;}
  OperationContextScope(const OperationContextScope &)=delete;
  OperationContextScope &operator=(const OperationContextScope &)=delete;
};
inline bool operation_cancellation_requested(){return active_operation_context&&active_operation_context->cancelled();}
inline uint32_t operation_remaining_timeout_ms(uint32_t ceiling){return active_operation_context?active_operation_context->remaining(ceiling):ceiling;}
}
