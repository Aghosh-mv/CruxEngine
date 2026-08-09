#pragma once

// ============================================================================
// FrostEngine MegaLights System
// ============================================================================
// Allows infinite dynamic shadow-casting lights, similar to UE5.7's MegaLights.
// Features: Light classification, Tiled light assignment, Clustered Forward+,
// Shadow atlas, Virtual shadow integration, Light culling,
// Stochastic shadow sampling, Light cookies.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Math.h"
#include "Renderer/Types.h"

namespace Frost {

struct Camera;

// ============================================================================
// Constants
// ============================================================================
static constexpr u32 MEGALIGHTS_MAX_LIGHTS = 65536;
static constexpr u32 MEGALIGHTS_TILE_SIZE = 16;
static constexpr u32 MEGALIGHTS_MAX_LIGHTS_PER_TILE = 256;
static constexpr u32 MEGALIGHTS_CLUSTER_X = 16;
static constexpr u32 MEGALIGHTS_CLUSTER_Y = 16;
static constexpr u32 MEGALIGHTS_CLUSTER_Z = 24;
static constexpr u32 MEGALIGHTS_SHADOW_ATLAS_SIZE = 16384;
static constexpr u32 MEGALIGHTS_SHADOW_PAGE_SIZE = 256;
static constexpr u32 MEGALIGHTS_MAX_SHADOW_PAGES = 1024;
static constexpr u32 MEGALIGHTS_MAX_COOKIES = 256;

// ============================================================================
// MegaLight types
// ============================================================================
enum class MegaLightType : u8 {
    Directional = 0,
    Point,
    Spot,
    AreaRect,
    AreaDisk,
    AreaTube,
    COUNT
};

enum class ShadowType : u8 {
    None = 0,
    Virtual,            // virtual shadow map
    Cascaded,           // cascaded shadow maps
    RayTraced,          // ray-traced shadows
    ContactOnly         // contact shadows only
};

enum class CookieType : u8 {
    None = 0,
    Texture,            // 2D texture projection
    Cubemap,            // omnidirectional
    Tube                // linear projection
};

// ============================================================================
// MegaLight data
// ============================================================================
struct MegaLight {
    u64 id;
    MegaLightType type;
    bool enabled;
    bool castShadow;
    bool isStatic;

    Vec3 position;
    Vec3 direction;
    Vec3 color;
    f32 intensity;

    // Attenuation
    f32 range;
    f32 falloffExponent;

    // Spot
    f32 innerConeAngle;
    f32 outerConeAngle;

    // Area
    f32 width;
    f32 height;
    f32 radius;

    // Shadow
    ShadowType shadowType;
    u32 shadowResolution;
    f32 shadowBias;
    f32 shadowNormalBias;
    f32 shadowSoftness;
    f32 shadowDistance;

    // Cookie
    bool hasCookie = false;
    CookieType cookieType;
    u32 cookieIndex;
    f32 cookieIntensity;
    f32 cookieScale;

    // Volumetric
    bool volumetric;
    f32 volumetricScattering;

    // Animation
    bool animated;
    f32 flickerSpeed;
    f32 flickerAmount;

    // Classification
    u32 tileMask;           // bitmask of tiles this light affects
    u32 clusterX, clusterY, clusterZ;   // cluster grid position
    f32 importanceScore;

    f32 attenuation(f32 dist) const {
        f32 atten = 1.0f / (1.0f + powf(dist / range, falloffExponent));
        return atten * (dist < range ? 1.0f : 0.0f);
    }

    f32 spotAttenuation(Vec3 toLight) const {
        f32 cosAngle = toLight.normalized().dot(direction.normalized());
        f32 innerCos = cosf(innerConeAngle * 0.017453f);
        f32 outerCos = cosf(outerConeAngle * 0.017453f);
        return Mathf::clamp((cosAngle - outerCos) / (innerCos - outerCos), 0.0f, 1.0f);
    }

