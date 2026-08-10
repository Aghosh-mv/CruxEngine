#pragma once

// ============================================================================
// FrostEngine Virtual Shadow Maps
// ============================================================================
// Replaces cascaded shadow maps with virtual texturing, similar to UE5's VSM.
// Features: Virtual texture 16Kx16K, Clipmap structure, Page management,
// Per-pixel caching, Moment Shadow Maps, Contact shadows,
// Temporal stability, Nanite integration.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/HashMap.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Math.h"
#include "Renderer/Types.h"
#include "Renderer/Light.h"
#include "Renderer/NaniteSystem.h"

namespace Frost {

struct Camera;

// ============================================================================
// Constants
// ============================================================================
static constexpr u32 VSM_VIRTUAL_RESOLUTION = 16384;       // 16K x 16K
static constexpr u32 VSM_PAGE_SIZE = 128;                  // 128x128 texels per page
static constexpr u32 VSM_PAGES_PER_ROW = VSM_VIRTUAL_RESOLUTION / VSM_PAGE_SIZE;  // 128
static constexpr u32 VSM_MAX_PAGES = 131072;               // max allocated pages
static constexpr u32 VSM_CLIPMAP_LEVELS = 8;              // concentric clipmap rings
static constexpr u32 VSM_MAX_LIGHTS = 64;                 // max shadow-casting lights

// ============================================================================
// Page states
// ============================================================================
enum class PageState : u8 {
    Free = 0,
    Unallocated = 0,
    Allocated,
    Resident,
    Rendering,
    Rendered,
    Cached,
    Evicted,
    Dirty
};

enum class PageType : u8 {
    DirectLight = 0,        // sun/directional shadow
    PointLight,             // point light shadow (cube face)
    SpotLight,              // spot light shadow
    AreaLight,              // area light shadow
    ContactShadow           // short-range contact shadow
};

// ============================================================================
// Physical page slot in the virtual page table
// ============================================================================
struct Page {
    u32 x = 0;
    u32 y = 0;
    u32 level = 0;
    PageState state = PageState::Free;
    u64 lastTouchedFrame = 0;
};

// ============================================================================
// Shadow page
// ============================================================================
struct ShadowPage {
    u32 pageIndex;
    u32 clipmapLevel;
    u32 tileX;              // tile position in virtual texture (0..127)
    u32 tileY;
    u32 lightIndex;
    PageType type;
    PageState state;
    f32 resolutionScale;    // effective resolution multiplier
    f32 minDepth;
    f32 maxDepth;
    Vec3 boundsMin;
    Vec3 boundsMax;
    Vec3 lightDirection;
    Mat4 viewProjection;
    u32 frameLastRendered;
    u32 frameLastAccessed;
    bool dirty;
};

// ============================================================================
// Clipmap ring
// ============================================================================
struct ClipmapRing {
    u32 level;
    f32 innerRadius;
    f32 outerRadius;
    f32 texelSize;
    f32 pageResolution;
    Vec3 centerOffset;      // camera-relative offset
    Mat4 viewProjection;
    Vector<u32> pages;      // indices into page table
    u32 activePageCount;
    bool needsUpdate;
};

// ============================================================================
// Cascade clipmap ring
// ============================================================================
struct CascadeClipmap {
    f32 splitNear = 0.0f;
    f32 splitFar = 0.0f;
    f32 texelSize = 0.0f;
    Mat4 viewProjection;
    u32 mapIndex = 0;
    bool active = true;
};

// ============================================================================
// Moment Shadow Map data
// ============================================================================
struct MomentData {
    f32 depth;
    f32 depthSquared;
    f32 moment3;
    f32 moment4;
    f32 weight;
};

// ============================================================================
// Contact shadow configuration
// ============================================================================
struct ContactShadowConfig {
    bool enabled = true;
    u32 maxSteps = 16;
    f32 stepSize = 0.5f;
    f32 thickness = 0.1f;
    f32 intensity = 1.0f;
    f32 maxDistance = 5.0f;
    bool hiZAcceleration = true;
};

// ============================================================================
// VSM configuration
// ============================================================================
struct VSMConfig {
    u32 virtualResolution = VSM_VIRTUAL_RESOLUTION;
    u32 pageSize = VSM_PAGE_SIZE;
    u32 clipmapLevels = 4;
    u32 pagesPerDim = 16;
    f32 farPlane = 500.0f;
    f32 firstSplit = 10.0f;
    f32 splitFactor = 3.0f;
    f32 clipmapBaseResolution = 0.5f;   // meters per texel at level 0
    f32 clipmapSpacingFactor = 2.0f;
    f32 pageLifeTime = 300.0f;          // frames before eviction
    u32 maxPagesPerFrame = 256;
    bool useMomentShadowMaps = true;
    bool useContactShadows = true;
    ContactShadowConfig contactShadow;
    f32 shadowBias = 0.001f;
    f32 normalBias = 0.02f;
    f32 temporalStability = 0.95f;
    u32 maxReprojectionFrames = 16;
    f32 evictionThreshold = 0.01f;
};

// ============================================================================
// Page allocation stats
// ============================================================================
struct PageAllocStats {
    u32 totalAllocated;
    u32 totalCached;
    u32 totalDirty;
    u32 allocatedThisFrame;
    u32 evictedThisFrame;
    u32 renderedThisFrame;
};

// ============================================================================
// Virtual Shadow Map System
// ============================================================================
class VirtualShadowMaps {
public:
    VirtualShadowMaps();
    ~VirtualShadowMaps();

