#pragma once
#include <cstdint>
#include <string>
namespace lunaredu::lunaros { struct ApplicationContext; }
namespace lunaredu::app {
class CapabilityRegistry;
struct ApplicationLaunch { uint64_t generation=0; };
enum class ApplicationLaunchState { Queued, Switching, Opened, Failed, Cancelled };
struct ApplicationLaunchSnapshot {
  ApplicationLaunchState state=ApplicationLaunchState::Failed;
  uint32_t transition_id=0;
  std::string application_id, error;
};
// Trusted native service host, never caller identity supplied by a remote
// payload. Returns a receipt; acceptance is not proof that the App opened.
// The owner executor copies strings before returning. No new worker is made.
bool request_application_launch(lunaros::ApplicationContext &, const char *source,
    const char *application_id, ApplicationLaunch &, std::string &error);
bool application_launch_snapshot(lunaros::ApplicationContext &, const char *source,
    ApplicationLaunch, ApplicationLaunchSnapshot &, std::string &error);
// Only queued work can be cancelled; an already-started transition must finish
// its checked cleanup/rollback. A stale receipt never cancels newer work.
bool cancel_application_launch(lunaros::ApplicationContext &, const char *source,
    ApplicationLaunch, std::string &error);
enum class ApplicationLaunchAction { Submit, Read, Cancel };
struct ApplicationLaunchRequest { ApplicationLaunchAction action=ApplicationLaunchAction::Read; char application_id[65]{}; ApplicationLaunch token; };
struct ApplicationLaunchResponse { ApplicationLaunch token; ApplicationLaunchSnapshot snapshot; };
bool declare_application_launch_capability(CapabilityRegistry &,lunaros::ApplicationContext &,std::string &);
}
