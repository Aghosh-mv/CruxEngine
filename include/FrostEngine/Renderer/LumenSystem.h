#pragma once

// ============================================================================
// FrostEngine Lumen-like Dynamic Global Illumination System
// ============================================================================
// Replaces baked lightmaps with real-time GI, similar to UE5's Lumen.
// Features: Surface Cache, Screen Tracing, Mesh Distance Field Tracing,
// Global Distance Field, Radiance Cache, Temporal Accumulation,
// Reflections, Light Bounce, Emissive Surfaces, Quality Presets.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Math.h"
#include "Core/HashMap.h"
#include "Renderer/Types.h"

namespace Frost {

struct Camera;

// ============================================================================
// Quality presets
// ============================================================================
enum class LumenQuality : u8 {
    Low,        // 1/4 resolution, 1 bounce
    Medium,     // 1/2 resolution, 1 bounce
    High,       // Full resolution, 1 bounce
    Epic        // Full resolution, 2 bounces
};

// ============================================================================
// Surface Cache: screen-space cards for visible surfaces
// ============================================================================
struct SurfaceCard {
    Vec3 position;
    Vec3 normal;
    Vec3 tangent;
    Vec3 bitangent;
    Vec2 uvMin;
    Vec2 uvMax;
    Vec3 albedo;
    Vec3 emissive;
    f32 emissiveIntensity;
    f32 worldRadius;
    Vec3 boundsMin;
    Vec3 boundsMax;
    u32 materialIndex;
    u32 objectId;
    bool isEmissive;
    bool isValid;
};

struct SurfaceCacheConfig {
    u32 maxCards = 16384;
    u32 cardResolution = 8;
    f32 minCardWorldSize = 0.1f;
    f32 maxCardWorldSize = 50.0f;
    f32 normalThreshold = 0.5f;
};

// ============================================================================
// Surface Cache: page-based radiance atlas (Lumen-style residency)
// ============================================================================
struct SurfaceCachePage {
    u32 pageX = 0;
    u32 pageY = 0;
    u32 level = 0;
    u32 x = 0;
    u32 y = 0;
    f32 lastUpdateTime = 0.0f;
    bool resident = false;
    bool requested = false;
    u32 patchCount = 0;
};

struct SurfaceCachePageConfig {
    u32 atlasSize = 2048;
    u32 pageSize = 64;
    u32 maxPages = 256;
    u32 raysPerPatch = 16;
    f32 minLightingRadius = 5.0f;
};

struct CachedPatch {
    Vec3 position;
    Vec3 normal;
    Vec3 radiance;
    u32 pageIndex = 0;
    bool dirty = true;
};

// ============================================================================
// Mesh Distance Field
// ============================================================================
struct SDFMeshData {
    Vector<Vec3> sdfSamples;
    Vec3 gridOrigin;
    Vec3 gridSpacing;
    Vec3i gridSize;
    Vec3 boundsMin;
    Vec3 boundsMax;
    u32 objectId;
    f32 maxDistance;
    bool valid;
};

struct SDFMeshConfig {
    u32 resolutionPerObjectAxis = 16;
    f32 distanceScale = 1.0f;
    f32 biasDistance = 0.01f;
};

// ============================================================================
// Global Distance Field: merged SDF clipmap
// ============================================================================
struct GlobalDFConfig {
    u32 clipmapLevels = 6;
    u32 clipmapResolution = 128;
    f32 clipmapBaseResolution = 1.0f;
    f32 clipmapSpacingFactor = 2.0f;
    f32 maxTraceDistance = 10000.0f;
    f32 meshSDFMargin = 0.1f;
};

struct ClipmapLevel {
    Vec3 center;
    f32 spacing;
    u32 resolution;
    u32 textureHandle;
    bool dirty;
};

// ============================================================================
// Radiance Cache: hierarchical 3D probe grid
// ============================================================================
struct RadianceProbe {
    Vec3 position;
    Vec3 radiance[6];       // 6 faces of cube (simplified)
    Vec3 historyRadiance[6];
    f32 weight;
    f32 historyWeight;
    bool needsUpdate;
    u32 frameLastUpdated;
};

struct RadianceCacheConfig {
    u32 probesPerAxis = 16;
    f32 probeSpacing = 50.0f;
    f32 probeHysteresis = 0.97f;
    u32 maxRaysPerProbe = 64;
    u32 updateProbesPerFrame = 128;
    f32 minTraceDistance = 0.5f;
    f32 maxTraceDistance = 5000.0f;
    f32 probeRadius = 80.0f;
};

// ============================================================================
// Screen Trace configuration
// ============================================================================
struct ScreenTraceConfig {
    f32 maxDistance = 1000.0f;
    u32 maxSteps = 64;
    f32 stepSize = 1.0f;
    f32 thickness = 0.05f;
    bool hiZEnabled = true;
    u32 hiZMaxMip = 6;
};

// ============================================================================
// Temporal Accumulation configuration
// ============================================================================
struct TemporalConfig {
    f32 historyBlendWeight = 0.9f;
    f32 responseSpeed = 0.1f;
    f32 disocclusionBlend = 0.3f;
    f32 motionThreshold = 0.01f;
    u32 maxReprojectionFrames = 16;
    bool neighborhoodClipping = true;
    f32 clipRadius = 0.15f;
};

// ============================================================================
// Reflection configuration
// ============================================================================
struct ReflectionConfig {
    bool enabled = true;
    f32 maxRoughness = 0.5f;
    u32 maxRoughnessSamples = 16;
    u32 maxReflectionRays = 256;
    bool useRTForMirror = true;
    f32 mirrorThreshold = 0.05f;
    f32 temporalBlendWeight = 0.85f;
};

// ============================================================================
// Bounce configuration
// ============================================================================
struct BounceConfig {
    u32 maxBounces = 1;
    f32 bounceEnergyFalloff = 0.7f;
    bool screenSpaceBounce = true;
    bool worldSpaceBounce = true;
    u32 bounceResolutionScale = 2;
};

// ============================================================================
// Emissive surface configuration
// ============================================================================
struct EmissiveConfig {
    bool enabled = true;
    u32 maxEmissiveCards = 2048;
    f32 emissiveMultiplier = 1.0f;
    f32 minEmissiveIntensity = 0.1f;
    bool areaLightSampling = true;
    u32 samplesPerEmissive = 4;
};

// ============================================================================
// Lumen System main class
// ============================================================================
class LumenSystem {
public:
    LumenSystem();
    ~LumenSystem();

