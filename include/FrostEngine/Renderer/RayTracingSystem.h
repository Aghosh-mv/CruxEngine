#pragma once

// ============================================================================
// FrostEngine Hardware Ray Tracing Pipeline
// ============================================================================
// Provides hardware-accelerated ray tracing, similar to UE5's RT pipeline.
// Features: BLAS/TLAS construction, Ray generation, Closest-hit/Miss/Any-hit
// shaders, Ray-traced shadows, reflections, AO, Denoising.
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
// Acceleration structure types
// ============================================================================
enum class AccelerationStructureType : u8 {
    BottomLevel,    // per-mesh BVH
    TopLevel        // per-instance BVH
};

// ============================================================================
// Geometry flags
// ============================================================================
enum class GeometryFlags : u8 {
    Opaque = 1 << 0,
    NoDuplicateAnyHitInvocation = 1 << 1,
    AllowTransform = 1 << 2
};

// ============================================================================
// BLAS geometry descriptor
// ============================================================================
struct BLASGeometryDesc {
    u32 vertexBufferHandle;
    u32 indexBufferHandle;
    u32 vertexStride;
    u32 vertexCount;
    u32 indexCount;
    u32 indexFormat;        // 0 = 16-bit, 1 = 32-bit
    GeometryFlags flags;
    Vec3 boundsMin;
    Vec3 boundsMax;
};

// ============================================================================
// BLAS instance
// ============================================================================
struct BLASInstance {
    u32 blasIndex;
    u32 instanceId;
    u32 hitGroupIndex;
    bool enabled = true;
    GeometryFlags flags;
    Mat4 transform;
    Mat4 prevTransform;
    Vec3 boundsCenter;
    f32 boundsRadius;
    u32 materialIndex;
    bool isTransparent;
    bool isEmissive;
    f32 emissiveIntensity;
};

// ============================================================================
// Ray payload
// ============================================================================
struct RayPayload {
    Vec3 position;
    Vec3 normal;
    Vec3 albedo;
    Vec3 emission;
    f32 metallic;
    f32 roughness;
    f32 alpha;
    f32 tHit;
    u32 instanceId;
    u32 triangleIndex;
    u32 materialIndex;
    bool hit;
};

// ============================================================================
// Shadow ray payload
// ============================================================================
struct ShadowPayload {
    f32 tHit;
    bool hit;
    bool transparent;
    f32 opacity;
};

// ============================================================================
// Denoising configuration
// ============================================================================
struct RTDenoiseConfig {
    bool enabled = true;
    u32 temporalFrames = 8;
    f32 temporalBlendWeight = 0.9f;
    f32 spatialBlurRadius = 2.0f;
    f32 edgeStoppingNormalThreshold = 0.3f;
    f32 edgeStoppingDepthThreshold = 0.1f;
    f32 luminanceThreshold = 0.5f;
    bool useATrous = true;
    u32 atrousIterations = 4;
    f32 atrousSigma = 0.2f;
    bool useTemporalVarianceClipping = true;
};

// ============================================================================
// Ray tracing configuration
// ============================================================================
struct RayTracingConfig {
    u32 maxRayRecursionDepth = 4;
    u32 maxRayPayloadSize = 128;
    u32 maxShadowPayloadSize = 16;
    u32 shadowRayMaxDistance = 100.0f;
    u32 aoRayMaxDistance = 5.0f;
    u32 aoSamplesPerPixel = 4;
    u32 reflectionMaxSamples = 16;
    bool enableAnyHit = true;
    bool enableTransparency = true;
    RTDenoiseConfig denoise;
};

// ============================================================================
// Acceleration structure entry
// ============================================================================
struct AccelerationStructure {
    u32 handle;
    AccelerationStructureType type;
    u64 gpuAddress;
    u64 size;
    bool built;
    bool dirty;
    u32 instanceCount;
};

// ============================================================================
// Ray Tracing System
// ============================================================================
class RayTracingSystem {
public:
    RayTracingSystem();
    ~RayTracingSystem();

    bool init();
    void shutdown();

    // Frame lifecycle
    void beginFrame(const Camera& camera, u32 screenWidth, u32 screenHeight);
    void buildAccelerationStructures();
    void dispatchRays();
    void denoise();
    void endFrame();

