#include "Core/FrostWarden.h"

namespace Frost {

SystemEntry* FrostWarden::findSystem(const String& name) {
    for (usize i = 0; i < systems_.size(); i++) {
        if (systems_[i].name == name) return &systems_[i];
    }
    return nullptr;
}

void FrostWarden::registerSystem(const String& name) {
    if (findSystem(name)) return;
    SystemEntry entry;
    entry.name = name;
    entry.initialized = false;
    entry.responding = true;
    entry.lastUpdateMs = 0.0f;
    entry.lastUpdateFrame = 0;
    entry.health = SystemHealth::OK;
    entry.warningCount = 0;
    entry.criticalCount = 0;
    systems_.push_back(entry);
}

void FrostWarden::heartbeat(const String& systemName, u64 frameIndex, f32 updateMs) {
    SystemEntry* entry = findSystem(systemName);
    if (!entry) return;

    entry->initialized = true;
    entry->responding = true;

    if (frameIndex <= entry->lastUpdateFrame) {
        entry->warningCount++;
    }

    entry->lastUpdateFrame = frameIndex;
    entry->lastUpdateMs = updateMs;

    if (updateMs > config_.criticalUpdateMs) {
        entry->health = SystemHealth::Critical;
        entry->criticalCount++;
        stats_.totalCriticals++;
    } else if (updateMs > config_.warningUpdateMs) {
        entry->health = SystemHealth::Warning;
        stats_.totalWarnings++;
    } else {
        if (entry->health == SystemHealth::Warning || entry->health == SystemHealth::Critical) {
            stats_.totalRecoveries++;
        }
        entry->health = SystemHealth::OK;
    }
}

void FrostWarden::scan() {
    stats_.totalScans++;
    currentFrame_++;
    f32 totalUpdateMs = 0.0f;
    peakUpdateMs_ = 0.0f;

    for (usize i = 0; i < systems_.size(); i++) {
        SystemEntry& entry = systems_[i];
        if (!entry.initialized) continue;

        totalUpdateMs += entry.lastUpdateMs;
        if (entry.lastUpdateMs > peakUpdateMs_) {
            peakUpdateMs_ = entry.lastUpdateMs;
        }

        u64 framesSinceUpdate = currentFrame_ - entry.lastUpdateFrame;
        if (framesSinceUpdate > (u64)config_.maxFrameSkip) {
            entry.responding = false;
            entry.criticalCount++;
            stats_.totalCriticals++;
            if (entry.criticalCount > config_.maxCriticalBeforeDead) {
                entry.health = SystemHealth::Dead;
            } else {
                entry.health = SystemHealth::Critical;
            }
        }
    }
}

void FrostWarden::checkMemory(u64 allocatedBytes) {
    memoryAllocated_ = allocatedBytes;
}

WardenReport FrostWarden::report() {
    WardenReport rpt;
    rpt.systemsTotal = (u32)systems_.size();
    rpt.systemsOK = 0;
    rpt.systemsWarning = 0;
    rpt.systemsCritical = 0;
    rpt.systemsDead = 0;
    rpt.peakUpdateMs = peakUpdateMs_;
    rpt.memoryAllocated = memoryAllocated_;

    f32 totalMs = 0.0f;
    for (usize i = 0; i < systems_.size(); i++) {
        const SystemEntry& entry = systems_[i];
        totalMs += entry.lastUpdateMs;
        switch (entry.health) {
            case SystemHealth::OK: rpt.systemsOK++; break;
            case SystemHealth::Warning: rpt.systemsWarning++; break;
            case SystemHealth::Critical: rpt.systemsCritical++; break;
            case SystemHealth::Dead: rpt.systemsDead++; break;
        }
    }
    rpt.totalUpdateMs = totalMs;

    u64 memMB = memoryAllocated_ / 1048576;

    String s;
    s.append("Warden: ");
    s.append(String::fromInt((i64)rpt.systemsTotal));
    s.append(" systems, ");
    s.append(String::fromInt((i64)rpt.systemsOK));
    s.append(" OK, ");
    s.append(String::fromInt((i64)rpt.systemsWarning));
    s.append(" Warning, ");
    s.append(String::fromInt((i64)rpt.systemsDead));
    s.append(" Dead. Memory: ");
    s.append(String::fromInt((i64)memMB));
    s.append("MB. Peak frame: ");
    s.append(String::fromFloat((f64)peakUpdateMs_, 1));
    s.append("ms");

    rpt.summary = s;
    return rpt;
}

String FrostWarden::suggestRecovery(const String& systemName) {
    const SystemEntry* entry = findSystem(systemName);
    if (!entry) return String("System not found");

    switch (entry->health) {
        case SystemHealth::OK: return String("OK");
        case SystemHealth::Warning: return String("Check for memory leaks");
        case SystemHealth::Critical: return String("Consider restart");
        case SystemHealth::Dead: return String("System unresponsive - restart required");
    }
    return String("Unknown");
}

bool FrostWarden::isHealthy() const {
    for (usize i = 0; i < systems_.size(); i++) {
        if (systems_[i].health == SystemHealth::Critical ||
            systems_[i].health == SystemHealth::Dead) {
            return false;
        }
    }
    return true;
}

void FrostWarden::reset() {
    systems_.clear();
    stats_ = WardenStats{};
    memoryAllocated_ = 0;
    currentFrame_ = 0;
    peakUpdateMs_ = 0.0f;
}

} // namespace Frost
