#include "icon_texture.hpp"
#include <d3d11.h>
#include <dxgi.h>
#include <vector>
#include "resource.h"
#include "state.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace plugin {

static ID3D11ShaderResourceView* g_srv = nullptr;

// The game's boon icons are 32x32. ImGui samples linearly, so blow the pixels up
// on the CPU first; the result stays sharp instead of smearing at large sizes.
static std::vector<unsigned char> upscale_nearest(const unsigned char* src, int w, int h, int k) {
    std::vector<unsigned char> out((size_t)w * k * h * k * 4);
    for (int y = 0; y < h * k; ++y)
        for (int x = 0; x < w * k; ++x)
            memcpy(&out[((size_t)y * w * k + x) * 4], &src[((size_t)(y / k) * w + x / k) * 4], 4);
    return out;
}

bool icon_init(void* swapchain) {
    if (g_srv || !swapchain) return g_srv != nullptr;

    HRSRC res = FindResourceA(g_self, MAKEINTRESOURCEA(IDR_ICON_STABILITY), (LPCSTR)RT_RCDATA);
    if (!res) return false;
    HGLOBAL mem = LoadResource(g_self, res);
    const unsigned char* png = mem ? (const unsigned char*)LockResource(mem) : nullptr;
    if (!png) return false;

    int w = 0, h = 0, comp = 0;
    unsigned char* rgba = stbi_load_from_memory(png, (int)SizeofResource(g_self, res), &w, &h, &comp, 4);
    if (!rgba) return false;
    const int k = 8;
    std::vector<unsigned char> pixels = upscale_nearest(rgba, w, h, k);
    stbi_image_free(rgba);

    ID3D11Device* dev = nullptr;
    if (FAILED(((IDXGISwapChain*)swapchain)->GetDevice(__uuidof(ID3D11Device), (void**)&dev)) || !dev)
        return false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = (UINT)(w * k);
    desc.Height = (UINT)(h * k);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = pixels.data();
    init.SysMemPitch = desc.Width * 4;

    ID3D11Texture2D* tex = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&desc, &init, &tex)) && tex) {
        dev->CreateShaderResourceView(tex, nullptr, &g_srv);
        tex->Release();
    }
    dev->Release();
    return g_srv != nullptr;
}

void* icon_texture() { return g_srv; }

void icon_release() {
    if (g_srv) { g_srv->Release(); g_srv = nullptr; }
}

}  // namespace plugin

namespace plugin {

static IconLoader g_loader = nullptr;

void set_icon_loader(IconLoader loader) { g_loader = loader; }

void* boon_icon_texture(uint32_t boon_id) {
    if (boon_id == 1122 && g_srv) return g_srv;   // embedded, pre-upscaled copy looks better than the download
    return g_loader ? g_loader(boon_id) : nullptr;
}

}  // namespace plugin
