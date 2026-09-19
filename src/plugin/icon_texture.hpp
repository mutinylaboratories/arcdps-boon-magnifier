#pragma once
#include <cstdint>

namespace plugin {

// Embedded Stability icon (resource), created on the host's D3D11 device.
// swapchain: IDXGISwapChain* from the host (D3D11 only). Safe to call repeatedly.
bool  icon_init(void* swapchain);
void* icon_texture();   // ID3D11ShaderResourceView*, or null when unavailable
void  icon_release();

// Icons for any boon in the catalogue. Under Nexus its texture API downloads the
// official 32x32 icon from the wiki (set by entry_nexus); elsewhere only Stability's
// embedded texture exists and the overlay draws a labelled badge for the rest.
using IconLoader = void* (*)(uint32_t boon_id);   // returns ID3D11ShaderResourceView* or null (may be null while loading)
void  set_icon_loader(IconLoader loader);
void* boon_icon_texture(uint32_t boon_id);

}  // namespace plugin
