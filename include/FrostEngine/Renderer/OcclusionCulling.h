#pragma once

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Vector.h"
#include "Core/UniquePtr.h"
#include "Renderer/Types.h"
#include "Renderer/Texture.h"

namespace Frost {
namespace Renderer {

struct AABB {
    Vec3 min;
    Vec3 max;
    
    AABB() = default;
    AABB(const Vec3& mn, const Vec3& mx) : min(mn), max(mx) {}
    
    bool intersects(const AABB& other) const {
        return min.x <= other.max.x && max.x >= other.min.x &&
               min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
    }
    
    bool contains(const Vec3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }
    
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extents() const { return (max - min) * 0.5f; }
};

struct Frustum {
    Vec4 planes[6];
    
    void update(const Mat4& viewProj);
    bool intersects(const AABB& box) const;
    bool contains(const AABB& box) const;
    bool contains(const Vec3& point) const;
};

struct OcclusionMesh {
    u32 vertexOffset = 0;
    u32 indexOffset = 0;
    u32 indexCount = 0;
    AABB bounds;
    u32 meshId = 0;
};

struct OcclusionQuery {
    u32 queryId = 0;
    AABB bounds;
    bool result = true;
    bool issued = false;
    u64 frameIssued = 0;
};

struct HiZMipLevel {
    u32 width = 0;
    u32 height = 0;
    u32 offset = 0;
};

class HiZBuffer {
public:
    HiZBuffer() = default;
    ~HiZBuffer() = default;
    
    void initialize(u32 width, u32 height);
    void shutdown();
    
    void buildFromDepth(void* cmd, FrameBuffer* depthTexture);
    void buildFromDepthCPU(const f32* depthData, u32 width, u32 height);
    
    bool testAABB(const AABB& box, const Mat4& viewProj) const;
    bool testSphere(const Vec3& center, f32 radius, const Mat4& viewProj) const;
    
    u32 getWidth() const { return width_; }
    u32 getHeight() const { return height_; }
    u32 getNumMipLevels() const { return mipLevels_; }
    const Vector<f32>& getData() const { return data_; }
    const Vector<HiZMipLevel>& getMipLevelData() const { return mipData_; }
    
private:
    void generateMipsCPU();
    f32 sampleMipLevel(u32 mip, f32 u, f32 v) const;
    
    u32 width_ = 0;
    u32 height_ = 0;
    u32 mipLevels_ = 0;
    Vector<f32> data_;
    Vector<HiZMipLevel> mipData_;
    void* gpuBuffer_ = nullptr;
};

struct SoftwareRasterizer {
    struct Vertex {
        Vec4 position;
        f32 depth;
    };
    
    struct Triangle {
        Vertex v[3];
        u32 objectId;
    };
    
    struct Tile {
        static constexpr u32 SIZE = 64;
        f32 depthBuffer[SIZE * SIZE];
        u32 objectIdBuffer[SIZE * SIZE];
        u32 minX = SIZE, minY = SIZE, maxX = 0, maxY = 0;
        bool dirty = false;
    };
    
    SoftwareRasterizer(u32 width, u32 height);
    ~SoftwareRasterizer();
    
    void clear();
    void setViewProjection(const Mat4& viewProj);
    void addMesh(const OcclusionMesh& mesh, const Mat4& transform, u32 objectId);
    void rasterize();
    
    bool testOcclusion(const AABB& box, const Mat4& viewProj);
    bool testOcclusion(const Vec3& center, f32 radius, const Mat4& viewProj);
    
    f32* getDepthBuffer() { return depthBuffer_.data(); }
    u32 getWidth() const { return width_; }
    u32 getHeight() const { return height_; }
    
private:
    void rasterizeTriangle(const Triangle& tri);
    void setupTileGrid();
    bool triangleOverlapsTile(const Triangle& tri, u32 tileX, u32 tileY) const;
    void processTile(u32 tileX, u32 tileY);
    
    u32 width_;
    u32 height_;
    u32 tileWidth_;
    u32 tileHeight_;
    Mat4 viewProj_;
    Vector<Triangle> triangles_;
    Vector<Tile> tiles_;
    Vector<f32> depthBuffer_;
    Vector<u32> objectIdBuffer_;
};

class OcclusionCullingSystem {
public:
    struct Config {
        u32 hizWidth = 256;
        u32 hizHeight = 256;
        u32 softwareRasterWidth = 512;
        u32 softwareRasterHeight = 512;
        bool useHiZ = true;
        bool useSoftwareRasterizer = true;
        bool useGPUQueries = false;
        u32 maxQueriesPerFrame = 1024;
        u32 maxOcclusionMeshes = 8192;
        f32 conservativeFactor = 1.01f;
    };
    
