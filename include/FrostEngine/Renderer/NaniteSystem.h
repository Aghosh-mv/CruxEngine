#pragma once

// ============================================================================
// FrostEngine Nanite-like Virtualized Geometry System
// ============================================================================
// Eliminates manual LOD creation, similar to UE5's Nanite.
// Features: Cluster representation, DAG hierarchy, screen-size error,
// Frustum culling, Occlusion culling, Two-pass rendering,
// Material batching, Persistence, Fallback mesh.
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
// Cluster constants
// ============================================================================
static constexpr u32 NANITE_CLUSTER_TRIANGLES = 128;
static constexpr u32 NANITE_CLUSTER_VERTICES = 128;
static constexpr u32 NANITE_MAX_CLUSTERS_PER_MESH = 65536;
static constexpr u32 NANITE_MAX_LODS = 8;
static constexpr u32 NANITE_DAG_FANOUT = 2;

// ============================================================================
// Bounding volume for clusters
// ============================================================================
struct ClusterBounds {
    Vec3 center;
    f32 radius;
    Vec3 coneAxis;          // normal cone axis for backface culling
    f32 coneCutoff;         // cos(half-angle)
    Vec3 boundsMin;
    Vec3 boundsMax;
    f32 screenSizeThreshold;

    bool containsPoint(const Vec3& point) const {
        Vec3 delta = point - center;
        return delta.dot(delta) <= radius * radius;
    }

    f32 estimateScreenSize(const Vec3& viewPos, const Mat4& viewProj) const {
        Vec3 centerCS = Vec3(
            viewProj.m[0] * center.x + viewProj.m[4] * center.y + viewProj.m[8] * center.z + viewProj.m[12],
            viewProj.m[1] * center.x + viewProj.m[5] * center.y + viewProj.m[9] * center.z + viewProj.m[13],
            viewProj.m[2] * center.x + viewProj.m[6] * center.y + viewProj.m[10] * center.z + viewProj.m[14]
        );
        f32 dist = (center - viewPos).length();
        if (dist < 0.001f) dist = 0.001f;
        return (radius / dist) * 100.0f;
    }
};

// ============================================================================
// Triangle cluster
// ============================================================================
struct ClusterTriangle {
    u32 indices[3];
    u32 materialIndex;
    u32 padding;
};

struct Cluster {
    u32 clusterId;
    u32 meshId;
    u32 lodLevel;
    u32 triangleCount;
    u32 vertexCount;
    ClusterBounds bounds;

    // DAG parent index (UINT32_MAX = no parent)
    u32 parentClusterIndex;
    u32 childIndices[NANITE_DAG_FANOUT];

    // Vertex data (local to cluster)
    Vec3 positions[NANITE_CLUSTER_VERTICES];
    Vec3 normals[NANITE_CLUSTER_VERTICES];
    Vec4 tangents[NANITE_CLUSTER_VERTICES];
    Vec2 uvs[NANITE_CLUSTER_VERTICES];

    // Index data
    ClusterTriangle triangles[NANITE_CLUSTER_TRIANGLES];

    // GPU state
    u32 vboHandle;
    u32 eboHandle;
    u32 vaoHandle;

    // Culling state
    bool visible;
    bool occluded;
    bool rasterized;
    f32 screenError;
};

// ============================================================================
// LOD descriptor
// ============================================================================
struct LODDescriptor {
    f32 screenErrorThreshold;     // pixels-per-triangle threshold
    u32 clusterStart;
    u32 clusterCount;
    u32 totalTriangles;
    u32 totalVertices;
};

// ============================================================================
// Mesh descriptor for Nanite
// ============================================================================
struct NaniteMeshDescriptor {
    u32 meshId;
    Vec3 boundsMin;
    Vec3 boundsMax;
    Vec3 boundsCenter;
    f32 boundsRadius;
    u32 totalTriangles;
    u32 totalVertices;
    u32 lodCount;
    LODDescriptor lods[NANITE_MAX_LODS];
    u32 fallbackIndexCount;
    u32 fallbackVertexCount;
};

// ============================================================================
// Visible cluster for rendering
// ============================================================================
struct VisibleCluster {
    u32 clusterIndex;
    u32 meshId;
    u32 materialIndex;
    f32 screenError;
    u32 triangleStart;
    u32 triangleCount;
};

// ============================================================================
// Material batch for state grouping
// ============================================================================
struct MaterialBatch {
    u32 materialIndex;
    Vector<VisibleCluster*> clusters;
    u32 totalTriangles;
};