    bool init(u32 screenWidth, u32 screenHeight);
    void shutdown();
    void resize(u32 screenWidth, u32 screenHeight);

    // Frame lifecycle
    void beginFrame(const Camera& camera, f32 deltaTime);
    void allocatePages();
    void renderShadowMaps();
    void applyShadowMaps();
    void endFrame();

    // Configuration
    void setConfig(const VSMConfig& cfg) { config_ = cfg; pageSize_ = cfg.pageSize; pagesPerDim_ = cfg.pagesPerDim; }
    const VSMConfig& config() const { return config_; }
    const VSMConfig& getConfig() const;

    // Cascade clipmaps
    void computeClipmaps(const Vec3& lightDir, const Vec3& center, f32 radius);
    void allocatePagesForBounds(const Vec3& center, f32 radius);
    void evictOldestPages(u32 maxResident);
    void requestPage(u32 x, u32 y, u32 level);
    u32 selectCascade(const Vec3& worldPos, const Vec3& camPos, f32 camNear) const;
    void placePage(u32 pageIndex, u32& atlasX, u32& atlasY);
    void reset();

    const HashMap<u64, u32>& getPageTable() const;
    const Vector<Page>& getPages() const;

    // Cascade clipmap stats accessors
    u32 getResidentPages() const;
    u32 getPagesEvicted() const;
    u32 getPagesRequested() const;
    u32 getActiveClipmaps() const;
    u64 getPageTableEntries() const;

    // Light management
    u32 addShadowCastingLight(const Vec3& position, const Vec3& direction,
                              f32 intensity, f32 range, LightType type);
    void removeShadowCastingLight(u32 lightIndex);
    void updateLightTransform(u32 lightIndex, const Vec3& position, const Vec3& direction);

    // Page management
    u32 allocatePage(u32 clipmapLevel, u32 tileX, u32 tileY, u32 lightIndex, PageType type);
    void deallocatePage(u32 pageIndex);
    void markPageDirty(u32 pageIndex);
    u32 findPage(u32 clipmapLevel, u32 tileX, u32 tileY) const;

    // Clipmap management
    void updateClipmaps();
    void renderClipmapLevel(u32 level);
    void updateClipmapPages(u32 level, const Vec3& cameraPos);

    // Moment Shadow Maps
    void computeMomentShadowMap(const ShadowPage& page);
    f32 sampleShadowMOM(const Vec2& uv, f32 comparisonDepth, u32 lightIndex);
    void computeMoments4(const f32* depthBuffer, f32* momentBuffer, u32 width, u32 height);

    // Contact shadows
    void renderContactShadows();
    f32 traceContactShadow(const Vec3& worldPos, const Vec3& lightDir, f32 maxDist);
    f32 traceContactShadowHiZ(const Vec3& worldPos, const Vec3& lightDir, f32 maxDist);

    // Temporal stability
    void temporalReprojection();
    bool reprojectPage(const ShadowPage& currentPage, ShadowPage& historyPage);
    f32 computeTemporalWeight(u32 currentFrame, u32 lastFrame) const;

    // Nanite integration
    void renderNaniteShadow(const Cluster& cluster, const Mat4& viewProj);
    void rasterizeClusterShadow(const Cluster& cluster, const Mat4& viewProj, u32 pageIndex);

