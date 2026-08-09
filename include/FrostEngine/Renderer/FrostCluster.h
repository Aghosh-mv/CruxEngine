#pragma once

// ============================================================================
// FrostEngine FrostCluster — Cluster-Based Virtualized Geometry
// ============================================================================
// Proprietary virtualized geometry system. Fundamentally different from Nanite's
// DAG approach. Uses binary tree hierarchy with hybrid SW/HW rasterization and
// smaller 64-triangle clusters for finer LOD granularity.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Math.h"
#include "Core/Vector.h"

namespace Frost {

// ============================================================================
// Cluster structures
// ============================================================================

static constexpr u32 CLUSTER_TRIANGLES = 64;
static constexpr u32 CLUSTER_VERTICES = 64 * 3;  // worst case unique verts
static constexpr u32 MAX_CLUSTERS = 16 * 1024 * 1024;
static constexpr u32 MAX_CLUSTER_NODES = 32 * 1024 * 1024;

// Rasterization method selection
enum class RasterMethod : u8 {
    Software = 0,    // CPU rasterizer for sub-pixel triangles
    Hardware = 1,    // GPU rasterizer for large triangles
    Hybrid = 2       // automatic per-cluster selection
};

// Cluster triangle data (compressed)
struct ClusterTriangle {
    u16 indices[3];         // local vertex indices (within cluster)
    u8 materialSlot;        // material bin
    u8 flags;               // bit 0: double-sided, bit 1: transparent
    u16 pad;

    ClusterTriangle() { memset(this, 0, sizeof(ClusterTriangle)); }
};

// Compressed AABB (16-bit quantized)
struct CompressedAABB {
    u16 minXYZ[3];  // quantized min corner
    u16 maxXYZ[3];  // quantized max corner
    Vec3 dequantizeMin;
    Vec3 dequantizeMax;

    void compress(const Vec3& bmin, const Vec3& bmax, const Vec3& worldMin, const Vec3& worldMax) {
        Vec3 range = worldMax - worldMin;
        f32 rangeArr[3] = { range.x, range.y, range.z };
        f32 bminArr[3] = { bmin.x, bmin.y, bmin.z };
        f32 bmaxArr[3] = { bmax.x, bmax.y, bmax.z };
        f32 worldMinArr[3] = { worldMin.x, worldMin.y, worldMin.z };
        for (u32 i = 0; i < 3; i++) {
            f32 invRange = (rangeArr[i] > 0.0001f) ? (1.0f / rangeArr[i]) : 0.0f;
            minXYZ[i] = (u16)(((bminArr[i] - worldMinArr[i]) * invRange) * 65535.0f);
            maxXYZ[i] = (u16)(((bmaxArr[i] - worldMinArr[i]) * invRange) * 65535.0f);
        }
        dequantizeMin = bmin;
        dequantizeMax = bmax;
    }

    void decompress(const Vec3& worldMin, const Vec3& worldMax) {
        Vec3 range = worldMax - worldMin;
        f32 rangeArr[3] = { range.x, range.y, range.z };
        f32 worldMinArr[3] = { worldMin.x, worldMin.y, worldMin.z };
        for (u32 i = 0; i < 3; i++) {
            f32 s = rangeArr[i] / 65535.0f;
            f32 worldMinComp[3] = { dequantizeMin.x, dequantizeMin.y, dequantizeMin.z };
            worldMinComp[i] = worldMinArr[i] + minXYZ[i] * s;
            dequantizeMin = Vec3(worldMinComp[0], worldMinComp[1], worldMinComp[2]);

            f32 worldMaxComp[3] = { dequantizeMax.x, dequantizeMax.y, dequantizeMax.z };
            worldMaxComp[i] = worldMinArr[i] + maxXYZ[i] * s;
            dequantizeMax = Vec3(worldMaxComp[0], worldMaxComp[1], worldMaxComp[2]);
        }
    }
};

// A cluster of triangles with local geometry
struct Cluster {
    ClusterTriangle triangles[CLUSTER_TRIANGLES];
    u32 triangleCount;

    // Local vertex data (unique vertices in this cluster)
    Vec3 positions[CLUSTER_VERTICES];
    Vec3 normals[CLUSTER_VERTICES];
    Vec2 uvs[CLUSTER_VERTICES];
    u32 vertexCount;

    // Hierarchy
    u32 parentNodeIndex;        // parent cluster (0xFFFFFFFF = root)
    u32 childNodeIndex;         // first child (0xFFFFFFFF = leaf)
    u32 depthLevel;             // LOD level (0 = highest detail)

