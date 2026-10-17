#include "log.hpp"

#ifdef SURUME_DEBUG

#include "platform.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <ctime>

static FILE* g_log = nullptr;

static const char* Basename(const char* path) {
    const char* p = path;
    const char* last = path;
    while (*p) {
        if (*p == '/' || *p == '\\') last = p + 1;
        ++p;
    }
    return last;
}

void Log::Init() {
    // Lives in app data next to the settings files, so bin\ only holds binaries
    std::string dir = Platform::GetAppDataDir();
    if (dir.empty()) return;
    std::string path = dir + "surume_debug.log";

    wchar_t widePath[MAX_PATH] = {};
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, widePath, MAX_PATH);

    _wfopen_s(&g_log, widePath, L"w");
    if (!g_log) return;

    fprintf(g_log, "=== Surume Debug Log ===\n");
    fprintf(g_log, "Log file: %s\n", path.c_str());
    fprintf(g_log, "==========================\n\n");
    fflush(g_log);
}

void Log::Shutdown() {
    if (!g_log) return;
    fprintf(g_log, "\n=== Shutdown ===\n");
    fflush(g_log);
    fclose(g_log);
    g_log = nullptr;
}

void Log::Write(const char* level, const char* file, int line, const char* fmt, ...) {
    if (!g_log) return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    fprintf(g_log, "[%02d:%02d:%02d.%03d] [%-5s] %s:%d: ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        level, Basename(file), line);

    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);

    fprintf(g_log, "\n");
    fflush(g_log);
}

#endif
