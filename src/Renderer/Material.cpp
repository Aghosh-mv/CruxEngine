#include "Renderer/Material.h"

namespace Crux {

#define MAT_COLOR(r, g, b) Color((r), (g), (b), 1.0f)

Material Material::gold() { Material m; m.baseColor = MAT_COLOR(1.0f, 0.766f, 0.336f); m.metallic = 1.0f; m.roughness = 0.22f; return m; }
Material Material::silver() { Material m; m.baseColor = MAT_COLOR(0.95f, 0.95f, 0.97f); m.metallic = 1.0f; m.roughness = 0.18f; return m; }
Material Material::copper() { Material m; m.baseColor = MAT_COLOR(0.95f, 0.52f, 0.31f); m.metallic = 1.0f; m.roughness = 0.30f; return m; }
Material Material::iron() { Material m; m.baseColor = MAT_COLOR(0.55f, 0.57f, 0.59f); m.metallic = 1.0f; m.roughness = 0.38f; return m; }
Material Material::brass() { Material m; m.baseColor = MAT_COLOR(0.68f, 0.57f, 0.22f); m.metallic = 1.0f; m.roughness = 0.34f; return m; }
Material Material::bronze() { Material m; m.baseColor = MAT_COLOR(0.72f, 0.45f, 0.20f); m.metallic = 1.0f; m.roughness = 0.42f; return m; }
Material Material::rust() { Material m; m.baseColor = MAT_COLOR(0.48f, 0.21f, 0.12f); m.metallic = 0.0f; m.roughness = 0.85f; return m; }
Material Material::chrome() { Material m; m.baseColor = MAT_COLOR(0.87f, 0.89f, 0.92f); m.metallic = 1.0f; m.roughness = 0.06f; return m; }
Material Material::aluminum() { Material m; m.baseColor = MAT_COLOR(0.91f, 0.92f, 0.93f); m.metallic = 1.0f; m.roughness = 0.25f; return m; }
Material Material::titanium() { Material m; m.baseColor = MAT_COLOR(0.72f, 0.74f, 0.76f); m.metallic = 1.0f; m.roughness = 0.35f; return m; }
Material Material::steel() { Material m; m.baseColor = MAT_COLOR(0.62f, 0.65f, 0.70f); m.metallic = 1.0f; m.roughness = 0.40f; return m; }

Material Material::wood() { Material m; m.baseColor = MAT_COLOR(0.52f, 0.36f, 0.20f); m.metallic = 0.0f; m.roughness = 0.72f; return m; }
Material Material::oak() { Material m; m.baseColor = MAT_COLOR(0.60f, 0.42f, 0.22f); m.metallic = 0.0f; m.roughness = 0.66f; return m; }
Material Material::walnut() { Material m; m.baseColor = MAT_COLOR(0.42f, 0.26f, 0.13f); m.metallic = 0.0f; m.roughness = 0.60f; return m; }
Material Material::cherry() { Material m; m.baseColor = MAT_COLOR(0.58f, 0.28f, 0.18f); m.metallic = 0.0f; m.roughness = 0.55f; return m; }
Material Material::bamboo() { Material m; m.baseColor = MAT_COLOR(0.72f, 0.66f, 0.30f); m.metallic = 0.0f; m.roughness = 0.70f; return m; }

Material Material::grass() { Material m; m.baseColor = MAT_COLOR(0.25f, 0.48f, 0.18f); m.metallic = 0.0f; m.roughness = 0.92f; return m; }
Material Material::dryGrass() { Material m; m.baseColor = MAT_COLOR(0.62f, 0.57f, 0.26f); m.metallic = 0.0f; m.roughness = 0.95f; return m; }
Material Material::moss() { Material m; m.baseColor = MAT_COLOR(0.28f, 0.36f, 0.20f); m.metallic = 0.0f; m.roughness = 0.90f; return m; }
Material Material::leaf() { Material m; m.baseColor = MAT_COLOR(0.22f, 0.40f, 0.16f); m.metallic = 0.0f; m.roughness = 0.85f; return m; }
Material Material::fern() { Material m; m.baseColor = MAT_COLOR(0.24f, 0.42f, 0.20f); m.metallic = 0.0f; m.roughness = 0.88f; return m; }

Material Material::stone() { Material m; m.baseColor = MAT_COLOR(0.50f, 0.50f, 0.52f); m.metallic = 0.0f; m.roughness = 0.88f; return m; }
Material Material::granite() { Material m; m.baseColor = MAT_COLOR(0.55f, 0.55f, 0.56f); m.metallic = 0.0f; m.roughness = 0.82f; return m; }
Material Material::marble() { Material m; m.baseColor = MAT_COLOR(0.92f, 0.92f, 0.90f); m.metallic = 0.0f; m.roughness = 0.45f; return m; }
Material Material::concrete() { Material m; m.baseColor = MAT_COLOR(0.60f, 0.60f, 0.58f); m.metallic = 0.0f; m.roughness = 0.95f; return m; }
Material Material::sandstone() { Material m; m.baseColor = MAT_COLOR(0.70f, 0.62f, 0.48f); m.metallic = 0.0f; m.roughness = 0.90f; return m; }
Material Material::brick() { Material m; m.baseColor = MAT_COLOR(0.55f, 0.27f, 0.20f); m.metallic = 0.0f; m.roughness = 0.85f; return m; }
Material Material::cobblestone() { Material m; m.baseColor = MAT_COLOR(0.45f, 0.45f, 0.47f); m.metallic = 0.0f; m.roughness = 0.90f; return m; }
Material Material::gravel() { Material m; m.baseColor = MAT_COLOR(0.48f, 0.47f, 0.45f); m.metallic = 0.0f; m.roughness = 0.95f; return m; }
Material Material::slate() { Material m; m.baseColor = MAT_COLOR(0.30f, 0.30f, 0.33f); m.metallic = 0.0f; m.roughness = 0.75f; return m; }
Material Material::basalt() { Material m; m.baseColor = MAT_COLOR(0.18f, 0.18f, 0.20f); m.metallic = 0.0f; m.roughness = 0.80f; return m; }
Material Material::lava() { Material m; m.baseColor = MAT_COLOR(0.55f, 0.10f, 0.04f); m.emission = MAT_COLOR(1.0f, 0.35f, 0.05f); m.emissionStrength = 2.5f; m.metallic = 0.0f; m.roughness = 0.55f; return m; }
Material Material::ice() { Material m; m.baseColor = MAT_COLOR(0.70f, 0.85f, 0.95f); m.metallic = 0.0f; m.roughness = 0.10f; m.smoothness = 0.8f; m.opacity = 0.55f; m.alphaBlend = true; return m; }
Material Material::snow() { Material m; m.baseColor = MAT_COLOR(0.95f, 0.96f, 0.97f); m.metallic = 0.0f; m.roughness = 0.70f; return m; }
Material Material::beachSand() { Material m; m.baseColor = MAT_COLOR(0.83f, 0.76f, 0.58f); m.metallic = 0.0f; m.roughness = 0.98f; return m; }

Material Material::water() { Material m; m.baseColor = MAT_COLOR(0.10f, 0.35f, 0.48f); m.metallic = 0.0f; m.roughness = 0.06f; m.smoothness = 1.0f; m.opacity = 0.55f; m.alphaBlend = true; m.castShadow = false; return m; }
Material Material::deepWater() { Material m; m.baseColor = MAT_COLOR(0.05f, 0.18f, 0.32f); m.metallic = 0.0f; m.roughness = 0.04f; m.smoothness = 1.0f; m.opacity = 0.80f; m.alphaBlend = true; m.castShadow = false; return m; }
Material Material::glass() { Material m; m.baseColor = MAT_COLOR(0.80f, 0.88f, 0.95f); m.metallic = 0.0f; m.roughness = 0.04f; m.smoothness = 1.0f; m.opacity = 0.22f; m.alphaBlend = true; m.castShadow = false; return m; }
Material Material::crystal() { Material m; m.baseColor = MAT_COLOR(0.75f, 0.85f, 1.0f); m.metallic = 0.0f; m.roughness = 0.05f; m.smoothness = 1.0f; m.opacity = 0.35f; m.alphaBlend = true; m.castShadow = false; return m; }
Material Material::rubber() { Material m; m.baseColor = MAT_COLOR(0.10f, 0.10f, 0.10f); m.metallic = 0.0f; m.roughness = 0.75f; return m; }
Material Material::plastic() { Material m; m.baseColor = MAT_COLOR(0.30f, 0.35f, 0.45f); m.metallic = 0.0f; m.roughness = 0.50f; return m; }
Material Material::plasticGlossy() { Material m; m.baseColor = MAT_COLOR(0.85f, 0.20f, 0.20f); m.metallic = 0.0f; m.roughness = 0.12f; return m; }
Material Material::leather() { Material m; m.baseColor = MAT_COLOR(0.32f, 0.20f, 0.12f); m.metallic = 0.0f; m.roughness = 0.60f; return m; }
Material Material::fabric() { Material m; m.baseColor = MAT_COLOR(0.50f, 0.28f, 0.30f); m.metallic = 0.0f; m.roughness = 0.95f; return m; }
Material Material::ceramic() { Material m; m.baseColor = MAT_COLOR(0.88f, 0.88f, 0.85f); m.metallic = 0.0f; m.roughness = 0.30f; return m; }
Material Material::emerald() { Material m; m.baseColor = MAT_COLOR(0.06f, 0.60f, 0.40f); m.metallic = 0.0f; m.roughness = 0.08f; m.smoothness = 1.0f; m.opacity = 0.70f; m.alphaBlend = true; m.castShadow = false; return m; }
Material Material::ruby() { Material m; m.baseColor = MAT_COLOR(0.70f, 0.04f, 0.08f); m.metallic = 0.0f; m.roughness = 0.06f; m.smoothness = 1.0f; m.opacity = 0.80f; m.alphaBlend = true; m.castShadow = false; return m; }
Material Material::sapphire() { Material m; m.baseColor = MAT_COLOR(0.05f, 0.12f, 0.55f); m.metallic = 0.0f; m.roughness = 0.05f; m.smoothness = 1.0f; m.opacity = 0.75f; m.alphaBlend = true; m.castShadow = false; return m; }
Material Material::amber() { Material m; m.baseColor = MAT_COLOR(0.85f, 0.55f, 0.15f); m.metallic = 0.0f; m.roughness = 0.12f; m.smoothness = 1.0f; m.opacity = 0.60f; m.alphaBlend = true; m.castShadow = false; return m; }
Material Material::diamond() { Material m; m.baseColor = MAT_COLOR(0.95f, 0.97f, 1.0f); m.metallic = 0.0f; m.roughness = 0.03f; m.smoothness = 1.0f; m.opacity = 0.12f; m.alphaBlend = true; m.castShadow = false; return m; }
Material Material::pearl() { Material m; m.baseColor = MAT_COLOR(0.92f, 0.88f, 0.85f); m.metallic = 0.0f; m.roughness = 0.20f; m.smoothness = 0.7f; return m; }
Material Material::goldFoil() { Material m; m.baseColor = MAT_COLOR(1.0f, 0.80f, 0.35f); m.metallic = 1.0f; m.roughness = 0.10f; return m; }

}
