cmake_minimum_required(VERSION 3.20)
project(arcdps_boon_magnifier VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the DLL must load in GW2 without a VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CORE_SRC
    src/core/boon_tracker.cpp
    src/core/boon_events.cpp
    src/core/config.cpp
    src/core/realtime_merge.cpp
    src/core/sigscan.cpp
    src/core/pulse_projector.cpp)

# ---- Plugin DLL (one binary, loadable by both arcdps and Nexus) ----
# The hosts ship different Dear ImGui versions and a plugin must match its host's:
#   Nexus  -> 1.80   (third_party/imgui, global namespace)
#   arcdps -> 1.92.7 (third_party/imgui192, compiled inside namespace arc192 by imgui192_unity.cpp)
file(GLOB IMGUI180_SRC third_party/imgui/*.cpp)
file(GLOB IMGUI192_SRC third_party/imgui192/*.cpp)
add_library(arcdps_boon_magnifier SHARED
    src/plugin/entry_arcdps.cpp
    src/plugin/entry_nexus.cpp
    src/plugin/state.cpp
    src/plugin/overlay_nexus.cpp
    src/plugin/overlay_arcdps.cpp
    src/plugin/imgui192_unity.cpp
    src/plugin/icon_texture.cpp
    src/plugin/realtime_source.cpp
    src/plugin/realtime_gw2.cpp
    src/plugin/resource.rc
    ${CORE_SRC}
    ${IMGUI180_SRC})
set_target_properties(arcdps_boon_magnifier PROPERTIES PREFIX "" OUTPUT_NAME "arcdps_boon_magnifier")
target_include_directories(arcdps_boon_magnifier PRIVATE
    src/core src/plugin third_party/imgui third_party/nexus third_party/stb)
target_compile_definitions(arcdps_boon_magnifier PRIVATE
    PLUGIN_VERSION="${PROJECT_VERSION}"
    PLUGIN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PLUGIN_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PLUGIN_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(arcdps_boon_magnifier PRIVATE d3d11 dxgi winmm)

# ---- Core unit tests (no game needed) ----
add_executable(core_tests
    tests/doctest_main.cpp
    tests/test_tracker.cpp
    tests/test_events.cpp
    tests/test_config.cpp
    tests/test_realtime_merge.cpp
    tests/test_sigscan.cpp
    tests/test_pulse_projector.cpp
    ${CORE_SRC})
target_include_directories(core_tests PRIVATE src/core third_party/doctest)
enable_testing()
add_test(NAME core_tests COMMAND core_tests)

# ---- Headless fake hosts: load the real DLL and render frames (see tests/smoke_host.cpp) ----
add_executable(smoke_nexus tests/smoke_host.cpp ${IMGUI180_SRC})
target_include_directories(smoke_nexus PRIVATE src/core third_party/imgui third_party/nexus)
target_link_libraries(smoke_nexus PRIVATE winmm)
add_dependencies(smoke_nexus arcdps_boon_magnifier)
add_test(NAME smoke_nexus COMMAND smoke_nexus $<TARGET_FILE:arcdps_boon_magnifier>)

add_executable(smoke_arcdps tests/smoke_host.cpp ${IMGUI192_SRC})
target_include_directories(smoke_arcdps PRIVATE src/core third_party/imgui192 third_party/nexus)
target_compile_definitions(smoke_arcdps PRIVATE SMOKE_ARCDPS)
target_link_libraries(smoke_arcdps PRIVATE winmm)
add_dependencies(smoke_arcdps arcdps_boon_magnifier)
add_test(NAME smoke_arcdps COMMAND smoke_arcdps $<TARGET_FILE:arcdps_boon_magnifier>)

# ---- Desktop preview: D3D11 window acting as arcdps (ImGui 1.92.7) + fake stability events ----
add_executable(preview_host
    tools/preview_host.cpp
    third_party/imgui192/backends/imgui_impl_win32.cpp
    third_party/imgui192/backends/imgui_impl_dx11.cpp
    ${IMGUI192_SRC})
target_include_directories(preview_host PRIVATE src/core third_party/imgui192)
target_compile_definitions(preview_host PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(preview_host PRIVATE d3d11 dxgi d3dcompiler winmm)
add_dependencies(preview_host arcdps_boon_magnifier)
