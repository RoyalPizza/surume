#pragma once

#include <string>

// Open surume_debug.log in dir (the app-data folder), overwriting the previous log.
// Logging stays off if dir is empty, the file can't be opened, or this is a release build.
void log_init(const std::string& dir);

// Close the log file. Safe to call when the log was never opened.
void log_shutdown();

// Write one line with a timestamp, level and call site. Does nothing if the log isn't open.
// Call through the LOG_* macros so the call site is captured.
void log_write(const char* level, const char* file, int line, const char* format, ...);

#ifdef SURUME_DEBUG

#define LOG_INFO(format, ...)  log_write("INFO",  __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  log_write("WARN",  __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) log_write("ERROR", __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) log_write("DEBUG", __FILE__, __LINE__, format, ##__VA_ARGS__)

#else

#define LOG_INFO(...)  ((void)0)
#define LOG_WARN(...)  ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)

#endif
