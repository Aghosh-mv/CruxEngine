#pragma once

#include "Core/Types.h"
#include "Core/Math.h"

namespace Frost {

// Deterministic multi-octave value/gradient noise used for terrain, clouds,
// wind fields and procedural textures. Seeded per-instance so worlds repeat.
class Noise {
public:
    Noise(u32 seed = 1337);

    // Perlin-style smooth gradient noise in [-1, 1]
    f32 perlin(f32 x, f32 y) const;
    f32 perlin(f32 x, f32 y, f32 z) const;

    // Value noise in [0, 1]
    f32 value(f32 x, f32 y) const;

    // Fractal brownian motion, amplitude-weighted sum of octaves.
    f32 fbm2(f32 x, f32 y, i32 octaves = 4, f32 lacunarity = 2.0f, f32 gain = 0.5f) const;
    f32 fbm3(f32 x, f32 y, f32 z, i32 octaves = 4, f32 lacunarity = 2.0f, f32 gain = 0.5f) const;

    // Ridged multifractal (mountain ridges), output [0, 1]
    f32 ridged(f32 x, f32 y, i32 octaves = 4) const;

    // Billow noise, rounded hills, output [0, 1]
    f32 billow(f32 x, f32 y, i32 octaves = 4) const;

    // Worley / cellular: distance to nearest feature point. Returns value in [0,1]
    f32 worley(f32 x, f32 y, f32 z) const;

    u32 seed() const { return seed_; }
    void setSeed(u32 s);

private:
    u32 hash2(u32 x, u32 y) const;
    u32 hash3(u32 x, u32 y, u32 z) const;
    f32 grad(i32 h, f32 x, f32 y) const;
    f32 grad(i32 h, f32 x, f32 y, f32 z) const;
    f32 fade(f32 t) const { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
    f32 lerp(f32 a, f32 b, f32 t) const { return a + t * (b - a); }

    u32 seed_;
};

}
