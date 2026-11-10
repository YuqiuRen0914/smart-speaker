#include "app/lifecycle/foreground_recovery.h"
#include "app/lifecycle/app_controller.h"

namespace lunaredu::app {
bool ForegroundRecovery::quiesce(AppController *controller,const char *reason,const char *fallback,std::string &error){
  error.clear();
  if(captured_&&owner_!=controller){error="foreground_recovery_owner_mismatch";return false;}
  if(!controller||!controller->setup_started())return true;
  AppControllerStatusSnapshot status;controller->status_snapshot_into(status);
  if(!captured_){
    owner_=controller;captured_=true;
    destination_=status.active_application_id.empty()?(fallback?fallback:""):status.active_application_id;
  }
  const bool stopped=controller->stop(reason);
  controller->status_snapshot_into(status);
  expected_transition_=status.transition_id;
  if(!stopped){error="foreground_cleanup_pending";return false;}
  if(status.transition_phase!=AppTransitionPhase::Idle){error="foreground_transition_pending";return false;}
  return true;
}
bool ForegroundRecovery::restore(AppController *controller,std::string &error){
  error.clear();
  if(!captured_)return true;
  if(!controller||owner_!=controller){error="foreground_recovery_owner_mismatch";return false;}
  AppControllerStatusSnapshot status;controller->status_snapshot_into(status);
  // A subsequent explicit switch or stop supersedes our intent, including a
  // new selection that has already exited and left no active application.
  // Its controller owns any remaining transition and cleanup.
  if(status.transition_id!=expected_transition_){clear();return true;}
  if(status.transition_phase!=AppTransitionPhase::Idle){
    // Cleanup must advance even when restore gates the normal foreground poll.
    // A pending start is never replaced with another transition here.
    controller->tick();error="foreground_restore_pending";return false;
  }
  // A failed stop or a newer explicit foreground selection wins.
  if(!status.active_application_id.empty()||destination_.empty()){clear();return true;}
  const bool accepted=controller->switch_to(destination_,error);
  controller->status_snapshot_into(status);
  // A failed start can itself enter asynchronous rollback/cleanup. Retain that
  // attempt's generation so it is not mistaken for a newer user's selection.
  expected_transition_=status.transition_id;
  if(!accepted)return false;
  if(status.transition_phase!=AppTransitionPhase::Idle||status.active_application_id.empty()){
    error="foreground_restore_pending";return false;
  }
  clear();return true;
}
}
