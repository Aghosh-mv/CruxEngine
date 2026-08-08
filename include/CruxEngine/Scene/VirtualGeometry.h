#pragma once

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Vector.h"
#include "Core/UniquePtr.h"

namespace Crux {
namespace Geometry {

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
    u32 cullingSphere;
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
    f32几何ErrorThreshold = 0.5f;
    
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

namespace Crux {

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
};

}
}