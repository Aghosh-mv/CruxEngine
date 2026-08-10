#pragma once

#include "Core/Types.h"
#include "Core/String.h"
#include "Core/Vector.h"

namespace Frost {

enum class TimeMode : u8 {
    Fixed = 0,
    Variable,
    SemiFixed
};

struct TimeConfig {
    f32 fixedDeltaSeconds = 0.0166667f;
    f32 maxDeltaSeconds = 0.25f;
    f32 timeScale = 1.0f;
    u32 maxFramesPerTick = 4;
    bool enableReplay = true;
    u32 replayCapacity = 60 * 120;
};

struct Timestamp {
    u64 frame = 0;
    f32 seconds = 0.0f;
    u64 deltaUs = 0;
};

struct ScheduledEvent {
    u64 id = 0;
    u64 fireFrame = 0;
    u32 repeatCount = 0;
    f32 repeatIntervalSeconds = 0.0f;
    void (*callback)(u64 eventId, u64 frame, void* userData) = nullptr;
    void* userData = nullptr;
    bool active = true;
};

struct Keyframe {
    u64 frame = 0;
    f32 value = 0.0f;
};

struct Timeline {
    String name;
    Vector<Keyframe> keyframes;
    f32 evaluate(u64 frame) const;
};

class Chronos {
public:
    struct Stats {
        u64 ticks = 0;
        u64 eventsFired = 0;
        u64 framesSkipped = 0;
        u64 replaysRecorded = 0;
    };

    bool initialize(const TimeConfig& config);
    void shutdown();

    u32 update(f32 realDeltaSeconds);
    void advanceTick();

    u64 getFrame() const { return frame_; }
    f32 getSeconds() const { return seconds_; }
    f32 getDeltaSeconds() const { return fixedDeltaSeconds_; }
    f32 getRealDeltaSeconds() const { return realDeltaSeconds_; }
    f32 getTimeScale() const { return timeScale_; }
    void setTimeScale(f32 scale) { timeScale_ = scale; }
    void setTimeMode(TimeMode mode) { timeMode_ = mode; }
    Timestamp getTimestamp() const { return { frame_, seconds_, lastDeltaUs_ }; }

    u64 scheduleEvent(u64 fireFrame, void (*callback)(u64, u64, void*), void* userData, u32 repeatCount = 0, f32 repeatIntervalSeconds = 0.0f);
    void cancelEvent(u64 eventId);

    u64 createTimeline(const String& name);
    void addKeyframe(u64 timelineIndex, u64 frame, f32 value);
    f32 sampleTimeline(u64 timelineIndex, u64 frame) const;

    void recordFrame(const void* stateData, u32 sizeBytes);
    const u8* seekToFrame(u64 frame) const;
    u32 getReplayFrameCount() const { return (u32)replayBuffer_.size(); }

    void setFixedDelta(f32 dt) { fixedDeltaSeconds_ = dt > 0.0f ? dt : fixedDeltaSeconds_; }
    void setMaxDelta(f32 dt) { maxDeltaSeconds_ = dt > 0.0f ? dt : maxDeltaSeconds_; }

    void reset();

    const Stats& getStats() const { return stats_; }

private:
    void advanceTick(f32 deltaSeconds);
    void processScheduledEvents();
    void compactEvents();

    TimeMode timeMode_ = TimeMode::Fixed;
    f32 fixedDeltaSeconds_ = 0.0166667f;
    f32 maxDeltaSeconds_ = 0.25f;
    f32 timeScale_ = 1.0f;
    u32 maxFramesPerTick_ = 4;
    bool enableReplay_ = true;

    u64 frame_ = 0;
    f32 seconds_ = 0.0f;
    f32 accumulator_ = 0.0f;
    f32 realDeltaSeconds_ = 0.0f;
    u64 lastDeltaUs_ = 0;

    Vector<ScheduledEvent> events_;
    Vector<Timeline> timelines_;
    Vector<Vector<u8>> replayBuffer_;
    Vector<u64> replayFrames_;
    u32 replayCapacity_ = 60 * 120;

    u64 nextEventId_ = 1;
    Stats stats_;
};

} // namespace Frost