    bool init(u32 screenWidth, u32 screenHeight);
    void shutdown();
    void resize(u32 screenWidth, u32 screenHeight);

    // Frame lifecycle
    void beginFrame(const Camera& camera, f32 deltaTime);
    void render();
    void endFrame();
    void applyGI(/* output texture handle */);

    // Configuration
    void setQuality(LumenQuality quality);
    LumenQuality quality() const { return quality_; }
    void setScreenTraceConfig(const ScreenTraceConfig& cfg) { screenTraceCfg_ = cfg; }
    void setTemporalConfig(const TemporalConfig& cfg) { temporalCfg_ = cfg; }
    void setReflectionConfig(const ReflectionConfig& cfg) { reflectionCfg_ = cfg; }
    void setBounceConfig(const BounceConfig& cfg) { bounceCfg_ = cfg; }
    void setEmissiveConfig(const EmissiveConfig& cfg) { emissiveCfg_ = cfg; }
    void setRadianceCacheConfig(const RadianceCacheConfig& cfg) { radianceCacheCfg_ = cfg; }
    void setSurfaceCacheConfig(const SurfaceCacheConfig& cfg) { surfaceCacheCfg_ = cfg; }
    void setGlobalDFConfig(const GlobalDFConfig& cfg) { globalDFCfg_ = cfg; }

    // Surface cache page residency
    void setSurfaceCachePageConfig(const SurfaceCachePageConfig& cfg) { pageCacheCfg_ = cfg; }
    const SurfaceCachePageConfig& surfaceCachePageConfig() const { return pageCacheCfg_; }
    u32 requestPage(const Vec3& worldPos);
    void evictPage(u32 pageIndex);
    void ensureResidentPages(u32 cameraProxyCount, const Vec3* cameraProxies, f32 radius);
    u32 addPatch(const Vec3& pos, const Vec3& normal);
    void updatePageRadiance(u32 pageIndex, u32 rayCount, const Vec3* rayDirections,
                            const Vec3* rayHits, const Vec3* rayRadiance);
    Vector<u32> dirtyPages() const;
    Vec3 traceRadiance(const Vec3& origin, const Vec3& dir, f32 maxDist, const f32* sceneDepth);
    Vec3 sampleAtlas(const Vec3& worldPos, u32 pageIndex) const;
    void updateSurfaceCache(f32 dt, u32 frameIndex, u32 maxRaysPerFrame);
    void resetSurfaceCache();
    const Vector<f32>& getRadianceAtlas() const { return radianceAtlas_; }
    const Vector<SurfaceCachePage>& getPages() const { return pages_; }
    u32 getDirtyPageCount() const;

