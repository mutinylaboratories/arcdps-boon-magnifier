#pragma once
#include <string>

// Self-updater: asks GitHub for the newest release of this repository, and on request
// downloads its DLL (and signature file) beside the running one and swaps them in. The
// running module stays mapped until the game restarts, so an installed update is always
// "restart to apply". Nexus does the same thing on its own at launch; this exists for the
// plain-arcdps case and for a mid-session check from the options. All work runs on a
// background thread; the UI polls self_update_status().
namespace plugin {

struct SelfUpdateStatus {
    bool        busy = false;
    bool        available = false;   // a newer release exists
    bool        installed = false;   // it has been written beside the DLL; takes effect on restart
    std::string latest;              // e.g. "0.1.3"
    std::string text;                // one line for the options panel
};

// Starts a check (and an install when the release is newer and `install` is set); no-op while busy.
void             self_update_check(bool install);
SelfUpdateStatus self_update_status();
void             self_update_shutdown();   // joins the worker; call before the DLL unloads

}  // namespace plugin
