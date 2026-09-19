// Compiles all of Dear ImGui 1.92.7 inside `namespace arc192` (see imgui192.hpp).
#define IMGUI_DEFINE_MATH_OPERATORS   // the .cpp files expect this set before imgui.h is first seen
#include "imgui192.hpp"

#ifdef _MSC_VER
#pragma comment(lib, "imm32")
#endif

namespace arc192 {
#include "../../third_party/imgui192/imgui.cpp"
#include "../../third_party/imgui192/imgui_draw.cpp"
#include "../../third_party/imgui192/imgui_tables.cpp"
#include "../../third_party/imgui192/imgui_widgets.cpp"
}
