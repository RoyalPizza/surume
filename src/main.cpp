#include "git.hpp"
#include "gui.hpp"
#include "jobs.hpp"
#include "log.hpp"
#include "model.hpp"
#include "persistence.hpp"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// Deliberately not declared in imgui_impl_win32.h, to keep <windows.h> out of that header
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg,
    WPARAM wparam, LPARAM lparam);

constexpr int INITIAL_WIDTH = 1280;
constexpr int INITIAL_HEIGHT = 800;
constexpr UINT SWAP_CHAIN_BUFFER_COUNT = 2;
constexpr UINT REFRESH_RATE = 60;
constexpr float CLEAR_COLOR[4] = { 0.11f, 0.11f, 0.11f, 1.0f };
constexpr int HEARTBEAT_FRAMES = 600;
constexpr INT_PTR SHELL_EXECUTE_SUCCESS_MIN = 32; // ShellExecute returns > 32 on success
constexpr const wchar_t* WINDOW_CLASS_NAME = L"SurumeWnd";
constexpr int TERMINAL_ARGS_SIZE = MAX_PATH * 2;
constexpr WORD APP_ICON_ID = 1; // ICON resource in surume.rc
constexpr DWORD OCCLUDED_SLEEP_MILLISECONDS = 10;
constexpr float BASE_DPI = 96.0f;
constexpr float UI_FONT_SIZE = 16.0f; // also used for the monospace diff font
constexpr const wchar_t* UI_FONT_FILE = L"segoeui.ttf";
constexpr const wchar_t* MONO_FONT_FILE = L"consola.ttf";
constexpr const wchar_t* CJK_FONT_FILE = L"msyh.ttc"; // merged into both for CJK glyphs

struct Window
{
    HWND hwnd = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* device_context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* render_target_view = nullptr;
    int width = INITIAL_WIDTH;
    int height = INITIAL_HEIGHT;
    bool is_quit_requested = false;
    bool is_occluded = false; // minimized or fully covered; skip rendering
    bool has_pending_resize = false;
    int pending_width = 0;
    int pending_height = 0;
    float dpi_scale = 1.0f;
    float pending_dpi_scale = 0.0f; // non-zero when the window moved to a monitor with another DPI
};

// Single window; window_proc needs to reach it
static Window main_window;

// -------------------------------------------------------------------------
// Text conversion
// -------------------------------------------------------------------------

static std::string to_utf8(const wchar_t* text)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1)
    {
        return "";
    }

    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), len, nullptr, nullptr);
    utf8.pop_back(); // terminator written by WideCharToMultiByte
    return utf8;
}

// Convert UTF-8 to a null-terminated UTF-16 buffer for Win32 W functions.
static std::vector<wchar_t> to_wide(const std::string& text)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 0)
    {
        return std::vector<wchar_t>(1, L'\0');
    }

    std::vector<wchar_t> wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), len);
    return wide;
}

// -------------------------------------------------------------------------
// Window and D3D11
// -------------------------------------------------------------------------

static LRESULT WINAPI window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
    {
        return true;
    }

    switch (msg)
    {
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED)
            {
                main_window.has_pending_resize = true;
                main_window.pending_width = LOWORD(lparam);
                main_window.pending_height = HIWORD(lparam);
            }
            return 0;

        case WM_DPICHANGED:
        {
            // Move to the size Windows suggests for the new DPI; rescale on the next frame
            const RECT* suggested = (const RECT*)lparam;
            main_window.pending_dpi_scale = HIWORD(wparam) / BASE_DPI;
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_SYSCOMMAND:
            // Keep Alt from opening the (nonexistent) system menu bar
            if ((wparam & 0xfff0) == SC_KEYMENU)
            {
                return 0;
            }
            break;

        case WM_CLOSE:
            // Leave the main loop; window_shutdown destroys the window after D3D is released
            main_window.is_quit_requested = true;
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static bool create_device(Window& window)
{
    DXGI_SWAP_CHAIN_DESC description = {};
    description.BufferCount = SWAP_CHAIN_BUFFER_COUNT;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferDesc.RefreshRate = { REFRESH_RATE, 1 };
    description.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window.hwnd;
    description.SampleDesc = { 1, 0 };
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#ifdef SURUME_DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL feature_level;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, levels, 1, D3D11_SDK_VERSION, &description, &window.swap_chain,
        &window.device, &feature_level, &window.device_context);
    if (FAILED(hr))
    {
        LOG_ERROR("D3D11CreateDeviceAndSwapChain failed: HRESULT 0x%08X", (unsigned)hr);
        return false;
    }

    LOG_INFO("D3D11 device created, feature level 0x%04X", (unsigned)feature_level);
    return true;
}

