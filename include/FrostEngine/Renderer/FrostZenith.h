#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace Frost {
namespace Renderer {

enum class SplitMode : u8 {
    None,
    Halves,
    Quadrants,
    Strips,
    Columns,
    Rows
};

struct RenderRegion {
    u32 x = 0;
    u32 y = 0;
    u32 width = 0;
    u32 height = 0;
    f32 loadBalance = 1.0f;
    bool active = true;
};

struct FrameJob {
    u64 id = 0;
    u32 workerId = 0;
    RenderRegion region;
    u32 frameIndex = 0;
    void (*render)(const RenderRegion&, u32 workerId, void* userData) = nullptr;
    void* userData = nullptr;
    bool completed = false;
};

class FrostZenith {
public:
    struct Stats {
        u32 framesSubmitted = 0;
        u32 jobsCompleted = 0;
        f32 lastFrameMs = 0.0f;
        Vector<f32> perWorkerMs;
    };

    FrostZenith() = default;
    ~FrostZenith();

    FrostZenith(const FrostZenith&) = delete;
    FrostZenith& operator=(const FrostZenith&) = delete;

    void initialize(u32 workerCount, u32 viewportWidth, u32 viewportHeight, SplitMode mode);
    void shutdown();

    void setViewport(u32 width, u32 height);
    void setSplitMode(SplitMode mode);

    void submitFrame(u32 frameIndex, void (*render)(const RenderRegion&, u32 workerId, void*), void* userData);
    void waitForFrame();

    u32 getRegionCount() const { return static_cast<u32>(regions_.size()); }
    const RenderRegion& getRegion(u32 i) const { return regions_[i]; }
    u32 getWorkerCount() const { return workerCount_; }

    void setRegionWeight(u32 regionIndex, f32 loadBalance);
    void rebalance(u32 frameIndex);

    const Stats& getStats() const { return stats_; }

    bool isFrameDone() const;
    bool idle() const;
    void setPaused(bool paused);

private:
    void computeRegions();
    void recomputeDispatchOrderLocked();
    void workerLoop(u32 workerId);

    Vector<RenderRegion> regions_;
    Vector<u32> dispatchOrder_;
    Vector<f32> regionWeights_;
    Vector<f32> workerFrameTimes_;

    std::deque<FrameJob> jobQueue_;
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    mutable std::condition_variable frameCv_;
    Vector<std::thread> workers_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};

    u64 jobIdCounter_ = 0;
    u32 workerCount_ = 0;
    u32 viewportWidth_ = 0;
    u32 viewportHeight_ = 0;
    SplitMode splitMode_ = SplitMode::None;

    u32 pendingFrames_ = 0;
    u64 activeFrame_ = 0;
    bool frameActive_ = false;
    Stats stats_;
};

}
}
