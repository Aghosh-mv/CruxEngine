#include "Animation/AnimationClip.h"
#include <algorithm>
#include <cstring>
#include <numeric>

namespace Frost {

// ─── Keyframe sampling ──────────────────────────────────────────────────

template<typename T>
T AnimationClip::sampleKeyframes(const Vector<Keyframe<T>>& keys, f32 time) {
    if (keys.empty()) return T{};
    if (keys.size() == 1) return keys[0].value;
    if (time <= keys[0].time) return keys[0].value;
    if (time >= keys.back().time) return keys.back().value;

    for (usize i = 0; i < keys.size() - 1; i++) {
        if (time >= keys[i].time && time <= keys[i + 1].time) {
            f32 range = keys[i + 1].time - keys[i].time;
            if (range < Mathf::EPSILON) return keys[i].value;
            f32 t = (time - keys[i].time) / range;
            if constexpr (std::is_same_v<T, Vec3>) {
                return lerpVec3(keys[i].value, keys[i + 1].value, t);
            } else if constexpr (std::is_same_v<T, Quat>) {
                return Quat::slerp(keys[i].value, keys[i + 1].value, t);
            } else {
                return keys[i].value + (keys[i + 1].value - keys[i].value) * t;
            }
        }
    }
    return keys.back().value;
}

template<typename T>
T AnimationClip::sampleCubicSpline(const Vector<Keyframe<T>>& keys, f32 time) {
    if (keys.empty()) return T{};
    if (keys.size() == 1) return keys[0].value;
    if (time <= keys[0].time) return keys[0].value;
    if (time >= keys.back().time) return keys.back().value;

    for (usize i = 0; i < keys.size() - 1; i++) {
        if (time >= keys[i].time && time <= keys[i + 1].time) {
            f32 range = keys[i + 1].time - keys[i].time;
            if (range < Mathf::EPSILON) return keys[i].value;
            f32 t = (time - keys[i].time) / range;
            f32 tt = t * t;
            f32 ttt = tt * t;

            f32 h1 = 2.0f * ttt - 3.0f * tt + 1.0f;
            f32 h2 = -2.0f * ttt + 3.0f * tt;
            f32 h3 = ttt - 2.0f * tt + t;
            f32 h4 = ttt - tt;

            if constexpr (std::is_same_v<T, Vec3>) {
                Vec3 p0 = keys[i].value;
                Vec3 p1 = keys[i + 1].value;
                Vec3 m0 = keys[i].outTangent * range;
                Vec3 m1 = keys[i + 1].inTangent * range;
                return p0 * h1 + p1 * h2 + m0 * h3 + m1 * h4;
            } else if constexpr (std::is_same_v<T, Quat>) {
                return Quat::slerp(keys[i].value, keys[i + 1].value, t);
            } else {
                return keys[i].value * h1 + keys[i + 1].value * h2;
            }
        }
    }
    return keys.back().value;
}

// ─── Private helpers ────────────────────────────────────────────────────

template<typename T>
static T AnimationClip::lerpVec3(const Vec3& a, const Vec3& b, f32 t) {
    return Vec3(
        Mathf::lerp(a.x, b.x, t),
        Mathf::lerp(a.y, b.y, t),
        Mathf::lerp(a.z, b.z, t));
}

template<typename T>
static Vec3 AnimationClip::catmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, f32 t) {
    f32 t2 = t * t;
    f32 t3 = t2 * t;
    return Vec3(
        0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
        0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3),
        0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t + (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 + (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3));
}

template<typename T>
static Quat AnimationClip::squad(const Quat& s0, const Quat& s1, const Quat& s2, const Quat& s3, f32 t) {
    Quat q1 = Quat::slerp(s0, s1, t);
    Quat q2 = Quat::slerp(s2, s3, t);
    return Quat::slerp(q1, q2, 2.0f * t * (1.0f - t));
}

// ─── Public methods ─────────────────────────────────────────────────────

inline void AnimationClip::addTrack(u32 boneIndex) {
    tracks_.emplace_back(boneIndex, {});
}

inline void AnimationClip::addKeyframe(u32 trackId, f32 time, const Vec3& pos, const Vec3& rot, const Vec3& scale) {
    if (trackId >= tracks_.size()) return;
    Vector<Keyframe<Vec3>>& posKeys = positionKeys[trackId];
    Vector<Keyframe<Quat>>& rotKeys = rotationKeys[trackId];
    Vector<Keyframe<Vec3>>& sclKeys = scaleKeys[trackId];

    if (posKeys.empty() || posKeys.back().time < time) {
        posKeys.push_back(Keyframe<Vec3>{time, pos, Rotator::fromQuat(Quat::identity()).toVec3(), scale});
    }
    if (rotKeys.empty() || rotKeys.back().time < time) {
        rotKeys.push_back(Keyframe<Quat>{time, rot, Quat::identity(), Quat::identity()});
    }
    if (sclKeys.empty() || sclKeys.back().time < time) {
        sclKeys.push_back(Keyframe<Vec3>{time, scale, Vec3::identity(), Vec3::identity()});
    }
}