    f32 areaLightSolidAngle(const Vec3& target) const {
        Vec3 toTarget = target - position;
        f32 dist = toTarget.length();
        if (dist < 0.001f) return 0.0f;
        f32 area = width * height;
        return area / (dist * dist);
    }
};

// ============================================================================
// Tile light list
// ============================================================================
struct TileLightList {
    u32 lightIndices[MEGALIGHTS_MAX_LIGHTS_PER_TILE];
    u32 lightCount;
};

// ============================================================================
// Cluster light list
// ============================================================================
struct ClusterLightList {
    u32 lightIndices[MEGALIGHTS_MAX_LIGHTS_PER_TILE];
    u32 lightCount;
};

// ============================================================================
// Shadow atlas page
// ============================================================================
struct ShadowAtlasPage {
    u32 pageIndex;
    u32 lightIndex;
    u32 resolution;
    f32 x, y;              // position in atlas (normalized)
    f32 size;               // size in atlas (normalized)
    bool dirty;
    u32 frameLastRendered;
};

// ============================================================================
// Shadow atlas
// ============================================================================
struct ShadowAtlas {
    u32 textureHandle;
    u32 resolution;
    Vector<ShadowAtlasPage> pages;
    u32 pageCount;
    u32 dirtyPageCount;
    u32 usedMemoryBytes;
};

// ============================================================================
// Cookie data
// ============================================================================
struct LightCookie {
    u32 textureHandle;
    Vec2 scale;
    Vec2 offset;
    f32 rotation;
    f32 intensity;
    bool isProjected;
};

// ============================================================================
// Stochastic sampling config
// ============================================================================
struct StochasticConfig {
    bool enabled = true;
    u32 maxLightsPerSample = 8;
    u32 samplesPerPixel = 4;
    bool useBlueNoise = true;
    f32 denoiseRadius = 2.0f;
    f32 denoiseStrength = 0.8f;
    bool temporalAccumulation = true;
    f32 temporalBlendWeight = 0.9f;
};

// ============================================================================
// Light culling config
// ============================================================================
struct CullingConfig {
    bool frustumCull = true;
    bool distanceCull = true;
    f32 maxShadowDistance = 1000.0f;
    f32 maxLightInfluence = 0.01f;
    bool twoPassCulling = true;
    u32 cullingBatchSize = 256;
};

// ============================================================================
// MegaLights System
// ============================================================================
class MegaLights {
public:
    MegaLights();
    ~MegaLights();

    bool init(u32 screenWidth, u32 screenHeight);
    void shutdown();
    void resize(u32 screenWidth, u32 screenHeight);

    // Frame lifecycle
    void beginFrame(const Camera& camera, f32 deltaTime);
    void classifyLights();
    void cullLights();
    void assignLightsToTiles();
    void assignLightsToClusters();
    void renderShadows();
    void applyLighting();
    void denoiseShadows();
    void endFrame();

    // Light management
    u64 addLight(const MegaLight& light);
    void removeLight(u64 lightId);
    void updateLight(u64 lightId, const MegaLight& light);
    MegaLight* getLight(u64 lightId);
    const MegaLight* getLight(u64 lightId) const;
    u32 lightCount() const { return static_cast<u32>(activeLights_.size()); }

    // Directional light
    void setDirectionalLight(const Vec3& direction, const Vec3& color, f32 intensity);

    // Shadow atlas
    u32 allocateShadowPage(u32 lightIndex, u32 resolution);
    void deallocateShadowPage(u32 pageIndex);
    void renderShadowPage(u32 pageIndex);
    void updateShadowAtlas();

    // Cookies
    u32 addCookie(u32 textureHandle, const Vec2& scale, const Vec2& offset);
    void removeCookie(u32 cookieIndex);
    void bindCookie(u32 lightIndex, u32 cookieIndex);

    // Light classification
    void classifyLightType(MegaLight& light);
    void computeImportanceScore(MegaLight& light);
    void sortLightsByImportance();

    // Tile assignment
    void computeTileBounds(u32 tileX, u32 tileY, Vec3& boundsMin, Vec3& boundsMax) const;
    bool lightIntersectsTile(const MegaLight& light, u32 tileX, u32 tileY) const;
    void buildTileLightLists();