    // Scene submission
    void addMeshToSurfaceCache(u32 objectId, const Vec3& boundsMin, const Vec3& boundsMax,
                               const Vec3& albedo, const Vec3& emissive, f32 emissiveIntensity);
    void removeMeshFromSurfaceCache(u32 objectId);
    void updateMeshTransform(u32 objectId, const Mat4& transform);
    void addSDFMesh(u32 objectId, const SDFMeshData& sdfData);
    void removeSDFMesh(u32 objectId);

    // Light injection
    void setSunDirection(const Vec3& dir);
    void setSunColor(const Vec3& color, f32 intensity);
    void setAmbientColor(const Vec3& color, f32 intensity);

    // Query
    f32 getGIFactor() const { return giFactor_; }
    f32 getReflectionFactor() const { return reflectionFactor_; }
    u32 getActiveCards() const { return activeCardCount_; }
    u32 getActiveProbes() const { return activeProbeCount_; }

    // Adaptive quality
    void adaptQuality(f32 frameTimeMs);

    // Debug visualization
    void visualizeSurfaceCards(Vec3* debugOutput);
    void visualizeRadianceCache(Vec3* debugOutput);

    // Screen-space GI compositing
    void compositeScreenSpaceGI(Vec3* outputBuffer);

    // Stats
    struct Stats {
        u32 cardsRendered;
        u32 probesUpdated;
        u32 screenTraces;
        u32 sdfTraces;
        u32 bouncePasses;
        f32 giBuildTimeMs;
        f32 reflectionTimeMs;
        u32 residentPages;
        u32 patchesCached;
        u32 pagesEvicted;
        u64 raysTraced;
        f32 cacheUpdateMs;
    };
    const Stats& stats() const { return stats_; }

private:
    // Surface Cache pipeline
    void buildSurfaceCards();
    void renderSurfaceCacheCards();
    void generateScreenCards();
    void updateEmissiveCards();
    void updateSurfaceCacheHierarchy();
    f32 computeCardScreenSize(const Vec3& center, f32 radius, const Mat4& viewProj) const;

    // Screen Tracing
    void performScreenTracing();
    void traceScreenSpaceRay(const Vec3& origin, const Vec3& dir, Vec3& hitPos, f32& hitT);
    Vec3 hiZTrace(const Vec3& origin, const Vec3& dir, f32 maxDist);
    bool sampleHiZ(i32 mip, i32 x, i32 y, f32& depth);

    // SDF Tracing
    void performSDFTracing();
    void traceSDFsWithTemporalCoherence();
    f32 sampleSDFGlobal(const Vec3& worldPos);
    f32 sampleSDFMesh(const Vec3& worldPos, u32 meshIndex);
    Vec3 estimateSDFNormal(const Vec3& worldPos);
    bool traceSDFRay(const Vec3& origin, const Vec3& dir, f32 maxDist, Vec3& hitPos, Vec3& hitNormal);

    // Global Distance Field
    void buildGlobalDistanceField();
    void mergeSDFClipmap();
    f32 sampleClipmap(i32 level, const Vec3& localPos);

    // Radiance Cache
    void updateRadianceCache();
    void updateProbe(u32 probeIndex);
    void injectDirectLight();
    void injectIndirectLight();
    Vec3 computeProbeRadiance(const RadianceProbe& probe, const Vec3& direction);
    void temporalFilterRadianceCache();
    i32 findNearestProbe(const Vec3& worldPos) const;

    // Temporal Accumulation
    void temporalAccumulation();
    Vec3 neighborhoodClip(const Vec3& current, const Vec3& history);
    bool isDisoccluded(const Vec2& currentUV, const Vec2& historyUV, f32 currentDepth, f32 historyDepth);

