#pragma once

// ============================================================================
// FrostEngine Compute Shader Sources — GPU kernels for revolutionary tech
// ============================================================================
// These are GLSL 4.60 compute shaders that execute on the GPU.
// They implement: SVOR injection/propagation, TCSM reprojection,
// NRC training, GPU-driven culling, and auto-exposure histogram.
// ============================================================================

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

}
}
