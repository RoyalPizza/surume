#pragma once
#include <string>

struct PlatformContext {
    void* hwnd               = nullptr;
    void* device             = nullptr;
    void* deviceContext      = nullptr;
    void* swapChain          = nullptr;
    void* mainRenderTargetView = nullptr;
    int   width              = 1280;
    int   height             = 800;
    bool  wantsQuit          = false;
    bool  pendingResize      = false;
    int   pendingWidth       = 0;
    int   pendingHeight      = 0;
};

namespace Platform {
    bool Init(PlatformContext& ctx, const char* title, int width, int height);
    void PumpMessages(PlatformContext& ctx);
    void HandleResize(PlatformContext& ctx);
    void NewFrame();
    void Present(PlatformContext& ctx);
    void Shutdown(PlatformContext& ctx);

    // Dialogs
    std::string OpenFolderDialog(void* parentHwnd, const char* title);

    // Shell actions
    void OpenInExplorer(const std::string& path);
    void OpenInTerminal(void* parentHwnd, const std::string& path);

    // App data directory: %APPDATA%\Gitaluna\ (created on first call)
    std::string GetAppDataDir();
}
