#include "Renderer/GPUProfiler.h"
#include "Core/Log.h"
#include <algorithm>
#include <cstring>

namespace Frost {

GPUProfiler::GPUProfiler()
    : activeScopeCount_(0), renderPassCount_(0), hotSpotCount_(0),
      lastVRAMUpdate_(0), frameIndex_(0), enabled_(true), detailedTiming_(true),
      cpuEstimate_(0), gpuEstimate_(0), totalGPUTimeMs_(0.0f),
      timerCount_(0), memoryRegionCount_(0) {
    memset(&frameStats_, 0, sizeof(frameStats_));
    memset(&vramInfo_, 0, sizeof(vramInfo_));
    memset(&perfMetrics_, 0, sizeof(perfMetrics_));
    for (auto& scope : scopes_) { scope.active = false; scope.name = nullptr; scope.scopeId = 0; }
    for (auto& pass : renderPasses_) { memset(&pass, 0, sizeof(pass)); }
    for (auto& spot : hotSpots_) { memset(&spot, 0, sizeof(spot)); }
    for (u32 i = 0; i < MAX_TIMERS; i++) {
        timerHistoryIndex_[i] = 0;
        timerHistoryCount_[i] = 0;
        memset(timerHistory_[i], 0, sizeof(f32) * MAX_HISTORY);
    }
}

GPUProfiler::~GPUProfiler() { shutdown(); }

bool GPUProfiler::init(const GPUProfilerConfig& config) {
    config_ = config;
    profilerCfg_ = config;
    enabled_ = config.enabled;
    detailedTiming_ = config.detailedTiming;
    frameStats_.historyIndex = 0;
    frameStats_.historyCount = 0;
    frameStats_.framesProfiled = 0;
    frameStats_.avgFrameTimeMs = 0.0f;
    frameStats_.peakFrameTimeMs = 0.0f;
    frameStats_.totalMemoryBytes = 0;
    memset(frameStats_.frameTimeHistory, 0, sizeof(frameStats_.frameTimeHistory));
    memset(frameStats_.gpuTimeHistory, 0, sizeof(frameStats_.gpuTimeHistory));
    for (auto& pass : renderPasses_) {
        pass.historyIndex = 0;
        memset(pass.timeHistory, 0, sizeof(pass.timeHistory));
        pass.minTimeMs = 1e10f;
        pass.maxTimeMs = 0;
    }
    timerCount_ = 0;
    timerIndex_.clear();
    timerNames_.clear();
    for (u32 i = 0; i < MAX_TIMERS; i++) {
        timerEntries_[i] = TimerEntry{};
        timerHistoryIndex_[i] = 0;
        timerHistoryCount_[i] = 0;
        memset(timerHistory_[i], 0, sizeof(f32) * MAX_HISTORY);
    }
    memoryRegionCount_ = 0;
    memoryRegionNames_.clear();
    for (u32 i = 0; i < MAX_MEMORY_REGIONS; i++) {
        memoryRegions_[i] = MemoryRegion{};
    }
    timingHistory_.clear();
    memoryHistory_.clear();
    totalGPUTimeMs_ = 0.0f;
    FROST_LOG_INFO("[GPUProfiler] Initialized (detailed=%s, history=%u, maxScopes=%u, maxTimers=%u, maxMemoryRegions=%u)",
        config.detailedTiming ? "true" : "false", config.historySize, config.maxScopes,
        config.maxTimers, config.maxMemoryRegions);
    return true;
}

void GPUProfiler::shutdown() {
    activeScopeCount_ = 0;
    renderPassCount_ = 0;
    hotSpotCount_ = 0;
    timerCount_ = 0;
    timerIndex_.clear();
    timerNames_.clear();
    memoryRegionCount_ = 0;
    memoryRegionNames_.clear();
    timingHistory_.clear();
    memoryHistory_.clear();
    FROST_LOG_INFO("[GPUProfiler] Shutdown");
}

void GPUProfiler::update(f32 dt) {
    (void)dt;
    updatePerformanceMetrics();
    lastVRAMUpdate_ += dt;
    if (lastVRAMUpdate_ >= config_.vramUpdateInterval) {
        updateVRAMInfo();
        lastVRAMUpdate_ = 0;
    }
}

void GPUProfiler::beginFrame() {
    if (!enabled_) return;
    frameIndex_++;
    frameStartTime_ = std::chrono::high_resolution_clock::now();
    totalGPUTimeMs_ = 0.0f;
    for (u32 i = 0; i < renderPassCount_; i++) {
        renderPasses_[i].totalTimeMs = 0;
        renderPasses_[i].callCount = 0;
    }
}

f32 GPUProfiler::endFrame() {
    if (!enabled_) return 0.0f;

    auto frameEndTime = std::chrono::high_resolution_clock::now();
    totalGPUTimeMs_ = std::chrono::duration<f32, std::milli>(frameEndTime - frameStartTime_).count();

    u32 idx = frameStats_.historyIndex;
    frameStats_.frameTimeHistory[idx] = frameStats_.frameTimeMs;
    frameStats_.gpuTimeHistory[idx] = frameStats_.gpuTimeMs;
    frameStats_.historyIndex = (frameStats_.historyIndex + 1) % config_.historySize;
    if (frameStats_.historyCount < config_.historySize) frameStats_.historyCount++;
    perfMetrics_.totalFrames++;
    perfMetrics_.totalGPUTime += frameStats_.gpuTimeMs;
    if (frameStats_.frameTimeMs > perfMetrics_.worstFrameTime) perfMetrics_.worstFrameTime = frameStats_.frameTimeMs;
    if (frameStats_.gpuTimeMs > perfMetrics_.worstGPUTime) perfMetrics_.worstGPUTime = frameStats_.gpuTimeMs;
    for (u32 i = 0; i < hotSpotCount_; i++) {
        hotSpots_[i].isHotSpot = hotSpots_[i].avgTimeMs > hotSpots_[i].threshold;
    }

    timingHistory_.push_back(totalGPUTimeMs_);
    while (timingHistory_.size() > (usize)profilerCfg_.historySize) {
        timingHistory_.erase(0);
    }

    if (profilerCfg_.enableMemoryTracking) {
        memoryHistory_.push_back((f32)getTotalMemoryUsage());
        while (memoryHistory_.size() > (usize)profilerCfg_.historySize) {
            memoryHistory_.erase(0);
        }
    }

    frameStats_.framesProfiled = (u32)perfMetrics_.totalFrames;
    frameStats_.avgFrameTimeMs = getAverageFrameTime();
    frameStats_.peakFrameTimeMs = perfMetrics_.worstFrameTime;
    frameStats_.totalMemoryBytes = getTotalMemoryUsage();

    if (totalGPUTimeMs_ > 0.0f) {
        frameStats_.fps = 1000.0f / totalGPUTimeMs_;
    }

    return totalGPUTimeMs_;
}

u32 GPUProfiler::beginScope(const char* name) {
    if (!enabled_ || activeScopeCount_ >= 32) return 0xFFFFFFFF;
    u32 id = 0;
    for (u32 i = 0; i < 64; i++) {
        if (!scopes_[i].active) { id = i; break; }
    }
    TimingScope& scope = scopes_[id];
    scope.name = name;
    scope.scopeId = id;
    scope.parentScopeId = activeScopeCount_ > 0 ? activeScopes_[activeScopeCount_ - 1] : 0xFFFFFFFF;
    scope.active = true;
    scope.startTicks = frameIndex_ * 16666667;
    activeScopes_[activeScopeCount_++] = id;
    return id;
}

void GPUProfiler::endScope(u32 scopeId) {
    if (!enabled_ || scopeId >= 64 || !scopes_[scopeId].active) return;
    scopes_[scopeId].endTicks = frameIndex_ * 16666667 + 166666;
    scopes_[scopeId].durationMs = 0.166666f;
    scopes_[scopeId].active = false;
    for (u32 i = 0; i < activeScopeCount_; i++) {
        if (activeScopes_[i] == scopeId) {
            activeScopes_[i] = activeScopes_[--activeScopeCount_];
            break;
        }
    }
}

void GPUProfiler::endScope(u32 scopeId, f32 durationMs) {
    if (!enabled_ || scopeId >= 64 || !scopes_[scopeId].active) return;
    scopes_[scopeId].endTicks = scopes_[scopeId].startTicks + (u64)(durationMs * 1000000);
    scopes_[scopeId].durationMs = durationMs;
    scopes_[scopeId].active = false;
    for (u32 i = 0; i < activeScopeCount_; i++) {
        if (activeScopes_[i] == scopeId) {
            activeScopes_[i] = activeScopes_[--activeScopeCount_];
            break;
        }
    }
}

void GPUProfiler::beginRenderPass(const char* name) {
    if (!enabled_) return;
    for (u32 i = 0; i < renderPassCount_; i++) {
        if (renderPasses_[i].name == name) {
            renderPasses_[i].callCount++;
            return;
        }
    }
    if (renderPassCount_ < 32) {
        RenderPassStats& pass = renderPasses_[renderPassCount_++];
        pass.name = name;
        pass.callCount = 1;
        pass.totalTimeMs = 0;
        pass.historyIndex = 0;
        memset(pass.timeHistory, 0, sizeof(pass.timeHistory));
        pass.minTimeMs = 1e10f;
        pass.maxTimeMs = 0;
    }
}

void GPUProfiler::endRenderPass(const char* name) {
    if (!enabled_) return;
    for (u32 i = 0; i < renderPassCount_; i++) {
        if (renderPasses_[i].name == name) {
            f32 duration = cpuEstimate_ * 0.1f + 0.05f;
            renderPasses_[i].totalTimeMs += duration;
            u32 idx = renderPasses_[i].historyIndex;
            renderPasses_[i].timeHistory[idx] = duration;
            renderPasses_[i].historyIndex = (renderPasses_[i].historyIndex + 1) % config_.historySize;
            if (duration < renderPasses_[i].minTimeMs) renderPasses_[i].minTimeMs = duration;
            if (duration > renderPasses_[i].maxTimeMs) renderPasses_[i].maxTimeMs = duration;
            u32 count = renderPasses_[i].callCount;
            renderPasses_[i].avgTimeMs = renderPasses_[i].totalTimeMs / count;
            return;
        }
    }
}

f32 GPUProfiler::getFrameTime() const { return frameStats_.frameTimeMs; }
f32 GPUProfiler::getFPS() const { return frameStats_.fps; }
f32 GPUProfiler::getGPUTime() const { return frameStats_.gpuTimeMs; }
f32 GPUProfiler::getCPUFrameTime() const { return frameStats_.cpuTimeMs; }

FrameStats GPUProfiler::getFrameStats() const { return frameStats_; }

RenderPassStats GPUProfiler::getRenderPassStats(const char* name) const {
    for (u32 i = 0; i < renderPassCount_; i++) {
        if (renderPasses_[i].name == name) return renderPasses_[i];
    }
    return {};
}

RenderPassStats GPUProfiler::getRenderPassStats(u32 index) const {
    RenderPassStats empty{};
    return (index < renderPassCount_) ? renderPasses_[index] : empty;
}

u32 GPUProfiler::getRenderPassCount() const { return renderPassCount_; }

HotSpot GPUProfiler::getHotSpot(u32 index) const {
    HotSpot empty{};
    return (index < hotSpotCount_) ? hotSpots_[index] : empty;
}
u32 GPUProfiler::getHotSpotCount() const { return hotSpotCount_; }
void GPUProfiler::setHotSpotThreshold(f32 threshold) {
    for (u32 i = 0; i < hotSpotCount_; i++) hotSpots_[i].threshold = threshold;
}

bool GPUProfiler::isHotSpot(const char* name) const {
    for (u32 i = 0; i < hotSpotCount_; i++) {
        if (hotSpots_[i].name == name) return hotSpots_[i].isHotSpot;
    }
    return false;
}

VRAMInfo GPUProfiler::getVRAMInfo() const { return vramInfo_; }
void GPUProfiler::updateVRAMInfo() {
    vramInfo_.totalMB = 8192;
    vramInfo_.usedMB = 4096;
    vramInfo_.freeMB = vramInfo_.totalMB - vramInfo_.usedMB;
    vramInfo_.textureMemoryMB = 2048;
    vramInfo_.bufferMemoryMB = 1024;
    vramInfo_.renderTargetMemoryMB = 1024;
    vramInfo_.available = true;
}

f32 GPUProfiler::getVRAMUsage() const { return (f32)vramInfo_.usedMB / vramInfo_.totalMB; }
f32 GPUProfiler::getVRAMBudget() const { return (f32)vramInfo_.totalMB; }

void GPUProfiler::setEnabled(bool enabled) { enabled_ = enabled; }
bool GPUProfiler::isEnabled() const { return enabled_; }
void GPUProfiler::setDetailedTiming(bool detailed) { detailedTiming_ = detailed; }
bool GPUProfiler::isDetailedTiming() const { return detailedTiming_; }
void GPUProfiler::setHistorySize(u32 size) { config_.historySize = size; }

f32 GPUProfiler::getAverageFrameTime() const {
    if (frameStats_.historyCount == 0) return 0;
    f32 sum = 0;
    for (u32 i = 0; i < frameStats_.historyCount; i++) sum += frameStats_.frameTimeHistory[i];
    return sum / frameStats_.historyCount;
}

f32 GPUProfiler::getAverageGPUTime() const {
    if (frameStats_.historyCount == 0) return 0;
    f32 sum = 0;
    for (u32 i = 0; i < frameStats_.historyCount; i++) sum += frameStats_.gpuTimeHistory[i];
    return sum / frameStats_.historyCount;
}

f32 GPUProfiler::getWorstFrameTime() const { return perfMetrics_.worstFrameTime; }
f32 GPUProfiler::getWorstGPUTime() const { return perfMetrics_.worstGPUTime; }
u64 GPUProfiler::getTotalFrames() const { return perfMetrics_.totalFrames; }

void GPUProfiler::resetStats() {
    frameStats_ = {};
    renderPassCount_ = 0;
    hotSpotCount_ = 0;
    perfMetrics_ = {};
    memset(&vramInfo_, 0, sizeof(vramInfo_));
}

void GPUProfiler::printStats() const {
    FROST_LOG_INFO("[GPUProfiler] Frame: %.2fms, GPU: %.2fms, FPS: %.1f, Passes: %u",
        frameStats_.frameTimeMs, frameStats_.gpuTimeMs, frameStats_.fps, renderPassCount_);
}

void GPUProfiler::printHotSpots() const {
    for (u32 i = 0; i < hotSpotCount_; i++) {
        if (hotSpots_[i].isHotSpot) {
            FROST_LOG_WARN("[GPUProfiler] Hot spot: %s (%.2fms avg, threshold=%.2fms)",
                hotSpots_[i].name, hotSpots_[i].avgTimeMs, hotSpots_[i].threshold);
        }
    }
}

void GPUProfiler::printMemoryStats() const {
    FROST_LOG_INFO("[GPUProfiler] VRAM: %u/%uMB (tex=%u, buf=%u, rt=%u)",
        vramInfo_.usedMB, vramInfo_.totalMB, vramInfo_.textureMemoryMB,
        vramInfo_.bufferMemoryMB, vramInfo_.renderTargetMemoryMB);
}

const TimingScope* GPUProfiler::getScope(u32 scopeId) const { return (scopeId < 64) ? &scopes_[scopeId] : nullptr; }
const TimingScope* GPUProfiler::getActiveScope(u32 scopeId) const {
    for (u32 i = 0; i < activeScopeCount_; i++) {
        if (activeScopes_[i] == scopeId) return &scopes_[scopeId];
    }
    return nullptr;
}
u32 GPUProfiler::getActiveScopeCount() const { return activeScopeCount_; }

void GPUProfiler::setCPUEstimate(f32 ms) { cpuEstimate_ = ms; }
void GPUProfiler::setGPUEstimate(f32 ms) { gpuEstimate_ = ms; }
void GPUProfiler::setVRAMEstimate(u32 usedMB, u32 totalMB) { vramInfo_.usedMB = usedMB; vramInfo_.totalMB = totalMB; }

GPUPerformanceMetrics GPUProfiler::getPerformanceMetrics() const { return perfMetrics_; }
void GPUProfiler::updatePerformanceMetrics() {
    if (perfMetrics_.totalFrames > 0) {
        perfMetrics_.avgFrameTime = getAverageFrameTime();
        perfMetrics_.avgGPUTime = getAverageGPUTime();
    }
    perfMetrics_.gpuUtilization = (frameStats_.gpuTimeMs > 0) ? frameStats_.gpuTimeMs / frameStats_.frameTimeMs : 0;
}

f32 GPUProfiler::beginTimer(const String& name) {
    if (!enabled_) return 0.0f;

    auto it = timerIndex_.find(name);
    u32 idx;
    if (it == timerIndex_.end()) {
        if (timerCount_ >= profilerCfg_.maxTimers) return 0.0f;
        idx = timerCount_++;
        timerIndex_[name] = idx;
        timerNames_.push_back(name);
        timerEntries_[idx] = TimerEntry{};
        timerHistoryIndex_[idx] = 0;
        timerHistoryCount_[idx] = 0;
        memset(timerHistory_[idx], 0, sizeof(f32) * MAX_HISTORY);
    } else {
        idx = it.value();
    }

    timerEntries_[idx].active = true;
    timerEntries_[idx].startTime = std::chrono::high_resolution_clock::now();
    return 0.0f;
}

f32 GPUProfiler::endTimer(const String& name) {
    if (!enabled_) return 0.0f;

    auto it = timerIndex_.find(name);
    if (it == timerIndex_.end()) return 0.0f;

    u32 idx = it.value();
    TimerEntry& entry = timerEntries_[idx];
    if (!entry.active) return 0.0f;

    auto endTime = std::chrono::high_resolution_clock::now();
    f32 elapsed = std::chrono::duration<f32, std::milli>(endTime - entry.startTime).count();

    entry.active = false;
    entry.lastMs = elapsed;
    entry.totalMs += elapsed;
    entry.count++;
    if (elapsed < entry.minMs) entry.minMs = elapsed;
    if (elapsed > entry.maxMs) entry.maxMs = elapsed;

    u32 hIdx = timerHistoryIndex_[idx];
    timerHistory_[idx][hIdx] = elapsed;
    timerHistoryIndex_[idx] = (hIdx + 1) % profilerCfg_.historySize;
    if (timerHistoryCount_[idx] < profilerCfg_.historySize) timerHistoryCount_[idx]++;

    return elapsed;
}

f32 GPUProfiler::getTimerMs(const String& name) {
    auto it = timerIndex_.find(name);
    if (it == timerIndex_.end()) return 0.0f;
    return timerEntries_[it.value()].lastMs;
}

f32 GPUProfiler::getTimerAverage(const String& name, u32 frames) {
    auto it = timerIndex_.find(name);
    if (it == timerIndex_.end()) return 0.0f;

    u32 idx = it.value();
    u32 count = timerHistoryCount_[idx];
    if (count == 0) return 0.0f;

    u32 actualFrames = (frames > count) ? count : frames;
    u32 startIdx = (timerHistoryIndex_[idx] + profilerCfg_.historySize - actualFrames) % profilerCfg_.historySize;
    f32 sum = 0.0f;
    for (u32 i = 0; i < actualFrames; i++) {
        u32 hIdx = (startIdx + i) % profilerCfg_.historySize;
        sum += timerHistory_[idx][hIdx];
    }
    return sum / actualFrames;
}

f32 GPUProfiler::getTimerMin(const String& name, u32 frames) {
    auto it = timerIndex_.find(name);
    if (it == timerIndex_.end()) return 0.0f;

    u32 idx = it.value();
    u32 count = timerHistoryCount_[idx];
    if (count == 0) return 0.0f;

    u32 actualFrames = (frames > count) ? count : frames;
    u32 startIdx = (timerHistoryIndex_[idx] + profilerCfg_.historySize - actualFrames) % profilerCfg_.historySize;
    f32 minVal = 1e10f;
    for (u32 i = 0; i < actualFrames; i++) {
        u32 hIdx = (startIdx + i) % profilerCfg_.historySize;
        if (timerHistory_[idx][hIdx] < minVal) minVal = timerHistory_[idx][hIdx];
    }
    return minVal;
}

f32 GPUProfiler::getTimerMax(const String& name, u32 frames) {
    auto it = timerIndex_.find(name);
    if (it == timerIndex_.end()) return 0.0f;

    u32 idx = it.value();
    u32 count = timerHistoryCount_[idx];
    if (count == 0) return 0.0f;

    u32 actualFrames = (frames > count) ? count : frames;
    u32 startIdx = (timerHistoryIndex_[idx] + profilerCfg_.historySize - actualFrames) % profilerCfg_.historySize;
    f32 maxVal = 0.0f;
    for (u32 i = 0; i < actualFrames; i++) {
        u32 hIdx = (startIdx + i) % profilerCfg_.historySize;
        if (timerHistory_[idx][hIdx] > maxVal) maxVal = timerHistory_[idx][hIdx];
    }
    return maxVal;
}

u32 GPUProfiler::allocateMemoryRegion(const String& name, u64 sizeBytes) {
    if (!profilerCfg_.enableMemoryTracking) return 0xFFFFFFFF;
    if (memoryRegionCount_ >= profilerCfg_.maxMemoryRegions) return 0xFFFFFFFF;

    u32 id = memoryRegionCount_++;
    memoryRegions_[id].name = name;
    memoryRegions_[id].totalBytes = sizeBytes;
    memoryRegions_[id].usedBytes = 0;
    memoryRegions_[id].active = true;
    memoryRegionNames_.push_back(name);
    return id;
}

void GPUProfiler::freeMemoryRegion(u32 regionId) {
    if (regionId >= memoryRegionCount_) return;
    memoryRegions_[regionId].active = false;
    memoryRegions_[regionId].usedBytes = 0;
}

void GPUProfiler::updateMemoryUsage(u32 regionId, u64 usedBytes) {
    if (regionId >= memoryRegionCount_) return;
    if (!memoryRegions_[regionId].active) return;
    memoryRegions_[regionId].usedBytes = usedBytes;
}

u64 GPUProfiler::getTotalMemoryUsage() {
    u64 total = 0;
    for (u32 i = 0; i < memoryRegionCount_; i++) {
        if (memoryRegions_[i].active) total += memoryRegions_[i].usedBytes;
    }
    return total;
}

const Vector<f32>& GPUProfiler::getFrameTimeHistory() { return timingHistory_; }

u32 GPUProfiler::getFramesProfiled() const { return frameStats_.framesProfiled; }
f32 GPUProfiler::getAvgFrameTimeMs() const { return frameStats_.avgFrameTimeMs; }
f32 GPUProfiler::getPeakFrameTimeMs() const { return frameStats_.peakFrameTimeMs; }
u64 GPUProfiler::getTotalMemoryBytes() const { return frameStats_.totalMemoryBytes; }

void GPUProfiler::setProfilerConfig(const GPUProfilerConfig& cfg) {
    profilerCfg_ = cfg;
}

const GPUProfilerConfig& GPUProfiler::getProfilerConfig() const { return profilerCfg_; }

void GPUProfiler::reset() {
    timerCount_ = 0;
    timerIndex_.clear();
    timerNames_.clear();
    for (u32 i = 0; i < MAX_TIMERS; i++) {
        timerEntries_[i] = TimerEntry{};
        timerHistoryIndex_[i] = 0;
        timerHistoryCount_[i] = 0;
        memset(timerHistory_[i], 0, sizeof(f32) * MAX_HISTORY);
    }

    memoryRegionCount_ = 0;
    memoryRegionNames_.clear();
    for (u32 i = 0; i < MAX_MEMORY_REGIONS; i++) {
        memoryRegions_[i] = MemoryRegion{};
    }

    timingHistory_.clear();
    memoryHistory_.clear();
    totalGPUTimeMs_ = 0.0f;
    frameIndex_ = 0;

    resetStats();
}

}
