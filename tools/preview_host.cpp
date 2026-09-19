// Desktop preview: a bare D3D11 window that loads the plugin DLL the way arcdps
// does and feeds it looping fake stability events, so the overlay and options can
// be tried without launching the game.
// Usage: preview_host <plugin.dll> [--screenshot out.bmp]
#include <windows.h>
#include <mmsystem.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "arcdps_defs.hpp"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static IDXGISwapChain* g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;

static void* host_malloc(size_t n, void*) { return malloc(n); }
static void host_free(void* p, void*) { free(p); }

static void create_rtv() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}

static LRESULT WINAPI wnd_proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, msg, w, l)) return 1;
    if (msg == WM_SIZE && g_dev && w != SIZE_MINIMIZED) {
        if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
        g_swap->ResizeBuffers(0, LOWORD(l), HIWORD(l), DXGI_FORMAT_UNKNOWN, 0);
        create_rtv();
        return 0;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, msg, w, l);
}

static bool save_backbuffer_bmp(const char* path) {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    D3D11_TEXTURE2D_DESC d;
    back->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    g_dev->CreateTexture2D(&d, nullptr, &staging);
    g_ctx->CopyResource(staging, back);
    back->Release();
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(g_ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) { staging->Release(); return false; }

    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih);
    ih.biWidth = (LONG)d.Width;
    ih.biHeight = -(LONG)d.Height;   // top-down
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + d.Width * d.Height * 4;
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(&fh, sizeof(fh), 1, f);
        fwrite(&ih, sizeof(ih), 1, f);
        std::vector<unsigned char> row(d.Width * 4);
        for (UINT y = 0; y < d.Height; ++y) {
            const unsigned char* src = (const unsigned char*)m.pData + (size_t)y * m.RowPitch;
            for (UINT x = 0; x < d.Width; ++x) {   // RGBA -> BGRA
                row[x * 4 + 0] = src[x * 4 + 2];
                row[x * 4 + 1] = src[x * 4 + 1];
                row[x * 4 + 2] = src[x * 4 + 0];
                row[x * 4 + 3] = 255;
            }
            fwrite(row.data(), 1, row.size(), f);
        }
        fclose(f);
    }
    g_ctx->Unmap(staging, 0);
    staging->Release();
    return f != nullptr;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: preview_host <plugin.dll> [--screenshot out.bmp]\n"); return 2; }
    const char* shot = (argc >= 4 && !strcmp(argv[2], "--screenshot")) ? argv[3] : nullptr;

    WNDCLASSEXA wc{sizeof(wc), CS_CLASSDC, wnd_proc, 0, 0, GetModuleHandle(nullptr)};
    wc.lpszClassName = "BoonMagnifierPreview";
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "Boon Magnifier preview", WS_OVERLAPPEDWINDOW,
                              100, 100, 1280, 720, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                               D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx);
    if (FAILED(hr))   // no GPU (CI, remote session): fall back to the software rasterizer
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                           D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx);
    if (FAILED(hr)) { std::printf("D3D11 init failed\n"); return 1; }
    create_rtv();
    ShowWindow(hwnd, shot ? SW_SHOWNOACTIVATE : SW_SHOWDEFAULT);

    ImGui::SetAllocatorFunctions(host_malloc, host_free);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = shot ? nullptr : "preview_host_imgui.ini";
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) { std::printf("could not load %s\n", argv[1]); return 1; }
    using init_fn = void* (*)(char*, void*, void*, HANDLE, void*, void*, uint32_t);
    auto get_init = (init_fn)GetProcAddress(dll, "get_init_addr");
    auto get_release = (void* (*)())GetProcAddress(dll, "get_release_addr");
    char ver[] = "preview";
    auto* ex = ((arcdps_exports* (*)())get_init(ver, ctx, g_swap, nullptr, (void*)host_malloc,
                                                (void*)host_free, 11))();
    if (!ex || !ex->sig) { std::printf("plugin refused to load\n"); return 1; }
    auto combat = (uintptr_t (*)(cbtevent*, ag*, ag*, const char*, uint64_t, uint64_t))ex->combat;
    auto imgui_cb = (uintptr_t (*)(uint32_t, uint32_t))ex->imgui;
    auto options = (uintptr_t (*)())ex->options_end;

    if (auto status = (const char* (*)())GetProcAddress(dll, "boon_magnifier_status"))
        std::printf("realtime source: %s\n", status());

    ag me{"Preview", 0x1000, 1, 0, 1, 0};
    uint32_t next_id = 1;
    auto apply = [&](int32_t ms) {
        cbtevent ev{};
        ev.time = timeGetTime();
        ev.is_statechange = CBTS_BUFFAPPLY;
        ev.skillid = 1122;
        ev.value = ms;
        uint32_t id = next_id++;
        std::memcpy(&ev.pad61, &id, 4);
        combat(&ev, &me, &me, "Stability", 2, 1);
    };

    ULONGLONG next_cast = 0;
    int frames = 0;
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        // Every 10s: three stacks of different lengths, like a real stab rotation.
        if (GetTickCount64() >= next_cast) {
            apply(shot ? 7400 : 8000); apply(5000); apply(3000);
            next_cast = GetTickCount64() + 10000;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        imgui_cb(1, 0);
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::Begin("Boon Magnifier options (as shown in arcdps / Nexus)", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize);
        options();
        ImGui::End();
        ImGui::Render();

        const float clear[4] = {0.16f, 0.22f, 0.18f, 1.0f};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (shot && ++frames == 30) {
            bool ok = save_backbuffer_bmp(shot);
            std::printf(ok ? "wrote %s\n" : "screenshot failed: %s\n", shot);
            running = false;
        }
        g_swap->Present(1, 0);
    }

    ((uintptr_t (*)())get_release())();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(ctx);
    FreeLibrary(dll);
    if (g_rtv) g_rtv->Release();
    g_swap->Release();
    g_ctx->Release();
    g_dev->Release();
    DestroyWindow(hwnd);
    return 0;
}
