#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>

#include "log.hpp"
#include "git.hpp"
#include "gui.hpp"
#include "persistence.hpp"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Must be forward-declared manually — intentionally excluded from imgui_impl_win32.h to avoid pulling in <windows.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct Window {
    HWND                    hwnd                 = nullptr;
    ID3D11Device*           device               = nullptr;
    ID3D11DeviceContext*    deviceContext        = nullptr;
    IDXGISwapChain*         swapChain            = nullptr;
    ID3D11RenderTargetView* mainRenderTargetView = nullptr;
    int                     width                = 1280;
    int                     height               = 800;
    bool                    wantsQuit            = false;
    bool                    pendingResize        = false;
    int                     pendingWidth         = 0;
    int                     pendingHeight        = 0;
};

// Single window; WndProc needs to reach it
static Window g_window;

// -------------------------------------------------------------------------
// Window and D3D11
// -------------------------------------------------------------------------

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            LOG_DEBUG("WM_SIZE: %dx%d", (int)LOWORD(lParam), (int)HIWORD(lParam));
            g_window.pendingResize = true;
            g_window.pendingWidth  = LOWORD(lParam);
            g_window.pendingHeight = HIWORD(lParam);
        } else {
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

static bool CreateD3DDevice(Window& win) {
    LOG_INFO("Creating D3D11 device and swap chain");

    DXGI_SWAP_CHAIN_DESC sd       = {};
    sd.BufferCount                = 2;
    sd.BufferDesc.Format          = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate     = { 60, 1 };
    sd.Flags                      = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow               = win.hwnd;
    sd.SampleDesc                 = { 1, 0 };
    sd.Windowed                   = TRUE;
    sd.SwapEffect                 = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#ifdef SURUME_DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
    LOG_DEBUG("D3D11_CREATE_DEVICE_DEBUG flag set");
#endif

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, levels, 1, D3D11_SDK_VERSION,
        &sd, &win.swapChain, &win.device, &featureLevel, &win.deviceContext);

    if (FAILED(hr)) {
        LOG_ERROR("D3D11CreateDeviceAndSwapChain failed: HRESULT 0x%08X", (unsigned)hr);
        return false;
    }

    LOG_INFO("D3D11 device created — feature level 0x%04X", (unsigned)featureLevel);
    return true;
}

static bool CreateRenderTarget(Window& win) {
    LOG_DEBUG("Creating render target view");
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = win.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr) || !backBuffer) {
        LOG_ERROR("GetBuffer failed: HRESULT 0x%08X", (unsigned)hr);
        return false;
    }

    hr = win.device->CreateRenderTargetView(backBuffer, nullptr, &win.mainRenderTargetView);
    backBuffer->Release();

    if (FAILED(hr) || !win.mainRenderTargetView) {
        LOG_ERROR("CreateRenderTargetView failed: HRESULT 0x%08X", (unsigned)hr);
        return false;
    }

    LOG_DEBUG("Render target view created");
    return true;
}

static void DestroyRenderTarget(Window& win) {
    if (win.mainRenderTargetView) {
        LOG_DEBUG("Destroying render target view");
        win.mainRenderTargetView->Release();
        win.mainRenderTargetView = nullptr;
    }
}

static bool InitWindow(Window& win, const wchar_t* title, int width, int height) {
    LOG_INFO("InitWindow begin — size=%dx%d", width, height);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LOG_DEBUG("CoInitializeEx result: 0x%08X", (unsigned)hr);

    WNDCLASSEXW wc  = {};
    wc.cbSize       = sizeof(wc);
    wc.style        = CS_CLASSDC;
    wc.lpfnWndProc  = WndProc;
    wc.hInstance    = GetModuleHandleW(nullptr);
    wc.hCursor      = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SurumeWnd";

    if (!RegisterClassExW(&wc)) {
        LOG_ERROR("RegisterClassExW failed: GetLastError=%u", (unsigned)GetLastError());
        return false;
    }
    LOG_DEBUG("Window class registered");

    win.hwnd = CreateWindowExW(
        0, L"SurumeWnd", title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!win.hwnd) {
        LOG_ERROR("CreateWindowExW failed: GetLastError=%u", (unsigned)GetLastError());
        return false;
    }
    LOG_INFO("Win32 window created — HWND=0x%p", (void*)win.hwnd);
    win.width  = width;
    win.height = height;

    if (!CreateD3DDevice(win)) return false;
    if (!CreateRenderTarget(win)) return false;

    ShowWindow(win.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(win.hwnd);
    LOG_INFO("Window shown");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    LOG_INFO("ImGui context created — version %s", ImGui::GetVersion());

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(win.hwnd);
    ImGui_ImplDX11_Init(win.device, win.deviceContext);
    LOG_INFO("InitWindow complete");
    return true;
}

static void PumpMessages(Window& win) {
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) {
            LOG_INFO("PumpMessages: WM_QUIT — setting wantsQuit");
            win.wantsQuit = true;
        }
    }
}

