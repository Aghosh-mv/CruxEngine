#pragma once

// ============================================================================
// FrostEngine FrostRadiance — Surfel-Based Global Illumination
// ============================================================================
// Proprietary surfel radiance system. Fundamentally different from Lumen's
// screen-trace + distance-field approach. Uses surface element (surfel)
// accumulation with hierarchical octree gathering for multi-bounce GI.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Math.h"
#include "Core/Vector.h"

namespace Frost {

// Quality presets controlling surfel density
enum class SurfQuality : u8 {
    Low = 0,     // 50K surfels
    Medium,      // 200K surfels
    High,        // 500K surfels
    Epic,        // 1M surfels
    COUNT
};

// A single surfel: surface element storing lighting information
struct Surfel {
    Vec3 position;          // world-space center
    Vec3 normal;            // geometric normal
    Vec3 flux;              // accumulated light flux (RGB)
    Vec3 radiance;          // direct + indirect radiance
    Vec3 albedo;            // surface albedo
    f32 radius;             // disk radius (adaptive)
    f32 age;                // frames since creation
    u32 triangleID;         // source triangle
    u32 meshID;             // source mesh
    u32 flags;              // bit 0: dirty, bit 1: static, bit 2: needs bounces
    u32 prevFrameIndex;     // for temporal reprojection
    Vec3 prevWorldPos;      // previous frame world position
    Vec2 prevScreenUV;      // previous frame screen UV

    Surfel() { memset(this, 0, sizeof(Surfel)); }
};

// Octree node for hierarchical surfel gathering
struct SurfelOctreeNode {
    Vec3 center;
    Vec3 boundsMin;
    Vec3 boundsMax;
    u32 firstChild;         // index of first child, or 0xFFFFFFFF if leaf
    u32 firstSurfel;        // first surfel index in this node
    u32 surfelCount;        // number of surfels in this node
    u32 parentIndex;
    f32 splitThreshold;     // max surfels before splitting
};

// Surfel pool: fixed-size GPU-resident surfel storage
struct SurfelPool {
    static constexpr u32 MAX_SURFELS = 1024 * 1024;  // 1M

    Vector<Surfel> surfels;
    u32 activeCount = 0;
    u32 freeListHead = 0;
    Vector<u32> freeList;

    void init(u32 capacity) {
        surfels.resize(capacity);
        freeList.resize(capacity);
        activeCount = 0;
        // Build free list
        for (u32 i = 0; i < capacity; i++) {
            freeList[i] = i + 1;
        }
        freeList[capacity - 1] = 0xFFFFFFFF;
        freeListHead = 0;
    }

    u32 allocate() {
        if (freeListHead == 0xFFFFFFFF) return 0xFFFFFFFF;
        u32 idx = freeListHead;
        freeListHead = freeList[idx];
        freeList[idx] = 0xFFFFFFFF;
        if (idx >= activeCount) activeCount = idx + 1;
        return idx;
    }

    void free(u32 idx) {
        if (idx == 0xFFFFFFFF) return;
        surfels[idx] = Surfel{};
        freeList[idx] = freeListHead;
        freeListHead = idx;
    }

    Surfel& operator[](u32 idx) { return surfels[idx]; }
    const Surfel& operator[](u32 idx) const { return surfels[idx]; }
};

// Irradiance volume: 3D texture for distant object irradiance
struct IrradianceVolume {
    Vec3 origin;
    Vec3 cellSize;
    u32 resX, resY, resZ;
    Vector<Vec3> texels;  // RGB irradiance per voxel

    void init(Vec3 volOrigin, Vec3 volSize, u32 resolution) {
        origin = volOrigin;
        resX = resolution;
        resY = resolution;
        resZ = resolution;
        cellSize = Vec3(volSize.x / resX, volSize.y / resY, volSize.z / resZ);
        texels.resize(resX * resY * resZ);
        for (auto& t : texels) t = Vec3(0);
    }

    void write(Vec3 worldPos, Vec3 irradiance) {
        Vec3 local = worldPos - origin;
        i32 x = (i32)(local.x / cellSize.x);
        i32 y = (i32)(local.y / cellSize.y);
        i32 z = (i32)(local.z / cellSize.z);
        if (x < 0 || x >= (i32)resX || y < 0 || y >= (i32)resY || z < 0 || z >= (i32)resZ) return;
        u32 idx = (u32)(z * resY * resX + y * resX + x);
        texels[idx] = irradiance;
    }

