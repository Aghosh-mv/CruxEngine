#include "Core/Chronos.h"

#include <cstring>
#include <utility>

namespace Frost {

f32 Timeline::evaluate(u64 frame) const {
    if (keyframes.empty()) return 0.0f;
    if (keyframes.size() == 1) return keyframes[0].value;

    if (frame <= keyframes[0].frame) return keyframes[0].value;
    const Keyframe& last = keyframes.back();
    if (frame >= last.frame) return last.value;

    for (usize i = 0; i + 1 < keyframes.size(); ++i) {
        const Keyframe& a = keyframes[i];
        const Keyframe& b = keyframes[i + 1];
        if (frame >= a.frame && frame <= b.frame) {
            if (b.frame == a.frame) return b.value;
            f32 t = (f32)(frame - a.frame) / (f32)(b.frame - a.frame);
            return a.value + t * (b.value - a.value);
        }
    }
    return last.value;
}

bool Chronos::initialize(const TimeConfig& config) {
    fixedDeltaSeconds_ = config.fixedDeltaSeconds > 0.0f ? config.fixedDeltaSeconds : 0.0166667f;
    maxDeltaSeconds_ = config.maxDeltaSeconds > 0.0f ? config.maxDeltaSeconds : 0.25f;
    timeScale_ = config.timeScale;
    maxFramesPerTick_ = config.maxFramesPerTick;
    enableReplay_ = config.enableReplay;
    replayCapacity_ = config.replayCapacity;
    reset();
    return true;
}

void Chronos::shutdown() {
    reset();
    enableReplay_ = false;
}

u32 Chronos::update(f32 realDeltaSeconds) {
    realDeltaSeconds_ = realDeltaSeconds;
    u32 ticksThisCall = 0;

    switch (timeMode_) {
    case TimeMode::Fixed:
        accumulator_ += realDeltaSeconds_ * timeScale_;
        while (accumulator_ >= fixedDeltaSeconds_ && ticksThisCall < maxFramesPerTick_) {
            advanceTick();
            accumulator_ -= fixedDeltaSeconds_;
            ++ticksThisCall;
        }
        if (accumulator_ >= fixedDeltaSeconds_) {
            stats_.framesSkipped += (u64)(accumulator_ / fixedDeltaSeconds_);
        }
        break;
    case TimeMode::Variable: {
        f32 dt = realDeltaSeconds_ * timeScale_;
        if (dt > maxDeltaSeconds_) dt = maxDeltaSeconds_;
        if (dt < 0.0f) dt = 0.0f;
        advanceTick(dt);
        ticksThisCall = 1;
        break;
    }
    case TimeMode::SemiFixed: {
        f32 dt = realDeltaSeconds_ * timeScale_;
        if (dt > fixedDeltaSeconds_) dt = fixedDeltaSeconds_;
        if (dt < 0.0f) dt = 0.0f;
        advanceTick(dt);
        ticksThisCall = 1;
        break;
    }
    }
    return ticksThisCall;
}

void Chronos::advanceTick() {
    advanceTick(fixedDeltaSeconds_);
}

void Chronos::advanceTick(f32 deltaSeconds) {
    ++frame_;
    seconds_ += deltaSeconds;
    lastDeltaUs_ = (u64)(deltaSeconds * 1000000.0);
    ++stats_.ticks;
    processScheduledEvents();
}

u64 Chronos::scheduleEvent(u64 fireFrame, void (*callback)(u64, u64, void*), void* userData, u32 repeatCount, f32 repeatIntervalSeconds) {
    ScheduledEvent ev;
    ev.id = nextEventId_;
    ev.fireFrame = fireFrame;
    ev.callback = callback;
    ev.userData = userData;
    ev.repeatCount = repeatCount;
    ev.repeatIntervalSeconds = repeatIntervalSeconds > 0.0f ? repeatIntervalSeconds : 0.0f;
    ev.active = true;
    events_.push_back(ev);
    return nextEventId_++;
}

void Chronos::cancelEvent(u64 eventId) {
    for (usize i = 0; i < events_.size(); ++i) {
        if (events_[i].id == eventId) {
            events_[i].active = false;
            return;
        }
    }
}

void Chronos::processScheduledEvents() {
    if (events_.empty()) return;
    u32 fired = 0;
    for (usize i = 0; i < events_.size(); ++i) {
        ScheduledEvent& ev = events_[i];
        if (!ev.active || ev.fireFrame > frame_) continue;

        if (ev.callback) ev.callback(ev.id, frame_, ev.userData);
        ++fired;
        ++stats_.eventsFired;

        if (ev.repeatCount > 0) {
            --ev.repeatCount;
            u64 interval = (u64)(ev.repeatIntervalSeconds / fixedDeltaSeconds_ + 0.5f);
            if (interval < 1) interval = 1;
            u64 next = ev.fireFrame + interval;
            ev.fireFrame = next > frame_ ? next : frame_ + 1;
        } else {
            ev.active = false;
        }
    }
    if (fired > 0) compactEvents();
}

void Chronos::compactEvents() {
    usize active = 0;
    for (usize i = 0; i < events_.size(); ++i) {
        if (events_[i].active) {
            events_[active] = events_[i];
            ++active;
        }
    }
    events_.resize(active);
}

u64 Chronos::createTimeline(const String& name) {
    Timeline tl;
    tl.name = name;
    timelines_.push_back(tl);
    return timelines_.size() - 1;
}

void Chronos::addKeyframe(u64 timelineIndex, u64 frame, f32 value) {
    if (timelineIndex >= timelines_.size()) return;
    Timeline& tl = timelines_[timelineIndex];
    tl.keyframes.push_back(Keyframe{ frame, value });
    usize i = tl.keyframes.size() - 1;
    while (i > 0 && tl.keyframes[i - 1].frame > tl.keyframes[i].frame) {
        std::swap(tl.keyframes[i - 1], tl.keyframes[i]);
        --i;
    }
}

f32 Chronos::sampleTimeline(u64 timelineIndex, u64 frame) const {
    if (timelineIndex >= timelines_.size()) return 0.0f;
    return timelines_[timelineIndex].evaluate(frame);
}

void Chronos::recordFrame(const void* stateData, u32 sizeBytes) {
    if (!enableReplay_ || replayCapacity_ == 0) return;
    if (stateData == nullptr || sizeBytes == 0) return;

    Vector<u8> snapshot;
    snapshot.resize(sizeBytes);
    memcpy(snapshot.data(), stateData, sizeBytes);

    replayBuffer_.push_back(std::move(snapshot));
    replayFrames_.push_back(frame_);
    ++stats_.replaysRecorded;

    while (replayBuffer_.size() > replayCapacity_) {
        replayBuffer_.erase(0);
        replayFrames_.erase(0);
    }
}

const u8* Chronos::seekToFrame(u64 frame) const {
    for (usize i = replayFrames_.size(); i-- > 0;) {
        if (replayFrames_[i] == frame) return replayBuffer_[i].data();
    }
    return nullptr;
}

void Chronos::reset() {
    frame_ = 0;
    seconds_ = 0.0f;
    accumulator_ = 0.0f;
    realDeltaSeconds_ = 0.0f;
    lastDeltaUs_ = 0;
    events_.clear();
    timelines_.clear();
    replayBuffer_.clear();
    replayFrames_.clear();
    nextEventId_ = 1;
    stats_ = {};
}

} // namespace Frost