// ============================================================================
// Hierarchical occlusion node
// ============================================================================
struct OcclusionNode {
    Vec3 boundsMin;
    Vec3 boundsMax;
    u32 children[4];        // child indices (UINT32_MAX = none)
    u32 clusterStart;
    u32 clusterCount;
    bool visible;
    bool fullyOccluded;
};

// ============================================================================
// GPU-side cluster representation
// ============================================================================
struct GPUMeshlet {
    u32 vertexOffset;
    u32 indexOffset;
    u32 vertexCount;
    u32 triangleCount;
    Vec3 boundsCenter;
    f32 boundsRadius;
};

struct GPUInstance {
    Mat4 transform;
    Vec3 boundsCenter;
    f32 boundsRadius;
    u32 clusterOffset;
    u32 clusterCount;
    u32 enabled;
};

struct ClusterDraw {
    u32 instanceId;
    u32 clusterId;
    u32 materialId;
};

struct FrustumPlane {
    Vec3 normal;
    f32 d;
};

// ============================================================================
// Nanite System
// ============================================================================
class NaniteSystem {
public:
    NaniteSystem();
    ~NaniteSystem();

    bool init();
    void shutdown();

    // Mesh management
    u32 addMesh(const Vector<Vec3>& positions, const Vector<Vec3>& normals,
                const Vector<Vec2>& uvs, const Vector<u32>& indices,
                u32 materialIndex, const Mat4& initialTransform);
    void removeMesh(u32 meshId);
    void updateMeshTransform(u32 meshId, const Mat4& transform);

    // Per-frame processing
    void beginFrame(const Camera& camera, u32 screenWidth, u32 screenHeight);
    void cull();
    void buildRenderLists();
    void prepareDraws();
    void endFrame();

    // Two-pass rendering
    void renderDepthPass();
    void renderMaterialPass();

    // Material batching
    void batchByMaterial();

    // Persistence / streaming
    void setStreamingDistance(f32 distance) { streamingDistance_ = distance; }
    void setMaxResidentClusters(u32 max) { maxResidentClusters_ = max; }
    u32 residentClusterCount() const { return residentClusterCount_; }

    // Fallback mesh for RT/shadows
    const Vector<Vec3>& getFallbackPositions(u32 meshId) const;
    const Vector<u32>& getFallbackIndices(u32 meshId) const;

    // Stats
    struct Stats {
        u32 totalMeshes;
        u32 totalClusters;
        u32 visibleClusters;
        u32 culledClusters;
        u32 occludedClusters;
        u32 renderedTriangles;
        u32 materialBatches;
        u32 drawCalls;
        f32 cullingTimeMs;
        f32 renderTimeMs;
        u32 clustersVisible = 0;
        u64 trianglesRasterized = 0;
        u32 drawsGenerated = 0;
        u32 lodSelected = 0;
        u64 gpuBytesUploaded = 0;
    };
    const Stats& stats() const { return stats_; }

    // GPU-side cluster generation
    Vector<GPUMeshlet> buildMeshletList(const Vector<f32>& positions,
                                        const Vector<u32>& indices,
                                        f32 maxTrianglesPerCluster);
    void createGPUResources(u32 meshletCount, u32 instanceCount);

    // Software rasterization fallback
    void rasterizeClusterDepth(Mat4 viewProj, u32 viewportW, u32 viewportH,
                               const GPUMeshlet& meshlet, const GPUInstance& instance,
                               Vector<f32>& depthBuffer, Vector<u32>& visibleClusterFlags);
    bool cullCluster(const Mat4& viewProj, const f32* frustumPlanes,
                     const GPUMeshlet& meshlet, const GPUInstance& instance) const;

    // GPU-driven frame processing
    void processFrame(const Mat4& viewProj, const f32* frustumPlanes,
                      u32 viewportW, u32 viewportH, u32 frameIndex);
    u32 selectLOD(u32 clusterCount, const Vector<f32>& clusterRadii,
                  f32 maxError, f32 viewDistance);
    void resetFrame();

    const Vector<ClusterDraw>& getDrawList() const { return drawList_; }
    const Vector<GPUMeshlet>& getMeshlets() const { return gpuMeshlets_; }
    const Vector<GPUInstance>& getInstances() const { return gpuInstances_; }
    const Vector<u32>& getClusterIndexData() const { return clusterIndexData_; }
    const Vector<f32>& getDepthBuffer() const { return depthBuffer_; }
    u32 clustersVisible() const { return stats_.clustersVisible; }
    u64 trianglesRasterized() const { return stats_.trianglesRasterized; }
    u32 drawsGenerated() const { return stats_.drawsGenerated; }
    u32 lodSelected() const { return stats_.lodSelected; }
    u64 gpuBytesUploaded() const { return stats_.gpuBytesUploaded; }

private:
    // Cluster generation
    void splitMeshIntoClusters(u32 meshId, const Vector<Vec3>& positions,
                               const Vector<Vec3>& normals, const Vector<Vec2>& uvs,
                               const Vector<u32>& indices, u32 materialIndex);
    void buildClusterHierarchy(u32 meshId);
    void computeClusterBounds(Cluster& cluster);
    void buildLODHierarchy(u32 meshId);

