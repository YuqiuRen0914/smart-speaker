#include "interaction/conversation/suspension_policy.h"
#include <cassert>
#include <cstring>
using namespace lunaredu::pet::conversation;
int main() {
 for(auto first : {SuspensionReason::AudioFocus, SuspensionReason::CloudMaintenance}) {
   const auto second = first == SuspensionReason::AudioFocus ? SuspensionReason::CloudMaintenance : SuspensionReason::AudioFocus;
   SuspensionPolicy policy;
   assert(!policy.suspended());
   policy.set(first,true);policy.set(second,true);
   assert(std::strcmp(policy.detail(),"audio_focus,cloud_maintenance")==0);
   policy.set(first,false);assert(policy.suspended());
   policy.set(first,false);assert(policy.suspended()); // Repeated rollback only releases its owner.
   policy.set(second,false);assert(!policy.suspended());
 }
 SuspensionPolicy policy;
 policy.set(SuspensionReason::CloudMaintenance,true);
 policy.set(SuspensionReason::AudioFocus,true);
 policy.set(SuspensionReason::CloudMaintenance,false); // Failure/timeout path
 assert(policy.suspended() && std::strcmp(policy.detail(),"audio_focus")==0);
}
