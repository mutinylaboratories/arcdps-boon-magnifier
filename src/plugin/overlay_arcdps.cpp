// Overlay UI built against ImGui 1.92.7 (namespace arc192) for arcdps.
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>
#include "icon_texture.hpp"
#include "imgui192.hpp"
#include "overlay.hpp"
#include "realtime_source.hpp"
#include "state.hpp"

namespace plugin {
namespace ui192 {
using namespace arc192;
#include "overlay_impl.inl"
}
}
