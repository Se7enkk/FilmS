// Explicit boundaries for effects added upstream after the first Windows port.
// Included only by Windows call sites; macOS retains its upstream behavior.
#pragma once
#include "params.hpp"

namespace spk {

inline const char* windows_unported_error(bool di, bool latitude, bool mask,
                                         bool /*overscan*/, bool /*date*/) {
    if (di) return "digital intermediate is not implemented by the Windows Vulkan backend yet";
    if (latitude) return "scene latitude mapping is not implemented by the Windows Vulkan backend yet";
    if (mask) return "contrast mask is not implemented by the Windows Vulkan backend yet";
    // overscan and date_imprint are now implemented via Vulkan shaders + CPU rasterise.
    return nullptr;
}

inline const char* windows_unported_error(const Params& p) {
    return windows_unported_error(p.io.digital_intermediate, p.camera.scene_latitude.active,
                                  p.print_render.contrast_mask.active,
                                  p.film_render.overscan.active, p.film_render.date_imprint.active);
}

// Validate an already schema-checked delta before it mutates the session.
inline const char* windows_unported_delta_error(const Json& delta) {
    return windows_unported_error(delta.at("digital_intermediate").as_bool(),
                                  delta.at("scene_latitude_active").as_bool(),
                                  delta.at("contrast_mask_active").as_bool(),
                                  /*overscan=*/false,
                                  /*date=*/false);
}

}  // namespace spk