inline void AnimationClip::setDuration(f32 d) { duration_ = d; }

inline f32 AnimationClip::getDuration() const { return duration_; }

inline void AnimationClip::setLooping(bool l) { looping_ = l; }

inline bool AnimationClip::isLooping() const { return looping_; }

inline f32 AnimationClip::getTicksPerSecond() const { return ticksPerSecond_; }

inline void AnimationClip::advance(f32 dt) {
    playbackTime_ += dt;
    if (looping_) {
        playbackTime_ = std::fmod(playbackTime_, duration_);
    } else {
        playbackTime_ = Mathf::clamp(playbackTime_, 0.0f, duration_);
    }
    lastSampleTime_ = playbackTime_;
}

inline void AnimationClip::setPlaybackTime(f32 t) {
    playbackTime_ = Mathf::clamp(t, 0.0f, duration_);
    lastSampleTime_ = playbackTime_;
}

inline f32 AnimationClip::getPlaybackTime() const { return playbackTime_; }

inline u32 AnimationClip::getActiveBoneCount() const {
    u32 count = 0;
    for (u32 i = 0; i < tracks_.size(); i++) {
        if (!tracks_[i].keyframes.empty()) count++;
    }
    return count;
}

inline u32 AnimationClip::getTrackCount() const { return tracks_.size(); }

inline void AnimationClip::resetTime() {
    playbackTime_ = 0.0f;
    lastSampleTime_ = 0.0f;
}

inline f32 AnimationClip::getActiveBoneKeyframeTime(u32 boneIndex) const {
    if (boneIndex >= tracks_.size() || tracks_[boneIndex].keyframes.empty()) return 0.0f;
    return tracks_[boneIndex].keyframes.back().time;
}

inline bool AnimationClip::retarget(const AnimationClip& source, const HashMap& boneRemap) {
    // Try to remap bones from source clip
    return false; // TODO: implement bone remapping
}

inline f32 AnimationClip::estimateMemoryUsage() const {
    u32 bytes = sizeof(AnimationClip);
    for (u32 b = 0; b < positionKeys.size(); b++) {
        bytes += (u32)(positionKeys[b].size() * sizeof(Keyframe<Vec3>));
    }
    for (u32 b = 0; b < rotationKeys.size(); b++) {
        bytes += (u32)(rotationKeys[b].size() * sizeof(Keyframe<Quat>));
    }
    for (u32 b = 0; b < scaleKeys.size(); b++) {
        bytes += (u32)(scaleKeys[b].size() * sizeof(Keyframe<Vec3>));
    }
    return bytes;
}

inline bool AnimationClip::hasPositionKeyframes() const {
    return !positionKeys.empty();
}

inline void AnimationClip::ensureBoneKeyframe(u32 boneIndex, f32 time, Vec3& pos, Quat& rot, Vec3& scl) {
    if (boneIndex < positionKeys.size() && !positionKeys[boneIndex].empty()) {
        pos = sampleKeyframes(positionKeys[boneIndex], time);
        rot = sampleKeyframes(rotationKeys[boneIndex], time);
        scl = sampleKeyframes(scaleKeys[boneIndex], time);
    }
}

inline void AnimationClip::getPositions(f32 time, u32 boneIndex, Vec3& out) const {
    if (boneIndex < positionKeys.size() && !positionKeys[boneIndex].empty()) {
        out = sampleCubicSpline(positionKeys[boneIndex], time);
    }
}

inline void AnimationClip::getRotations(f32 time, u32 boneIndex, Quat& out) const {
    if (boneIndex < rotationKeys.size() && !rotationKeys[boneIndex].empty()) {
        out = sampleCubicSpline(rotationKeys[boneIndex], time);
    }
}

inline void AnimationClip::getScales(f32 time, u32 boneIndex, Vec3& out) const {
    if (boneIndex < scaleKeys.size() && !scaleKeys[boneIndex].empty()) {
        out = sampleCubicSpline(scaleKeys[boneIndex], time);
    }
}

inline void AnimationClip::getPositionAtKeyframe(u32 boneIndex, u32 keyframeIndex, Vec3& out) const {
    if (boneIndex < positionKeys.size() && keyframeIndex < positionKeys[boneIndex].size()) {
        out = positionKeys[boneIndex][keyframeIndex].value;
    }
}

inline void AnimationClip::getRotationAtKeyframe(u32 boneIndex, u32 keyframeIndex, Quat& out) const {
    if (boneIndex < rotationKeys.size() && keyframeIndex < rotationKeys[boneIndex].size()) {
        out = rotationKeys[boneIndex][keyframeIndex].value;
    }
}

