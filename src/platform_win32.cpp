#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>

#include "Platform.h"
#include "Log.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Must be forward-declared manually — intentionally excluded from imgui_impl_win32.h to avoid pulling in <windows.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static PlatformContext* g_ctx = nullptr;

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_ctx && wParam != SIZE_MINIMIZED) {
            LOG_DEBUG("WM_SIZE: %dx%d", (int)LOWORD(lParam), (int)HIWORD(lParam));
            g_ctx->pendingResize = true;
            g_ctx->pendingWidth  = LOWORD(lParam);
            g_ctx->pendingHeight = HIWORD(lParam);
        } else if (wParam == SIZE_MINIMIZED) {
            LOG_DEBUG("WM_SIZE: minimized");
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        LOG_INFO("WM_DESTROY received");
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

static bool CreateD3DDevice(PlatformContext& ctx) {
    LOG_INFO("Creating D3D11 device and swap chain");

    DXGI_SWAP_CHAIN_DESC sd       = {};
    sd.BufferCount                = 2;
    sd.BufferDesc.Format          = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate     = { 60, 1 };
    sd.Flags                      = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow               = static_cast<HWND>(ctx.hwnd);
    sd.SampleDesc                 = { 1, 0 };
    sd.Windowed                   = TRUE;
    sd.SwapEffect                 = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#ifdef GITALUNA_DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
    LOG_DEBUG("D3D11_CREATE_DEVICE_DEBUG flag set");
#endif

    ID3D11Device*        device    = nullptr;
    ID3D11DeviceContext* deviceCtx = nullptr;
    IDXGISwapChain*      swapChain = nullptr;
    D3D_FEATURE_LEVEL    featureLevel;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, levels, 1, D3D11_SDK_VERSION,
        &sd, &swapChain, &device, &featureLevel, &deviceCtx);

    if (FAILED(hr)) {
        LOG_ERROR("D3D11CreateDeviceAndSwapChain failed: HRESULT 0x%08X", (unsigned)hr);
        return false;
    }

    LOG_INFO("D3D11 device created — feature level 0x%04X", (unsigned)featureLevel);
    ctx.device        = device;
    ctx.deviceContext = deviceCtx;
    ctx.swapChain     = swapChain;
    return true;
}

static bool CreateRenderTarget(PlatformContext& ctx) {
    LOG_DEBUG("Creating render target view");
    auto* swapChain = static_cast<IDXGISwapChain*>(ctx.swapChain);
    auto* device    = static_cast<ID3D11Device*>(ctx.device);

    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr) || !backBuffer) {
        LOG_ERROR("GetBuffer failed: HRESULT 0x%08X", (unsigned)hr);
        return false;
    }

    ID3D11RenderTargetView* rtv = nullptr;
    hr = device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
    backBuffer->Release();

    if (FAILED(hr) || !rtv) {
        LOG_ERROR("CreateRenderTargetView failed: HRESULT 0x%08X", (unsigned)hr);
        return false;
    }

    LOG_DEBUG("Render target view created");
    ctx.mainRenderTargetView = rtv;
    return true;
}

static void DestroyRenderTarget(PlatformContext& ctx) {
    if (ctx.mainRenderTargetView) {
        LOG_DEBUG("Destroying render target view");
        static_cast<ID3D11RenderTargetView*>(ctx.mainRenderTargetView)->Release();
        ctx.mainRenderTargetView = nullptr;
    }
}

