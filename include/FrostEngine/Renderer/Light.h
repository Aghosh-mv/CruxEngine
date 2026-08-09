#pragma once

#include "Core/Types.h"
#include "Core/Math.h"

namespace Frost {

enum class LightType : u8 { Directional, Point, Spot };

struct Light {
    LightType type = LightType::Directional;
    Color color{ 1, 1, 1, 1 };
    f32 intensity = 1.0f;
    Vec3 position{ 0, 0, 0 };
    Vec3 direction{ 0, -1, 0 };       // points where light travels
    f32 range = 30.0f;                // point/spot attenuation radius
    f32 spotAngleInner = 15.0f;       // degrees
    f32 spotAngleOuter = 35.0f;       // degrees
    bool castShadow = false;
    f32 shadowBias = 0.002f;
    f32 constantAtten = 1.0f;
    f32 linearAtten = 0.09f;
    f32 quadraticAtten = 0.032f;

    static Light directional(const Vec3& dir, const Color& c, f32 intensity) {
        Light l;
        l.type = LightType::Directional;
        l.direction = dir.normalized();
        l.color = c;
        l.intensity = intensity;
        return l;
    }
    static Light point(const Vec3& pos, const Color& c, f32 intensity, f32 range) {
        Light l;
        l.type = LightType::Point;
        l.position = pos;
        l.color = c;
        l.intensity = intensity;
        l.range = range;
        return l;
    }
    static Light spot(const Vec3& pos, const Vec3& dir, const Color& c, f32 intensity, f32 range) {
        Light l;
        l.type = LightType::Spot;
        l.position = pos;
        l.direction = dir.normalized();
        l.color = c;
        l.intensity = intensity;
        l.range = range;
        return l;
    }
};

}