static bool create_render_target(Window& window)
{
    ID3D11Texture2D* back_buffer = nullptr;
    HRESULT hr = window.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (FAILED(hr) || back_buffer == nullptr)
    {
        LOG_ERROR("GetBuffer failed: HRESULT 0x%08X", (unsigned)hr);
        return false;
    }

    hr = window.device->CreateRenderTargetView(back_buffer, nullptr, &window.render_target_view);
    back_buffer->Release();
    if (FAILED(hr) || window.render_target_view == nullptr)
    {
        LOG_ERROR("CreateRenderTargetView failed: HRESULT 0x%08X", (unsigned)hr);
        return false;
    }
    return true;
}

static void destroy_render_target(Window& window)
{
    if (window.render_target_view != nullptr)
    {
        window.render_target_view->Release();
        window.render_target_view = nullptr;
    }
}

// Path of a font in the Windows fonts folder as UTF-8, or empty if the file doesn't exist.
static std::string system_font_path(const wchar_t* file_name)
{
    wchar_t path[MAX_PATH] = {};
    UINT len = GetWindowsDirectoryW(path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
    {
        return "";
    }
    wcscat_s(path, MAX_PATH, L"\\Fonts\\");
    wcscat_s(path, MAX_PATH, file_name);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
    {
        return "";
    }
    return to_utf8(path);
}

// Add a font, with the CJK font merged in for characters it lacks. Returns false if the main
// font file is missing; ImGui's built-in font is used instead.
static bool add_font(const wchar_t* file_name)
{
    std::string path = system_font_path(file_name);
    if (path.empty())
    {
        LOG_WARN("font not found: %ls", file_name);
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF(path.c_str(), UI_FONT_SIZE);
    std::string cjk_path = system_font_path(CJK_FONT_FILE);
    if (!cjk_path.empty())
    {
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(cjk_path.c_str(), UI_FONT_SIZE, &merge);
    }
    return true;
}

// Fonts[0] is the UI font, Fonts[1] the monospace font the GUI uses for diffs.
static void load_fonts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (!add_font(UI_FONT_FILE))
    {
        io.Fonts->AddFontDefault();
    }
    add_font(MONO_FONT_FILE);
}

// Reset the style and scale every size and the fonts for the given DPI scale.
static void apply_dpi_scale(float scale)
{
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    ImGui::GetStyle() = style;
}

static bool window_init(Window& window, const wchar_t* title)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        LOG_ERROR("CoInitializeEx failed: HRESULT 0x%08X", (unsigned)hr);
        return false;
    }

    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_CLASSDC;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(window_class.hInstance, MAKEINTRESOURCEW(APP_ICON_ID));
    window_class.hIconSm = (HICON)LoadImageW(window_class.hInstance,
        MAKEINTRESOURCEW(APP_ICON_ID), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    window_class.lpszClassName = WINDOW_CLASS_NAME;
    if (!RegisterClassExW(&window_class))
    {
        LOG_ERROR("RegisterClassExW failed: %u", (unsigned)GetLastError());
        return false;
    }

    POINT origin = { 0, 0 };
    window.dpi_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY));
    window.width = (int)(INITIAL_WIDTH * window.dpi_scale);
    window.height = (int)(INITIAL_HEIGHT * window.dpi_scale);
    window.hwnd = CreateWindowExW(0, WINDOW_CLASS_NAME, title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, window.width, window.height,
        nullptr, nullptr, window_class.hInstance, nullptr);
    if (window.hwnd == nullptr)
    {
        LOG_ERROR("CreateWindowExW failed: %u", (unsigned)GetLastError());
        return false;
    }

    if (!create_device(window) || !create_render_target(window))
    {
        return false;
    }

    ShowWindow(window.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(window.hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // layout is app-owned and persisted in settings.toml
    apply_dpi_scale(window.dpi_scale);
    load_fonts();
    if (!ImGui_ImplWin32_Init(window.hwnd) ||
        !ImGui_ImplDX11_Init(window.device, window.device_context))
    {
        LOG_ERROR("ImGui backend init failed");
        return false;
    }

    LOG_INFO("window_init complete, ImGui %s, DPI scale %.2f", ImGui::GetVersion(),
        window.dpi_scale);
    return true;
}

static void pump_messages(Window& window)
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT)
        {
            window.is_quit_requested = true;
        }
    }
}

