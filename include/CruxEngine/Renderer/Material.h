#pragma once

#include "Core/Types.h"
#include "Core/Math.h"
#include "Renderer/Texture.h"

namespace Crux {

// Physically-based material parameters (metallic-roughness workflow).
// Textures are optional; when a texture id is 0 the constant is used.
struct Material {
    Color baseColor{ 1, 1, 1, 1 };
    f32 metallic = 0.0f;
    f32 roughness = 0.5f;
    f32 ambientOcclusion = 1.0f;
    Color emission{ 0, 0, 0, 1 };
    f32 emissionStrength = 0.0f;
    f32 opacity = 1.0f;              // <1 enables alpha blending
    f32 normalStrength = 1.0f;       // normal-map multiplier
    f32 smoothness = 0.0f;           // optional extra specular (glass/skin)
    bool doubleSided = false;
    bool unlit = false;
    bool alphaBlend = false;
    bool castShadow = true;
    bool receiveShadow = true;
    bool useVertexColor = false;
    u8 uvScale = 1;                  // tile factor for procedural detail

    // Texture bindings (0 = none). Owned by TextureLibrary.
    GLuint albedoMap = 0;
    GLuint normalMap = 0;
    GLuint metallicMap = 0;
    GLuint roughnessMap = 0;
    GLuint aoMap = 0;
    GLuint emissionMap = 0;
    GLuint heightMap = 0;
    GLuint detailMap = 0;

    // Named presets covering many surface types.
    static Material gold();
    static Material silver();
    static Material copper();
    static Material iron();
    static Material brass();
    static Material bronze();
    static Material rust();
    static Material chrome();
    static Material aluminum();
    static Material titanium();
    static Material steel();

    static Material wood();
    static Material oak();
    static Material walnut();
    static Material cherry();
    static Material bamboo();

    static Material grass();
    static Material dryGrass();
    static Material moss();
    static Material leaf();
    static Material fern();

    static Material stone();
    static Material granite();
    static Material marble();
    static Material concrete();
    static Material sandstone();
    static Material brick();
    static Material cobblestone();
    static Material gravel();
    static Material slate();
    static Material basalt();
    static Material lava();
    static Material ice();
    static Material snow();
    static Material beachSand();

    static Material water();
    static Material deepWater();
    static Material glass();
    static Material crystal();
    static Material rubber();
    static Material plastic();
    static Material leather();
    static Material fabric();
    static Material ceramic();
    static Material plasticGlossy();
    static Material emerald();
    static Material ruby();
    static Material sapphire();
    static Material amber();
    static Material diamond();
    static Material pearl();
    static Material goldFoil();
};

}
