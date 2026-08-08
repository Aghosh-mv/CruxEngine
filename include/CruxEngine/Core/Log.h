#pragma once

#include "Core/Types.h"
#include <cstdio>
#include <ctime>
#include <cstdarg>

namespace Crux {

enum class LogLevel : u8 { Debug, Info, Warn, Error, Fatal };

struct Log {
    static LogLevel minLevel;
    static bool initialized;
    
    static void init() { initialized = true; }
    static void shutdown() { initialized = false; }
    
    static void log(LogLevel level, const char* fmt, ...) {
        if(level < minLevel) return;
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        const char* tag = (level == LogLevel::Debug) ? "DEBUG" :
                          (level == LogLevel::Info) ? "INFO" :
                          (level == LogLevel::Warn) ? "WARN" : "ERROR";
        printf("[%s] %s\n", tag, buffer);
    }
    template<typename... A> static void debug(const char* f, A... a) { log(LogLevel::Debug, f, a...); }
    template<typename... A> static void info(const char* f, A... a) { log(LogLevel::Info, f, a...); }
    template<typename... A> static void warn(const char* f, A... a) { log(LogLevel::Warn, f, a...); }
    template<typename... A> static void error(const char* f, A... a) { log(LogLevel::Error, f, a...); }
};

inline LogLevel Log::minLevel = LogLevel::Debug;
inline bool Log::initialized = false;

}

#define CRUX_LOG_DEBUG(...) ::Crux::Log::debug(__VA_ARGS__)
#define CRUX_LOG_INFO(...) ::Crux::Log::info(__VA_ARGS__)
#define CRUX_LOG_WARN(...) ::Crux::Log::warn(__VA_ARGS__)
#define CRUX_LOG_ERROR(...) ::Crux::Log::error(__VA_ARGS__)
#define CRUX_LOG_FATAL(...) ::Crux::Log::error(__VA_ARGS__)