    // Configuration
    void setConfig(const RayTracingConfig& cfg) { config_ = cfg; }
    const RayTracingConfig& config() const { return config_; }

    // BLAS management
    u32 createBLAS(const Vector<BLASGeometryDesc>& geometries);
    void destroyBLAS(u32 blasIndex);
    void updateBLASVertexData(u32 blasIndex, u32 geomIndex,
                              const Vector<Vec3>& vertices, const Vector<Vec3>& normals);
    void rebuildBLAS(u32 blasIndex);

    // TLAS management
    void createTLAS(const Vector<BLASInstance>& instances);
    void updateTLASInstance(u32 instanceIndex, const Mat4& transform);
    void addInstanceToTLAS(const BLASInstance& instance);
    void removeInstanceFromTLAS(u32 instanceIndex);
    void rebuildTLAS();

    // Ray dispatch
    void dispatchRayGeneration(u32 width, u32 height);
    void dispatchRayTracedShadows(u32 width, u32 height);
    void dispatchRayTracedReflections(u32 width, u32 height);
    void dispatchRayTracedAO(u32 width, u32 height);

    // Hit group management
    u32 createHitGroup(const char* closestHitShader, const char* anyHitShader,
                       const char* intersectionShader);
    void bindHitGroupToInstance(u32 instanceIndex, u32 hitGroupIndex);

    // Shader management
    u32 loadRayGenShader(const char* shaderPath);
    u32 loadClosestHitShader(const char* shaderPath);
    u32 loadAnyHitShader(const char* shaderPath);
    u32 loadMissShader(const char* shaderPath);

    // Advanced ray tracing
    void pathTrace(u32 width, u32 height, u32 maxBounces);
    void progressivePathTrace(u32 width, u32 height, u32 sampleIndex);
    void adaptiveRayBudget(u32 width, u32 height, f32 targetFrameTimeMs);
    void featureAwareDenoise(const Vec3* normals, const f32* depths);
    void buildShaderBindingTable();
    void dispatchRaysFromSBT(u32 width, u32 height);

    // Query
    const RayPayload* getRayResults() const { return rayResults_.data(); }
    u32 getRayResultCount() const { return static_cast<u32>(rayResults_.size()); }
    const Vec3* getReflectionOutput() const { return reflectionOutput_.data(); }
    const f32* getAOOutput() const { return aoOutput_.data(); }
    const f32* getShadowOutput() const { return shadowOutput_.data(); }

    // Stats
    struct Stats {
        u32 blasCount;
        u32 tlasInstanceCount;
        u32 totalTriangles;
        u32 raysDispatched;
        u32 shadowRays;
        u32 reflectionRays;
        u32 aoRays;
        f32 blasBuildTimeMs;
        f32 tlasBuildTimeMs;
        f32 rayTraceTimeMs;
        f32 denoiseTimeMs;
    };
    const Stats& stats() const { return stats_; }

private:
    // BLAS build
    void buildBLASInternal(u32 blasIndex);
    void computeBLASBounds(u32 blasIndex);
    void buildBVHForGeometry(const BLASGeometryDesc& geom,
                             Vector<Vec3>& bvhNodes, u32& nodeCount);

    // TLAS build
    void buildTLASInternal();
    void computeInstanceBounds(const BLASInstance& instance, Vec3& min, Vec3& max);

    // BVH construction
    struct BVHNode {
        Vec3 boundsMin;
        Vec3 boundsMax;
        i32 leftChild;          // -1 = leaf
        i32 rightChild;         // -1 = leaf
        i32 primitiveIndex;     // leaf: triangle index
        i32 primitiveCount;     // leaf: 1, internal: count
        f32 surfaceArea;
    };

    void buildBVH(Vector<BVHNode>& nodes, Vector<Vec3>& centroids,
                  Vector<Vec3>& boundsMin, Vector<Vec3>& boundsMax,
                  i32 start, i32 end, i32& nodeIndex);
    i32 findSplitAxis(const Vector<Vec3>& centroids, i32 start, i32 end, f32& splitPos);
    f32 computeSAH(const Vec3& boundsMin, const Vec3& boundsMax, i32 count) const;

