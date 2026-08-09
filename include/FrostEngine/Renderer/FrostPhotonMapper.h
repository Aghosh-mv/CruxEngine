#pragma once

// ============================================================================
// FrostEngine FrostPhotonMapper — Photon Mapping Global Illumination
// ============================================================================
// Proprietary photon mapping system. Fundamentally different from both Lumen
// and FrostRadiance. Uses k-d tree based photon storage with progressive
// refinement for accurate caustics and multi-bounce indirect lighting.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Math.h"
#include "Core/Vector.h"

namespace Frost {

// ============================================================================
// Photon structures
// ============================================================================

static constexpr u32 MAX_PHOTONS = 4 * 1024 * 1024;  // 4M photons
static constexpr u32 MAX_PHOTON_BOUNCES = 8;
static constexpr u32 KD_TREE_MAX_DEPTH = 24;

// Photon storage on GPU surface cache
struct Photon {
    Vec3 position;          // world-space hit position
    Vec3 direction;         // incoming direction (toward surface)
    Vec3 power;             // photon power (RGB)
    Vec3 normal;            // surface normal at hit point
    u16 flags;              // bit 0: from specular, bit 1: caustic, bit 2: volumetric
    u16 bounceCount;        // number of bounces
    f32 wavelengthR;        // spectral wavelength (for dispersion)
    f32 wavelengthG;
    f32 wavelengthB;

    Photon() : position(0), direction(0), power(0), normal(0),
               flags(0), bounceCount(0), wavelengthR(1), wavelengthG(1), wavelengthB(1) {}
};

// K-d tree node for efficient photon lookup
struct KDTreeNode {
    Vec3 splitPoint;        // split plane position
    u32 leftChild;          // left child index (0xFFFFFFFF = leaf)
    u32 rightChild;         // right child index
    u32 splitAxis;          // 0=X, 1=Y, 2=Z
    u32 photonIndex;        // index into photon array (for leaves)
    u32 photonCount;        // number of photons in subtree
    Vec3 subtreeMin;        // AABB min of all photons in subtree
    Vec3 subtreeMax;        // AABB max of all photons in subtree

    KDTreeNode() : leftChild(0xFFFFFFFF), rightChild(0xFFFFFFFF),
                   splitAxis(0), photonIndex(0xFFFFFFFF), photonCount(0),
                   subtreeMin(1e30f), subtreeMax(-1e30f) {}
};

// Reconstruction filter types
enum class PhotonFilter : u8 {
    Cone = 0,       // cone filter (sharper, less noise)
    Cylinder = 1,   // cylinder filter (simple, uniform)
    Gaussian = 2    // Gaussian filter (smooth, more physically accurate)
};

// Photon emission statistics
struct PhotonStats {
    u32 emitted;
    u32 absorbed;
    u32 causticCount;
    u32 indirectCount;
    u32 directCount;
    f32 avgBounces;
    f32 buildTimeMs;
    f32 renderTimeMs;
};

// Surface material for photon tracing
struct PhotonMaterial {
    Vec3 albedo;
    Vec3 emission;
    f32 roughness;
    f32 metalness;
    f32 ior;            // index of refraction
    f32 absorption;     // absorption coefficient
    bool isSpecular;
    bool isEmissive;
    bool isTransmissive;

    PhotonMaterial() : albedo(0.8f), emission(0), roughness(0.5f),
                       metalness(0.0f), ior(1.5f), absorption(0.0f),
                       isSpecular(false), isEmissive(false), isTransmissive(false) {}
};

// Mesh data for photon tracing
struct PhotonMeshData {
    Vector<Vec3> positions;
    Vector<Vec3> normals;
    Vector<Vec2> uvs;
    Vector<u32> indices;
    u32 materialID;
    u32 meshID;
};

// Light source for photon emission
struct PhotonLightSource {
    Vec3 position;
    Vec3 direction;
    Vec3 color;
    f32 intensity;
    f32 radius;             // for area lights
    u32 photonsToEmit;      // per frame
    bool isDirectional;
    bool enabled;

    PhotonLightSource() : position(0), direction(0, -1, 0), color(1),
                          intensity(100), radius(1), photonsToEmit(100000),
                          isDirectional(false), enabled(true) {}
};

// ============================================================================
// Main FrostPhotonMapper system
// ============================================================================

class FrostPhotonMapper {
public:
    FrostPhotonMapper();
    ~FrostPhotonMapper();

    bool init(u32 maxPhotons = MAX_PHOTONS);
    void shutdown();
    void reset();

    // Set scene geometry for photon tracing
    void setSceneMeshes(const PhotonMeshData* meshes, u32 meshCount,
                        const PhotonMaterial* materials, u32 materialCount);

    // Set light sources for photon emission
    void setLightSources(const PhotonLightSource* lights, u32 lightCount);

    // Main update: emit photons, build k-d tree, compute final gathering
    void update(f32 deltaTime, const Mat4& viewProj, u32 screenW, u32 screenH);

    // Emit photons from all light sources
    void emitPhotons();

    // Build k-d tree from current photon set
    void buildKDTree();

    // Trace a single photon through the scene
    bool tracePhoton(Photon& photon, u32 maxBounces);

    // Final gathering: compute irradiance at a surface point
    Vec3 finalGather(Vec3 position, Vec3 normal, f32 gatherRadius,
                     u32 maxPhotons) const;

    // Splat photons to screen for visualization
    void splatPhotonsToScreen(const Mat4& viewProj, u32 screenW, u32 screenH,
                              Vector<Vec3>& screenBuffer) const;

    // Progressive refinement: accumulate photons over frames
    void beginProgressiveFrame();
    void endProgressiveFrame();