    // Cluster assignment
    void computeClusterBounds(u32 x, u32 y, u32 z, Vec3& boundsMin, Vec3& boundsMax) const;
    bool lightIntersectsCluster(const MegaLight& light, u32 x, u32 y, u32 z) const;
    void buildClusterLightLists();

    // Stochastic sampling
    u32 selectLightForSample(const Vec2& screenUV, u32 sampleIndex) const;
    f32 computeLightProbability(const MegaLight& light, const Vec2& screenUV) const;

    // Query
    const TileLightList& getTileLights(u32 tileX, u32 tileY) const;
    const ClusterLightList& getClusterLights(u32 x, u32 y, u32 z) const;
    f32 getLightAtPosition(const Vec3& worldPos, u32& lightCount) const;

    // Stats
    struct Stats {
        u32 totalLights;
        u32 activeLights;
        u32 shadowCastingLights;
        u32 tilesUpdated;
        u32 clustersUpdated;
        u32 shadowPagesRendered;
        u32 cookieBindings;
        f32 classifyTimeMs;
        f32 cullTimeMs;
        f32 shadowTimeMs;
        f32 lightingTimeMs;
    };
    const Stats& stats() const { return stats_; }

private:
    // Internal helpers
    void initializeTileGrid();
    void initializeClusterGrid();
    void clearTileLightLists();
    void clearClusterLightLists();

    // Shadow rendering
    void renderDirectionalShadow(const MegaLight& light);
    void renderPointShadow(const MegaLight& light, u32 pageIndex);
    void renderSpotShadow(const MegaLight& light, u32 pageIndex);
    void renderAreaShadow(const MegaLight& light, u32 pageIndex);

    // Atlas management
    void compactAtlas();
    u32 findFreeAtlasSlot(u32 resolution) const;
    void computeAtlasUV(const ShadowAtlasPage& page, f32& u0, f32& v0, f32& u1, f32& v1) const;

    // Denoising
    void spatialDenoise();
    void temporalAccumulate();
    void computeMotionVectors();

    // Light influence
    f32 computeLightInfluence(const MegaLight& light, const Vec2& screenUV, f32 sceneDepth) const;
    f32 computeTileCoverage(const MegaLight& light, u32 tileX, u32 tileY) const;

    // Screen-space helpers
    void screenToWorld(const Vec2& screenUV, f32 depth, Vec3& worldPos) const;
    void worldToScreen(const Vec3& worldPos, Vec2& screenUV, f32& depth) const;

    // Members
    StochasticConfig stochasticCfg_;
    CullingConfig cullingCfg_;
    u32 screenWidth_ = 0;
    u32 screenHeight_ = 0;
    u32 tileCountX_ = 0;
    u32 tileCountY_ = 0;
    f32 deltaTime_ = 0.0f;
    u32 frameIndex_ = 0;

    // Lights
    Vector<MegaLight> allLights_;
    Vector<MegaLight*> activeLights_;
    u64 nextLightId_ = 1;

    // Directional light (special case)
    MegaLight directionalLight_;
    bool hasDirectionalLight_ = false;

    // Tile grid
    Vector<TileLightList> tileGrid_;

    // Cluster grid
    Vector<ClusterLightList> clusterGrid_;

    // Shadow atlas
    ShadowAtlas shadowAtlas_;

    // Cookies
    Vector<LightCookie> cookies_;
    u32 cookieCount_ = 0;

    // Temporal buffers
    Vector<f32> historyShadow_;
    Vector<f32> currentShadow_;
    Vector<Vec3> motionVectors_;

    // Camera
    Vec3 cameraPosition_;
    Vec3 cameraDirection_;
    Mat4 viewMatrix_;
    Mat4 projMatrix_;
    Mat4 viewProjMatrix_;
    Mat4 prevViewProjMatrix_;
    f32 nearPlane_;
    f32 farPlane_;

    // Stats
    Stats stats_;

    bool initialized_ = false;
};

} // namespace Frost
