#pragma once

// Cross-talk between the two entry points when both hosts have loaded the DLL.
namespace plugin {

bool nexus_is_loaded();    // Nexus has priority for rendering and options while this is true
bool arcdps_is_loaded();   // arcdps' direct combat callback is live; Nexus events are redundant

}  // namespace plugin
