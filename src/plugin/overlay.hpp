#pragma once

// The same UI compiled twice, once per ImGui version (see overlay_impl.inl).
// gameplay: false on character select / loading screens.
// big_font: optional ImFont* (of the matching ImGui version) for the host-font digit style.
namespace plugin {

namespace ui180 {   // Nexus, ImGui 1.80
void draw_overlay(bool gameplay, void* big_font);
void draw_options();
}

namespace ui192 {   // arcdps, ImGui 1.92.7
void draw_overlay(bool gameplay, void* big_font);
void draw_options();
}

}  // namespace plugin
