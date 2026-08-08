#pragma once

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Log.h"
#include "Core/String.h"
#include "Core/Vector.h"
#include "Core/UniquePtr.h"

#ifdef CRUX_DEBUG
    #define CRUX_ASSERT(condition, message) \
        do { if (!(condition)) { CRUX_LOG_ERROR("Assertion failed: {}", message); __builtin_trap(); } } while(0)
#else
    #define CRUX_ASSERT(condition, message) ((void)0)
#endif

#define CRUX_LOG_INFO(...)    ::Crux::Log::info(__VA_ARGS__)
#define CRUX_LOG_WARN(...)   ::Crux::Log::warn(__VA_ARGS__)
#define CRUX_LOG_ERROR(...)  ::Crux::Log::error(__VA_ARGS__)
#define CRUX_LOG_DEBUG(...)  ::Crux::Log::debug(__VA_ARGS__)

#define CRUX_VERSION_MAJOR 0
#define CRUX_VERSION_MINOR 1
#define CRUX_VERSION_PATCH 0
#define CRUX_VERSION_STRING "0.1.0"