static void handle_dpi_change(Window& window)
{
    if (window.pending_dpi_scale == 0.0f)
    {
        return;
    }

    window.dpi_scale = window.pending_dpi_scale;
    window.pending_dpi_scale = 0.0f;
    apply_dpi_scale(window.dpi_scale);
    LOG_INFO("DPI scale changed to %.2f", window.dpi_scale);
}

static void handle_resize(Window& window)
{
    if (!window.has_pending_resize)
    {
        return;
    }

    LOG_INFO("handle_resize: %dx%d -> %dx%d",
        window.width, window.height, window.pending_width, window.pending_height);
    window.has_pending_resize = false;
    window.width = window.pending_width;
    window.height = window.pending_height;

    destroy_render_target(window);
    HRESULT hr = window.swap_chain->ResizeBuffers(0, (UINT)window.width, (UINT)window.height,
        DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
    {
        LOG_ERROR("ResizeBuffers failed: HRESULT 0x%08X", (unsigned)hr);
    }
    create_render_target(window);
}

static void begin_frame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

static void present(Window& window)
{
    ImGui::Render();

    window.device_context->OMSetRenderTargets(1, &window.render_target_view, nullptr);
    window.device_context->ClearRenderTargetView(window.render_target_view, CLEAR_COLOR);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    HRESULT hr = window.swap_chain->Present(1, 0);
    window.is_occluded = hr == DXGI_STATUS_OCCLUDED;
    if (FAILED(hr))
    {
        LOG_ERROR("Present failed: HRESULT 0x%08X", (unsigned)hr);
    }
}

// While minimized, Present returns immediately instead of waiting for vsync, so the loop
// would spin. Returns true if the window is still occluded and this frame should be skipped.
static bool is_still_occluded(Window& window)
{
    if (!window.is_occluded)
    {
        return false;
    }
    if (window.swap_chain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
    {
        Sleep(OCCLUDED_SLEEP_MILLISECONDS);
        return true;
    }
    window.is_occluded = false;
    return false;
}

static void window_shutdown(Window& window)
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    destroy_render_target(window);
    if (window.swap_chain != nullptr)
    {
        window.swap_chain->Release();
        window.swap_chain = nullptr;
    }
    if (window.device_context != nullptr)
    {
        window.device_context->Release();
        window.device_context = nullptr;
    }
    if (window.device != nullptr)
    {
        window.device->Release();
        window.device = nullptr;
    }

    if (window.hwnd != nullptr)
    {
        DestroyWindow(window.hwnd);
        window.hwnd = nullptr;
    }
    UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandleW(nullptr));
    CoUninitialize();
}

