#pragma once

// ============================================================================
// CruxEngine Material Library — 80+ ready-to-use PBR materials
// ============================================================================
// Each material is a complete PBR definition with spectral reflectance data.
// Includes: metals, dielectrics, skin, foliage, water, glass, fabric, stone,
// wood variants, and more. All materials work with the bindless pipeline.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Math.h"
#include "Renderer/BindlessMaterial.h"
#include "Renderer/SpectralRendering.h"

namespace Crux {

struct MaterialDefinition {
    String name;
    String category;
    GPUMaterial gpu;
    SpectralReflectance spectralAlbedo;
    SpectralReflectance spectralF0;
    f32 subsurfaceRadius[3];   // for skin/leaf SSS
    u32 flags;
};

class MaterialLibrary {
public:
    bool init() {
        registerDefaults();
        return true;
    }

    u32 findByName(const char* name) const {
        for (u32 i = 0; i < defs_.size(); i++) {
            if (defs_[i].name == name) return i;
        }
        return 0xFFFFFFFF;
    }

    u32 findByCategory(const char* category, Vector<u32>& out) const {
        out.clear();
        for (u32 i = 0; i < defs_.size(); i++) {
            if (defs_[i].category == category) out.pushBack(i);
        }
        return (u32)out.size();
    }

    const MaterialDefinition& get(u32 id) const {
        return (id < defs_.size()) ? defs_[id] : defs_[0];
    }