    // Reflections
    void performReflections();
    void traceReflectionRay(const Vec3& origin, const Vec3& dir, Vec3& hitPos, Vec3& hitNormal);
    Vec3 importanceSampleGGX(const Vec2& Xi, f32 roughness, const Vec3& N);
    f32 computeReflectionPDF(const Vec3& H, const Vec3& N, f32 roughness);

    // Bounce lighting
    void performBounceLighting();
    void computeBounceFromSurfaceCache(u32 bounceIndex);
    void computeBounceFromRadianceCache(u32 bounceIndex);
    void computeMultiBounceTransport();

    // Emissive surfaces
    void computeEmissiveContribution();
    Vec3 evaluateEmissiveAreaLight(const SurfaceCard& card, const Vec3& hitPos, const Vec3& hitNormal);
    f32 computeAreaLightPDF(const Vec3& pointOnLight, const Vec3& hitPos, const Vec3& lightNormal, f32 lightArea);
    Vec3 sampleEmissiveLights(const Vec3& hitPos, const Vec3& hitNormal, u32 sampleCount);

    // Fresnel
    Vec3 fresnelSchlick(f32 cosTheta, const Vec3& F0);

    // Quality-dependent settings
    void applyQualityPreset();

    // Ray utilities
    struct Ray {
        Vec3 origin;
        Vec3 direction;
        f32 tMin;
        f32 tMax;
    };

    struct RayHit {
        Vec3 position;
        Vec3 normal;
        f32 t;
        u32 objectId;
        bool hit;
    };

    // Members
    LumenQuality quality_ = LumenQuality::High;
    u32 screenWidth_ = 0;
    u32 screenHeight_ = 0;
    f32 deltaTime_ = 0.0f;
    f32 time_ = 0.0f;
    u32 frameIndex_ = 0;

    // Camera data
    Vec3 cameraPosition_;
    Vec3 cameraDirection_;
    Mat4 viewMatrix_;
    Mat4 projMatrix_;
    Mat4 viewProjMatrix_;
    Mat4 prevViewProjMatrix_;

    // Surface Cache
    SurfaceCacheConfig surfaceCacheCfg_;
    Vector<SurfaceCard> surfaceCards_;
    u32 activeCardCount_ = 0;

    // Surface Cache (page-based radiance atlas)
    SurfaceCachePageConfig pageCacheCfg_;
    Vector<SurfaceCachePage> pages_;
    Vector<CachedPatch> patches_;
    HashMap<u64, u32> pageMap_;
    Vector<f32> radianceAtlas_;
    Vector<u32> freePageSlots_;

    // SDF Meshes
    SDFMeshConfig sdfMeshCfg_;
    Vector<SDFMeshData> sdfMeshes_;
    u32 sdfMeshCount_ = 0;

    // Global Distance Field
    GlobalDFConfig globalDFCfg_;
    Vector<ClipmapLevel> clipmapLevels_;
    Vector<f32> clipmapData_;

    // Radiance Cache
    RadianceCacheConfig radianceCacheCfg_;
    Vector<RadianceProbe> radianceProbes_;
    u32 activeProbeCount_ = 0;

    // Screen Tracing
    ScreenTraceConfig screenTraceCfg_;
    Vector<f32> depthBuffer_;
    Vector<Vec3> normalBuffer_;
    Vector<Vec3> albedoBuffer_;

    // Temporal
    TemporalConfig temporalCfg_;
    Vector<Vec3> historyGI_;
    Vector<Vec3> currentGI_;
    Vector<f32> historyDepth_;

    // Reflections
    ReflectionConfig reflectionCfg_;
    Vector<Vec3> reflectionOutput_;

    // Bounce
    BounceConfig bounceCfg_;
    Vector<Vec3> bounceBuffer_[2];

    // Emissive
    EmissiveConfig emissiveCfg_;
    u32 emissiveCardCount_ = 0;

    // Lighting
    Vec3 sunDirection_;
    Vec3 sunColor_;
    f32 sunIntensity_;
    Vec3 ambientColor_;
    f32 ambientIntensity_;

    // Output
    f32 giFactor_ = 1.0f;
    f32 reflectionFactor_ = 1.0f;

    // Stats
    Stats stats_;

    bool initialized_ = false;
};

} // namespace Frost