// -------------------------------------------------------------------------
// Shell
// -------------------------------------------------------------------------

// Show the folder picker. Returns false if cancelled or failed; folder is UTF-8.
static bool pick_folder(HWND parent, const wchar_t* title, std::string& folder)
{
    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(hr))
    {
        LOG_ERROR("CoCreateInstance(IFileDialog) failed: HRESULT 0x%08X", (unsigned)hr);
        return false;
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options)))
    {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    }
    dialog->SetTitle(title);

    bool is_picked = false;
    hr = dialog->Show(parent);
    if (SUCCEEDED(hr))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)))
        {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
            {
                folder = to_utf8(path);
                is_picked = true;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    else if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        LOG_ERROR("IFileDialog::Show failed: HRESULT 0x%08X", (unsigned)hr);
    }

    dialog->Release();
    return is_picked;
}

static void open_in_explorer(const std::string& path)
{
    LOG_INFO("open_in_explorer '%s'", path.c_str());
    std::vector<wchar_t> wide_path = to_wide(path);
    ShellExecuteW(nullptr, L"explore", wide_path.data(), nullptr, nullptr, SW_SHOWDEFAULT);
}

static void open_in_terminal(HWND parent, const std::string& path)
{
    LOG_INFO("open_in_terminal '%s'", path.c_str());
    std::vector<wchar_t> wide_path = to_wide(path);

    // Prefer Windows Terminal, fall back to cmd.exe
    wchar_t terminal_args[TERMINAL_ARGS_SIZE] = {};
    swprintf_s(terminal_args, L"-d \"%s\"", wide_path.data());
    HINSTANCE result = ShellExecuteW(parent, L"open", L"wt.exe", terminal_args, nullptr,
        SW_SHOWDEFAULT);
    if ((INT_PTR)result > SHELL_EXECUTE_SUCCESS_MIN)
    {
        return;
    }

    LOG_DEBUG("Windows Terminal not found, falling back to cmd.exe");
    ShellExecuteW(parent, L"open", L"cmd.exe", nullptr, wide_path.data(), SW_SHOWDEFAULT);
}

// Returns %APPDATA%\Surume\ as UTF-8, creating it if missing. Empty on failure.
static std::string get_app_data_dir()
{
    wchar_t path[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path)))
    {
        return "";
    }

    wcscat_s(path, MAX_PATH, L"\\Surume\\");
    CreateDirectoryW(path, nullptr);
    return to_utf8(path);
}

// -------------------------------------------------------------------------
// App
// -------------------------------------------------------------------------

static Job make_job(const AppState& state, JobKind kind)
{
    Job job;
    job.kind = kind;
    job.generation = state.repo_generation;
    job.repo_path = state.active_repo;
    return job;
}

// Validate and load path on the worker. failure_message is shown if it isn't a repo root.
static void queue_open_repo(const AppState& state, const std::string& path,
    const std::string& failure_message)
{
    Job job = make_job(state, JobKind::OPEN_REPO);
    job.text = path;
    job.text2 = failure_message;
    jobs_push(job);
}

static void push_request(AppState& state, RequestKind kind)
{
    Request request;
    request.kind = kind;
    state.requests.push_back(request);
}

static void app_init(AppState& state, const std::string& app_data_dir)
{
    persistence_load(state, app_data_dir);
    jobs_push(make_job(state, JobKind::FIND_GIT));

    // Restore the last active repository once the worker has checked for Git
    if (!state.active_repo.empty())
    {
        std::string last_repo = state.active_repo;
        state.active_repo.clear();
        queue_open_repo(state, last_repo, "Last repository is no longer available.");
    }
}

static void queue_diff(const AppState& state)
{
    for (const FileStatus& file : state.repo.files)
    {
        if (file.path == state.selected_file_path)
        {
            Job job = make_job(state, JobKind::LOAD_DIFF);
            job.file = file;
            jobs_push(job);
            return;
        }
    }
}

