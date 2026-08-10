#include "Renderer/FrostZenith.h"

#include <algorithm>
#include <chrono>

namespace Frost {
namespace Renderer {

FrostZenith::~FrostZenith() {
    shutdown();
}

void FrostZenith::initialize(u32 workerCount, u32 viewportWidth, u32 viewportHeight, SplitMode mode) {
    shutdown();

    workerCount_ = workerCount > 0 ? workerCount : 1;
    viewportWidth_ = viewportWidth;
    viewportHeight_ = viewportHeight;
    splitMode_ = mode;

    running_.store(true);
    paused_.store(false);
    pendingFrames_ = 0;
    jobIdCounter_ = 0;
    frameActive_ = false;
    activeFrame_ = 0;
    jobQueue_.clear();

    workerFrameTimes_.resize(workerCount_, 0.0f);
    stats_ = Stats();
    stats_.perWorkerMs.resize(workerCount_, 0.0f);

    computeRegions();

    workers_.clear();
    for (u32 i = 0; i < workerCount_; i++) {
        workers_.push_back(std::thread(&FrostZenith::workerLoop, this, i));
    }
}

void FrostZenith::shutdown() {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        running_.store(false);
        jobQueue_.clear();
        pendingFrames_ = 0;
    }
    cv_.notify_all();
    frameCv_.notify_all();

    for (usize i = 0; i < workers_.size(); i++) {
        if (workers_[i].joinable()) {
            workers_[i].join();
        }
    }
    workers_.clear();
}

void FrostZenith::setViewport(u32 width, u32 height) {
    std::unique_lock<std::mutex> lock(mutex_);
    viewportWidth_ = width;
    viewportHeight_ = height;
    computeRegions();
}

void FrostZenith::setSplitMode(SplitMode mode) {
    std::unique_lock<std::mutex> lock(mutex_);
    splitMode_ = mode;
    computeRegions();
}

void FrostZenith::submitFrame(u32 frameIndex, void (*render)(const RenderRegion&, u32, void*), void* userData) {
    auto frameStart = std::chrono::steady_clock::now();

    {
        std::unique_lock<std::mutex> lock(mutex_);
        stats_.framesSubmitted++;
        activeFrame_ = frameIndex;
        frameActive_ = true;
        for (usize i = 0; i < workerFrameTimes_.size(); i++) {
            workerFrameTimes_[i] = 0.0f;
        }
        pendingFrames_ = 0;
        for (usize i = 0; i < dispatchOrder_.size(); i++) {
            u32 regionIndex = dispatchOrder_[i];
            if (!regions_[regionIndex].active) {
                continue;
            }
            FrameJob job;
            job.id = ++jobIdCounter_;
            job.region = regions_[regionIndex];
            job.frameIndex = frameIndex;
            job.render = render;
            job.userData = userData;
            pendingFrames_++;
            jobQueue_.push_back(job);
        }
    }
    cv_.notify_all();

    std::unique_lock<std::mutex> lock(mutex_);
    frameCv_.wait(lock, [this] { return pendingFrames_ == 0; });
    frameActive_ = false;

    auto frameEnd = std::chrono::steady_clock::now();
    f32 frameMs = std::chrono::duration_cast<std::chrono::duration<f32, std::milli>>(frameEnd - frameStart).count();
    stats_.lastFrameMs = frameMs;
    stats_.perWorkerMs = workerFrameTimes_;
    recomputeDispatchOrderLocked();
}

void FrostZenith::waitForFrame() {
    std::unique_lock<std::mutex> lock(mutex_);
    frameCv_.wait(lock, [this] { return !frameActive_ || pendingFrames_ == 0; });
}

void FrostZenith::setRegionWeight(u32 regionIndex, f32 loadBalance) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (regionIndex >= regions_.size()) {
        return;
    }
    regions_[regionIndex].loadBalance = loadBalance;
    regionWeights_[regionIndex] = loadBalance;
}

void FrostZenith::rebalance(u32 frameIndex) {
    (void)frameIndex;
    std::unique_lock<std::mutex> lock(mutex_);
    for (usize i = 0; i < workerFrameTimes_.size(); i++) {
        workerFrameTimes_[i] = 0.0f;
    }
    recomputeDispatchOrderLocked();
}

bool FrostZenith::isFrameDone() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return pendingFrames_ == 0;
}

bool FrostZenith::idle() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return jobQueue_.empty() && pendingFrames_ == 0;
}

void FrostZenith::setPaused(bool paused) {
    paused_.store(paused);
    cv_.notify_all();
}

