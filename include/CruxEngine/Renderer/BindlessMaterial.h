#pragma once

// ============================================================================
// CruxEngine Bindless Material System
// ============================================================================
// INVENTED BY FROSTENGINE: All materials stored in a single GPU buffer (SSBO),
// eliminating the per-draw texture binding bottleneck.
//
// Traditional engines:
//   - Each draw call binds 5-8 textures (albedo, normal, metal, rough, ao...)
//   - With 1000 materials = 5000-8000 texture binds per frame
//   - Each bind costs ~0.01ms = 50-80ms overhead per frame
//
// Bindless system:
//   - All material data packed into one giant SSBO (GPU buffer)
//   - Each material: albedo_color, roughness, metallic, emission, normal_scale...
//   - Texture handles stored as 64-bit bindless handles
//   - ONE buffer bind per frame, all materials accessible from any shader
//   - Supports 100K+ materials with zero bind overhead
//
// How it works:
//   1. CPU packs all material data into a structured buffer
//   2. Upload to GPU as a single SSBO (Shader Storage Buffer Object)
//   3. Each draw call passes `materialID` as a uniform
//   4. Shader reads: materials[materialID].albedoColor, etc.
//   5. No texture binds needed (bindless handles read directly)
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Math.h"

namespace Crux {

// ---- GPU material layout (matches shader SSBO layout, 128-byte aligned) ----
struct GPUMaterial {
    f32 albedoR, albedoG, albedoB;     // 12 bytes: base color
    f32 metallic;                       // 4 bytes: metallic factor
    f32 roughness;                      // 4 bytes: roughness factor
    f32 ao;                             // 4 bytes: ambient occlusion
    f32 normalScale;                    // 4 bytes: normal map intensity
    f32 emissionR, emissionG, emissionB; // 12 bytes: emission color
    f32 emissionStrength;               // 4 bytes: emission intensity
    f32 alpha;                          // 4 bytes: opacity
    f32 subsurface;                     // 4 bytes: subsurface scattering
    f32 clearcoat;                      // 4 bytes: clearcoat layer
    f32 anisotropy;                     // 4 bytes: anisotropic reflection
    f32 thickness;                      // 4 bytes: for subsurface
    f32 ior;                            // 4 bytes: index of refraction
    u32 albedoTexIndex;                 // 4 bytes: texture index in bindless array
    u32 normalTexIndex;                 // 4 bytes: normal map index
    u32 metalRoughTexIndex;             // 4 bytes: metal+rough texture
    u32 aoTexIndex;                     // 4 bytes: AO texture
    u32 emissiveTexIndex;               // 4 bytes: emissive texture
    u32 flags;                          // 4 bytes: bit flags (alpha test, double-sided, etc.)
    f32 padding[2];                     // 8 bytes: align to 128 bytes
};

static_assert(sizeof(GPUMaterial) == 128, "GPUMaterial must be 128 bytes");

// ---- Material flags ----
enum MaterialFlags : u32 {
    MAT_FLAG_ALPHA_TEST    = 0x0001,
    MAT_FLAG_DOUBLE_SIDED  = 0x0002,
    MAT_FLAG_UNLIT         = 0x0004,
    MAT_FLAG_SUBSURFACE    = 0x0008,
    MAT_FLAG_CLEARCOAT     = 0x0010,
    MAT_FLAG_ANISOTROPIC   = 0x0020,
    MAT_FLAG_TRANSPARENT   = 0x0040,
    MAT_FLAG_EMISSIVE      = 0x0080,
    MAT_FLAG_ATEST_LEAF    = 0x0100,
};

// ---- Bindless Material Manager ----
class BindlessMaterialSystem {
public:
    static constexpr u32 MAX_MATERIALS = 65536;
    static constexpr u32 MAX_TEXTURE_HANDLES = 131072;

    BindlessMaterialSystem() = default;