    Vec3 sample(Vec3 worldPos) const {
        Vec3 local = worldPos - origin;
        f32 fx = local.x / cellSize.x - 0.5f;
        f32 fy = local.y / cellSize.y - 0.5f;
        f32 fz = local.z / cellSize.z - 0.5f;
        i32 x0 = (i32)std::floor(fx);
        i32 y0 = (i32)std::floor(fy);
        i32 z0 = (i32)std::floor(fz);
        f32 tx = fx - (f32)x0;
        f32 ty = fy - (f32)y0;
        f32 tz = fz - (f32)z0;
        Vec3 result(0);
        for (i32 dz = 0; dz <= 1; dz++) {
            for (i32 dy = 0; dy <= 1; dy++) {
                for (i32 dx = 0; dx <= 1; dx++) {
                    i32 sx = Mathf::clamp(x0 + dx, 0, (i32)resX - 1);
                    i32 sy = Mathf::clamp(y0 + dy, 0, (i32)resY - 1);
                    i32 sz = Mathf::clamp(z0 + dz, 0, (i32)resZ - 1);
                    f32 w = ((dx == 0) ? (1.0f - tx) : tx) *
                            ((dy == 0) ? (1.0f - ty) : ty) *
                            ((dz == 0) ? (1.0f - tz) : tz);
                    result += texels[(u32)(sz * resY * resX + sy * resX + sx)] * w;
                }
            }
        }
        return result;
    }
};

// Mesh data for surfel generation
struct SurfelMeshData {
    Vector<Vec3> positions;
    Vector<Vec3> normals;
    Vector<Vec2> uvs;
    Vector<u32> indices;
    Vec3 boundsMin;
    Vec3 boundsMax;
    u32 meshID;
};

// Radiance cache configuration for probe-based GI
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

// A single radiance probe with 6-face radiance data
struct RadianceProbe {
    Vec3 position;
    Vec3 radiance[6];
    u32 lastUpdateFrame;
    RadianceProbe() : lastUpdateFrame(0) {
        for (u32 i = 0; i < 6; i++) radiance[i] = Vec3(0);
    }
};

// Main FrostRadiance system
class FrostRadiance {
public:
    static constexpr u32 MAX_OCTREE_NODES = 4 * 1024 * 1024;
    static constexpr u32 MAX_BOUNCES = 4;
    static constexpr u32 IRRI_VOLUME_RES = 32;

    FrostRadiance();
    ~FrostRadiance();

    bool init(SurfQuality quality = SurfQuality::Medium);
    void shutdown();
    void reset();

    // Main update: inject surfels, compute bounces, update irradiance
    void update(f32 deltaTime, const Mat4& viewProj, u32 screenW, u32 screenH);

    // Inject surfels from mesh data
    void injectSurfels(const SurfelMeshData& mesh);
    void injectSurfelsFromMeshes(const SurfelMeshData* meshes, u32 count);

    // Reproject surfels from previous frame
    void reproject(const Mat4& prevViewProj, u32 screenW, u32 screenH);

    // Compute multi-bounce radiance
    void computeBounces(u32 bounceCount);

    // Splat surfels to screen for indirect lighting
    void splatToScreen(const Mat4& viewProj, u32 screenW, u32 screenH,
                       Vector<Vec3>& screenBuffer) const;

    // Read irradiance at a world point (for distant objects)
    Vec3 sampleIrradiance(Vec3 worldPos) const;

    // Quality control
    void setQuality(SurfQuality quality);
    SurfQuality quality() const { return quality_; }
    u32 targetSurfelCount() const;

    // Statistics
    u32 activeSurfelCount() const { return pool_.activeCount; }
    u32 octreeNodeCount() const { return nodeCount_; }
    f32 lastUpdateTimeMs() const { return lastUpdateTimeMs_; }

    // Extended radiance cache stats
    u32 probesUpdated() const { return probesUpdated_; }
    u32 surfelsActive() const { return pool_.activeCount; }
    u32 irradianceQueries() const { return irradianceQueries_; }
    f32 cacheUpdateMs() const { return cacheUpdateMs_; }

    // Radiance cache
    void updateProbeCache(const Vec3& cameraPos);

    // Dynamic surfel injection (point-based)
    void injectDynamicSurfels(const Vec3& pos, const Vec3& normal,
                              const Vec3& albedo, u32 count);

    // Surfel position update (recompute from current transforms)
    void updateSurfelPositions();

    // Irradiance volume operations
    void blurIrradianceVolume(u32 passes);
    void downsampleIrradianceVolume();
    Vec3 queryIrradiance(const Vec3& pos, const Vec3& normal) const;

    // Cache configuration
    void setRadianceCacheConfig(const RadianceCacheConfig& cfg);
    const RadianceCacheConfig& getRadianceCacheConfig() const;

    // Cache management
    void clearCache();

private:
    // Surfel generation helpers
    void generateSurfelsForMesh(const SurfelMeshData& mesh);
    Vec3 samplePointOnTriangle(const Vec3& a, const Vec3& b, const Vec3& c, f32 u, f32 v) const;
    Vec3 interpolateNormal(const Vec3& a, const Vec3& b, const Vec3& c, f32 u, f32 v) const;
    f32 computeTriangleArea(const Vec3& a, const Vec3& b, const Vec3& c) const;
    f32 computeCurvature(const Vec3& pos, const Vec3& normal, const SurfelMeshData& mesh) const;
    f32 computeAdaptiveRadius(f32 distance, f32 curvature, f32 baseRadius) const;

    // Octree operations
    void buildOctree();
    void buildOctreeRecursive(u32 nodeIdx, u32 depth);
    void splitOctreeNode(u32 nodeIdx);
    void gatherSurfelsInRadius(Vec3 center, f32 radius, Vector<u32>& result) const;
    void gatherSurfelsInCone(Vec3 origin, Vec3 dir, f32 halfAngle, f32 maxDist,
                             Vector<u32>& result) const;

