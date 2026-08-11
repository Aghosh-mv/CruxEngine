#pragma once

// ============================================================================
// FrostEngine Compute Shader Sources — GPU kernels for revolutionary tech
// ============================================================================
// These are GLSL 4.60 compute shaders that execute on the GPU.
// They implement: SVOR injection/propagation, TCSM reprojection,
// NRC training, GPU-driven culling, and auto-exposure histogram.
// ============================================================================

#include "Core/Types.h"
#include "Core/Math.h"
#include "Core/HashMap.h"

namespace Frost {
namespace ShaderSource {

// ---- SVOR: Inject direct light into voxels ----
extern const char* svorInjectComp;

// ---- SVOR: Propagate radiance UP the octree ----
extern const char* svorPropagateUpComp;

// ---- SVOR: Propagate radiance DOWN ----
extern const char* svorPropagateDownComp;

// ---- TCSM: Temporal reprojection ----
extern const char* tcsmReprojectComp;

// ---- NRC: Train neural radiance cache ----
extern const char* nrcTrainComp;

// ---- NRC: Query the network for radiance ----
extern const char* nrcQueryComp;

// ---- GPU-Driven: Frustum + occlusion culling ----
extern const char* gpuCullComp;

// ---- Auto-Exposure: Luminance histogram ----
extern const char* autoExposureComp;

// ---- SSR: Screen-space reflections ----
extern const char* ssrComp;

// ---- Contact Shadows: Screen-space ray march ----
extern const char* contactShadowComp;

// ---- Volumetric Fog: Ray-marched fog ----
extern const char* volumetricFogComp;

// ---- Temporal Anti-Aliasing resolve ----
extern const char* taaResolveComp;

// ============================================================================
// Compute shader template system
// ============================================================================
// Templates are GLSL sources that may declare specialization constants with
// the FROST_SPEC(NAME, DEFAULT) macro and pull in other templates with
// #include "templateName". instantiateTemplate substitutes specialization
// constants and flattens the include graph into a concrete shader.

struct SpecializationValue {
    u32 constantId = 0;
    u32 value = 0;
};

struct ComputeShaderTemplate {
    String name;
    Vector<String> defines;
    Vector<SpecializationValue> specializationConstants;
    String source;
};

class ShaderSource_compute {
public:
    u32 registerTemplate(const char* name, const char* source);
    u32 instantiateTemplate(u32 templateId, const Vector<SpecializationValue>& specializations);
    void specializeConstant(u32 constantId, u32 value);
    const ComputeShaderTemplate& getTemplate(u32 id) const;
    u32 getGeneratedShaderCount() const { return generatedShaders_; }
    u32 getSpecializationCount() const { return specializationCount_; }
    f32 getGenerateTimeMs() const { return generateTimeMs_; }
    bool hasTemplate(const char* name) const;
    void invalidateCache() { templateCache_.clear(); }
    Vector<String> parseIncludes(const char* source);

private:
    String resolveIncludes(const String& source, u32 selfId, u32 depth);
    String applySpecializations(const String& source,
                                const ComputeShaderTemplate& tpl,
                                const Vector<SpecializationValue>& overrides);

    Vector<ComputeShaderTemplate> templates_;
    HashMap<String, u32> templateCache_;
    u32 specializationCount_ = 0;
    u32 generatedShaders_ = 0;
    u32 includeDepth_ = 0;
    f32 generateTimeMs_ = 0.0f;
};

}
}
