#pragma once
#include "Core/Types.h"
#include "Core/Math.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include <mutex>
#include <cstring>

namespace Frost {

struct GPUProfilerConfig {
    bool enabled = true;
    bool detailedTiming = true;
    bool perFrameStats = true;
    u32 historySize = 120;
    u32 maxScopes = 64;
    f32 vramUpdateInterval = 0.5f;
    bool trackHotSpots = true;
    bool trackMemory = true;
};

struct TimingScope {
    u64 startTicks;
    u64 endTicks;
    f32 durationMs;
    const char* name;
    u32 scopeId;
    u32 parentScopeId;
    bool active;
};

struct FrameStats {
    f32 frameTimeMs;
    f32 cpuTimeMs;
    f32 gpuTimeMs;
    f32 fps;
    f32 drawCalls;
    f32 triangles;
    f32 vertices;
    f32 textureMemoryMB;
    f32 bufferMemoryMB;
    f32 totalMemoryMB;
    f32 frameTimeHistory[120];
    f32 gpuTimeHistory[120];
    u32 historyIndex;
    u32 historyCount;
};

struct RenderPassStats {
    const char* name;
    f32 totalTimeMs;
    f32 avgTimeMs;
    f32 maxTimeMs;
    f32 minTimeMs;
    u32 callCount;
    f32 timeHistory[120];
    u32 historyIndex;
};

struct HotSpot {
    const char* name;
    f32 totalTimeMs;
    f32 avgTimeMs;
    f32 maxTimeMs;
    u32 callCount;
    f32 threshold;
    bool isHotSpot;
};

struct VRAMInfo {
    u32 totalMB;
    u32 usedMB;
    u32 freeMB;
    u32 textureMemoryMB;
    u32 bufferMemoryMB;
    u32 renderTargetMemoryMB;
    u32 lastUpdateTime;
    bool available;
};

struct GPUPerformanceMetrics {
    f32 gpuUtilization;
    f32 shaderCompilationTime;
    f32 textureUploadTime;
    f32 bufferUploadTime;
    u32 shaderCompilations;
    u32 textureUploads;
    u32 bufferUploads;
    f32 avgFrameTime;
    f32 avgGPUTime;
    f32 worstFrameTime;
    f32 worstGPUTime;
    u64 totalFrames;
    f32 totalGPUTime;
};

class GPUProfiler {
public:
    GPUProfiler();
    ~GPUProfiler();

    bool init(const GPUProfilerConfig& config);
    void shutdown();
    void update(f32 dt);
    void beginFrame();
    void endFrame();

    u32 beginScope(const char* name);
    void endScope(u32 scopeId);
    void endScope(u32 scopeId, f32 durationMs);

    void beginRenderPass(const char* name);
    void endRenderPass(const char* name);

    f32 getFrameTime() const;
    f32 getFPS() const;
    f32 getGPUTime() const;
    f32 getCPUFrameTime() const;

    FrameStats getFrameStats() const;
    RenderPassStats getRenderPassStats(const char* name) const;
    RenderPassStats getRenderPassStats(u32 index) const;
    u32 getRenderPassCount() const;

    HotSpot getHotSpot(u32 index) const;
    u32 getHotSpotCount() const;
    void setHotSpotThreshold(f32 threshold);
    bool isHotSpot(const char* name) const;

    VRAMInfo getVRAMInfo() const;
    void updateVRAMInfo();
    f32 getVRAMUsage() const;
    f32 getVRAMBudget() const;

    void setEnabled(bool enabled);
    bool isEnabled() const;
    void setDetailedTiming(bool detailed);
    bool isDetailedTiming() const;
    void setHistorySize(u32 size);

    f32 getAverageFrameTime() const;
    f32 getAverageGPUTime() const;
    f32 getWorstFrameTime() const;
    f32 getWorstGPUTime() const;
    u64 getTotalFrames() const;

    void resetStats();
    void printStats() const;
    void printHotSpots() const;
    void printMemoryStats() const;

    const TimingScope* getScope(u32 scopeId) const;
    const TimingScope* getActiveScope(u32 scopeId) const;
    u32 getActiveScopeCount() const;

    void setCPUEstimate(f32 ms);
    void setGPUEstimate(f32 ms);
    void setVRAMEstimate(u32 usedMB, u32 totalMB);

    GPUPerformanceMetrics getPerformanceMetrics() const;
    void updatePerformanceMetrics();

private:
    GPUProfilerConfig config_;
    TimingScope scopes_[64];
    u32 activeScopes_[32];
    u32 activeScopeCount_;
    FrameStats frameStats_;
    RenderPassStats renderPasses_[32];
    u32 renderPassCount_;
    HotSpot hotSpots_[32];
    u32 hotSpotCount_;
    VRAMInfo vramInfo_;
    GPUPerformanceMetrics perfMetrics_;
    f32 lastVRAMUpdate_;
    u32 frameIndex_;
    bool enabled_;
    bool detailedTiming_;
    f32 cpuEstimate_;
    f32 gpuEstimate_;
    mutable std::mutex mutex_;
};

}
