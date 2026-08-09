#pragma once

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Log.h"
#include "Core/String.h"
#include "Core/Vector.h"
#include "Core/UniquePtr.h"

#ifdef FROST_DEBUG
    #define FROST_ASSERT(condition, message) \
        do { if (!(condition)) { FROST_LOG_ERROR("Assertion failed: {}", message); __builtin_trap(); } } while(0)
#else
    #define FROST_ASSERT(condition, message) ((void)0)
#endif

#define FROST_LOG_INFO(...)    ::Frost::Log::info(__VA_ARGS__)
#define FROST_LOG_WARN(...)   ::Frost::Log::warn(__VA_ARGS__)
#define FROST_LOG_ERROR(...)  ::Frost::Log::error(__VA_ARGS__)
#define FROST_LOG_DEBUG(...)  ::Frost::Log::debug(__VA_ARGS__)

#define FROST_VERSION_MAJOR 0
#define FROST_VERSION_MINOR 1
#define FROST_VERSION_PATCH 0
#define FROST_VERSION_STRING "0.1.0"