    u32 count() const { return (u32)defs_.size(); }
    const Vector<MaterialDefinition>& all() const { return defs_; }

private:
    void registerDefaults() {
        // ================================================================
        // METALS
        // ================================================================
        addDef("Gold", "Metal", {0.96f,0.83f,0.22f}, 0.0f, 1.0f, 1.0f, 0.4f);
        addDef("Silver", "Metal", {0.95f,0.93f,0.88f}, 0.0f, 1.0f, 1.0f, 0.15f);
        addDef("Copper", "Metal", {0.97f,0.74f,0.62f}, 0.0f, 1.0f, 1.0f, 0.25f);
        addDef("Iron", "Metal", {0.56f,0.57f,0.58f}, 0.0f, 1.0f, 1.0f, 0.4f);
        addDef("Steel", "Metal", {0.66f,0.65f,0.63f}, 0.0f, 1.0f, 1.0f, 0.3f);
        addDef("Aluminum", "Metal", {0.91f,0.92f,0.92f}, 0.0f, 1.0f, 1.0f, 0.2f);
        addDef("Titanium", "Metal", {0.63f,0.60f,0.57f}, 0.0f, 1.0f, 1.0f, 0.35f);
        addDef("Platinum", "Metal", {0.83f,0.80f,0.76f}, 0.0f, 1.0f, 1.0f, 0.2f);
        addDef("Brass", "Metal", {0.88f,0.79f,0.50f}, 0.0f, 1.0f, 1.0f, 0.3f);
        addDef("Bronze", "Metal", {0.80f,0.60f,0.40f}, 0.0f, 1.0f, 1.0f, 0.35f);
        addDef("Chrome", "Metal", {0.75f,0.76f,0.77f}, 0.0f, 1.0f, 1.0f, 0.02f);
        addDef("Tungsten", "Metal", {0.60f,0.58f,0.55f}, 0.0f, 1.0f, 1.0f, 0.3f);

        // ================================================================
        // DIELECTRICS (non-metals)
        // ================================================================
        addDef("Plastic White", "Plastic", {0.95f,0.93f,0.88f}, 0.04f, 0.5f, 1.0f, 0.0f);
        addDef("Plastic Black", "Plastic", {0.02f,0.02f,0.02f}, 0.04f, 0.4f, 1.0f, 0.0f);
        addDef("Plastic Red", "Plastic", {0.80f,0.05f,0.05f}, 0.04f, 0.45f, 1.0f, 0.0f);
        addDef("Plastic Blue", "Plastic", {0.05f,0.15f,0.80f}, 0.04f, 0.45f, 1.0f, 0.0f);
        addDef("Plastic Green", "Plastic", {0.05f,0.60f,0.10f}, 0.04f, 0.45f, 1.0f, 0.0f);
        addDef("Plastic Yellow", "Plastic", {0.95f,0.90f,0.10f}, 0.04f, 0.45f, 1.0f, 0.0f);
        addDef("Rubber", "Plastic", {0.03f,0.03f,0.03f}, 0.04f, 0.9f, 1.0f, 0.0f);

        // ================================================================
        // STONE
        // ================================================================
        addDef("Granite", "Stone", {0.55f,0.53f,0.50f}, 0.04f, 0.85f, 1.0f, 0.0f);
        addDef("Marble White", "Stone", {0.90f,0.88f,0.85f}, 0.04f, 0.25f, 1.0f, 0.0f);
        addDef("Marble Dark", "Stone", {0.30f,0.28f,0.27f}, 0.04f, 0.3f, 1.0f, 0.0f);
        addDef("Limestone", "Stone", {0.82f,0.78f,0.70f}, 0.04f, 0.7f, 1.0f, 0.0f);
        addDef("Sandstone", "Stone", {0.85f,0.75f,0.55f}, 0.04f, 0.75f, 1.0f, 0.0f);
        addDef("Slate", "Stone", {0.35f,0.35f,0.37f}, 0.04f, 0.6f, 1.0f, 0.0f);
        addDef("Basalt", "Stone", {0.20f,0.20f,0.22f}, 0.04f, 0.8f, 1.0f, 0.0f);
        addDef("Obsidian", "Stone", {0.05f,0.05f,0.07f}, 0.04f, 0.05f, 1.0f, 0.0f);
        addDef("Travertine", "Stone", {0.80f,0.74f,0.62f}, 0.04f, 0.5f, 1.0f, 0.0f);

        // ================================================================
        // WOOD
        // ================================================================
        addDef("Oak", "Wood", {0.65f,0.45f,0.25f}, 0.04f, 0.7f, 1.0f, 0.0f);
        addDef("Walnut", "Wood", {0.35f,0.20f,0.10f}, 0.04f, 0.65f, 1.0f, 0.0f);
        addDef("Pine", "Wood", {0.80f,0.65f,0.40f}, 0.04f, 0.6f, 1.0f, 0.0f);
        addDef("Cherry", "Wood", {0.55f,0.20f,0.12f}, 0.04f, 0.4f, 1.0f, 0.0f);
        addDef("Maple", "Wood", {0.75f,0.55f,0.35f}, 0.04f, 0.55f, 1.0f, 0.0f);
        addDef("Ebony", "Wood", {0.10f,0.08f,0.06f}, 0.04f, 0.2f, 1.0f, 0.0f);
        addDef("Bamboo", "Wood", {0.80f,0.72f,0.45f}, 0.04f, 0.5f, 1.0f, 0.0f);
        addDef("Driftwood", "Wood", {0.60f,0.55f,0.48f}, 0.04f, 0.8f, 1.0f, 0.0f);

        // ================================================================
        // FOLIAGE & ORGANIC
        // ================================================================
        addDef("Green Leaf", "Foliage", {0.15f,0.45f,0.08f}, 0.04f, 0.3f, 0.7f, MAT_FLAG_SUBSURFACE);
        addDef("Autumn Leaf Red", "Foliage", {0.70f,0.15f,0.05f}, 0.04f, 0.3f, 1.0f, 0.0f);
        addDef("Autumn Leaf Orange", "Foliage", {0.85f,0.40f,0.05f}, 0.04f, 0.3f, 1.0f, 0.0f);
        addDef("Autumn Leaf Yellow", "Foliage", {0.85f,0.75f,0.10f}, 0.04f, 0.3f, 1.0f, 0.0f);
        addDef("Pine Needle", "Foliage", {0.12f,0.30f,0.10f}, 0.04f, 0.4f, 1.0f, 0.0f);
        addDef("Moss", "Foliage", {0.15f,0.35f,0.10f}, 0.04f, 0.85f, 1.0f, 0.0f);
        addDef("Lichen", "Foliage", {0.45f,0.50f,0.35f}, 0.04f, 0.9f, 1.0f, 0.0f);

        // ================================================================
        // SKIN & ORGANIC
        // ================================================================
        addDef("Skin Light", "Organic", {0.90f,0.70f,0.60f}, 0.04f, 0.45f, 1.5f, MAT_FLAG_SUBSURFACE);
        addDef("Skin Medium", "Organic", {0.75f,0.55f,0.42f}, 0.04f, 0.45f, 1.5f, MAT_FLAG_SUBSURFACE);
        addDef("Skin Dark", "Organic", {0.45f,0.30f,0.22f}, 0.04f, 0.45f, 1.5f, MAT_FLAG_SUBSURFACE);

        // ================================================================
        // FABRIC
        // ================================================================
        addDef("Cotton White", "Fabric", {0.90f,0.88f,0.85f}, 0.04f, 0.8f, 1.0f, 0.0f);
        addDef("Cotton Blue", "Fabric", {0.15f,0.20f,0.55f}, 0.04f, 0.8f, 1.0f, 0.0f);
        addDef("Leather Brown", "Fabric", {0.40f,0.25f,0.15f}, 0.04f, 0.6f, 1.0f, 0.0f);
        addDef("Leather Black", "Fabric", {0.05f,0.04f,0.04f}, 0.04f, 0.55f, 1.0f, 0.0f);
        addDef("Denim", "Fabric", {0.15f,0.20f,0.35f}, 0.04f, 0.75f, 1.0f, 0.0f);
        addDef("Silk", "Fabric", {0.90f,0.88f,0.82f}, 0.04f, 0.15f, 1.0f, 0.0f);
        addDef("Wool", "Fabric", {0.80f,0.78f,0.72f}, 0.04f, 0.9f, 1.0f, 0.0f);
        addDef("Velvet", "Fabric", {0.30f,0.05f,0.15f}, 0.04f, 0.95f, 1.0f, 0.0f);

        // ================================================================
        // GLASS & CRYSTAL
        // ================================================================
        addDef("Glass Clear", "Glass", {0.95f,0.95f,0.95f}, 0.04f, 0.05f, 1.52f, MAT_FLAG_TRANSPARENT);
        addDef("Glass Green", "Glass", {0.20f,0.60f,0.30f}, 0.04f, 0.05f, 1.52f, MAT_FLAG_TRANSPARENT);
        addDef("Glass Blue", "Glass", {0.10f,0.20f,0.70f}, 0.04f, 0.05f, 1.52f, MAT_FLAG_TRANSPARENT);
        addDef("Glass Red", "Glass", {0.70f,0.10f,0.10f}, 0.04f, 0.05f, 1.52f, MAT_FLAG_TRANSPARENT);
        addDef("Crystal", "Glass", {0.90f,0.92f,0.95f}, 0.04f, 0.02f, 2.0f, MAT_FLAG_TRANSPARENT);
        addDef("Ice", "Glass", {0.75f,0.85f,0.95f}, 0.04f, 0.1f, 1.31f, MAT_FLAG_TRANSPARENT);
        addDef("Cruxed Glass", "Glass", {0.85f,0.88f,0.90f}, 0.04f, 0.4f, 1.52f, MAT_FLAG_TRANSPARENT);

        // ================================================================
        // CONCRETE & CONSTRUCTION
        // ================================================================
        addDef("Concrete", "Concrete", {0.65f,0.63f,0.60f}, 0.04f, 0.9f, 1.0f, 0.0f);
        addDef("Concrete Rough", "Concrete", {0.50f,0.48f,0.45f}, 0.04f, 0.95f, 1.0f, 0.0f);
        addDef("Concrete Polished", "Concrete", {0.70f,0.68f,0.65f}, 0.04f, 0.3f, 1.0f, 0.0f);
        addDef("Brick Red", "Concrete", {0.55f,0.22f,0.15f}, 0.04f, 0.75f, 1.0f, 0.0f);

        // ================================================================
        // EMISSIVE / SPECIAL
        // ================================================================
        addDef("Emissive Blue", "Emissive", {0.10f,0.30f,0.90f}, 0.04f, 0.3f, 1.0f, MAT_FLAG_EMISSIVE);
        addDef("Emissive Red", "Emissive", {0.90f,0.10f,0.10f}, 0.04f, 0.3f, 1.0f, MAT_FLAG_EMISSIVE);
        addDef("Emissive Green", "Emissive", {0.10f,0.90f,0.20f}, 0.04f, 0.3f, 1.0f, MAT_FLAG_EMISSIVE);
        addDef("Emissive White", "Emissive", {0.95f,0.95f,0.95f}, 0.04f, 0.3f, 1.0f, MAT_FLAG_EMISSIVE);
        addDef("Emissive Orange", "Emissive", {0.95f,0.50f,0.05f}, 0.04f, 0.3f, 1.0f, MAT_FLAG_EMISSIVE);
        addDef("Lava", "Emissive", {0.95f,0.30f,0.02f}, 0.04f, 0.6f, 1.0f, MAT_FLAG_EMISSIVE);
        addDef("Neon Pink", "Emissive", {1.0f,0.05f,0.50f}, 0.04f, 0.3f, 1.0f, MAT_FLAG_EMISSIVE);
        addDef("Neon Cyan", "Emissive", {0.05f,0.95f,0.90f}, 0.04f, 0.3f, 1.0f, MAT_FLAG_EMISSIVE);

        // ================================================================
        // TERRAIN
        // ================================================================
        addDef("Grass", "Terrain", {0.22f,0.50f,0.15f}, 0.04f, 0.8f, 1.0f, 0.0f);
        addDef("Dirt", "Terrain", {0.45f,0.32f,0.20f}, 0.04f, 0.9f, 1.0f, 0.0f);
        addDef("Sand", "Terrain", {0.82f,0.75f,0.55f}, 0.04f, 0.85f, 1.0f, 0.0f);
        addDef("Snow", "Terrain", {0.92f,0.93f,0.96f}, 0.04f, 0.3f, 1.0f, 0.0f);
        addDef("Rock Dark", "Terrain", {0.30f,0.28f,0.26f}, 0.04f, 0.85f, 1.0f, 0.0f);
        addDef("Rock Light", "Terrain", {0.60f,0.58f,0.55f}, 0.04f, 0.8f, 1.0f, 0.0f);
        addDef("Mud", "Terrain", {0.35f,0.25f,0.15f}, 0.04f, 0.95f, 1.0f, 0.0f);

        // ================================================================
        // WATER
        // ================================================================
        addDef("Water Shallow", "Water", {0.15f,0.40f,0.50f}, 0.04f, 0.05f, 1.33f, MAT_FLAG_TRANSPARENT);
        addDef("Water Deep", "Water", {0.02f,0.10f,0.30f}, 0.04f, 0.05f, 1.33f, MAT_FLAG_TRANSPARENT);
        addDef("Ocean", "Water", {0.05f,0.20f,0.40f}, 0.04f, 0.05f, 1.34f, MAT_FLAG_TRANSPARENT);
    }