static void HandleResize(Window& win) {
    if (!win.pendingResize) return;
    LOG_INFO("HandleResize: %dx%d -> %dx%d",
        win.width, win.height, win.pendingWidth, win.pendingHeight);
    win.pendingResize = false;
    win.width         = win.pendingWidth;
    win.height        = win.pendingHeight;

    DestroyRenderTarget(win);
    HRESULT hr = win.swapChain->ResizeBuffers(
        0, (UINT)win.pendingWidth, (UINT)win.pendingHeight, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        LOG_ERROR("ResizeBuffers failed: HRESULT 0x%08X", (unsigned)hr);
    }
    CreateRenderTarget(win);
}

static void NewFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

static void Present(Window& win) {
    ImGui::Render();

    const float clearColor[] = { 0.11f, 0.11f, 0.11f, 1.0f };
    win.deviceContext->OMSetRenderTargets(1, &win.mainRenderTargetView, nullptr);
    win.deviceContext->ClearRenderTargetView(win.mainRenderTargetView, clearColor);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    HRESULT hr = win.swapChain->Present(1, 0);
    if (FAILED(hr)) {
        LOG_ERROR("Present failed: HRESULT 0x%08X", (unsigned)hr);
    }
}

static void ShutdownWindow(Window& win) {
    LOG_INFO("ShutdownWindow begin");

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    DestroyRenderTarget(win);
    if (win.swapChain)     { win.swapChain->Release();     win.swapChain     = nullptr; }
    if (win.deviceContext) { win.deviceContext->Release(); win.deviceContext = nullptr; }
    if (win.device)        { win.device->Release();        win.device        = nullptr; }

    if (win.hwnd) {
        DestroyWindow(win.hwnd);
        UnregisterClassW(L"SurumeWnd", GetModuleHandleW(nullptr));
        win.hwnd = nullptr;
    }

    CoUninitialize();
    LOG_INFO("ShutdownWindow complete");
}

// -------------------------------------------------------------------------
// Shell
// -------------------------------------------------------------------------

// Returns the chosen folder as UTF-8, or empty if cancelled or failed.
static std::string OpenFolderDialog(HWND parent, const wchar_t* title) {
    LOG_INFO("OpenFolderDialog");
    std::string result;

    IFileDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (FAILED(hr)) {
        LOG_ERROR("CoCreateInstance(IFileDialog) failed: HRESULT 0x%08X", (unsigned)hr);
        return result;
    }

    DWORD options = 0;
    pfd->GetOptions(&options);
    pfd->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pfd->SetTitle(title);

    hr = pfd->Show(parent);
    if (SUCCEEDED(hr)) {
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

static void OpenInExplorer(const std::string& path) {
    LOG_INFO("OpenInExplorer path='%s'", path.c_str());
    wchar_t wpath[MAX_PATH * 2] = {};
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath, MAX_PATH * 2);
    ShellExecuteW(nullptr, L"explore", wpath, nullptr, nullptr, SW_SHOWDEFAULT);
}

static void OpenInTerminal(HWND parent, const std::string& path) {
    LOG_INFO("OpenInTerminal path='%s'", path.c_str());
    wchar_t wpath[MAX_PATH * 2] = {};
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath, MAX_PATH * 2);

    // Try Windows Terminal first
    wchar_t wtArgs[MAX_PATH * 2] = {};
    swprintf_s(wtArgs, L"-d \"%s\"", wpath);
    HINSTANCE result = ShellExecuteW(parent, L"open", L"wt.exe", wtArgs, nullptr, SW_SHOWDEFAULT);
    if ((intptr_t)result > 32) {
        LOG_DEBUG("Opened Windows Terminal");
        return;
    }

    LOG_DEBUG("Windows Terminal not found, falling back to cmd.exe");
    ShellExecuteW(parent, L"open", L"cmd.exe", nullptr, wpath, SW_SHOWDEFAULT);
}

// Returns %APPDATA%\Surume\ as UTF-8 (created if missing), or empty on failure.
static std::string GetAppDataDir() {
    wchar_t path[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path)))
        return "";
    wcscat_s(path, MAX_PATH, L"\\Surume\\");
    CreateDirectoryW(path, nullptr);

    char narrow[MAX_PATH * 2] = {};
    WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr);
    return narrow;
}

// -------------------------------------------------------------------------
// App
// -------------------------------------------------------------------------