    // Bounds
    Vec3 boundsMin;
    Vec3 boundsMax;
    Vec3 boundsCenter;
    f32 boundsRadius;

    // Error metric (Hausdorff distance to children)
    f32 hausdorffError;
    f32 screenCoverage;         // computed each frame

    // Material binning
    u32 materialID;
    u32 meshID;

    // Rasterization
    RasterMethod rasterMethod;
    bool isVisible;
    bool needsRasterize;

    // GPU buffer handles
    u32 gpuBufferOffset;
    u32 indexBufferOffset;

    Cluster() : triangleCount(0), vertexCount(0),
                parentNodeIndex(0xFFFFFFFF), childNodeIndex(0xFFFFFFFF),
                depthLevel(0), hausdorffError(0), screenCoverage(0),
                materialID(0), meshID(0), rasterMethod(RasterMethod::Hybrid),
                isVisible(true), needsRasterize(false),
                gpuBufferOffset(0), indexBufferOffset(0) {
        boundsMin = Vec3(1e30f);
        boundsMax = Vec3(-1e30f);
        boundsCenter = Vec3(0);
        boundsRadius = 0;
    }
};

// Binary tree node (each cluster has 0 or 2 children)
struct ClusterTreeNode {
    u32 clusterIndex;
    u32 leftChild;              // index into nodes array
    u32 rightChild;             // index into nodes array
    u32 parentIndex;
    f32 splitError;             // Hausdorff distance to children
    f32 screenSize;             // projected screen size
    bool isLeaf;

    ClusterTreeNode() : clusterIndex(0xFFFFFFFF), leftChild(0xFFFFFFFF),
                        rightChild(0xFFFFFFFF), parentIndex(0xFFFFFFFF),
                        splitError(0), screenSize(0), isLeaf(true) {}
};

// Visibility buffer entry (per-pixel)
struct VisibilityEntry {
    u32 clusterID;
    u32 triangleID;
    f32 depth;

    VisibilityEntry() : clusterID(0xFFFFFFFF), triangleID(0xFFFFFFFF), depth(1e30f) {}
};

// Material bin for batched rendering
struct MaterialBin {
    u32 materialID;
    Vector<u32> clusterIndices;
    u32 totalTriangles;
};

// Mesh data input for cluster building
struct ClusterMeshInput {
    Vector<Vec3> positions;
    Vector<Vec3> normals;
    Vector<Vec2> uvs;
    Vector<u32> indices;
    u32 materialID;
    u32 meshID;
};

// Software rasterizer triangle
struct SWRasterTriangle {
    Vec3 screenPos[3];
    f32 depths[3];
    u32 clusterID;
    u32 triangleID;
    u32 materialID;
};

// ============================================================================
// Main FrostCluster system
// ============================================================================

class FrostCluster {
public:
    FrostCluster();
    ~FrostCluster();

    bool init(u32 maxClusters = MAX_CLUSTERS);
    void shutdown();
    void reset();

    // Build cluster hierarchy from mesh data
    void buildClusters(const ClusterMeshInput& mesh);
    void buildClusters(const ClusterMeshInput* meshes, u32 meshCount);

    // LOD selection: determine which clusters to render
    void selectLODs(const Mat4& viewProj, Vec3 cameraPos, u32 screenW, u32 screenH);

    // Rasterize selected clusters
    void rasterize(const Mat4& viewProj, u32 screenW, u32 screenH);

    // Software rasterize a cluster
    void softwareRasterizeCluster(const Cluster& cluster, const Mat4& viewProj,
                                  u32 screenW, u32 screenH);

    // Hardware rasterize a cluster (submit to GPU)
    void hardwareRasterizeCluster(const Cluster& cluster);

    // Get visibility buffer for deferred shading
    const Vector<VisibilityEntry>& visibilityBuffer() const { return visBuffer_; }
    u32 screenWidth() const { return screenWidth_; }
    u32 screenHeight() const { return screenHeight_; }

    // Get clusters for a specific material
    const Vector<u32>& getMaterialBin(u32 materialID) const;

    // Debug: get cluster hierarchy stats
    u32 totalClusters() const { return clusterCount_; }
    u32 visibleClusters() const { return visibleClusterCount_; }
    u32 leafClusters() const { return leafClusterCount_; }
    u32 maxDepth() const { return maxDepthLevel_; }
    f32 avgHausdorffError() const { return avgHausdorffError_; }

