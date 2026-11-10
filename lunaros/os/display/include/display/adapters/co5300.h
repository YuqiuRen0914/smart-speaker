#pragma once
#include "display/backend.h"
namespace lunaredu::device::co5300 { class Panel; }
namespace lunaredu::display {
// Panel must outlive the backend. Reacquire after stop/reinitialization.
ScreenBackend make_co5300_backend(device::co5300::Panel &panel);
}
