#include "Core/Noise.h"

namespace Crux {

Noise::Noise(u32 seed) : seed_(seed) {}

void Noise::setSeed(u32 s) { seed_ = s; }

u32 Noise::hash2(u32 x, u32 y) const {
    u32 h = seed_ * 374761393u + x * 668265263u + y * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

u32 Noise::hash3(u32 x, u32 y, u32 z) const {
    u32 h = seed_ * 374761393u + x * 3266489917u + y * 2246822519u + z * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

f32 Noise::grad(i32 h, f32 x, f32 y) const {
    switch (h & 7) {
        case 0: return x + y;
        case 1: return -x + y;
        case 2: return x - y;
        case 3: return -x - y;
        case 4: return x;
        case 5: return -x;
        case 6: return y;
        default: return -y;
    }
}

f32 Noise::grad(i32 h, f32 x, f32 y, f32 z) const {
    switch (h & 15) {
        case 0: return x + y;
        case 1: return -x + y;
        case 2: return x - y;
        case 3: return -x - y;
        case 4: return x + z;
        case 5: return -x + z;
        case 6: return x - z;
        case 7: return -x - z;
        case 8: return y + z;
        case 9: return -y + z;
        case 10: return y - z;
        case 11: return -y - z;
        case 12: return x + y;
        case 13: return -x + y;
        case 14: return x - y;
        default: return -x - y;
    }
}

f32 Noise::perlin(f32 x, f32 y) const {
    i32 xi = (i32)std::floor(x);
    i32 yi = (i32)std::floor(y);
    f32 xf = x - (f32)xi;
    f32 yf = y - (f32)yi;
    f32 u = fade(xf);
    f32 v = fade(yf);
    f32 n00 = grad((i32)hash2((u32)xi, (u32)yi), xf, yf);
    f32 n10 = grad((i32)hash2((u32)(xi + 1), (u32)yi), xf - 1, yf);
    f32 n01 = grad((i32)hash2((u32)xi, (u32)(yi + 1)), xf, yf - 1);
    f32 n11 = grad((i32)hash2((u32)(xi + 1), (u32)(yi + 1)), xf - 1, yf - 1);
    return lerp(lerp(n00, n10, u), lerp(n01, n11, u), v);
}

f32 Noise::perlin(f32 x, f32 y, f32 z) const {
    i32 xi = (i32)std::floor(x);
    i32 yi = (i32)std::floor(y);
    i32 zi = (i32)std::floor(z);
    f32 xf = x - (f32)xi;
    f32 yf = y - (f32)yi;
    f32 zf = z - (f32)zi;
    f32 u = fade(xf);
    f32 v = fade(yf);
    f32 w = fade(zf);

    f32 c000 = grad((i32)hash3((u32)xi, (u32)yi, (u32)zi), xf, yf, zf);
    f32 c100 = grad((i32)hash3((u32)(xi + 1), (u32)yi, (u32)zi), xf - 1, yf, zf);
    f32 c010 = grad((i32)hash3((u32)xi, (u32)(yi + 1), (u32)zi), xf, yf - 1, zf);
    f32 c110 = grad((i32)hash3((u32)(xi + 1), (u32)(yi + 1), (u32)zi), xf - 1, yf - 1, zf);
    f32 c001 = grad((i32)hash3((u32)xi, (u32)yi, (u32)(zi + 1)), xf, yf, zf - 1);
    f32 c101 = grad((i32)hash3((u32)(xi + 1), (u32)yi, (u32)(zi + 1)), xf - 1, yf, zf - 1);
    f32 c011 = grad((i32)hash3((u32)xi, (u32)(yi + 1), (u32)(zi + 1)), xf, yf - 1, zf - 1);
    f32 c111 = grad((i32)hash3((u32)(xi + 1), (u32)(yi + 1), (u32)(zi + 1)), xf - 1, yf - 1, zf - 1);

    f32 x00 = lerp(c000, c100, u);
    f32 x10 = lerp(c010, c110, u);
    f32 x01 = lerp(c001, c101, u);
    f32 x11 = lerp(c011, c111, u);
    f32 y0 = lerp(x00, x10, v);
    f32 y1 = lerp(x01, x11, v);
    return lerp(y0, y1, w);
}

f32 Noise::value(f32 x, f32 y) const {
    i32 xi = (i32)std::floor(x);
    i32 yi = (i32)std::floor(y);
    f32 xf = x - (f32)xi;
    f32 yf = y - (f32)yi;
    f32 u = fade(xf);
    f32 v = fade(yf);
    f32 a = (f32)(hash2((u32)xi, (u32)yi) & 0xFFFF) / 65535.0f;
    f32 b = (f32)(hash2((u32)(xi + 1), (u32)yi) & 0xFFFF) / 65535.0f;
    f32 c = (f32)(hash2((u32)xi, (u32)(yi + 1)) & 0xFFFF) / 65535.0f;
    f32 d = (f32)(hash2((u32)(xi + 1), (u32)(yi + 1)) & 0xFFFF) / 65535.0f;
    return lerp(lerp(a, b, u), lerp(c, d, u), v);
}

f32 Noise::fbm2(f32 x, f32 y, i32 octaves, f32 lacunarity, f32 gain) const {
    f32 amp = 0.5f;
    f32 freq = 1.0f;
    f32 sum = 0.0f;
    f32 norm = 0.0f;
    for (i32 i = 0; i < octaves; i++) {
        sum += amp * perlin(x * freq, y * freq);
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return sum / norm;
}

f32 Noise::fbm3(f32 x, f32 y, f32 z, i32 octaves, f32 lacunarity, f32 gain) const {
    f32 amp = 0.5f;
    f32 freq = 1.0f;
    f32 sum = 0.0f;
    f32 norm = 0.0f;
    for (i32 i = 0; i < octaves; i++) {
        sum += amp * perlin(x * freq, y * freq, z * freq);
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return sum / norm;
}

f32 Noise::ridged(f32 x, f32 y, i32 octaves) const {
    f32 amp = 0.5f;
    f32 freq = 1.0f;
    f32 sum = 0.0f;
    f32 norm = 0.0f;
    for (i32 i = 0; i < octaves; i++) {
        f32 n = 1.0f - std::abs(perlin(x * freq, y * freq));
        n *= n;
        sum += amp * n;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

f32 Noise::billow(f32 x, f32 y, i32 octaves) const {
    f32 amp = 0.5f;
    f32 freq = 1.0f;
    f32 sum = 0.0f;
    f32 norm = 0.0f;
    for (i32 i = 0; i < octaves; i++) {
        f32 n = std::abs(perlin(x * freq, y * freq));
        sum += amp * n;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

f32 Noise::worley(f32 x, f32 y, f32 z) const {
    f32 minDist = 1e9f;
    i32 xi = (i32)std::floor(x);
    i32 yi = (i32)std::floor(y);
    i32 zi = (i32)std::floor(z);
    for (i32 dx = -1; dx <= 1; dx++) {
        for (i32 dy = -1; dy <= 1; dy++) {
            for (i32 dz = -1; dz <= 1; dz++) {
                i32 cx = xi + dx, cy = yi + dy, cz = zi + dz;
                u32 h = hash3((u32)cx, (u32)cy, (u32)cz);
                f32 px = (f32)(h & 0xFF) / 255.0f;
                f32 py = (f32)((h >> 8) & 0xFF) / 255.0f;
                f32 pz = (f32)((h >> 16) & 0xFF) / 255.0f;
                f32 dxf = (cx + px) - x;
                f32 dyf = (cy + py) - y;
                f32 dzf = (cz + pz) - z;
                f32 d = dxf * dxf + dyf * dyf + dzf * dzf;
                if (d < minDist) minDist = d;
            }
        }
    }
    return std::sqrt(minDist);
}

}