// Take a new snapshot. Keep the file selection if the file is still changed and reload its
// diff, since the file may have changed too.
static void apply_snapshot(AppState& state, const RepoSnapshot& snapshot)
{
    state.repo = snapshot;
    if (state.selected_file_path.empty())
    {
        return;
    }

    for (const FileStatus& file : state.repo.files)
    {
        if (file.path == state.selected_file_path)
        {
            queue_diff(state);
            return;
        }
    }
    state.selected_file_path.clear();
    state.current_diff.clear();
}

static void apply_open_repo(AppState& state, const JobResult& result)
{
    const std::string& root = result.repo_path;
    size_t slash = root.find_last_of("\\/");
    state.active_repo = root;
    state.active_repo_name = slash != std::string::npos ? root.substr(slash + 1) : root;
    ++state.repo_generation;
    state.recent_repos.erase(
        std::remove(state.recent_repos.begin(), state.recent_repos.end(), root),
        state.recent_repos.end());
    state.recent_repos.insert(state.recent_repos.begin(), root);
    state.selected_file_path.clear();
    state.current_diff.clear();
    state.status_message.clear();
    state.repo = result.snapshot;
    push_request(state, RequestKind::SAVE_SETTINGS);
    LOG_INFO("open_repo: name='%s' branch='%s'", state.active_repo_name.c_str(),
        state.repo.current_branch.c_str());
}

static const char* failure_prefix(JobKind kind)
{
    switch (kind)
    {
        case JobKind::COMMIT:
            return "Commit failed: ";
        case JobKind::CHECKOUT_BRANCH:
            return "Checkout failed: ";
        case JobKind::CREATE_BRANCH:
            return "Could not create branch: ";
        case JobKind::DELETE_BRANCH:
            return "Could not delete branch: ";
        case JobKind::FIND_GIT:
        case JobKind::OPEN_REPO:
        case JobKind::REFRESH:
        case JobKind::LOAD_DIFF:
            return "";
    }
    return "";
}

static void apply_result(AppState& state, const JobResult& result)
{
    switch (result.kind)
    {
        case JobKind::FIND_GIT:
            state.is_git_found = result.is_success;
            state.git_executable = result.git_executable;
            if (!result.is_success)
            {
                state.status_message = result.error;
            }
            return;

        case JobKind::OPEN_REPO:
            if (result.is_success)
            {
                apply_open_repo(state, result);
            }
            else
            {
                state.status_message = result.error;
            }
            return;

        case JobKind::REFRESH:
        case JobKind::LOAD_DIFF:
        case JobKind::COMMIT:
        case JobKind::CHECKOUT_BRANCH:
        case JobKind::CREATE_BRANCH:
        case JobKind::DELETE_BRANCH:
            break;
    }

    // Everything below belongs to a repository; drop results from one we've left
    if (result.generation != state.repo_generation || result.repo_path != state.active_repo)
    {
        return;
    }

    if (result.kind == JobKind::LOAD_DIFF)
    {
        if (result.diff_path == state.selected_file_path)
        {
            state.current_diff = result.is_success ? result.diff : "";
        }
        return;
    }

    if (result.has_snapshot)
    {
        apply_snapshot(state, result.snapshot);
    }
    if (result.kind == JobKind::REFRESH)
    {
        if (!result.is_success)
        {
            state.status_message = result.error;
        }
        return;
    }

    if (result.is_success)
    {
        state.status_message.clear();
        state.has_commit_succeeded = result.kind == JobKind::COMMIT;
    }
    else
    {
        state.status_message = failure_prefix(result.kind) + result.error;
    }
}

static void apply_results(AppState& state)
{
    JobResult result;
    while (jobs_pop_result(result))
    {
        apply_result(state, result);
    }

    state.is_busy = jobs_get_status(state.is_mutation_running, state.busy_label);
}