void FrostZenith::computeRegions() {
    regions_.clear();
    regionWeights_.clear();
    dispatchOrder_.clear();

    const u32 w = viewportWidth_;
    const u32 h = viewportHeight_;

    switch (splitMode_) {
        case SplitMode::None: {
            if (w > 0 && h > 0) {
                RenderRegion region;
                region.width = w;
                region.height = h;
                regions_.push_back(region);
            }
            break;
        }
        case SplitMode::Halves: {
            const u32 half = w / 2;
            RenderRegion r0;
            r0.width = w - half;
            r0.height = h;
            RenderRegion r1;
            r1.x = half;
            r1.width = half;
            r1.height = h;
            if (r0.width > 0 && r0.height > 0) regions_.push_back(r0);
            if (r1.width > 0 && r1.height > 0) regions_.push_back(r1);
            break;
        }
        case SplitMode::Quadrants: {
            const u32 halfW = w / 2;
            const u32 halfH = h / 2;
            RenderRegion quad[4];
            quad[0] = {0, 0, w - halfW, h - halfH};
            quad[1] = {halfW, 0, halfW, h - halfH};
            quad[2] = {0, halfH, w - halfW, halfH};
            quad[3] = {halfW, halfH, halfW, halfH};
            for (u32 i = 0; i < 4; i++) {
                if (quad[i].width > 0 && quad[i].height > 0) {
                    regions_.push_back(quad[i]);
                }
            }
            break;
        }
        case SplitMode::Strips: {
            u32 count = 4;
            if (h < count) count = h;
            for (u32 i = 0; i < count; i++) {
                RenderRegion region;
                region.y = (i * h) / count;
                u32 nextY = ((i + 1) * h) / count;
                region.width = w;
                region.height = nextY - region.y;
                if (region.width > 0 && region.height > 0) {
                    regions_.push_back(region);
                }
            }
            break;
        }
        case SplitMode::Columns: {
            u32 count = workerCount_;
            if (count == 0) count = 1;
            if (w < count) count = w;
            for (u32 i = 0; i < count; i++) {
                RenderRegion region;
                region.x = (i * w) / count;
                u32 nextX = ((i + 1) * w) / count;
                region.width = nextX - region.x;
                region.height = h;
                if (region.width > 0 && region.height > 0) {
                    regions_.push_back(region);
                }
            }
            break;
        }
        case SplitMode::Rows: {
            u32 count = workerCount_;
            if (count == 0) count = 1;
            if (h < count) count = h;
            for (u32 i = 0; i < count; i++) {
                RenderRegion region;
                region.y = (i * h) / count;
                u32 nextY = ((i + 1) * h) / count;
                region.height = nextY - region.y;
                region.width = w;
                if (region.width > 0 && region.height > 0) {
                    regions_.push_back(region);
                }
            }
            break;
        }
    }

    const usize n = regions_.size();
    regionWeights_.resize(n, 1.0f);
    for (usize i = 0; i < n; i++) {
        regions_[i].loadBalance = 1.0f;
        regions_[i].active = true;
        dispatchOrder_.push_back(static_cast<u32>(i));
    }
}

void FrostZenith::recomputeDispatchOrderLocked() {
    const usize n = regions_.size();
    dispatchOrder_.clear();
    for (usize i = 0; i < n; i++) {
        dispatchOrder_.push_back(static_cast<u32>(i));
    }
    std::sort(dispatchOrder_.begin(), dispatchOrder_.end(), [this](u32 a, u32 b) {
        const f32 wa = regionWeights_[a];
        const f32 wb = regionWeights_[b];
        if (wa != wb) return wa > wb;
        return a < b;
    });
}

void FrostZenith::workerLoop(u32 workerId) {
    for (;;) {
        FrameJob job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return !running_.load() || (!paused_.load() && !jobQueue_.empty());
            });
            if (!running_.load()) {
                return;
            }
            if (jobQueue_.empty()) {
                continue;
            }
            job = jobQueue_.front();
            jobQueue_.pop_front();
            job.workerId = workerId;
        }

        auto start = std::chrono::steady_clock::now();
        if (job.render != nullptr) {
            job.render(job.region, workerId, job.userData);
        }
        auto end = std::chrono::steady_clock::now();
        f32 ms = std::chrono::duration_cast<std::chrono::duration<f32, std::milli>>(end - start).count();

        {
            std::unique_lock<std::mutex> lock(mutex_);
            job.completed = true;
            stats_.jobsCompleted++;
            if (workerId < workerFrameTimes_.size()) {
                workerFrameTimes_[workerId] += ms;
            }
            if (pendingFrames_ > 0) {
                pendingFrames_--;
            }
            if (pendingFrames_ == 0) {
                frameCv_.notify_all();
            }
        }
    }
}

}
}
