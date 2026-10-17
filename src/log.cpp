#include "log.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

#ifdef SURUME_DEBUG

static FILE* log_file = nullptr;
static SRWLOCK log_lock = SRWLOCK_INIT; // the UI thread and the jobs worker both log

// Strip directories so log lines show "git.cpp" instead of the full build path.
static const char* file_name_from_path(const char* path)
{
    const char* name = path;
    for (const char* ptr = path; *ptr != '\0'; ++ptr)
    {
        if (*ptr == '/' || *ptr == '\\')
        {
            name = ptr + 1;
        }
    }
    return name;
}

void log_init(const std::string& dir)
{
    if (dir.empty())
    {
        return;
    }

    std::string path = dir + "surume_debug.log";
    wchar_t wide_path[MAX_PATH] = {};
    if (MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide_path, MAX_PATH) == 0)
    {
        return;
    }

    if (_wfopen_s(&log_file, wide_path, L"w") != 0 || log_file == nullptr)
    {
        log_file = nullptr;
        return;
    }

    fprintf(log_file, "=== Surume Debug Log ===\n");
    fprintf(log_file, "Log file: %s\n", path.c_str());
    fprintf(log_file, "==========================\n\n");
    fflush(log_file);
}

void log_shutdown()
{
    if (log_file == nullptr)
    {
        return;
    }

    fprintf(log_file, "\n=== Shutdown ===\n");
    fclose(log_file);
    log_file = nullptr;
}

void log_write(const char* level, const char* file, int line, const char* format, ...)
{
    if (log_file == nullptr)
    {
        return;
    }

    SYSTEMTIME time;
    GetLocalTime(&time);
    AcquireSRWLockExclusive(&log_lock);
    fprintf(log_file, "[%02d:%02d:%02d.%03d] [%-5s] %s:%d: ",
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
        level, file_name_from_path(file), line);

    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file);
    ReleaseSRWLockExclusive(&log_lock);
}

#else

void log_init(const std::string&)
{
}

void log_shutdown()
{
}

void log_write(const char*, const char*, int, const char*, ...)
{
}

#endif