    // Screen-size error
    f32 computeScreenSpaceError(const Cluster& cluster, const Vec3& viewPos,
                                const Mat4& viewProj) const;
    void selectLODs(u32 meshId, const Vec3& viewPos, const Mat4& viewProj);

    // Frustum culling
    void frustumCull(const Camera& camera);
    bool isClusterInFrustum(const Cluster& cluster, const Vec4* planes) const;
    bool isBoundsInFrustum(const Vec3& center, f32 radius, const Vec4* planes) const;

    // Occlusion culling (hierarchical Z-buffer)
    void buildOcclusionHierarchy();
    void hierarchicalOcclusionCull(const Camera& camera);
    bool testBoundsOcclusion(const Vec3& boundsMin, const Vec3& boundsMax, const Mat4& viewProj);
    void rasterizeOcclusionDepth(const Cluster& cluster, const Mat4& viewProj);

    // DAG traversal
    void traverseDAG(u32 rootClusterIndex, const Camera& camera, const Mat4& viewProj);

    // Two-pass rendering helpers
    void softwareRasterizeCluster(const Cluster& cluster, const Mat4& viewProj);
    void softwareRasterizeTriangle(const Vec3& v0, const Vec3& v1, const Vec3& v2,
                                   u32 materialIndex, const Mat4& viewProj);

    // Material batching helpers
    void sortClustersByMaterial();
    void buildBatchTable();

    // GPU resource management
    void uploadClusterToGPU(Cluster& cluster);
    void evictClusterFromGPU(Cluster& cluster);
    void managePersistence();

    // Cluster index helpers
    Cluster& cluster(u32 index) { return clusters_[index]; }
    const Cluster& cluster(u32 index) const { return clusters_[index]; }

    // Cluster connectivity and streaming
    void updateClusterConnectivity();
    void streamClustersForCamera(const Vec3& cameraPos, f32 streamDistance);

    // GPU command buffer building
    void buildGPUCommandBuffer(Vector<u32>& drawCommands);
    void buildIndirectDrawBuffer(Vector<u32>& indirectBuffer);

    // Cluster memory management
    void compactClusterMemory();

    // Hierarchical HiZ occlusion
    void hierarchicalHiZOcclusion(const Camera& camera);

    // Material table
    void updateMaterialTable();

    // Debug visualization
    void visualizeClusters(Vec3* debugOutput, u32 width, u32 height);
    void visualizeLODs(Vec3* debugOutput, u32 width, u32 height);

    // Members
    Vector<Cluster> clusters_;
    Vector<NaniteMeshDescriptor> meshes_;
    Vector<LODDescriptor> lodDescriptors_;
    Vector<OcclusionNode> occlusionHierarchy_;
    Vector<VisibleCluster> visibleClusters_;
    Vector<MaterialBatch> materialBatches_;

    // GPU cluster data
    Vector<GPUMeshlet> gpuMeshlets_;
    Vector<GPUInstance> gpuInstances_;
    Vector<ClusterDraw> drawList_;
    Vector<f32> clusterPositions_;
    Vector<u32> clusterIndexData_;
    Vector<f32> depthBuffer_;
    Vector<u32> visibleClusterFlags_;
    u32 bufferId_ = 0;
    u32 instanceBufferId_ = 0;

    // Occlusion data
    Vector<f32> hierarchicalDepthBuffer_;
    u32 hierarchyWidth_ = 0;
    u32 hierarchyHeight_ = 0;

    // Rendering state
    u32 screenWidth_ = 0;
    u32 screenHeight_ = 0;
    const Camera* camera_ = nullptr;
    Vec3 cameraPosition_;
    Mat4 viewProjMatrix_;
    u32 frameIndex_ = 0;

    // Streaming
    f32 streamingDistance_ = 5000.0f;
    u32 maxResidentClusters_ = 100000;
    u32 residentClusterCount_ = 0;

    // Fallback meshes
    struct FallbackMesh {
        Vector<Vec3> positions;
        Vector<u32> indices;
    };
    Vector<FallbackMesh> fallbackMeshes_;

    // Stats
    Stats stats_;

    bool initialized_ = false;
};

} // namespace Frost
