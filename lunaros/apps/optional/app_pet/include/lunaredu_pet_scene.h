#pragma once
#include "lunaredu_pet_frame_region.h"
#include <memory>
#include <cstdint>
#include <string>
namespace lunaredu::pet {
// Immutable semantic presentation. Contains no pixels, driver or OS canvas.
// A retained scene from a retired activation cannot draw into a later one.
struct SceneFrame;
using Scene = std::shared_ptr<const SceneFrame>;
struct FramePresentation;
struct SceneCanvasReuse {bool retained=false;FrameRegion overwritten{};};
bool paint_scene(const Scene &,PetFrameBuffer,FramePresentation &,SceneCanvasReuse = {});
struct SceneStatus {uint32_t published=0,pool_busy=0;};
SceneStatus scene_status();
// Physical diagnostic-session benchmark only. Bounded RAM override; never
// persists appearance or opens a Factory session. Exit/reset cancels it.
bool set_diagnostic_expression(const char *,uint32_t duration_ms,std::string &);
void reset_diagnostic_expression();
}