    // Quality settings
    void setFilterType(PhotonFilter filter) { filterType_ = filter; }
    void setGatherRadius(f32 radius) { gatherRadius_ = radius; }
    void setMaxPhotonsToGather(u32 count) { maxGatherPhotons_ = count; }

    // Statistics
    const PhotonStats& stats() const { return stats_; }
    u32 photonCount() const { return photonCount_; }

private:
    // Photon emission
    void emitPhotonsFromSource(const PhotonLightSource& light);
    Vec3 generateEmissionDirection(const PhotonLightSource& light) const;
    Vec3 generateEmissionPosition(const PhotonLightSource& light) const;

    // Photon tracing
    bool traceReflection(Photon& photon, Vec3 hitPos, Vec3 hitNormal,
                         const PhotonMaterial& mat);
    bool traceRefraction(Photon& photon, Vec3 hitPos, Vec3 hitNormal,
                         const PhotonMaterial& mat, f32 eta);
    bool traceDiffuse(Photon& photon, Vec3 hitPos, Vec3 hitNormal,
                      const PhotonMaterial& mat);
    Vec3 computeSpecularDirection(Vec3 incident, Vec3 normal, f32 ior) const;
    f32 fresnelReflectance(Vec3 incident, Vec3 normal, f32 eta) const;
    bool russianRoulette(f32 survivalProb) const;

    // K-d tree building
    void buildKDTreeRecursive(u32 nodeIdx, Vector<u32>& indices,
                              u32 start, u32 end, u32 depth);
    void computeSubtreeBounds(u32 nodeIdx);
    u32 findBestSplitAxis(const Vector<u32>& indices, u32 start, u32 end,
                          f32& bestCost) const;
    f32 evaluateSAH(const Vector<u32>& indices, u32 start, u32 end,
                    u32 axis, f32 splitPos) const;

    // K-d tree query
    void queryNearest(Vec3 point, u32 nodeIdx, u32& nearestIdx,
                      f32& nearestDistSq, u32& foundCount, u32 maxCount) const;
    void queryRadius(Vec3 point, f32 radius, u32 nodeIdx,
                     Vector<u32>& result) const;

    // Reconstruction filters
    f32 coneFilter(f32 dist, f32 radius) const;
    f32 cylinderFilter(f32 dist, f32 radius) const;
    f32 gaussianFilter(f32 dist, f32 radius) const;
    f32 evaluateFilter(f32 dist, f32 radius) const;

    // Scene intersection (for photon tracing)
    bool intersectScene(Vec3 origin, Vec3 direction, f32& t,
                        Vec3& hitPos, Vec3& hitNormal,
                        u32& materialID, u32& meshID) const;
    bool intersectTriangle(Vec3 origin, Vec3 direction,
                           Vec3 a, Vec3 b, Vec3 c,
                           f32& t, f32& u, f32& v) const;

    // Caustic detection
    bool isCausticPath(const Photon& photon) const;

    // Data
    Vector<Photon> photons_;
    Vector<KDTreeNode> kdTree_;
    u32 photonCount_;
    u32 kdTreeNodeCount_;
    u32 nextPhotonSlot_;

    // Scene data
    Vector<PhotonMeshData> sceneMeshes_;
    Vector<PhotonMaterial> sceneMaterials_;
    Vector<PhotonLightSource> lightSources_;
    u32 meshCount_;
    u32 materialCount_;
    u32 lightCount_;

    // Settings
    PhotonFilter filterType_;
    f32 gatherRadius_;
    u32 maxGatherPhotons_;
    f32 globalPhotonPower_;

    // Progressive refinement
    u32 progressiveFrame_;
    u32 photonsPerFrame_;
    bool progressiveMode_;

    // Statistics
    PhotonStats stats_;

    bool initialized_;

    // Advanced query methods
    Vec3 computeCausticIrradiance(Vec3 pos, Vec3 normal, f32 radius) const;
    Vec3 computeIndirectIrradiance(Vec3 pos, Vec3 normal, f32 radius) const;
    f32 estimatePhotonDensity(Vec3 pos, f32 radius) const;
    f32 estimatePhotonPowerAt(Vec3 pos, f32 radius) const;
    u32 computeKDTreeDepth() const;
    f32 computeKDTreeBalance() const;
    f32 computeSAHCost() const;
    Vector<Vec3> getPhotonPositions() const;
    Vector<Vec3> getPhotonPowers() const;
    Vector<Vec3> getPhotonNormals() const;
    void resetProgressive();
    void splitPhoton(Photon& photon, u32& newSlot);
    bool intersectSceneAABB(Vec3 origin, Vec3 dir, Vec3 bmin, Vec3 bmax, f32& tmin, f32& tmax) const;
    f32 computeSceneBoundsVolume() const;
    u32 getTriangleCount() const;
    void computeNearestPhotons(Vec3 pos, f32 radius, u32 maxCount, Vector<u32>& result) const;
    f32 computeProgressiveConvergence() const;
    void normalizePhotonPower();
    f32 computeAveragePhotonPower() const;
    f32 estimateLocalDensity(Vec3 pos, u32 neighborCount) const;
    bool traceCausticPath(Photon& photon, u32 maxDepth);
    Vec3 computeRefractionDir(Vec3 incident, Vec3 normal, f32 eta) const;
    f32 computeFresnel(Vec3 incident, Vec3 normal, f32 eta) const;
    void getDetailedStats(u32& photonCount, u32& kdNodes, u32& avgBounces, f32& totalPower, f32& coverage) const;
    f32 computePhotonMapCoverage() const;
};

} // namespace Frost