// Make path the active repository if it is a valid repo root. Moves it to the front of the
// recent list and refreshes repository state. Returns false (state unchanged) if invalid.
static bool OpenRepo(AppState& state, const std::string& path) {
    std::string root = Git::GetRepoRoot(path);
    if (root.empty()) {
        LOG_WARN("OpenRepo: '%s' is not a valid git repo root", path.c_str());
        return false;
    }

    state.activeRepo = root;
    size_t slash = root.find_last_of("\\/");
    state.activeRepoName = (slash != std::string::npos) ? root.substr(slash + 1) : root;
    state.recentRepos.erase(
        std::remove(state.recentRepos.begin(), state.recentRepos.end(), root),
        state.recentRepos.end());
    state.recentRepos.insert(state.recentRepos.begin(), root);
    state.statusMessage.clear();
    Git::RefreshRepoState(state);
    LOG_INFO("OpenRepo: name='%s' branch='%s'",
        state.activeRepoName.c_str(), state.currentBranch.c_str());
    return true;
}

static void InitApp(AppState& state, const std::string& appDataDir) {
    state.gitExePath = Git::FindGitExecutable();
    state.gitFound   = !state.gitExePath.empty();
    if (!state.gitFound)
        LOG_WARN("Git not found — repository actions will be disabled");

    Persistence::Load(state, appDataDir);

    // Restore the last active repo
    if (state.activeRepo.empty()) return;
    std::string last = state.activeRepo;
    state.activeRepo.clear();
    if (!state.gitFound) {
        LOG_WARN("InitApp: skipping repo restore — git not found");
        return;
    }
    if (!OpenRepo(state, last))
        state.statusMessage = "Last repository is no longer available.";
}

// Handle requests the UI set during the frame. Runs after Present so blocking work
// (dialogs, git calls) doesn't stall a half-drawn frame.
static void HandleRequests(AppState& state, Window& win) {
    if (state.requestOpenRepo) {
        state.requestOpenRepo = false;
        std::string path = OpenFolderDialog(win.hwnd, L"Open Repository");
        if (!path.empty() && !OpenRepo(state, path))
            state.statusMessage = "Not a valid Git repository root.";
    }

    if (state.requestOpenInExplorer) {
        state.requestOpenInExplorer = false;
        OpenInExplorer(state.activeRepo);
    }

    if (state.requestOpenInTerminal) {
        state.requestOpenInTerminal = false;
        OpenInTerminal(win.hwnd, state.activeRepo);
    }

    if (state.requestDiff) {
        state.requestDiff = false;
        if (state.selectedFileIdx >= 0 && state.selectedFileIdx < (int)state.changedFiles.size()) {
            const FileStatus& f = state.changedFiles[state.selectedFileIdx];
            LOG_INFO("Fetching diff for '%s'", f.path.c_str());
            state.currentDiff = Git::GetDiff(state.activeRepo, f);
            LOG_INFO("Diff fetched — %d bytes", (int)state.currentDiff.size());
        }
    }

    if (!state.requestSwitchRepoPath.empty()) {
        std::string path = std::move(state.requestSwitchRepoPath);
        state.requestSwitchRepoPath.clear();
        LOG_INFO("Switch repo requested: '%s'", path.c_str());
        if (!OpenRepo(state, path))
            state.statusMessage = "Repository no longer available: " + path;
    }
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    std::string appDataDir = GetAppDataDir();
    Log::Init(appDataDir);
    LOG_INFO("=== Surume starting === appData='%s'", appDataDir.c_str());

    AppState state = {};

    if (!InitWindow(g_window, L"Surume", 1280, 800)) {
        LOG_ERROR("InitWindow failed — aborting");
        Log::Shutdown();
        return 1;
    }

    InitApp(state, appDataDir);
    LOG_INFO("InitApp complete — gitFound=%d activeRepo='%s'",
        (int)state.gitFound, state.activeRepo.c_str());

    int frameCount = 0;
    while (!g_window.wantsQuit) {
        PumpMessages(g_window);
        if (g_window.wantsQuit) {
            LOG_INFO("WM_QUIT received — exiting loop");
            break;
        }

        HandleResize(g_window);
        NewFrame();
        Gui::Render(state);
        Present(g_window);

        ++frameCount;
        if (frameCount % 600 == 0)
            LOG_DEBUG("Heartbeat — frame %d, activeRepo='%s', branch='%s'",
                frameCount, state.activeRepo.c_str(), state.currentBranch.c_str());

        HandleRequests(state, g_window);
    }

    LOG_INFO("Main loop exited after %d frames", frameCount);

    Persistence::Save(state, appDataDir);
    ShutdownWindow(g_window);

    LOG_INFO("=== Surume exiting cleanly ===");
    Log::Shutdown();
    return 0;
}