    // Shadow query
    f32 getShadowAtPosition(const Vec3& worldPos, u32 lightIndex) const;
    f32 computePCFShadow(const Vec2& uv, f32 depth, u32 lightIndex) const;
    f32 computePCSSShadow(const Vec3& worldPos, u32 lightIndex) const;

    // Page table query
    const ShadowPage* getPage(u32 pageIndex) const;
    u32 getPageCount() const { return pageCount_; }
    u32 getActivePageCount() const;

    // Stats
    struct Stats {
        u32 allocatedPages;
        u32 cachedPages;
        u32 dirtyPages;
        u32 renderedPages;
        u32 evictedPages;
        u32 clipmapUpdates;
        u32 contactShadows;
        f32 shadowBuildTimeMs;
        f32 shadowRenderTimeMs;
        f32 contactShadowTimeMs;
        f32 temporalResolveTimeMs;
        u32 residentPages;
        u32 pagesEvicted;
        u32 pagesRequested;
        u32 activeClipmaps;
        u64 pageTableEntries;
    };
    const Stats& stats() const { return stats_; }

    // Convenience: get the shadow VP matrix for a light
    const Mat4& getShadowVP(u32 lightIndex) const { return shadowVPs_[lightIndex]; }

private:
    // Page lifecycle
    void initializePageTable();
    void garbageCollectPages();
    void prioritizePageUpdates();

    // Clipmap construction
    void buildClipmapViewProjection(u32 level, const Vec3& cameraPos, Mat4& viewProj);
    Vec3 computeClipmapOrigin(u32 level, const Vec3& cameraPos) const;

    // Rendering internals
    void renderPageToTexture(const ShadowPage& page);
    void rasterizePageGeometry(const ShadowPage& page);
    void applyShadowBias(f32& depth, const Vec3& normal, const Vec3& lightDir);

    // Moment computation
    void computeMoments2(f32 depth, f32& m1, f32& m2);
    f32 chebyshevInequality(f32 m1, f32 m2, f32 distance);

    // Contact shadow internals
    f32 sampleSceneDepth(const Vec2& screenUV) const;
    bool hiZTraceSegment(const Vec3& start, const Vec3& end, f32& hitT) const;

    // Temporal helpers
    void computeReprojectionMatrix(const Mat4& currentVP, const Mat4& historyVP, Mat4& reprojection);

    // Stats update
    void updateStats();

    // Members
    VSMConfig config_;
    u32 screenWidth_ = 0;
    u32 screenHeight_ = 0;
    f32 deltaTime_ = 0.0f;
    u32 frameIndex_ = 0;

    // Page table
    Vector<ShadowPage> shadowPages_;
    u32 pageCount_ = 0;
    u32 pageCapacity_ = 0;

    // Cascade clipmap page table
    Vector<CascadeClipmap> clipmaps_;
    HashMap<u64, u32> pageTable_;
    Vector<u64> lastTouched_;
    Vector<Page> pages_;
    u32 pageSize_ = 128;
    u32 pagesPerDim_ = 16;
    u32 atlasX_ = 0;
    u32 atlasY_ = 0;
    u32 residentPageCount_ = 0;
    u64 totalPagesRequested_ = 0;
    u64 totalPagesEvicted_ = 0;

    // Clipmaps
    ClipmapRing clipmapRings_[VSM_CLIPMAP_LEVELS];

    // Light data
    struct ShadowLight {
        Vec3 position;
        Vec3 direction;
        f32 intensity;
        f32 range;
        LightType type;
        bool active;
        Mat4 viewProjection;
        u32 faceCount;      // 6 for point, 1 for others
    };
    ShadowLight lights_[VSM_MAX_LIGHTS];
    u32 lightCount_ = 0;
    Mat4 shadowVPs_[VSM_MAX_LIGHTS];

    // Temporal
    Vector<ShadowPage> historyPages_;
    Mat4 previousViewProj_;
    bool firstFrame_ = true;

    // Contact shadows
    ContactShadowConfig contactCfg_;

    // Camera
    Vec3 cameraPosition_;
    Vec3 cameraDirection_;
    Mat4 viewMatrix_;
    Mat4 projMatrix_;
    Mat4 viewProjMatrix_;

    // Stats
    Stats stats_;
    PageAllocStats allocStats_;

    bool initialized_ = false;
};

} // namespace Frost
