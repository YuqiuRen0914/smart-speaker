#pragma once
namespace lunaredu::app {
enum class AppControllerState {
  Stopped,
  Switching,
  Running,
  Error,
};

enum class AppTransitionPhase {
  Idle,
  StartingTarget,
  RestoringPrevious,
  StoppingFailedTarget,
  StoppingFailedRestore,
};
struct AppControllerProgress { AppControllerState state; AppTransitionPhase transition_phase; };
}
