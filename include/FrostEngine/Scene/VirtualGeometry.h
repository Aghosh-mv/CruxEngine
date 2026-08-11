#pragma once

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Vector.h"
#include "Core/UniquePtr.h"
#include "Scene/Entity.h"
#include "Scene/SceneManager.h"

namespace Frost {
namespace Geometry {

using ::Frost::BoundingBox;
using ::Frost::BoundingSphere;

struct Triangle {
    u32 v0, v1, v2;
    u32 materialId;
    Vec3 normal;
};

struct Cluster {
    u32 startIndex;
    u32 triangleCount;
    BoundingBox bounds;
    f32 error;
    u8 lodLevel;
    u8 flags;
};

struct Patch {
    u32 clusterIndex;
    u32 patchIndex;
    u32 controlPointOffset;
    u32 controlPointCount;
    f32 curvature;
    f32 meanAngle;
    f32 gaussianCurvature;
};

struct Meshlet {
    u32 center;
    u32 primitives[64];
    u32 primitiveCount;
    u32 vertices[64];
    u32 vertexCount;
    BoundingSphere cullingSphere;
    BoundingBox boundingBox;
    Vec3 coneNormal;
    Vec3 coneCenter;
    f32 coneCutoff;
};

struct Mesh {
    Vector<Vec3> vertices;
    Vector<Vec3> normals;
    Vector<Vec2> uvs;
    Vector<Vec4> tangents;
    Vector<u32> indices;
    
    Vector<Cluster> clusters;
    Vector<Patch> patches;
    Vector<Meshlet> meshlets;
    
    u32 lodLevels = 4;
    f32 geometryErrorThreshold = 0.5f;
    
    BoundingBox boundingBox;
    BoundingSphere boundingSphere;
    
    void computeClusters();
    void computeMeshlets();
    void computeLODs(f32 screenSize);
    void simplify(f32 targetError);
};

struct ClusterBatcher {
    struct DrawCommands {
        u32 vertexOffset;
        u32 vertexCount;
        u32 indexOffset;
        u32 indexCount;
        u32 instanceId;
        u32 cullinguid;
    };
    
    struct VisibleCluster {
        u32 clusterId;
        u32 drawId;
        f32 depth;
    };
    
    Vector<DrawCommands> commands;
    Vector<VisibleCluster> visibleClusters;
    Vector<VisibleCluster> culledClusters;
    
    void build(const Mesh& mesh, const Mat4& viewProj, const Vec3& cameraPos);
    void sortForDIP();
};

}
}

namespace Frost {

struct GeometryPage {
    u32 pageId;
    Vec3 min;
    Vec3 max;
    u32 level;
    bool resident;
    bool loaded;
    u32 meshCount;
};

struct GeometryRequest {
    u32 pageId;
    u32 priority;
    f32 distanceToCamera;
    bool streaming;
};

class VirtualGeometrySystem {
public:
    static VirtualGeometrySystem& instance();
    
    void init(u32 maxClusters, u32 maxMeshlets);
    void shutdown();
    
    void registerMesh(Geometry::Mesh* mesh);
    void unregisterMesh(Geometry::Mesh* mesh);
    
    void update(const ViewData& view, const Vector<LightComponent*>& lights);
    
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
    void setClusterErrorThreshold(f32 threshold) { clusterErrorThreshold_ = threshold; }
    void setMaxLODLevel(u32 level) { maxLODLevel_ = level; }
    
    u32 getVisibleClusterCount() const { return visibleClusters_.size(); }
    u32 getClusterCount() const { return clusters_.size(); }
    
    const Vector<Geometry::Meshlet>& getMeshlets() const { return meshlets_; }
    
    void init(u32 clipmapLevels, f32 scale);
    u32 computeClipmapPage(const Vec3& worldPos, u32 level) const;
    void requestPage(u32 pageId, u32 priority);
    u32 processRequests(u32 maxPerFrame);
    bool makeResident(u32 pageId);
    bool evictPage(u32 pageId);
    const GeometryPage& getPage(u32 pageId) const;
    u32 getResidentPageCount() const { return residentPageCount_; }
    u32 getRequestedPages() const { return requestedPages_; }
    u32 getStreamedPages() const { return streamedPages_; }
    u32 getClipmapLevels() const { return clipmapLevels_; }
    void update(const Vec3& cameraPos, f32 dt);
    void clearPages();
    
private:
    VirtualGeometrySystem() = default;
    ~VirtualGeometrySystem() = default;
    
    VirtualGeometrySystem(const VirtualGeometrySystem&) = delete;
    VirtualGeometrySystem& operator=(const VirtualGeometrySystem&) = delete;
    
    Vector<Geometry::Mesh*> meshes_;
    Vector<Geometry::Cluster> clusters_;
    Vector<Geometry::Meshlet> meshlets_;
    Vector<u32> visibleClusters_;
    
    bool enabled_ = true;
    f32 clusterErrorThreshold_ = 0.5f;
    u32 maxLODLevel_ = 4;
    u32 frameIndex_ = 0;
    
    Vector<GeometryPage> pages_;
    Vector<GeometryRequest> pendingRequests_;
    u32 clipmapLevels_ = 8;
    f32 clipmapScale_ = 10.0f;
    u32 maxResidentPages_ = 16384;
    u32 residentPageCount_ = 0;
    u32 requestedPages_ = 0;
    u32 streamedPages_ = 0;
    f32 lastUpdateTime_ = 0.0f;
    
    usize findPage(u32 pageId) const;
};

}
