#pragma once

#ifdef SURUME_DEBUG

namespace Log {
    void Init();
    void Shutdown();
    void Write(const char* level, const char* file, int line, const char* fmt, ...);
}

#define LOG_INFO(fmt, ...)  Log::Write("INFO",  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Log::Write("WARN",  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Log::Write("ERROR", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) Log::Write("DEBUG", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#else

namespace Log {
    inline void Init() {}
    inline void Shutdown() {}
}

#define LOG_INFO(...)  ((void)0)
#define LOG_WARN(...)  ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)

#endif