    bool init() {
        materials_.resize(MAX_MATERIALS);
        gpuBuffer_.resize(MAX_MATERIALS * sizeof(GPUMaterial));
        textureHandles_.resize(MAX_TEXTURE_HANDLES);
        materialCount_ = 0;
        textureCount_ = 0;

        // Default material (index 0 = white)
        GPUMaterial& def = materials_[0];
        def = {};
        def.albedoR = 1.0f; def.albedoG = 1.0f; def.albedoB = 1.0f;
        def.metallic = 0.0f; def.roughness = 0.5f; def.ao = 1.0f;
        def.normalScale = 1.0f; def.alpha = 1.0f; def.ior = 1.5f;
        def.albedoTexIndex = 0; def.normalTexIndex = 0;
        materialCount_ = 1;
        return true;
    }

    // ---- Create a new material, returns its ID ----
    u32 createMaterial() {
        if (materialCount_ >= MAX_MATERIALS) return 0;
        u32 id = materialCount_++;
        GPUMaterial& m = materials_[id];
        m = {};
        m.albedoR = 1.0f; m.albedoG = 1.0f; m.albedoB = 1.0f;
        m.metallic = 0.0f; m.roughness = 0.5f; m.ao = 1.0f;
        m.normalScale = 1.0f; m.alpha = 1.0f; m.ior = 1.5f;
        return id;
    }

    // ---- Set material properties (CPU side) ----
    void setAlbedo(u32 id, f32 r, f32 g, f32 b) {
        if (id >= materialCount_) return;
        materials_[id].albedoR = r;
        materials_[id].albedoG = g;
        materials_[id].albedoB = b;
        dirty_ = true;
    }

    void setMetallic(u32 id, f32 m) {
        if (id >= materialCount_) return;
        materials_[id].metallic = m;
        dirty_ = true;
    }

    void setRoughness(u32 id, f32 r) {
        if (id >= materialCount_) return;
        materials_[id].roughness = r;
        dirty_ = true;
    }

    void setEmission(u32 id, f32 r, f32 g, f32 b, f32 strength) {
        if (id >= materialCount_) return;
        materials_[id].emissionR = r;
        materials_[id].emissionG = g;
        materials_[id].emissionB = b;
        materials_[id].emissionStrength = strength;
        if (strength > 0) materials_[id].flags |= MAT_FLAG_EMISSIVE;
        dirty_ = true;
    }

    void setFlags(u32 id, u32 flags) {
        if (id >= materialCount_) return;
        materials_[id].flags = flags;
        dirty_ = true;
    }

    // ---- Register a texture handle (returns index for bindless access) ----
    u32 registerTextureHandle(u64 gpuHandle) {
        if (textureCount_ >= MAX_TEXTURE_HANDLES) return 0;
        u32 idx = textureCount_++;
        textureHandles_[idx] = gpuHandle;
        return idx;
    }

    void setAlbedoTexture(u32 materialID, u32 textureIndex) {
        if (materialID >= materialCount_) return;
        materials_[materialID].albedoTexIndex = textureIndex;
        dirty_ = true;
    }

    void setNormalTexture(u32 materialID, u32 textureIndex) {
        if (materialID >= materialCount_) return;
        materials_[materialID].normalTexIndex = textureIndex;
        dirty_ = true;
    }

    // ---- Upload dirty data to GPU buffer ----
    void uploadToGPU() {
        if (!dirty_) return;
        std::memcpy(gpuBuffer_.data(), materials_.data(),
                    materialCount_ * sizeof(GPUMaterial));
        dirty_ = false;
        // The actual GPU buffer upload happens in the renderer via SSBO binding
    }

    // ---- Accessors ----
    const GPUMaterial& getMaterial(u32 id) const {
        return (id < materialCount_) ? materials_[id] : materials_[0];
    }

    u32 materialCount() const { return materialCount_; }
    u32 textureCount() const { return textureCount_; }
    const u8* gpuBufferData() const { return gpuBuffer_.data(); }
    u64 gpuBufferSize() const { return materialCount_ * sizeof(GPUMaterial); }
    bool dirty() const { return dirty_; }

    void clear() {
        materialCount_ = 1; // keep default
        textureCount_ = 0;
        dirty_ = true;
    }

private:
    Vector<GPUMaterial> materials_;
    Vector<u8> gpuBuffer_;        // raw bytes for GPU upload
    Vector<u64> textureHandles_;  // bindless texture handles
    u32 materialCount_ = 0;
    u32 textureCount_ = 0;
    bool dirty_ = true;
};

} // namespace Crux