bool Platform::Init(PlatformContext& ctx, const char* title, int width, int height) {
    LOG_INFO("Platform::Init begin — title='%s' size=%dx%d", title, width, height);
    g_ctx = &ctx;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LOG_DEBUG("CoInitializeEx result: 0x%08X", (unsigned)hr);

    wchar_t wtitle[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);

    WNDCLASSEXW wc  = {};
    wc.cbSize       = sizeof(wc);
    wc.style        = CS_CLASSDC;
    wc.lpfnWndProc  = WndProc;
    wc.hInstance    = GetModuleHandleW(nullptr);
    wc.hCursor      = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"GitalunaWnd";

    if (!RegisterClassExW(&wc)) {
        LOG_ERROR("RegisterClassExW failed: GetLastError=%u", (unsigned)GetLastError());
        return false;
    }
    LOG_DEBUG("Window class registered");

    HWND hwnd = CreateWindowExW(
        0, L"GitalunaWnd", wtitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!hwnd) {
        LOG_ERROR("CreateWindowExW failed: GetLastError=%u", (unsigned)GetLastError());
        return false;
    }
    LOG_INFO("Win32 window created — HWND=0x%p", (void*)hwnd);
    ctx.hwnd   = hwnd;
    ctx.width  = width;
    ctx.height = height;

    if (!CreateD3DDevice(ctx)) return false;
    if (!CreateRenderTarget(ctx)) return false;

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    LOG_INFO("Window shown");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    LOG_INFO("ImGui context created — version %s", ImGui::GetVersion());

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    LOG_DEBUG("ImGui style set to Dark");

    ImGui_ImplWin32_Init(hwnd);
    LOG_DEBUG("ImGui Win32 backend initialized");

    ImGui_ImplDX11_Init(
        static_cast<ID3D11Device*>(ctx.device),
        static_cast<ID3D11DeviceContext*>(ctx.deviceContext));
    LOG_DEBUG("ImGui DX11 backend initialized");

    LOG_INFO("Platform::Init complete");
    return true;
}

void Platform::PumpMessages(PlatformContext& ctx) {
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) {
            LOG_INFO("PumpMessages: WM_QUIT — setting wantsQuit");
            ctx.wantsQuit = true;
        }
    }
}

void Platform::HandleResize(PlatformContext& ctx) {
    if (!ctx.pendingResize) return;
    LOG_INFO("HandleResize: %dx%d -> %dx%d",
        ctx.width, ctx.height, ctx.pendingWidth, ctx.pendingHeight);
    ctx.pendingResize = false;
    ctx.width         = ctx.pendingWidth;
    ctx.height        = ctx.pendingHeight;

    DestroyRenderTarget(ctx);
    HRESULT hr = static_cast<IDXGISwapChain*>(ctx.swapChain)->ResizeBuffers(
        0, (UINT)ctx.pendingWidth, (UINT)ctx.pendingHeight, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        LOG_ERROR("ResizeBuffers failed: HRESULT 0x%08X", (unsigned)hr);
    }
    CreateRenderTarget(ctx);
    LOG_DEBUG("Resize complete");
}

void Platform::NewFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Platform::Present(PlatformContext& ctx) {
    ImGui::Render();

    auto* dc  = static_cast<ID3D11DeviceContext*>(ctx.deviceContext);
    auto* rtv = static_cast<ID3D11RenderTargetView*>(ctx.mainRenderTargetView);
    const float clearColor[] = { 0.11f, 0.11f, 0.11f, 1.0f };
    dc->OMSetRenderTargets(1, &rtv, nullptr);
    dc->ClearRenderTargetView(rtv, clearColor);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    HRESULT hr = static_cast<IDXGISwapChain*>(ctx.swapChain)->Present(1, 0);
    if (FAILED(hr)) {
        LOG_ERROR("Present failed: HRESULT 0x%08X", (unsigned)hr);
    }
}

void Platform::Shutdown(PlatformContext& ctx) {
    LOG_INFO("Platform::Shutdown begin");

    ImGui_ImplDX11_Shutdown();
    LOG_DEBUG("ImGui DX11 backend shut down");
    ImGui_ImplWin32_Shutdown();
    LOG_DEBUG("ImGui Win32 backend shut down");
    ImGui::DestroyContext();
    LOG_DEBUG("ImGui context destroyed");

    DestroyRenderTarget(ctx);

    if (ctx.swapChain)     { static_cast<IDXGISwapChain*>(ctx.swapChain)->Release();           ctx.swapChain = nullptr;     LOG_DEBUG("SwapChain released"); }
    if (ctx.deviceContext) { static_cast<ID3D11DeviceContext*>(ctx.deviceContext)->Release();  ctx.deviceContext = nullptr; LOG_DEBUG("DeviceContext released"); }
    if (ctx.device)        { static_cast<ID3D11Device*>(ctx.device)->Release();                ctx.device = nullptr;        LOG_DEBUG("Device released"); }

    if (ctx.hwnd) {
        DestroyWindow(static_cast<HWND>(ctx.hwnd));
        UnregisterClassW(L"GitalunaWnd", GetModuleHandleW(nullptr));
        ctx.hwnd = nullptr;
        LOG_DEBUG("Window destroyed");
    }

    CoUninitialize();
    LOG_DEBUG("COM uninitialized");
    g_ctx = nullptr;
    LOG_INFO("Platform::Shutdown complete");
}

