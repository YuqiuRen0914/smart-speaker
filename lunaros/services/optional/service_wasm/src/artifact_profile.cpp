#include "lunaros_wasm_app_backend.h"
namespace lunaredu::lunaros::apps {
const ApplicationArtifactProfile &wasm_aot_artifact_profile() {
  // Must match the pinned WAMR compiler and the protected AOT ABI.
  static constexpr ApplicationArtifactProfile profile{"xtensa", "esp32s3",
      "0982ec5e5ecb4ad17061d6538875417727c8b1ea", "lunaros.aot.poll/v1", 1, 6};
  return profile;
}
}
