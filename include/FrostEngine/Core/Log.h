#pragma once

#include "Core/Types.h"
#include <cstdio>
#include <ctime>
#include <cstdarg>

namespace Frost {

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

#define FROST_LOG_DEBUG(...) ::Frost::Log::debug(__VA_ARGS__)
#define FROST_LOG_INFO(...) ::Frost::Log::info(__VA_ARGS__)
#define FROST_LOG_WARN(...) ::Frost::Log::warn(__VA_ARGS__)
#define FROST_LOG_ERROR(...) ::Frost::Log::error(__VA_ARGS__)
#define FROST_LOG_FATAL(...) ::Frost::Log::error(__VA_ARGS__)