static void queue_repo_job(const AppState& state, const Request& request, JobKind kind)
{
    if (state.active_repo.empty())
    {
        return;
    }

    Job job = make_job(state, kind);
    job.text = request.text;
    job.text2 = request.text2;
    job.flag = request.flag;
    jobs_push(job);
}

static void handle_request(AppState& state, const Request& request, Window& window,
    const std::string& app_data_dir)
{
    switch (request.kind)
    {
        case RequestKind::OPEN_REPO_DIALOG:
        {
            std::string folder;
            if (pick_folder(window.hwnd, L"Open Repository", folder))
            {
                queue_open_repo(state, folder, "Not a valid Git repository root.");
            }
            break;
        }

        case RequestKind::SWITCH_REPO:
            queue_open_repo(state, request.text,
                "Repository no longer available: " + request.text);
            break;

        case RequestKind::OPEN_IN_EXPLORER:
            open_in_explorer(state.active_repo);
            break;

        case RequestKind::OPEN_IN_TERMINAL:
            open_in_terminal(window.hwnd, state.active_repo);
            break;

        case RequestKind::REFRESH:
            queue_repo_job(state, request, JobKind::REFRESH);
            break;

        case RequestKind::LOAD_DIFF:
            queue_diff(state);
            break;

        case RequestKind::COMMIT:
            queue_repo_job(state, request, JobKind::COMMIT);
            break;

        case RequestKind::CHECKOUT_BRANCH:
            queue_repo_job(state, request, JobKind::CHECKOUT_BRANCH);
            break;

        case RequestKind::CREATE_BRANCH:
            queue_repo_job(state, request, JobKind::CREATE_BRANCH);
            break;

        case RequestKind::DELETE_BRANCH:
            queue_repo_job(state, request, JobKind::DELETE_BRANCH);
            break;

        case RequestKind::SAVE_SETTINGS:
            if (!persistence_save(state, app_data_dir))
            {
                state.status_message = "Could not save settings to " + app_data_dir;
            }
            break;

        case RequestKind::EXIT:
            window.is_quit_requested = true;
            break;
    }
}

// Handle the requests the GUI made this frame. Runs after present so a dialog doesn't stall a
// half-drawn frame.
static void handle_requests(AppState& state, Window& window, const std::string& app_data_dir)
{
    std::vector<Request> requests;
    requests.swap(state.requests);
    for (const Request& request : requests)
    {
        handle_request(state, request, window, app_data_dir);
    }
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    std::string app_data_dir = get_app_data_dir();
    log_init(app_data_dir);
    LOG_INFO("=== Surume starting === app data '%s'", app_data_dir.c_str());

    if (!window_init(main_window, L"Surume"))
    {
        LOG_ERROR("window_init failed, aborting");
        log_shutdown();
        return 1;
    }
    if (!jobs_start())
    {
        LOG_ERROR("jobs_start failed, aborting");
        window_shutdown(main_window);
        log_shutdown();
        return 1;
    }

    AppState state;
    app_init(state, app_data_dir);

    int frame_count = 0;
    while (true)
    {
        pump_messages(main_window);
        if (main_window.is_quit_requested)
        {
            break;
        }
        if (is_still_occluded(main_window))
        {
            continue;
        }

        apply_results(state);
        handle_dpi_change(main_window);
        handle_resize(main_window);
        begin_frame();
        gui_render(state);
        present(main_window);
        handle_requests(state, main_window, app_data_dir);

        ++frame_count;
        if (frame_count % HEARTBEAT_FRAMES == 0)
        {
            LOG_DEBUG("heartbeat: frame %d", frame_count);
        }
    }

    jobs_stop();
    if (!persistence_save(state, app_data_dir))
    {
        LOG_ERROR("persistence_save failed; settings or recent repos were not saved");
    }
    window_shutdown(main_window);

    LOG_INFO("=== Surume exiting cleanly after %d frames ===", frame_count);
    log_shutdown();
    return 0;
}
