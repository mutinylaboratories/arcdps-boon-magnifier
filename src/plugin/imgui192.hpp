#pragma once
// Dear ImGui 1.92.7 (what arcdps ships) wrapped in `namespace arc192`, so it can
// coexist in this DLL with the global-namespace ImGui 1.80 that Nexus requires.
// Every system header ImGui pulls in is included first, outside the namespace;
// their include guards then turn ImGui's own #includes into no-ops.
// A translation unit may include this OR the 1.80 "imgui.h", never both.
#include <windows.h>
#include <imm.h>
#include <shellapi.h>
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <immintrin.h>
#include <nmmintrin.h>
#include <new>

namespace arc192 {
#include "../../third_party/imgui192/imgui.h"
}
