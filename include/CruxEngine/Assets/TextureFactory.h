#pragma once

#include "Core/Types.h"
#include "Core/Noise.h"
#include "Renderer/Texture.h"

namespace Crux {

// Procedural texture generation so the engine ships with rich, varied
// materials without any external asset dependencies.
class TextureFactory {
public:
    static constexpr u32 kSize = 256;

    TextureFactory();

    // 2D albedo textures
    Texture2D makeWood();
    Texture2D makeOakPlanks();
    Texture2D makeGrass();
    Texture2D makeRock();
    Texture2D makeStone();
    Texture2D makeMarble();
    Texture2D makeBrick();
    Texture2D makeMetal();
    Texture2D makeRustyMetal();
    Texture2D makeSand();
    Texture2D makeSnow();
    Texture2D makeCheckerboard(u32 size = 64);
    Texture2D makeGrid(u32 size = 64);
    Texture2D makeLava();
    Texture2D makeLeaves();
    Texture2D makeFabric();
    Texture2D makeWater();
    Texture2D makeIce();
    Texture2D makeDesert();
    Texture2D makeTerrainGrass();
    Texture2D makeTerrainRock();
    Texture2D makeTerrainSnow();

    // Normal maps
    Texture2D makeRockNormal();
    Texture2D makeWoodNormal();
    Texture2D makeWaterNormal();
    Texture2D makeBrickNormal();

    // Single-channel maps
    Texture2D makeRoughnessFlat(f32 value);
    Texture2D makeAoFlat(f32 value);

private:
    // Fills `dst` with r,g,b,a. Helper mutators used by generators.
    static void fillSolid(u8* dst, u8 r, u8 g, u8 b, u8 a = 255);
    static u8 clamp255(i32 v);
    static u8* alloc();

    Noise noise_;
};

}
