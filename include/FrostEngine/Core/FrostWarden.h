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

using IntegrityCheckFn = bool (*)();

struct IntegrityReport {
    u32 checksRun = 0;
    u32 failures = 0;
    Vector<String> failureMessages;
    f32 runTimeMs = 0.0f;
    bool passed = true;
};

struct ResourceLeak {
    u32 resourceId = 0;
    String resourceType;
    String owner;
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

    IntegrityReport runIntegrityCheck();
    u32 registerCheck(const char* name, void* fnPtr);
    void recordLeak(u32 resourceId, const char* type, const char* owner);
    void clearLeaks();
    u32 getLeakCount() const;
    bool repairSystem(u32 checkId);
    u32 getTotalChecksRun() const;
    u32 getTotalFailures() const;
    f32 getLastRunTimeMs() const;
    const Vector<IntegrityReport>& getIntegrityHistory() const;
    void enableAutoRepair(bool enabled);
    bool isAutoRepairEnabled() const;
    IntegrityReport snapshot() const;

    void setConfig(const WardenConfig& config) { config_ = config; }
    const WardenConfig& getConfig() const { return config_; }
    const WardenStats& getStats() const { return stats_; }
    const Vector<SystemEntry>& getSystems() const { return systems_; }

private:
    struct IntegrityCheck {
        u32 id = 0;
        String name;
        void* fnPtr = nullptr;
        bool needsRepair = false;
        u32 repairAttempts = 0;
    };

    IntegrityCheck* findCheck(u32 checkId);
    bool invokeCheck(const IntegrityCheck& check) const;

    SystemEntry* findSystem(const String& name);

    Vector<SystemEntry> systems_;
    WardenConfig config_{};
    WardenStats stats_{};
    u64 memoryAllocated_ = 0;
    u64 memoryThresholdWarn_ = 536870912;
    u64 memoryThresholdCritical_ = 1073741824;
    u64 currentFrame_ = 0;
    f32 peakUpdateMs_ = 0.0f;

    Vector<ResourceLeak> leakRecords_;
    Vector<IntegrityReport> integrityHistory_;
    Vector<IntegrityCheck> checks_;
    u32 nextCheckId_ = 0;
    u32 totalChecksRun_ = 0;
    u32 totalFailures_ = 0;
    bool autoRepairEnabled_ = true;
    f32 lastRunTimeMs_ = 0.0f;
};

} // namespace Frost