    struct Stats {
        u32 totalObjects = 0;
        u32 culledObjects = 0;
        u32 visibleObjects = 0;
        mutable u32 hiZTests = 0;
        mutable u32 softwareRasterTests = 0;
        mutable u32 gpuQueryTests = 0;
        u32 queriesIssued = 0;
        f32 cullTimeMs = 0.0f;
        f32 rasterTimeMs = 0.0f;
    };
    
    struct HiZBuffer {
        Vector<f32> depths;
        u32 width = 0;
        u32 height = 0;
        u32 mipLevels = 0;
    };
    
    struct Object {
        u32 id = 0;
        AABB bounds;
        Mat4 transform;
        u32 meshIndex = 0xFFFFFFFF;
        bool staticObject = true;
        u32 lastVisibleFrame = 0;
        bool wasVisible = true;
    };
    
    OcclusionCullingSystem();
    ~OcclusionCullingSystem();
    
    bool initialize(const Config& config);
    void shutdown();
    
    void setConfig(const Config& config) { config_ = config; }
    const Config& getConfig() const { return config_; }
    
void beginFrame(u64 frameIndex, void* cmd);
    void endFrame();
    
    u32 registerObject(const AABB& bounds, const Mat4& transform, u32 meshIndex = 0xFFFFFFFF, bool staticObject = true);
    void unregisterObject(u32 objectId);
    void updateObjectTransform(u32 objectId, const Mat4& transform);
    void updateObjectBounds(u32 objectId, const AABB& bounds);
    
    void setCamera(const Mat4& view, const Mat4& proj);
    void setCamera(const Mat4& viewProj);
    
    void addOcclusionMesh(u32 meshId, const Vec3* vertices, u32 vertexCount, const u32* indices, u32 indexCount);
    void removeOcclusionMesh(u32 meshId);
    
    void cullObjects(Vector<u32>& outVisibleObjects, Vector<u32>& outCulledObjects);
    void cullObjects(Vector<Object*>& outVisibleObjects, Vector<Object*>& outCulledObjects);
    
    bool isObjectVisible(u32 objectId) const;
    bool isAABBVisible(const AABB& box) const;
    bool isSphereVisible(const Vec3& center, f32 radius) const;
    
    void issueGPUQuery(u32 objectId);
    void collectGPUQueryResults();
    
    void buildHiZFromDepth(FrameBuffer* depthTexture);
    void buildSoftwareRasterizerDepth();
    
    const Stats& getStats() const { return stats_; }
    void resetStats();
    
    void debugDrawHiZ(void* cmd, u32 mipLevel = 0);
    void debugDrawSoftwareRaster(void* cmd);
    
    void buildHiZ(const f32* depthBuffer, u32 width, u32 height);
    void downsampleHiZ(u32 mipLevel);
    bool testAABB(const Vec3& min, const Vec3& max, const Mat4& viewProj);
    bool testSphere(const Vec3& center, f32 radius, const Mat4& viewProj);
    void temporalReproject();
    const HiZBuffer& getHiZBuffer() const { return hizBuffer_; }
    u32 getOccludedCount() const { return occludedCount_; }
    u32 getTestedCount() const { return testedCount_; }
    f32 getCullTimeMs() const { return cullTimeMs_; }
    void setNearFar(f32 near, f32 far);
    
private:
    bool testObjectHiZ(const Object& obj) const;
    bool testObjectSoftwareRaster(const Object& obj) const;
    bool testObjectGPUQuery(const Object& obj) const;
    bool testObjectFrustum(const Object& obj) const;
    
    void updateObjectVisibility(Object& obj, bool visible);
    void processStaticObjects();
    void processDynamicObjects();
    
    Config config_;
    Stats stats_;
    u64 currentFrame_ = 0;
    void* currentCmd_ = nullptr;
    
    Mat4 view_;
    Mat4 proj_;
    Mat4 viewProj_;
    Frustum frustum_;
    
    ::Frost::Renderer::HiZBuffer hiZBuffer_;
    UniquePtr<SoftwareRasterizer> softwareRasterizer_;
    
    HiZBuffer hizBuffer_;
    HiZBuffer prevHzBuffer_;
    u32 tilesX_ = 0;
    u32 tilesY_ = 0;
    f32 zNear_ = 0.1f;
    f32 zFar_ = 1000.0f;
    bool enableTemporal_ = true;
    u32 occludedCount_ = 0;
    u32 testedCount_ = 0;
    f32 cullTimeMs_ = 0.0f;
    
    Vector<Object> objects_;
    Vector<OcclusionMesh> occlusionMeshes_;
    Vector<OcclusionQuery> gpuQueries_;
    
    Vector<u32> staticObjectIndices_;
    Vector<u32> dynamicObjectIndices_;
    
    void* queryBuffer_ = nullptr;
    FrameBuffer* queryTexture_ = nullptr;
};

}
}