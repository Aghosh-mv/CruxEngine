#pragma once

// ============================================================================
// CruxEngine Lighting System — Full lighting pipeline
// ============================================================================
// Supports: directional, point, spot, area lights, environment probes,
// IBL (image-based lighting), light cookies, volumetric shadows.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Vec3.h"
#include "Core/Math.h"

namespace Crux {

enum class LightType : u8 {
    Directional = 0,
    Point,
    Spot,
    Area,       // rectangle area light
    Disk,       // disk area light
    Tube,       // tube/strip light
    COUNT
};

struct LightData {
    LightType type = LightType::Directional;
    Vec3 position{0, 10, 0};
    Vec3 direction{0, -1, 0};
    Vec3 color{1, 0.96f, 0.9f};
    f32 intensity = 3.0f;
    f32 range = 50.0f;

    // Spot light
    f32 innerConeAngle = 30.0f;    // degrees
    f32 outerConeAngle = 45.0f;

    // Area light
    f32 width = 1.0f;
    f32 height = 1.0f;
    f32 radius = 0.5f;             // for disk

    // Shadows
    bool castShadow = true;
    u32 shadowResolution = 2048;
    f32 shadowBias = 0.0005f;
    f32 shadowNormalBias = 0.02f;
    f32 shadowSoftness = 1.0f;

    // Cookies
    bool hasCookie = false;
    u32 cookieTexture = 0;
    f32 cookieIntensity = 1.0f;

    // Volumetric
    bool volumetric = false;
    f32 volumetricIntensity = 0.5f;

    // Falloff
    f32 falloffExponent = 2.0f;

    // Animation
    bool animated = false;
    f32 animSpeed = 1.0f;
    f32 flickerAmount = 0.0f;
    f32 flickerSpeed = 5.0f;

    bool enabled = true;

    f32 attenuation(f32 dist) const {
        f32 atten = 1.0f / (1.0f + powf(dist / range, falloffExponent));
        return atten * (dist < range ? 1.0f : 0.0f);
    }

    f32 spotAttenuation(Vec3 toLight) const {
        f32 cosAngle = Vec3::dot(toLight.normalized(), direction.normalized());
        f32 innerCos = cosf(innerConeAngle * 0.017453f);
        f32 outerCos = cosf(outerConeAngle * 0.017453f);
        return Mathf::clamp((cosAngle - outerCos) / (innerCos - outerCos), 0.0f, 1.0f);
    }
};

class LightingSystem {
public:
    static constexpr u32 MAX_LIGHTS = 256;

    bool init() {
        // Default directional light (sun)
        sun_ = LightData{};
        sun_.type = LightType::Directional;
        sun_.direction = Vec3(0.5f, -0.8f, -0.3f).normalized();
        sun_.color = Vec3(1.0f, 0.96f, 0.9f);
        sun_.intensity = 3.0f;
        sun_.castShadow = true;
        return true;
    }

    u32 addLight(const LightData& light) {
        if (lightCount_ >= MAX_LIGHTS) return 0xFFFFFFFF;
        u32 idx = lightCount_++;
        lights_[idx] = light;
        return idx;
    }

    void removeLight(u32 idx) {
        if (idx >= lightCount_) return;
        lights_[idx] = lights_[--lightCount_];
    }

    LightData& light(u32 idx) { return lights_[idx]; }
    const LightData& light(u32 idx) const { return lights_[idx]; }

    LightData& sun() { return sun_; }
    const LightData& sun() const { return sun_; }

    u32 lightCount() const { return lightCount_; }

    // ---- Light baking: compute static light contribution for a point ----
    Vec3 evaluatePoint(Vec3 worldPos, Vec3 normal) const {
        Vec3 totalLight = Vec3(0, 0, 0);

        // Sun
        {
            Vec3 L = -sun_.direction;
            f32 NdotL = Mathf::max(Vec3::dot(normal, L), 0.0f);
            totalLight = totalLight + sun_.color * sun_.intensity * NdotL;
        }

        // All other lights
        for (u32 i = 0; i < lightCount_; i++) {
            const LightData& l = lights_[i];
            if (!l.enabled) continue;

            Vec3 toLight = l.position - worldPos;
            f32 dist = toLight.length();
            Vec3 L = toLight / dist;
            f32 NdotL = Mathf::max(Vec3::dot(normal, L), 0.0f);

            switch (l.type) {
            case LightType::Point: {
                f32 atten = l.attenuation(dist);
                totalLight = totalLight + l.color * l.intensity * NdotL * atten;
                break;
            }
            case LightType::Spot: {
                f32 atten = l.attenuation(dist);
                f32 spot = l.spotAttenuation(toLight);
                totalLight = totalLight + l.color * l.intensity * NdotL * atten * spot;
                break;
            }
            default: break;
            }
        }

        return totalLight;
    }

    // ---- Ambient hemispheric lighting ----
    Vec3 ambientHemisphere(Vec3 normal, Vec3 skyColor, Vec3 groundColor, f32 intensity) const {
        f32 up = normal.y * 0.5f + 0.5f;
        return (groundColor * (1.0f - up) + skyColor * up) * intensity;
    }

private:
    LightData sun_;
    LightData lights_[MAX_LIGHTS];
    u32 lightCount_ = 0;
};

// ---- Environment Probe for IBL ----
struct EnvironmentProbe {
    u32 cubemapTexture = 0;
    u32 irradianceMap = 0;
    u32 prefilterMap = 0;
    u32 brdfLUT = 0;
    Vec3 position{0, 0, 0};
    f32 radius = 100.0f;
    bool dirty = true;
    u32 resolution = 128;

    // Paraboloid mapping for local probes
    bool isLocal = false;
};

class ProbeSystem {
public:
    static constexpr u32 MAX_PROBES = 64;

    u32 addProbe(const EnvironmentProbe& probe) {
        if (probeCount_ >= MAX_PROBES) return 0xFFFFFFFF;
        u32 idx = probeCount_++;
        probes_[idx] = probe;
        return idx;
    }

    void removeProbe(u32 idx) {
        if (idx >= probeCount_) return;
        probes_[idx] = probes_[--probeCount_];
    }

    EnvironmentProbe& probe(u32 idx) { return probes_[idx]; }
    u32 probeCount() const { return probeCount_; }

    // Find the best probe for a world position
    const EnvironmentProbe* findBest(Vec3 pos) const {
        const EnvironmentProbe* best = nullptr;
        f32 bestDist = 1e30f;
        for (u32 i = 0; i < probeCount_; i++) {
            f32 d = (probes_[i].position - pos).length();
            if (probes_[i].isLocal && d > probes_[i].radius) continue;
            if (d < bestDist) {
                bestDist = d;
                best = &probes_[i];
            }
        }
        return best;
    }

private:
    EnvironmentProbe probes_[MAX_PROBES];
    u32 probeCount_ = 0;
};

} // namespace Crux