inline void AnimationClip::getScaleAtKeyframe(u32 boneIndex, u32 keyframeIndex, Vec3& out) const {
    if (boneIndex < scaleKeys.size() && keyframeIndex < scaleKeys[boneIndex].size()) {
        out = scaleKeys[boneIndex][keyframeIndex].value;
    }
}

inline f32 AnimationClip::normalizeTime(f32 time) const {
    if (looping_) {
        return std::fmod(time, duration_);
    }
    return Mathf::clamp(time, 0.0f, duration_);
}

inline void AnimationClip::resetEvents() {
    for (auto& e : events) {
        e.fired = false;
    }
}

inline void AnimationClip::addEvent(f32 time, const char* name, f32 parameter) {
    AnimationEvent evt;
    evt.time = time;
    evt.name = String(name);
    evt.parameter = parameter;
    evt.fired = false;
    events.pushBack(evt);
}

inline void AnimationClip::removeEvent(u32 index) {
    if (index < events.size()) {
        events.erase(index);
    }
}

inline void AnimationClip::sortEvents() {
    for (u32 i = 0; i < events.size() - 1; i++) {
        for (u32 j = i + 1; j < events.size(); j++) {
            if (events[j].time < events[i].time) {
                AnimationEvent tmp = events[i];
                events[i] = events[j];
                events[j] = tmp;
            }
        }
    }
}

inline f32 AnimationClip::getKeyframeTime(u32 boneIndex, u32 channel, u32 keyframeIndex) const {
    const Vector<Keyframe<Vec3>>* posKeys = nullptr;
    const Vector<Keyframe<Quat>>* rotKeys = nullptr;
    const Vector<Keyframe<Vec3>>* sclKeys = nullptr;

    if (channel == 0 && boneIndex < positionKeys.size()) posKeys = &positionKeys[boneIndex];
    else if (channel == 1 && boneIndex < rotationKeys.size()) rotKeys = &rotationKeys[boneIndex];
    else if (channel == 2 && boneIndex < scaleKeys.size()) sclKeys = &scaleKeys[boneIndex];

    if (posKeys && keyframeIndex < posKeys->size()) return (*posKeys)[keyframeIndex].time;
    if (rotKeys && keyframeIndex < rotKeys->size()) return (*rotKeys)[keyframeIndex].time;
    if (sclKeys && keyframeIndex < sclKeys->size()) return (*sclKeys)[keyframeIndex].time;
    return 0.0f;
}

inline u32 AnimationClip::getKeyframeCount(u32 boneIndex, u32 channel) const {
    if (channel == 0 && boneIndex < positionKeys.size()) return (u32)positionKeys[boneIndex].size();
    if (channel == 1 && boneIndex < rotationKeys.size()) return (u32)rotationKeys[boneIndex].size();
    if (channel == 2 && boneIndex < scaleKeys.size()) return (u32)scaleKeys[boneIndex].size();
    return 0;
}

inline void AnimationClip::compress(f32 positionThreshold, f32 rotationThreshold, f32 scaleThreshold) {
    removeRedundantKeyframes(positionThreshold);
}

inline void AnimationClip::removeRedundantKeyframes(f32 threshold) {
    for (u32 b = 0; b < positionKeys.size(); b++) {
        if (positionKeys[b].size() <= 2) continue;
        Vector<Keyframe<Vec3>> filtered;
        filtered.pushBack(positionKeys[b][0]);
        for (u32 i = 1; i < positionKeys[b].size() - 1; i++) {
            Vec3 diff = positionKeys[b][i].value - positionKeys[b][i - 1].value;
            if (diff.lengthSquared() > threshold * threshold) {
                filtered.pushBack(positionKeys[b][i]);
            }
        }
        filtered.pushBack(positionKeys[b].back());
        positionKeys[b] = filtered;
    }
    for (u32 b = 0; b < rotationKeys.size(); b++) {
        if (rotationKeys[b].size() <= 2) continue;
        Vector<Keyframe<Quat>> filtered;
        filtered.pushBack(rotationKeys[b][0]);
        for (u32 i = 1; i < rotationKeys[b].size() - 1; i++) {
            f32 dot = std::abs(rotationKeys[b][i].value.x * rotationKeys[b][i - 1].value.x +
                               rotationKeys[b][i].value.y * rotationKeys[b][i - 1].value.y +
                               rotationKeys[b][i].value.z * rotationKeys[b][i - 1].value.z +
                               rotationKeys[b][i].value.w * rotationKeys[b][i - 1].value.w);
            if (dot < (1.0f - threshold)) {
                filtered.pushBack(rotationKeys[b][i]);
            }
        }
        filtered.pushBack(rotationKeys[b].back());
        rotationKeys[b] = filtered;
    }
}

} // namespace Frost
