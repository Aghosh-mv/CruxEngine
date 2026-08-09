#include "Assets/TextureFactory.h"
#include "Core/Math.h"

namespace Frost {

TextureFactory::TextureFactory() : noise_(20260708u) {}

u8* TextureFactory::alloc() {
    return new u8[kSize * kSize * 4];
}

void TextureFactory::fillSolid(u8* dst, u8 r, u8 g, u8 b, u8 a) {
    for (u32 i = 0; i < kSize * kSize; i++) {
        dst[i * 4 + 0] = r; dst[i * 4 + 1] = g;
        dst[i * 4 + 2] = b; dst[i * 4 + 3] = a;
    }
}

u8 TextureFactory::clamp255(i32 v) { return v < 0 ? 0 : (v > 255 ? 255 : (u8)v); }

Texture2D TextureFactory::makeWood() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 grain = noise_.fbm2((f32)x * 0.08f, (f32)y * 0.5f, 3);
            f32 ring = Mathf::saturate(std::sin((f32)x * 0.35f + grain * 2.0f) * 0.5f + 0.5f);
            u8 r = clamp255((i32)(140 + ring * 40 + grain * 30));
            u8 g = clamp255((i32)(95 + ring * 30 + grain * 20));
            u8 b = clamp255((i32)(55 + ring * 20 + grain * 12));
            u32 i = (y * kSize + x) * 4;
            data[i] = r; data[i + 1] = g; data[i + 2] = b; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeOakPlanks() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 plank = Mathf::smoothstep(0.85f, 1.0f, Mathf::saturate(std::sin((f32)y * 0.32f)));
            f32 v = noise_.fbm2((f32)x * 0.2f, (f32)y * 0.1f, 2);
            u8 r = clamp255((i32)(150 + v * 50 - plank * 40));
            u8 g = clamp255((i32)(100 + v * 35 - plank * 30));
            u8 b = clamp255((i32)(55 + v * 25 - plank * 15));
            u32 i = (y * kSize + x) * 4;
            data[i] = r; data[i + 1] = g; data[i + 2] = b; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeGrass() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 v = noise_.fbm2((f32)x * 0.18f, (f32)y * 0.18f, 4);
            f32 v2 = noise_.fbm2((f32)x * 0.9f, (f32)y * 0.9f, 2);
            u8 r = clamp255((i32)(45 + v * 35 + v2 * 15));
            u8 g = clamp255((i32)(110 + v * 60 + v2 * 25));
            u8 b = clamp255((i32)(40 + v * 25));
            u32 i = (y * kSize + x) * 4;
            data[i] = r; data[i + 1] = g; data[i + 2] = b; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeRock() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 v = noise_.fbm2((f32)x * 0.15f, (f32)y * 0.15f, 5);
            f32 w = noise_.worley((f32)x * 0.05f, (f32)y * 0.05f, 0.5f);
            u8 base = clamp255((i32)(120 + v * 60 + w * 40));
            u32 i = (y * kSize + x) * 4;
            data[i] = base; data[i + 1] = base; data[i + 2] = (u8)(base * 0.95f + 5); data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeStone() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 v = noise_.fbm2((f32)x * 0.1f, (f32)y * 0.1f, 4);
            u8 base = clamp255((i32)(140 + v * 40));
            u32 i = (y * kSize + x) * 4;
            data[i] = base; data[i + 1] = base; data[i + 2] = base + 8; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeMarble() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 veined = Mathf::saturate(std::sin((f32)y * 0.25f + noise_.fbm2((f32)x * 0.2f, (f32)y * 0.1f, 3) * 4.0f) * 0.5f + 0.5f);
            f32 v = noise_.value((f32)x * 0.02f, (f32)y * 0.02f);
            u8 base = clamp255((i32)(235 - veined * 25));
            u8 g = clamp255((i32)(235 - veined * 28));
            u8 b = clamp255((i32)(230 - veined * 20));
            u32 i = (y * kSize + x) * 4;
            data[i] = (u8)(base + v * 10); data[i + 1] = g; data[i + 2] = b; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeBrick() {
    u8* data = alloc();
    const u32 brickW = kSize / 4, brickH = kSize / 8;
    const u32 mortar = 4;
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            u32 row = y / brickH;
            u32 offset = (row % 2) ? (brickW / 2) : 0;
            u32 bx = (x + offset) % brickW;
            bool inBrick = (x % brickW) >= mortar && bx < brickW - mortar && (y % brickH) >= mortar;
            u8 r, g, b;
            if (inBrick) {
                f32 v = noise_.value((f32)x * 0.3f, (f32)y * 0.3f);
                r = clamp255((i32)(160 + v * 40));
                g = clamp255((i32)(70 + v * 20));
                b = clamp255((i32)(55 + v * 15));
            } else {
                r = 95; g = 92; b = 88;
            }
            u32 i = (y * kSize + x) * 4;
            data[i] = r; data[i + 1] = g; data[i + 2] = b; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeMetal() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 v = noise_.fbm2((f32)x * 0.25f, (f32)y * 0.05f, 3);
            u8 base = clamp255((i32)(160 + v * 80));
            u32 i = (y * kSize + x) * 4;
            data[i] = base; data[i + 1] = base; data[i + 2] = clamp255(base + 12); data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeRustyMetal() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 v = noise_.fbm2((f32)x * 0.1f, (f32)y * 0.1f, 4);
            f32 rust = Mathf::smoothstep(0.2f, 0.9f, v);
            u8 r = clamp255((i32)(170 * (1 - rust) + 140 * rust));
            u8 g = clamp255((i32)(170 * (1 - rust) + 60 * rust));
            u8 b = clamp255((i32)(170 * (1 - rust) + 30 * rust));
            u32 i = (y * kSize + x) * 4;
            data[i] = r; data[i + 1] = g; data[i + 2] = b; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeSand() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 v = noise_.fbm2((f32)x * 0.4f, (f32)y * 0.4f, 3);
            f32 ripple = noise_.fbm2((f32)x * 0.05f, (f32)y * 0.05f, 2);
            u8 r = clamp255((i32)(200 + v * 20 + ripple * 25));
            u8 g = clamp255((i32)(180 + v * 18 + ripple * 20));
            u8 b = clamp255((i32)(130 + v * 12 + ripple * 15));
            u32 i = (y * kSize + x) * 4;
            data[i] = r; data[i + 1] = g; data[i + 2] = b; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeSnow() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 v = noise_.fbm2((f32)x * 0.2f, (f32)y * 0.2f, 3);
            u8 base = clamp255((i32)(245 - v * 15));
            u32 i = (y * kSize + x) * 4;
            data[i] = base; data[i + 1] = base; data[i + 2] = clamp255(base + 10); data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeLava() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 v = noise_.fbm2((f32)x * 0.12f, (f32)y * 0.12f, 4);
            f32 crust = Mathf::smoothstep(0.0f, 0.25f, v);
            u8 r = clamp255((i32)(240 * (1 - crust) + 60 * crust));
            u8 g = clamp255((i32)(80 * (1 - crust) + 30 * crust));
            u8 b = clamp255((i32)(10 * (1 - crust) + 20 * crust));
            u32 i = (y * kSize + x) * 4;
            data[i] = r; data[i + 1] = g; data[i + 2] = b; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeLeaves() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 v = noise_.fbm2((f32)x * 0.3f, (f32)y * 0.3f, 3);
            f32 hole = noise_.worley((f32)x * 0.2f, (f32)y * 0.2f, 1.0f);
            u8 r = clamp255((i32)(40 + v * 30));
            u8 g = clamp255((i32)(90 + v * 50 + (hole > 0.6f ? -40 : 0)));
            u8 b = clamp255((i32)(35 + v * 20));
            u32 i = (y * kSize + x) * 4;
            data[i] = r; data[i + 1] = g; data[i + 2] = b; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeFabric() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 weave = (std::sin((f32)x * 0.5f) * std::sin((f32)y * 0.5f));
            u8 base = clamp255((i32)(120 + weave * 30));
            u8 r = clamp255((i32)(120 + weave * 25));
            u8 b = clamp255((i32)(120 + weave * 30));
            u32 i = (y * kSize + x) * 4;
            data[i] = r; data[i + 1] = base; data[i + 2] = b; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeWater() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 v = noise_.fbm2((f32)x * 0.3f, (f32)y * 0.3f, 3);
            u32 i = (y * kSize + x) * 4;
            data[i] = clamp255((i32)(40 + v * 40));
            data[i + 1] = clamp255((i32)(120 + v * 50));
            data[i + 2] = clamp255((i32)(150 + v * 40));
            data[i + 3] = 200;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeIce() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 v = noise_.fbm2((f32)x * 0.15f, (f32)y * 0.15f, 3);
            u8 base = clamp255((i32)(200 + v * 40));
            u32 i = (y * kSize + x) * 4;
            data[i] = base; data[i + 1] = clamp255(base + 15); data[i + 2] = clamp255(base + 40); data[i + 3] = 220;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeDesert() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 v = noise_.fbm2((f32)x * 0.1f, (f32)y * 0.1f, 4);
            f32 dunes = Mathf::saturate(std::sin((f32)x * 0.08f + v * 6.0f));
            u8 r = clamp255((i32)(190 + dunes * 40 + v * 20));
            u8 g = clamp255((i32)(150 + dunes * 30 + v * 15));
            u8 b = clamp255((i32)(90 + dunes * 20 + v * 10));
            u32 i = (y * kSize + x) * 4;
            data[i] = r; data[i + 1] = g; data[i + 2] = b; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeTerrainGrass() {
    return makeGrass();
}
Texture2D TextureFactory::makeTerrainRock() {
    return makeRock();
}
Texture2D TextureFactory::makeTerrainSnow() {
    return makeSnow();
}

Texture2D TextureFactory::makeCheckerboard(u32 size) {
    u8* data = alloc();
    u32 cell = kSize / size;
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            bool even = ((x / cell) + (y / cell)) % 2 == 0;
            u8 v = even ? 230 : 40;
            u32 i = (y * kSize + x) * 4;
            data[i] = v; data[i + 1] = v; data[i + 2] = v; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeGrid(u32 size) {
    u8* data = alloc();
    u32 cell = kSize / size;
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            bool line = (x % cell < 2) || (y % cell < 2);
            u8 v = line ? 255 : 20;
            u32 i = (y * kSize + x) * 4;
            data[i] = v; data[i + 1] = v; data[i + 2] = v; data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data, false, TextureFilter::Nearest, TextureWrap::Repeat);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeRockNormal() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 nx = noise_.perlin((f32)x * 0.15f, (f32)y * 0.15f);
            f32 ny = noise_.perlin((f32)x * 0.15f + 100.0f, (f32)y * 0.15f + 100.0f);
            u32 i = (y * kSize + x) * 4;
            data[i] = clamp255((i32)(128 + nx * 40));
            data[i + 1] = clamp255((i32)(128 + ny * 40));
            data[i + 2] = 255;
            data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeWoodNormal() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 nx = std::sin((f32)y * 0.4f) * 0.15f;
            f32 ny = noise_.perlin((f32)x * 0.2f, (f32)y * 0.2f) * 0.2f;
            u32 i = (y * kSize + x) * 4;
            data[i] = clamp255((i32)(128 + nx * 60));
            data[i + 1] = clamp255((i32)(128 + ny * 60));
            data[i + 2] = 255;
            data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeWaterNormal() {
    u8* data = alloc();
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            f32 t = 0.15f;
            f32 nx = noise_.fbm2((f32)x * 0.3f, (f32)y * 0.3f, 2) * t;
            f32 ny = noise_.fbm2((f32)x * 0.3f + 50.0f, (f32)y * 0.3f + 50.0f, 2) * t;
            u32 i = (y * kSize + x) * 4;
            data[i] = clamp255((i32)(128 + nx * 80));
            data[i + 1] = clamp255((i32)(128 + ny * 80));
            data[i + 2] = 255;
            data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeBrickNormal() {
    u8* data = alloc();
    const u32 brickW = kSize / 4, brickH = kSize / 8;
    const u32 mortar = 4;
    for (u32 y = 0; y < kSize; y++) {
        for (u32 x = 0; x < kSize; x++) {
            u32 row = y / brickH;
            u32 offset = (row % 2) ? (brickW / 2) : 0;
            u32 bx = (x + offset) % brickW;
            bool inBrick = (x % brickW) >= mortar && bx < brickW - mortar && (y % brickH) >= mortar;
            f32 nx = 0, ny = 0;
            if (inBrick) {
                nx = noise_.perlin((f32)x * 0.4f, (f32)y * 0.4f) * 0.3f;
                ny = noise_.perlin((f32)x * 0.4f + 10.0f, (f32)y * 0.4f + 10.0f) * 0.3f;
            } else {
                nx = 0.6f; ny = 0.6f; // mortar indentation
            }
            u32 i = (y * kSize + x) * 4;
            data[i] = clamp255((i32)(128 + nx * 80));
            data[i + 1] = clamp255((i32)(128 + ny * 80));
            data[i + 2] = 255;
            data[i + 3] = 255;
        }
    }
    Texture2D t;
    t.create(kSize, kSize, data);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeRoughnessFlat(f32 value) {
    u8* data = alloc();
    u8 v = clamp255((i32)(value * 255.0f));
    for (u32 i = 0; i < kSize * kSize; i++) {
        data[i * 4] = v; data[i * 4 + 1] = v; data[i * 4 + 2] = v; data[i * 4 + 3] = 255;
    }
    Texture2D t;
    t.create(kSize, kSize, data, false, TextureFilter::Nearest, TextureWrap::Repeat);
    delete[] data;
    return t;
}

Texture2D TextureFactory::makeAoFlat(f32 value) {
    u8* data = alloc();
    u8 v = clamp255((i32)(value * 255.0f));
    for (u32 i = 0; i < kSize * kSize; i++) {
        data[i * 4] = v; data[i * 4 + 1] = v; data[i * 4 + 2] = v; data[i * 4 + 3] = 255;
    }
    Texture2D t;
    t.create(kSize, kSize, data, false, TextureFilter::Nearest, TextureWrap::Repeat);
    delete[] data;
    return t;
}

}
