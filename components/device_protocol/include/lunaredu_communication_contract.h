#pragma once
// Generated from the Platform communication-contracts snapshot SHA256 5cfb75099955011abe81a9a2cdd59a84f0d500d7ec071daa37bc7f3bcbbe59cb. Do not edit.
#include <string_view>
namespace lunaredu::communication {
inline constexpr bool is_terminal_activity(std::string_view state) {
  return state == "completed" || state == "failed" || state == "rejected" || state == "canceled" || state == "expired";
}
enum class connection_phase { pending, active, draining, closed };
inline constexpr bool allows_connection_transition(connection_phase from,connection_phase to) {
  switch(from) {
    case connection_phase::pending: return to==connection_phase::active || to==connection_phase::draining || to==connection_phase::closed;
    case connection_phase::active: return to==connection_phase::draining || to==connection_phase::closed;
    case connection_phase::draining: return to==connection_phase::closed;
    case connection_phase::closed: return false;
  }
  return false;
}
struct recovery_input { bool authorized,same_binding,same_epoch,checkpoint,idempotent,expired; };
inline constexpr std::string_view plan_recovery(std::string_view strategy,recovery_input input) {
  if(!input.authorized || !input.same_binding || input.expired)return "reject";
  if(strategy=="durable") {if(input.idempotent && input.same_epoch)return "retry-operation";return "reconcile";}
  if(strategy=="realtime") {return "snapshot";}
  if(strategy=="event") {if(input.same_epoch && input.checkpoint)return "cursor";return "snapshot";}
  if(strategy=="collaborative") {return "state-vector";}
  if(strategy=="media") {return "fresh-media";}
  if(strategy=="bulk") {if(input.same_epoch && input.checkpoint)return "verified-chunks";return "restart-transfer";}
  return "reject";
}
}