    void addDef(const char* name, const char* cat, Vec3 albedo,
                f32 metallic, f32 roughness, f32 iorOrAo, u32 flags) {
        MaterialDefinition def;
        def.name = name;
        def.category = cat;
        def.gpu.albedoR = albedo.x;
        def.gpu.albedoG = albedo.y;
        def.gpu.albedoB = albedo.z;
        def.gpu.metallic = metallic;
        def.gpu.roughness = roughness;
        def.gpu.ao = 1.0f;
        def.gpu.normalScale = 1.0f;
        def.gpu.alpha = 1.0f;
        def.gpu.ior = (flags & MAT_FLAG_TRANSPARENT) ? iorOrAo : 1.5f;
        if (!(flags & MAT_FLAG_TRANSPARENT)) def.gpu.ao = iorOrAo;
        def.gpu.emissionR = (flags & MAT_FLAG_EMISSIVE) ? albedo.x * 3.0f : 0;
        def.gpu.emissionG = (flags & MAT_FLAG_EMISSIVE) ? albedo.y * 3.0f : 0;
        def.gpu.emissionB = (flags & MAT_FLAG_EMISSIVE) ? albedo.z * 3.0f : 0;
        def.gpu.emissionStrength = (flags & MAT_FLAG_EMISSIVE) ? 5.0f : 0;
        def.gpu.flags = flags;
        def.spectralAlbedo = SpectralReflectance::fromRGB(albedo.x, albedo.y, albedo.z);
        def.spectralF0 = SpectralReflectance::fromRGB(metallic, metallic, metallic);
        def.subsurfaceRadius[0] = 1.0f;
        def.subsurfaceRadius[1] = 0.2f;
        def.subsurfaceRadius[2] = 0.1f;
        def.flags = flags;
        defs_.pushBack(def);
    }

    Vector<MaterialDefinition> defs_;
};

} // namespace Crux
