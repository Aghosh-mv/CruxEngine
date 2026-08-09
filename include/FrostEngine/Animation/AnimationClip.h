#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Math.h"

namespace Frost {

template<typename T>
struct Keyframe {
    f32 time = 0.0f;
    T value{};
    T inTangent{};
    T outTangent{};
};

struct AnimationEvent {
    f32 time = 0.0f;
    String name;
    bool fired = false;
    f32 parameter = 0.0f;
};

struct AnimationClip {
    String name;
    Vector<Vector<Keyframe<Vec3>>> positionKeys;
    Vector<Vector<Keyframe<Quat>>> rotationKeys;
    Vector<Vector<Keyframe<Vec3>>> scaleKeys;
    Vector<AnimationEvent> events;
    f32 duration = 1.0f;
    f32 ticksPerSecond = 30.0f;
    bool looping = true;

    f32 getDuration() const { return duration; }
    f32 getTicksPerSecond() const { return ticksPerSecond; }

    void getPositions(f32 time, u32 boneIndex, Vec3& out) const;
    void getRotations(f32 time, u32 boneIndex, Quat& out) const;
    void getScales(f32 time, u32 boneIndex, Vec3& out) const;

    void getPositionAtKeyframe(u32 boneIndex, u32 keyframeIndex, Vec3& out) const;
    void getRotationAtKeyframe(u32 boneIndex, u32 keyframeIndex, Quat& out) const;
    void getScaleAtKeyframe(u32 boneIndex, u32 keyframeIndex, Vec3& out) const;

    f32 normalizeTime(f32 time) const;
    void resetEvents();
    bool hasEvents() const { return !events.empty(); }
    void addEvent(f32 time, const char* name, f32 parameter = 0.0f);
    void removeEvent(u32 index);
    void sortEvents();

    f32 getKeyframeTime(u32 boneIndex, u32 channel, u32 keyframeIndex) const;
    u32 getKeyframeCount(u32 boneIndex, u32 channel) const;

    void compress(f32 positionThreshold = 0.001f, f32 rotationThreshold = 0.001f, f32 scaleThreshold = 0.001f);
    void removeRedundantKeyframes(f32 threshold = 0.0001f);
    u32 estimateMemoryUsage() const;

private:
    template<typename T>
    static T sampleKeyframes(const Vector<Keyframe<T>>& keys, f32 time);
    template<typename T>
    static T sampleCubicSpline(const Vector<Keyframe<T>>& keys, f32 time);
    static Vec3 lerpVec3(const Vec3& a, const Vec3& b, f32 t);
    static Vec3 catmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, f32 t);
    static Quat squad(const Quat& s0, const Quat& s1, const Quat& s2, const Quat& s3, f32 t);
};

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

inline Vec3 AnimationClip::lerpVec3(const Vec3& a, const Vec3& b, f32 t) {
    return Vec3(
        Mathf::lerp(a.x, b.x, t),
        Mathf::lerp(a.y, b.y, t),
        Mathf::lerp(a.z, b.z, t));
}

inline Vec3 AnimationClip::catmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, f32 t) {
    f32 t2 = t * t;
    f32 t3 = t2 * t;
    return Vec3(
        0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
        0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3),
        0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t + (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 + (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3));
}

inline Quat AnimationClip::squad(const Quat& s0, const Quat& s1, const Quat& s2, const Quat& s3, f32 t) {
    Quat q1 = Quat::slerp(s0, s1, t);
    Quat q2 = Quat::slerp(s2, s3, t);
    return Quat::slerp(q1, q2, 2.0f * t * (1.0f - t));
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
    if (looping) {
        return std::fmod(time, duration);
    }
    return Mathf::clamp(time, 0.0f, duration);
}

inline void AnimationClip::resetEvents() {
    for (auto& e : events) e.fired = false;
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

inline void AnimationClip::compress(f32 positionThreshold, f32 rotationThreshold, f32 scaleThreshold) {
    removeRedundantKeyframes(positionThreshold);
}

inline u32 AnimationClip::estimateMemoryUsage() const {
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

}
