#pragma once

#include "Core/Types.h"
#include "Core/String.h"
#include "Core/Vector.h"

namespace Frost {

enum class SystemHealth : u8 {
    OK = 0,
    Warning,
    Critical,
    Dead
};

struct SystemEntry {
    String name;
    bool initialized = false;
    bool responding = true;
    f32 lastUpdateMs = 0.0f;
    u64 lastUpdateFrame = 0;
    SystemHealth health = SystemHealth::OK;
    u32 warningCount = 0;
    u32 criticalCount = 0;
};

struct WardenReport {
    u32 systemsTotal = 0;
    u32 systemsOK = 0;
    u32 systemsWarning = 0;
    u32 systemsCritical = 0;
    u32 systemsDead = 0;
    f32 totalUpdateMs = 0.0f;
    f32 peakUpdateMs = 0.0f;
    u64 memoryAllocated = 0;
    String summary;
};

struct WardenConfig {
    u32 maxFrameSkip = 10;
    f32 warningUpdateMs = 100.0f;
    f32 criticalUpdateMs = 500.0f;
    u32 maxCriticalBeforeDead = 3;
};

struct WardenStats {
    u64 totalScans = 0;
    u64 totalWarnings = 0;
    u64 totalCriticals = 0;
    u64 totalRecoveries = 0;
};

class FrostWarden {
public:
    FrostWarden() = default;
    ~FrostWarden() = default;

    void registerSystem(const String& name);
    void heartbeat(const String& systemName, u64 frameIndex, f32 updateMs);
    void scan();
    void checkMemory(u64 allocatedBytes);
    WardenReport report();
    String suggestRecovery(const String& systemName);
    bool isHealthy() const;
    void reset();

    void setConfig(const WardenConfig& config) { config_ = config; }
    const WardenConfig& getConfig() const { return config_; }
    const WardenStats& getStats() const { return stats_; }
    const Vector<SystemEntry>& getSystems() const { return systems_; }

private:
    SystemEntry* findSystem(const String& name);

    Vector<SystemEntry> systems_;
    WardenConfig config_{};
    WardenStats stats_{};
    u64 memoryAllocated_ = 0;
    u64 memoryThresholdWarn_ = 536870912;
    u64 memoryThresholdCritical_ = 1073741824;
    u64 currentFrame_ = 0;
    f32 peakUpdateMs_ = 0.0f;
};

} // namespace Frost