std::string Platform::OpenFolderDialog(void* parentHwnd, const char* title) {
    LOG_INFO("OpenFolderDialog: title='%s' parentHwnd=0x%p", title, parentHwnd);
    std::string result;

    IFileDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (FAILED(hr)) {
        LOG_ERROR("CoCreateInstance(IFileDialog) failed: HRESULT 0x%08X", (unsigned)hr);
        return result;
    }
    LOG_DEBUG("IFileDialog created");

    DWORD options = 0;
    pfd->GetOptions(&options);
    pfd->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

    wchar_t wtitle[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);
    pfd->SetTitle(wtitle);

    LOG_DEBUG("Calling IFileDialog::Show — UI will block here");
    hr = pfd->Show(static_cast<HWND>(parentHwnd));

    if (SUCCEEDED(hr)) {
        LOG_DEBUG("IFileDialog::Show returned success — retrieving result");
        IShellItem* psi = nullptr;
        if (SUCCEEDED(pfd->GetResult(&psi))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                char narrow[MAX_PATH * 3] = {};
                WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr);
                result = narrow;
                CoTaskMemFree(path);
                LOG_INFO("OpenFolderDialog result: '%s'", result.c_str());
            } else {
                LOG_WARN("GetDisplayName failed");
            }
            psi->Release();
        } else {
            LOG_WARN("GetResult failed");
        }
    } else if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        LOG_INFO("OpenFolderDialog: user cancelled");
    } else {
        LOG_ERROR("IFileDialog::Show failed: HRESULT 0x%08X", (unsigned)hr);
    }

    pfd->Release();
    return result;
}

void Platform::OpenInExplorer(const std::string& path) {
    LOG_INFO("Platform::OpenInExplorer path='%s'", path.c_str());
    wchar_t wpath[MAX_PATH * 2] = {};
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath, MAX_PATH * 2);
    ShellExecuteW(nullptr, L"explore", wpath, nullptr, nullptr, SW_SHOWDEFAULT);
}

void Platform::OpenInTerminal(void* parentHwnd, const std::string& path) {
    LOG_INFO("Platform::OpenInTerminal path='%s'", path.c_str());
    wchar_t wpath[MAX_PATH * 2] = {};
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath, MAX_PATH * 2);

    // Try Windows Terminal first
    wchar_t wtArgs[MAX_PATH * 2] = {};
    swprintf_s(wtArgs, L"-d \"%s\"", wpath);
    HINSTANCE result = ShellExecuteW(static_cast<HWND>(parentHwnd), L"open", L"wt.exe", wtArgs, nullptr, SW_SHOWDEFAULT);
    if ((intptr_t)result > 32) {
        LOG_DEBUG("Opened Windows Terminal");
        return;
    }

    // Fall back to cmd.exe
    LOG_DEBUG("Windows Terminal not found, falling back to cmd.exe");
    ShellExecuteW(static_cast<HWND>(parentHwnd), L"open", L"cmd.exe", nullptr, wpath, SW_SHOWDEFAULT);
}

std::string Platform::GetAppDataDir() {
    wchar_t path[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        LOG_ERROR("Platform::GetAppDataDir SHGetFolderPathW failed");
        return "";
    }
    wcscat_s(path, MAX_PATH, L"\\Gitaluna\\");
    CreateDirectoryW(path, nullptr);

    char narrow[MAX_PATH * 2] = {};
    WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr);
    LOG_DEBUG("Platform::GetAppDataDir='%s'", narrow);
    return narrow;
}
