#pragma once

#include "Core/Types.h"
#include "Core/Math.h"

namespace Frost {

// Fast, seedable PRNG (splitmix64) with a small cache-friendly helper API
// used across the engine for procedural content and gameplay.
class Random {
public:
    Random(u64 seed = 0x9E3779B97F4A7C15ull) : state_(seed ? seed : 0x9E3779B97F4A7C15ull) {}

    void seed(u64 s) { state_ = s ? s : 0x9E3779B97F4A7C15ull; }
    u64 nextU64() {
        u64 z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    u32 nextU32() { return (u32)(nextU64() >> 32); }

    // [0, 1)
    f32 nextF32() { return (f32)(nextU64() >> 40) * (1.0f / 16777216.0f); }

    // [min, max)
    f32 range(f32 min, f32 max) { return min + nextF32() * (max - min); }

    // [min, max] inclusive integer
    i32 rangeInt(i32 min, i32 max) {
        u64 span = (u64)(max - min) + 1u;
        return min + (i32)(nextU64() % span);
    }

    // Standard normal-ish by central limit (sum of 3 uniforms)
    f32 gaussian(f32 mean = 0.0f, f32 stddev = 1.0f) {
        f32 v = (nextF32() + nextF32() + nextF32()) / 3.0f;
        return mean + (v - 0.5f) * 6.9282032302f * stddev; // 2*sqrt(12)
    }

    Vec3 direction() {
        f32 y = range(-1.0f, 1.0f);
        f32 az = range(0.0f, Mathf::TWO_PI);
        f32 r = std::sqrt(std::max(0.0f, 1.0f - y * y));
        return Vec3(std::cos(az) * r, y, std::sin(az) * r);
    }

    bool chance(f32 probability) { return nextF32() < probability; }

private:
    u64 state_;
};

}
