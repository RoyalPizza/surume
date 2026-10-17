#include "Log.h"

#ifdef GITALUNA_DEBUG

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
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    wcscat_s(exePath, MAX_PATH, L"gitaluna_debug.log");

    _wfopen_s(&g_log, exePath, L"w");
    if (!g_log) return;

    // Print log path in narrow chars for the first line
    char narrow[MAX_PATH * 2] = {};
    WideCharToMultiByte(CP_UTF8, 0, exePath, -1, narrow, sizeof(narrow), nullptr, nullptr);

    fprintf(g_log, "=== Gitaluna Debug Log ===\n");
    fprintf(g_log, "Log file: %s\n", narrow);
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