    // Ray tracing algorithms
    void traceRay(const Vec3& origin, const Vec3& dir, f32 tMin, f32 tMax,
                  RayPayload& payload, u32 recursionDepth = 0);
    void traceShadowRay(const Vec3& origin, const Vec3& dir, f32 tMin, f32 tMax,
                        ShadowPayload& payload);

    // Closest hit evaluation
    void evaluateClosestHit(const RayPayload& payload, Vec3& color);
    void evaluateMaterial(const RayPayload& payload, const Vec3& viewDir,
                          Vec3& albedo, f32& metallic, f32& roughness);

    // Miss shader
    Vec3 evaluateMiss(const Vec3& direction);

    // Any hit (alpha testing)
    bool evaluateAnyHit(const RayPayload& payload);

    // GGX importance sampling for reflections
    Vec3 importanceSampleGGX(const Vec2& Xi, f32 roughness, const Vec3& N);
    f32 distributionGGX(const Vec3& N, const Vec3& H, f32 roughness);
    f32 geometrySmith(const Vec3& N, const Vec3& V, const Vec3& L, f32 roughness);
    f32 geometrySchlickGGX(f32 NdotV, f32 roughness);
    Vec3 fresnelSchlick(f32 cosTheta, const Vec3& F0);
    Vec3 fresnelSchlickRoughness(f32 cosTheta, const Vec3& F0, f32 roughness);

    // Denoising
    void temporalAccumulation();
    void spatialBlur();
    void atrousWavelet(const f32* input, f32* output, u32 width, u32 height,
                       f32 sigma, u32 iteration);
    void varianceClipping(const f32* current, const f32* history,
                          const f32* motionVec, u32 width, u32 height);

    // Hit testing
    bool testRayTriangle(const Vec3& origin, const Vec3& dir,
                         const Vec3& v0, const Vec3& v1, const Vec3& v2,
                         f32& t, f32& u, f32& v);
    bool testRayAABB(const Vec3& origin, const Vec3& dir,
                     const Vec3& boundsMin, const Vec3& boundsMax,
                     f32& tMin, f32& tMax);
    bool testRaySphere(const Vec3& origin, const Vec3& dir,
                       const Vec3& center, f32 radius, f32& t);

    // Random number generation
    Vec2 hammersley(u32 i, u32 N);
    Vec3 cosineWeightedHemisphere(const Vec2& Xi, const Vec3& N);
    Vec3 uniformSphereDirection(const Vec2& Xi);

    // Members
    RayTracingConfig config_;
    u32 screenWidth_ = 0;
    u32 screenHeight_ = 0;
    u32 frameIndex_ = 0;

    // BLAS storage
    struct BLASEntry {
        Vector<BLASGeometryDesc> geometries;
        Vector<BVHNode> bvhNodes;
        u32 nodeCount;
        Vec3 boundsMin;
        Vec3 boundsMax;
        bool dirty;
        u64 gpuAddress;
        u64 size;
    };
    Vector<BLASEntry> blasList_;

    // TLAS
    Vector<BLASInstance> instances_;
    Vector<BVHNode> tlasNodes_;
    u32 tlasNodeCount_;
    AccelerationStructure tlas_;
    bool tlasDirty_ = true;

    // Shader binding table
    struct ShaderBindingEntry {
        String rayGenShader;
        String closestHitShader;
        String anyHitShader;
        String intersectionShader;
        String missShader;
    };
    Vector<ShaderBindingEntry> hitGroups_;
    Vector<ShaderBindingEntry> missShaders_;
    u32 rayGenShaderIndex_;

    // SBT entries
    struct SBTEntry {
        u32 shaderIndex;
        u32 offset;
        u32 stride;
    };
    Vector<SBTEntry> sbtEntries_;

    // Ray results
    Vector<RayPayload> rayResults_;

    // Output buffers
    Vector<Vec3> reflectionOutput_;
    Vector<f32> aoOutput_;
    Vector<f32> shadowOutput_;
    Vector<Vec3> denoisedOutput_;

    // Temporal buffers
    Vector<Vec3> historyColor_;
    Vector<f32> historyDepth_;
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