    // Bounce computation
    void computeSingleBounce(u32 bounceIndex);
    Vec3 gatherRadianceAtSurfel(u32 surfelIdx, f32 gatherRadius) const;
    Vec3 evaluateDirectLight(const Surfel& s) const;
    f32 visibilityTest(Vec3 from, Vec3 to) const;

    // Surfel reprojection
    bool reprojectSurfel(Surfel& s, const Mat4& prevViewProj,
                         u32 screenW, u32 screenH) const;

    // Surfel splatting helpers
    void rasterizeSurfel(const Surfel& s, const Mat4& viewProj,
                         u32 screenW, u32 screenH,
                         Vector<Vec3>& screenBuffer,
                         Vector<f32>& depthBuffer) const;

    // Update helpers
    void updateDirtySurfels();
    void evictOldSurfels(f32 maxAge);
    void updateIrradianceVolume();

    // Math helpers
    f32 hashFloat(f32 x, f32 y, f32 z) const;
    Vec3 cosineWeightedHemisphere(Vec3 normal, f32 u1, f32 u2) const;

    // Configuration
    SurfQuality quality_;
    u32 maxSurfels_;
    f32 baseSurfelRadius_;
    f32 bounceWeight_;
    f32 temporalBlendFactor_;
    f32 splatRadiusFactor_;

    // State
    SurfelPool pool_;
    Vector<SurfelOctreeNode> octree_;
    u32 nodeCount_;
    IrradianceVolume irrVolume_;
    bool initialized_;
    f32 lastUpdateTimeMs_;
    u32 frameNumber_;
    u32 bounceCount_;

    // Previous frame data for reprojection
    Vector<Vec3> prevScreenPositions_;
    Vector<f32> prevDepthBuffer_;

    // Advanced features - private methods
    void compactPool();
    u32 findClosestSurfel(Vec3 pos, f32 radius) const;
    u32 findSurfelInCone(Vec3 origin, Vec3 dir, f32 halfAngle, f32 maxDist) const;
    f32 computeSurfelDensity(Vec3 pos, f32 radius) const;
    Vec3 computeSurfelNormal(Vec3 pos, f32 radius) const;
    void computeProgressiveBounces(u32 bounceCount, f32 weight);
    Vec3 computeFormFactor(const Surfel& a, const Surfel& b) const;
    void gatherHemicube(Vec3 pos, Vec3 normal, f32 radius, Vec3& result) const;
    void injectSurfelsAdaptive(const SurfelMeshData& mesh, u32 targetCount);
    void injectSurfelsOnEdges(const SurfelMeshData& mesh, u32 edgeCount);
    void rebuildOctreeIncremental();
    void pruneEmptyNodes();
    u32 computeOctreeDepth() const;
    f32 computeOctreeBalance() const;
    f32 computeCoverageRatio() const;
    f32 computeOverlapRatio() const;
    Vec3 computeAverageRadiance() const;
    f32 computeTemporalStability() const;
    void debugDrawOctree(Vec3& minBounds, Vec3& maxBounds, u32& nodeCount, u32& leafCount) const;
    void getSurfelStats(u32& total, u32& active, f32& avgRadius, f32& avgAge, Vec3& avgFlux) const;
    Vector<Vec3> getSurfelPositions() const;
    Vector<Vec3> getSurfelNormals() const;
    Vector<Vec3> getSurfelColors() const;
    void debugPrintPoolState() const;
    u32 computePoolFragmentation() const;
    Vec3 computeViewDependentRadiance(Vec3 pos, Vec3 normal, Vec3 viewDir, f32 roughness) const;
    void computeSpecularHighlights(Vec3 pos, Vec3 normal, Vec3 viewDir, f32 roughness, Vec3& outRadiance) const;
    f32 computeFormFactorApproximation(const Surfel& a, const Surfel& b) const;
    void blurIrradianceVolume(f32 sigma);
    void downsampleIrradianceVolume(u32 level);
    void injectDynamicSurfels(const SurfelMeshData& mesh, Vec3 velocity, f32 timeScale);
    void removeSurfelsForMesh(u32 meshId);
    void updateSurfelPositions(const SurfelMeshData& mesh);

    // Radiance cache helpers
    Vec3 traceProbeRay(const Vec3& origin, const Vec3& dir) const;
    u32 findNearestProbe(const Vec3& pos) const;
    void updateSurfelToProbeMapping();

    // Radiance cache members
    RadianceCacheConfig radianceCfg_;
    Vector<RadianceProbe> probeCache_;
    Vector<Vec3> irradianceVolume_;
    u32 irradianceResolution_;
    Vector<u32> surfelToProbe_;
    u32 probesUpdatedThisFrame_;

    // Extended stats members (mutable for const accessor updates)
    mutable u32 probesUpdated_;
    mutable u32 irradianceQueries_;
    mutable f32 cacheUpdateMs_;
};

} // namespace Frost
