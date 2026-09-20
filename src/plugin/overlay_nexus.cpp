// Overlay UI built against ImGui 1.80 for Nexus.
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>
#include "icon_texture.hpp"
#include "imgui.h"
#include "overlay.hpp"
#include "realtime_source.hpp"
#include "state.hpp"

namespace plugin {
namespace ui180 {
#include "overlay_impl.inl"
}
}