    // Cluster analysis
    f32 computeClusterSolidity(const Cluster& cluster) const;
    f32 computeClusterConvexity(const Cluster& cluster) const;
    Vec3 computeClusterCentroid(const Cluster& cluster) const;
    f32 computeTriangleDensity(const Cluster& cluster) const;

    // LOD quality assessment
    f32 computeLODQuality(u32 nodeIdx, const Mat4& viewProj,
                          u32 screenW, u32 screenH) const;
    f32 computeLODBalance() const;
    u32 computeMaxLODDepth() const;
    f32 computeAverageTriangleSize() const;

    // Material binning optimization
    void sortMaterialBins();
    u32 computeMaterialBinCount() const;
    u32 computeBatchCount() const;

    // Visibility buffer analysis
    u32 countVisiblePixels() const;
    f32 computeOverdrawRatio() const;
    u32 countUniqueMaterials() const;

    // Debug and statistics
    void getHierarchyStats(u32& totalNodes, u32& leafNodes,
                           u32& internalNodes, f32& avgChildren) const;
    void getRasterStats(u32& swClusters, u32& hwClusters,
                        u32& hybridClusters) const;
    f32 computeMemoryUsage() const;

    // Cluster memory and performance analysis
    f32 computeVertexCacheEfficiency() const;
    f32 computeIndexOverhead() const;
    u32 estimateGPUMemoryUsage() const;

    // Cluster merging and splitting
    void mergeClusters(u32 clusterA, u32 clusterB);
    u32 splitCluster(u32 clusterIdx);

    // Cluster visibility analysis
    u32 countVisibleTriangles() const;
    f32 computeOcclusionCullingEfficiency() const;
    f32 computeRasterizationEfficiency() const;

    // Material and batch analysis
    u32 computeMaxBatchSize() const;
    f32 computeMaterialCoherency() const;

    // Full stats
    void getFullStats(u32& clusters, u32& triangles, u32& vertices,
                      u32& materials, f32& memoryMB) const;

private:
    // Cluster building
    void buildBinaryHierarchy();
    void buildBinaryHierarchyRecursive(u32 nodeIndex, const Vector<u32>& triIndices,
                                       const ClusterMeshInput& mesh, u32 depth);
    void computeClusterBounds(Cluster& cluster);
    f32 computeHausdorffDistance(const Cluster& a, const Cluster& b) const;
    f32 pointToTriangleDistance(Vec3 point, Vec3 a, Vec3 b, Vec3 c) const;
    void assignRasterMethod(Cluster& cluster);
    void binClustersByMaterial();

    // LOD selection helpers
    f32 computeScreenCoverage(const Cluster& cluster, const Mat4& viewProj,
                              u32 screenW, u32 screenH) const;
    void selectLODsRecursive(u32 nodeIdx, const Mat4& viewProj, Vec3 cameraPos,
                             u32 screenW, u32 screenH, f32 parentCoverage);

    // Software rasterization
    void swRasterizeTriangle(const SWRasterTriangle& tri,
                             Vector<VisibilityEntry>& buffer,
                             u32 screenW, u32 screenH);
    void edgeFunction(f32 ax, f32 ay, f32 bx, f32 by, f32 cx, f32 cy,
                      f32& area, f32& bary1, f32& bary2) const;

    // Frustum culling
    bool frustumCullCluster(const Cluster& cluster, const Mat4& viewProj) const;
    bool sphereInFrustum(Vec3 center, f32 radius, const Mat4& viewProj) const;

    // Data
    Vector<Cluster> clusters_;
    Vector<ClusterTreeNode> treeNodes_;
    u32 clusterCount_;
    u32 treeNodeCount_;
    u32 leafClusterCount_;
    u32 visibleClusterCount_;
    u32 maxDepthLevel_;
    f32 avgHausdorffError_;

    // Visibility buffer
    Vector<VisibilityEntry> visBuffer_;
    u32 screenWidth_;
    u32 screenHeight_;

    // Material bins
    Vector<MaterialBin> materialBins_;
    u32 materialBinCount_;

    // Global bounds for AABB compression
    Vec3 worldBoundsMin_;
    Vec3 worldBoundsMax_;

    // Software rasterizer buffers
    Vector<f32> swDepthBuffer_;
    Vector<u32> swCoverageBuffer_;

    bool initialized_;
};

} // namespace Frost
