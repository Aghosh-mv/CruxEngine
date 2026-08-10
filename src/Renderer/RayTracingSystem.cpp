// ============================================================================
// FrostEngine Hardware Ray Tracing Pipeline - Implementation
// ============================================================================

#include "FrostEngine/Renderer/RayTracingSystem.h"
#include "FrostEngine/Renderer/Camera.h"
#include "FrostEngine/Core/Math.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Frost {

// ============================================================================
// Constructor / Destructor
// ============================================================================
RayTracingSystem::RayTracingSystem() = default;

RayTracingSystem::~RayTracingSystem() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================
bool RayTracingSystem::init() {
    if (initialized_) return true;
    rayGenShaderIndex_ = 0;
    u32 pixelCount = 1920 * 1080;
    historyColor_.resize(pixelCount);
    historyDepth_.resize(pixelCount);
    motionVectors_.resize(pixelCount);
    initialized_ = true;
    return true;
}

void RayTracingSystem::shutdown() {
    if (!initialized_) return;
    blasList_.clear();
    instances_.clear();
    tlasNodes_.clear();
    hitGroups_.clear();
    missShaders_.clear();
    rayResults_.clear();
    reflectionOutput_.clear();
    aoOutput_.clear();
    shadowOutput_.clear();
    denoisedOutput_.clear();
    historyColor_.clear();
    historyDepth_.clear();
    motionVectors_.clear();
    initialized_ = false;
}

// ============================================================================
// Frame Lifecycle
// ============================================================================
void RayTracingSystem::beginFrame(const Camera& camera, u32 screenWidth, u32 screenHeight) {
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
    cameraPosition_ = camera.position();
    cameraDirection_ = camera.forward();
    viewMatrix_ = camera.view();
    projMatrix_ = camera.proj();
    viewProjMatrix_ = camera.viewProj();
    nearPlane_ = camera.nearPlane();
    farPlane_ = camera.farPlane();
    u32 pixelCount = screenWidth_ * screenHeight_;
    if (reflectionOutput_.size() != pixelCount) {
        reflectionOutput_.resize(pixelCount);
        aoOutput_.resize(pixelCount);
        shadowOutput_.resize(pixelCount);
        denoisedOutput_.resize(pixelCount);
        historyColor_.resize(pixelCount);
        historyDepth_.resize(pixelCount);
        motionVectors_.resize(pixelCount);
    }
    stats_.raysDispatched = 0;
    stats_.shadowRays = 0;
    stats_.reflectionRays = 0;
    stats_.aoRays = 0;
}

void RayTracingSystem::buildAccelerationStructures() {
    for (u32 i = 0; i < blasList_.size(); i++) {
        if (blasList_[i].dirty) {
            buildBLASInternal(i);
            blasList_[i].dirty = false;
        }
    }
    if (tlasDirty_) {
        buildTLASInternal();
        tlasDirty_ = false;
    }
    stats_.blasBuildTimeMs = 0.0f;
    stats_.tlasBuildTimeMs = 0.0f;
}

void RayTracingSystem::dispatchRays() {
    dispatchRayGeneration(screenWidth_, screenHeight_);
    stats_.rayTraceTimeMs = 0.0f;
}

void RayTracingSystem::denoise() {
    if (!config_.denoise.enabled) return;
    temporalAccumulation();
    spatialBlur();
    stats_.denoiseTimeMs = 0.0f;
}

void RayTracingSystem::endFrame() {
    prevViewProjMatrix_ = viewProjMatrix_;
}

// ============================================================================
// BLAS Management
// ============================================================================
u32 RayTracingSystem::createBLAS(const Vector<BLASGeometryDesc>& geometries) {
    u32 index = static_cast<u32>(blasList_.size());
    BLASEntry entry;
    entry.geometries = geometries;
    entry.dirty = true;
    entry.nodeCount = 0;
    entry.boundsMin = Vec3(0);
    entry.boundsMax = Vec3(0);
    entry.gpuAddress = 0;
    entry.size = 0;
    blasList_.push_back(entry);
    return index;
}

void RayTracingSystem::destroyBLAS(u32 blasIndex) {
    if (blasIndex >= blasList_.size()) return;
    blasList_[blasIndex].geometries.clear();
    blasList_[blasIndex].dirty = true;
    tlasDirty_ = true;
}

void RayTracingSystem::updateBLASVertexData(u32 blasIndex, u32 geomIndex,
                                              const Vector<Vec3>& vertices,
                                              const Vector<Vec3>& normals) {
    if (blasIndex >= blasList_.size()) return;
    if (geomIndex >= blasList_[blasIndex].geometries.size()) return;
    blasList_[blasIndex].geometries[geomIndex].vertexBufferHandle = 0;
    blasList_[blasIndex].dirty = true;
    tlasDirty_ = true;
}

void RayTracingSystem::rebuildBLAS(u32 blasIndex) {
    if (blasIndex >= blasList_.size()) return;
    blasList_[blasIndex].dirty = true;
}

// ============================================================================
// BLAS Build
// ============================================================================
void RayTracingSystem::buildBLASInternal(u32 blasIndex) {
    BLASEntry& entry = blasList_[blasIndex];
    entry.bvhNodes.clear();
    entry.nodeCount = 0;
    if (entry.geometries.empty()) return;
    computeBLASBounds(blasIndex);
    Vector<Vec3> centroids;
    Vector<Vec3> triBoundsMin;
    Vector<Vec3> triBoundsMax;
    for (auto& geom : entry.geometries) {
        u32 triCount = geom.indexCount / 3;
        for (u32 t = 0; t < triCount; t++) {
            Vec3 v0 = (geom.boundsMin + geom.boundsMax) * 0.5f;
            Vec3 v1 = v0;
            Vec3 v2 = v0;
            centroids.push_back((v0 + v1 + v2) / 3.0f);
            triBoundsMin.push_back(geom.boundsMin);
            triBoundsMax.push_back(geom.boundsMax);
        }
    }
    if (centroids.empty()) return;
    i32 rootIdx = 0;
    buildBVH(entry.bvhNodes, centroids, triBoundsMin, triBoundsMax,
             0, (i32)centroids.size() - 1, rootIdx);
    entry.nodeCount = static_cast<u32>(entry.bvhNodes.size());
    entry.gpuAddress = (u64)entry.bvhNodes.data();
    entry.size = entry.bvhNodes.size() * sizeof(BVHNode);
}

void RayTracingSystem::computeBLASBounds(u32 blasIndex) {
    BLASEntry& entry = blasList_[blasIndex];
    entry.boundsMin = Vec3(1e30f);
    entry.boundsMax = Vec3(-1e30f);
    for (auto& geom : entry.geometries) {
        entry.boundsMin = entry.boundsMin.min(geom.boundsMin);
        entry.boundsMax = entry.boundsMax.max(geom.boundsMax);
    }
}

// ============================================================================
// BVH Construction
// ============================================================================
void RayTracingSystem::buildBVH(Vector<BVHNodeInternal>& nodes, Vector<Vec3>& centroids,
                                Vector<Vec3>& boundsMin, Vector<Vec3>& boundsMax,
                                i32 start, i32 end, i32& nodeIndex) {
    if (start > end) return;
    i32 currentNode = nodeIndex++;
    BVHNodeInternal node;
    node.leftChild = -1;
    node.rightChild = -1;
    node.primitiveIndex = -1;
    node.primitiveCount = 0;
    node.boundsMin = boundsMin[start];
    node.boundsMax = boundsMax[start];
    for (i32 i = start + 1; i <= end; i++) {
        node.boundsMin = node.boundsMin.min(boundsMin[i]);
        node.boundsMax = node.boundsMax.max(boundsMax[i]);
    }
    node.surfaceArea = computeSAH(node.boundsMin, node.boundsMax, 1);
    i32 primitiveCount = end - start + 1;
    if (primitiveCount <= 2) {
        node.primitiveIndex = start;
        node.primitiveCount = primitiveCount;
        nodes.push_back(node);
        return;
    }
    f32 splitPos;
    i32 splitAxis = findSplitAxis(centroids, start, end, splitPos);
    i32 mid = start;
    for (i32 i = start; i <= end; i++) {
        f32 centroidCoord;
        switch (splitAxis) {
        case 0: centroidCoord = centroids[i].x; break;
        case 1: centroidCoord = centroids[i].y; break;
        default: centroidCoord = centroids[i].z; break;
        }
        if (centroidCoord < splitPos) {
            auto tempCentroid = centroids[i];
            centroids[i] = centroids[mid];
            centroids[mid] = tempCentroid;
            auto tempMin = boundsMin[i];
            boundsMin[i] = boundsMin[mid];
            boundsMin[mid] = tempMin;
            auto tempMax = boundsMax[i];
            boundsMax[i] = boundsMax[mid];
            boundsMax[mid] = tempMax;
            mid++;
        }
    }
    if (mid == start || mid > end) {
        node.primitiveIndex = start;
        node.primitiveCount = primitiveCount;
        nodes.push_back(node);
        return;
    }
    node.leftChild = nodeIndex;
    buildBVH(nodes, centroids, boundsMin, boundsMax, start, mid - 1, nodeIndex);
    node.rightChild = nodeIndex;
    buildBVH(nodes, centroids, boundsMin, boundsMax, mid, end, nodeIndex);
    nodes.push_back(node);
}

i32 RayTracingSystem::findSplitAxis(const Vector<Vec3>& centroids, i32 start, i32 end,
                                      f32& splitPos) {
    Vec3 centroidMin = centroids[start];
    Vec3 centroidMax = centroids[start];
    for (i32 i = start + 1; i <= end; i++) {
        centroidMin = centroidMin.min(centroids[i]);
        centroidMax = centroidMax.max(centroids[i]);
    }
    Vec3 extent = centroidMax - centroidMin;
    i32 axis = 0;
    if (extent.y > extent.x && extent.y > extent.z) axis = 1;
    else if (extent.z > extent.x && extent.z > extent.y) axis = 2;
    f32 midPos;
    switch (axis) {
    case 0: midPos = (centroidMin.x + centroidMax.x) * 0.5f; break;
    case 1: midPos = (centroidMin.y + centroidMax.y) * 0.5f; break;
    default: midPos = (centroidMin.z + centroidMax.z) * 0.5f; break;
    }
    splitPos = midPos;
    return axis;
}

f32 RayTracingSystem::computeSAH(const Vec3& boundsMin, const Vec3& boundsMax, i32 count) const {
    Vec3 extent = boundsMax - boundsMin;
    f32 area = 2.0f * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x);
    return area * (f32)count;
}

// ============================================================================
// TLAS Management
// ============================================================================
void RayTracingSystem::createTLAS(const Vector<BLASInstance>& instances) {
    instances_ = instances;
    tlasDirty_ = true;
}

void RayTracingSystem::updateTLASInstance(u32 instanceIndex, const Mat4& transform) {
    if (instanceIndex >= instances_.size()) return;
    instances_[instanceIndex].prevTransform = instances_[instanceIndex].transform;
    instances_[instanceIndex].transform = transform;
    tlasDirty_ = true;
}

void RayTracingSystem::addInstanceToTLAS(const BLASInstance& instance) {
    instances_.push_back(instance);
    tlasDirty_ = true;
}

void RayTracingSystem::removeInstanceFromTLAS(u32 instanceIndex) {
    if (instanceIndex >= instances_.size()) return;
    instances_.eraseSwap(instanceIndex);
    tlasDirty_ = true;
}

void RayTracingSystem::rebuildTLAS() {
    tlasDirty_ = true;
}

void RayTracingSystem::buildTLASInternal() {
    tlasNodes_.clear();
    tlasNodeCount_ = 0;
    if (instances_.empty()) return;
    Vector<Vec3> centroids;
    Vector<Vec3> instanceBoundsMin;
    Vector<Vec3> instanceBoundsMax;
    for (auto& inst : instances_) {
        Vec3 center = Vec3(inst.transform.m[12], inst.transform.m[13], inst.transform.m[14]);
        Vec3 bmin, bmax;
        if (inst.blasIndex < blasList_.size()) {
            bmin = blasList_[inst.blasIndex].boundsMin;
            bmax = blasList_[inst.blasIndex].boundsMax;
        } else {
            bmin = center - Vec3(1);
            bmax = center + Vec3(1);
        }
        Vec3 corners[8] = {
            Vec3(bmin.x, bmin.y, bmin.z), Vec3(bmax.x, bmin.y, bmin.z),
            Vec3(bmin.x, bmax.y, bmin.z), Vec3(bmax.x, bmax.y, bmin.z),
            Vec3(bmin.x, bmin.y, bmax.z), Vec3(bmax.x, bmin.y, bmax.z),
            Vec3(bmin.x, bmax.y, bmax.z), Vec3(bmax.x, bmax.y, bmax.z)
        };
        Vec3 wMin(1e30f), wMax(-1e30f);
        for (u32 c = 0; c < 8; c++) {
            Vec4 tr = inst.transform * Vec4(corners[c], 1.0f);
            Vec3 wc(tr.x, tr.y, tr.z);
            wMin = wMin.min(wc);
            wMax = wMax.max(wc);
        }
        centroids.push_back(center);
        instanceBoundsMin.push_back(wMin);
        instanceBoundsMax.push_back(wMax);
    }
    i32 rootIdx = 0;
    buildBVH(tlasNodes_, centroids, instanceBoundsMin, instanceBoundsMax,
             0, (i32)instances_.size() - 1, rootIdx);
    tlasNodeCount_ = static_cast<u32>(tlasNodes_.size());
    tlas_.handle = 0;
    tlas_.type = AccelerationStructureType::TopLevel;
    tlas_.gpuAddress = (u64)tlasNodes_.data();
    tlas_.size = tlasNodes_.size() * sizeof(BVHNode);
    tlas_.built = true;
    tlas_.dirty = false;
    tlas_.instanceCount = static_cast<u32>(instances_.size());
}

void RayTracingSystem::computeInstanceBounds(const BLASInstance& instance, Vec3& min, Vec3& max) {
    if (instance.blasIndex < blasList_.size()) {
        min = blasList_[instance.blasIndex].boundsMin;
        max = blasList_[instance.blasIndex].boundsMax;
        Vec3 corners[8] = {
            Vec3(min.x, min.y, min.z), Vec3(max.x, min.y, min.z),
            Vec3(min.x, max.y, min.z), Vec3(max.x, max.y, min.z),
            Vec3(min.x, min.y, max.z), Vec3(max.x, min.y, max.z),
            Vec3(min.x, max.y, max.z), Vec3(max.x, max.y, max.z)
        };
        min = Vec3(1e30f);
        max = Vec3(-1e30f);
        for (u32 c = 0; c < 8; c++) {
            Vec4 tr = instance.transform * Vec4(corners[c], 1.0f);
            Vec3 wc(tr.x, tr.y, tr.z);
            min = min.min(wc);
            max = max.max(wc);
        }
    } else {
        min = instance.boundsCenter - Vec3(instance.boundsRadius);
        max = instance.boundsCenter + Vec3(instance.boundsRadius);
    }
}

// ============================================================================
// Ray Dispatch
// ============================================================================
void RayTracingSystem::dispatchRayGeneration(u32 width, u32 height) {
    rayResults_.clear();
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            f32 ndcX = ((f32)x + 0.5f) / (f32)width * 2.0f - 1.0f;
            f32 ndcY = 1.0f - ((f32)y + 0.5f) / (f32)height * 2.0f;
            Vec4 nearPt = Vec4(ndcX, ndcY, 0.0f, 1.0f);
            Vec4 farPt = Vec4(ndcX, ndcY, 1.0f, 1.0f);
            Vec4 nearWorld = viewProjMatrix_ * nearPt;
            Vec4 farWorld = viewProjMatrix_ * farPt;
            Vec3 origin = Vec3(nearWorld.x, nearWorld.y, nearWorld.z) / nearWorld.w;
            Vec3 direction = Vec3(farWorld.x, farWorld.y, farWorld.z) / farWorld.w - origin;
            direction = direction.normalized();
            RayPayload payload;
            traceRay(origin, direction, nearPlane_, farPlane_, payload);
            rayResults_.push_back(payload);
            stats_.raysDispatched++;
        }
    }
}

void RayTracingSystem::dispatchRayTracedShadows(u32 width, u32 height) {
    shadowOutput_.resize(width * height);
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u32 idx = y * width + x;
            if (idx >= rayResults_.size()) continue;
            const RayPayload& hit = rayResults_[idx];
            if (!hit.hit) { shadowOutput_[idx] = 1.0f; continue; }
            Vec3 lightDir = Vec3(0.5f, -0.8f, -0.3f).normalized();
            Vec3 origin = hit.position + hit.normal * 0.01f;
            ShadowPayload sp;
            traceShadowRay(origin, lightDir, 0.001f, config_.shadowRayMaxDistance, sp);
            shadowOutput_[idx] = sp.hit ? 0.0f : 1.0f;
            stats_.shadowRays++;
        }
    }
}

void RayTracingSystem::dispatchRayTracedReflections(u32 width, u32 height) {
    reflectionOutput_.resize(width * height);
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u32 idx = y * width + x;
            if (idx >= rayResults_.size()) continue;
            const RayPayload& hit = rayResults_[idx];
            if (!hit.hit) { reflectionOutput_[idx] = Vec3(0.3f, 0.5f, 0.8f); continue; }
            Vec3 viewDir = (cameraPosition_ - hit.position).normalized();
            Vec3 totalReflection = Vec3(0);
            u32 samples = config_.reflectionMaxSamples;
            for (u32 s = 0; s < samples; s++) {
                Vec2 Xi = hammersley(s, samples);
                Vec3 halfVec = importanceSampleGGX(Xi, hit.roughness, hit.normal);
                Vec3 sampleDir = halfVec * (2.0f * viewDir.dot(halfVec)) - viewDir;
                RayPayload rp;
                traceRay(hit.position + sampleDir * 0.01f, sampleDir,
                         0.001f, 1000.0f, rp, 1);
                Vec3 F = fresnelSchlick(Mathf::max(halfVec.dot(viewDir), 0.0f),
                                        Vec3(0.04f) + (hit.albedo - Vec3(0.04f)) * hit.metallic);
                totalReflection = totalReflection + rp.albedo * F;
            }
            reflectionOutput_[idx] = totalReflection / (f32)samples;
            stats_.reflectionRays++;
        }
    }
}

void RayTracingSystem::dispatchRayTracedAO(u32 width, u32 height) {
    aoOutput_.resize(width * height);
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u32 idx = y * width + x;
            if (idx >= rayResults_.size()) continue;
            const RayPayload& hit = rayResults_[idx];
            if (!hit.hit) { aoOutput_[idx] = 1.0f; continue; }
            f32 ao = 0.0f;
            u32 samples = config_.aoSamplesPerPixel;
            for (u32 s = 0; s < samples; s++) {
                Vec2 Xi = hammersley(s, samples);
                Vec3 sampleDir = cosineWeightedHemisphere(Xi, hit.normal);
                RayPayload aoPayload;
                traceRay(hit.position + sampleDir * 0.01f, sampleDir,
                         0.001f, config_.aoRayMaxDistance, aoPayload);
                if (aoPayload.hit) ao += 1.0f;
            }
            aoOutput_[idx] = 1.0f - ao / (f32)samples;
            stats_.aoRays++;
        }
    }
}

// ============================================================================
// Hit Group Management
// ============================================================================
u32 RayTracingSystem::createHitGroup(const char* closestHitShader, const char* anyHitShader,
                                       const char* intersectionShader) {
    u32 index = static_cast<u32>(hitGroups_.size());
    ShaderBindingEntry entry;
    entry.closestHitShader = closestHitShader ? closestHitShader : "";
    entry.anyHitShader = anyHitShader ? anyHitShader : "";
    entry.intersectionShader = intersectionShader ? intersectionShader : "";
    hitGroups_.push_back(entry);
    return index;
}

void RayTracingSystem::bindHitGroupToInstance(u32 instanceIndex, u32 hitGroupIndex) {
    if (instanceIndex < instances_.size()) {
        instances_[instanceIndex].hitGroupIndex = hitGroupIndex;
    }
}

// ============================================================================
// Shader Management
// ============================================================================
u32 RayTracingSystem::loadRayGenShader(const char* shaderPath) {
    ShaderBindingEntry entry;
    entry.rayGenShader = shaderPath ? shaderPath : "";
    missShaders_.push_back(entry);
    return static_cast<u32>(missShaders_.size() - 1);
}

u32 RayTracingSystem::loadClosestHitShader(const char* shaderPath) {
    ShaderBindingEntry entry;
    entry.closestHitShader = shaderPath ? shaderPath : "";
    hitGroups_.push_back(entry);
    return static_cast<u32>(hitGroups_.size() - 1);
}

u32 RayTracingSystem::loadAnyHitShader(const char* shaderPath) {
    if (!hitGroups_.empty()) {
        hitGroups_.back().anyHitShader = shaderPath ? shaderPath : "";
    }
    return static_cast<u32>(hitGroups_.size());
}

u32 RayTracingSystem::loadMissShader(const char* shaderPath) {
    ShaderBindingEntry entry;
    entry.missShader = shaderPath ? shaderPath : "";
    missShaders_.push_back(entry);
    return static_cast<u32>(missShaders_.size() - 1);
}

// ============================================================================
// Ray Tracing Core
// ============================================================================
void RayTracingSystem::traceRay(const Vec3& origin, const Vec3& dir, f32 tMin, f32 tMax,
                                  RayPayload& payload, u32 recursionDepth) {
    payload.hit = false;
    payload.tHit = tMax;
    payload.albedo = Vec3(0);
    payload.normal = Vec3(0, 1, 0);

    if (recursionDepth > config_.maxRayRecursionDepth) return;

    // Traverse TLAS
    for (u32 i = 0; i < instances_.size(); i++) {
        const BLASInstance& inst = instances_[i];
        if (!inst.enabled && inst.hitGroupIndex == 0) continue;

        // Test against instance AABB
        Vec3 bmin, bmax;
        computeInstanceBounds(inst, bmin, bmax);

        f32 tEntry, tExit;
        if (!testRayAABB(origin, dir, bmin, bmax, tEntry, tExit)) continue;
        if (tEntry > tMax || tExit < tMin) continue;

        // Test against BLAS triangles
        if (inst.blasIndex < blasList_.size()) {
            const BLASEntry& blas = blasList_[inst.blasIndex];
            for (auto& geom : blas.geometries) {
                u32 triCount = geom.indexCount / 3;
                for (u32 t = 0; t < triCount; t++) {
                    Vec3 v0 = (geom.boundsMin + geom.boundsMax) * 0.5f;
                    Vec3 v1 = v0 + Vec3(0.1f, 0, 0);
                    Vec3 v2 = v0 + Vec3(0, 0.1f, 0);

                    f32 triT, u, v;
                    if (testRayTriangle(origin, dir, v0, v1, v2, triT, u, v)) {
                        if (triT > tMin && triT < payload.tHit) {
                            payload.tHit = triT;
                            payload.hit = true;
                            payload.position = origin + dir * triT;
                            payload.normal = (v1 - v0).cross(v2 - v0).normalized();
                            payload.instanceId = inst.instanceId;
                            payload.materialIndex = inst.materialIndex;
                            payload.albedo = Vec3(0.8f, 0.2f, 0.2f);

                            // Any-hit shader evaluation
                            if (!evaluateAnyHit(payload)) {
                                payload.hit = false;
                            }
                        }
                    }
                }
            }
        }
    }

    if (payload.hit) {
        evaluateClosestHit(payload, payload.albedo);
    } else {
        payload.albedo = evaluateMiss(dir);
    }
}

void RayTracingSystem::traceShadowRay(const Vec3& origin, const Vec3& dir, f32 tMin, f32 tMax,
                                        ShadowPayload& payload) {
    payload.hit = false;
    payload.tHit = tMax;
    payload.transparent = false;
    payload.opacity = 1.0f;

    for (u32 i = 0; i < instances_.size(); i++) {
        const BLASInstance& inst = instances_[i];
        Vec3 bmin, bmax;
        computeInstanceBounds(inst, bmin, bmax);
        f32 tEntry, tExit;
        if (!testRayAABB(origin, dir, bmin, bmax, tEntry, tExit)) continue;
        if (tEntry > tMax || tExit < tMin) continue;

        if (inst.blasIndex < blasList_.size()) {
            const BLASEntry& blas = blasList_[inst.blasIndex];
            for (auto& geom : blas.geometries) {
                Vec3 v0 = (geom.boundsMin + geom.boundsMax) * 0.5f;
                Vec3 v1 = v0 + Vec3(0.1f, 0, 0);
                Vec3 v2 = v0 + Vec3(0, 0.1f, 0);
                f32 t, u, v;
                if (testRayTriangle(origin, dir, v0, v1, v2, t, u, v)) {
                    if (t > tMin && t < payload.tHit) {
                        payload.tHit = t;
                        payload.hit = true;
                        return;
                    }
                }
            }
        }
    }
}

// ============================================================================
// Material Evaluation
// ============================================================================
void RayTracingSystem::evaluateClosestHit(const RayPayload& payload, Vec3& color) {
    Vec3 albedo, emission;
    f32 metallic, roughness;
    evaluateMaterial(payload, (cameraPosition_ - payload.position).normalized(),
                     albedo, metallic, roughness);

    // Direct lighting
    Vec3 lightDir = Vec3(0.5f, -0.8f, -0.3f).normalized();
    Vec3 lightColor = Vec3(1.0f, 0.96f, 0.9f) * 3.0f;

    Vec3 N = payload.normal;
    Vec3 V = (cameraPosition_ - payload.position).normalized();
    Vec3 H = (V + lightDir).normalized();

    f32 NdotL = Mathf::max(N.dot(lightDir), 0.0f);
    f32 NdotV = Mathf::max(N.dot(V), 0.0f);
    f32 NdotH = Mathf::max(N.dot(H), 0.0f);
    f32 HdotV = Mathf::max(H.dot(V), 0.0f);
    Vec3 F0 = Vec3(0.04f) + (albedo - Vec3(0.04f)) * metallic;

    Vec3 F = fresnelSchlick(HdotV, F0);

    f32 D = distributionGGX(N, H, roughness);
    f32 G = geometrySmith(N, V, lightDir, roughness);

    Vec3 numerator = F * (D * G);
    f32 denominator = 4.0f * NdotV * NdotL + 0.0001f;
    Vec3 specular = numerator / denominator;

    Vec3 kS = F;
    Vec3 kD = (Vec3(1) - kS) * (1.0f - metallic);

    color = (kD * albedo / Mathf::PI + specular) * lightColor * NdotL;
}

void RayTracingSystem::evaluateMaterial(const RayPayload& payload, const Vec3& viewDir,
                                          Vec3& albedo, f32& metallic, f32& roughness) {
    albedo = payload.albedo;
    metallic = payload.metallic;
    roughness = payload.roughness;
    if (roughness < 0.04f) roughness = 0.04f;
}

Vec3 RayTracingSystem::evaluateMiss(const Vec3& direction) {
    f32 skyFactor = Mathf::saturate(direction.y * 0.5f + 0.5f);
    Vec3 skyColor = Vec3(0.4f, 0.6f, 0.9f) * skyFactor + Vec3(0.1f, 0.15f, 0.3f) * (1.0f - skyFactor);
    return skyColor;
}

bool RayTracingSystem::evaluateAnyHit(const RayPayload& payload) {
    return true;
}

// ============================================================================
// Ray-Triangle / Ray-AABB / Ray-Sphere Intersection
// ============================================================================
bool RayTracingSystem::testRayTriangle(const Vec3& origin, const Vec3& dir,
                                         const Vec3& v0, const Vec3& v1, const Vec3& v2,
                                         f32& t, f32& u, f32& v) {
    Vec3 edge1 = v1 - v0;
    Vec3 edge2 = v2 - v0;
    Vec3 h = dir.cross(edge2);
    f32 a = edge1.dot(h);
    if (a > -0.00001f && a < 0.00001f) return false;
    f32 invA = 1.0f / a;
    Vec3 s = origin - v0;
    u = s.dot(h) * invA;
    if (u < 0.0f || u > 1.0f) return false;
    Vec3 q = s.cross(edge1);
    v = dir.dot(q) * invA;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = edge2.dot(q) * invA;
    return t > 0.00001f;
}

bool RayTracingSystem::testRayAABB(const Vec3& origin, const Vec3& dir,
                                     const Vec3& boundsMin, const Vec3& boundsMax,
                                     f32& tMin, f32& tMax) {
    tMin = -1e30f;
    tMax = 1e30f;
    for (int i = 0; i < 3; i++) {
        f32 o = (i == 0) ? origin.x : (i == 1) ? origin.y : origin.z;
        f32 d = (i == 0) ? dir.x : (i == 1) ? dir.y : dir.z;
        f32 bmin = (i == 0) ? boundsMin.x : (i == 1) ? boundsMin.y : boundsMin.z;
        f32 bmax = (i == 0) ? boundsMax.x : (i == 1) ? boundsMax.y : boundsMax.z;
        if (std::abs(d) < 0.00001f) {
            if (o < bmin || o > bmax) return false;
        } else {
            f32 invD = 1.0f / d;
            f32 t1 = (bmin - o) * invD;
            f32 t2 = (bmax - o) * invD;
            if (t1 > t2) { f32 tmp = t1; t1 = t2; t2 = tmp; }
            tMin = std::fmax(tMin, t1);
            tMax = std::fmin(tMax, t2);
            if (tMin > tMax) return false;
        }
    }
    return true;
}

bool RayTracingSystem::testRaySphere(const Vec3& origin, const Vec3& dir,
                                       const Vec3& center, f32 radius, f32& t) {
    Vec3 oc = origin - center;
    f32 a = dir.dot(dir);
    f32 b = 2.0f * oc.dot(dir);
    f32 c = oc.dot(oc) - radius * radius;
    f32 discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0) return false;
    t = (-b - std::sqrt(discriminant)) / (2.0f * a);
    if (t < 0) t = (-b + std::sqrt(discriminant)) / (2.0f * a);
    return t >= 0;
}

// ============================================================================
// PBR Functions
// ============================================================================
Vec3 RayTracingSystem::importanceSampleGGX(const Vec2& Xi, f32 roughness, const Vec3& N) {
    f32 a = roughness * roughness;
    f32 a2 = a * a;
    f32 phi = Xi.x * Mathf::TWO_PI;
    f32 cosTheta = std::sqrt((1.0f - Xi.y) / (1.0f + (a2 - 1.0f) * Xi.y));
    f32 sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
    Vec3 up = std::abs(N.z) < 0.999f ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
    Vec3 tangent = up.cross(N).normalized();
    Vec3 bitangent = N.cross(tangent);
    return (tangent * (std::cos(phi) * sinTheta) + bitangent * (std::sin(phi) * sinTheta) + N * cosTheta).normalized();
}

f32 RayTracingSystem::distributionGGX(const Vec3& N, const Vec3& H, f32 roughness) {
    f32 a = roughness * roughness;
    f32 a2 = a * a;
    f32 NdotH = Mathf::max(N.dot(H), 0.0f);
    f32 NdotH2 = NdotH * NdotH;
    f32 denom = NdotH2 * (a2 - 1.0f) + 1.0f;
    return a2 / (Mathf::PI * denom * denom + 0.0001f);
}

f32 RayTracingSystem::geometrySmith(const Vec3& N, const Vec3& V, const Vec3& L, f32 roughness) {
    return geometrySchlickGGX(Mathf::max(N.dot(V), 0.0f), roughness) *
           geometrySchlickGGX(Mathf::max(N.dot(L), 0.0f), roughness);
}

f32 RayTracingSystem::geometrySchlickGGX(f32 NdotV, f32 roughness) {
    f32 r = (roughness + 1.0f);
    f32 k = (r * r) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

Vec3 RayTracingSystem::fresnelSchlick(f32 cosTheta, const Vec3& F0) {
    return F0 + (Vec3(1.0f) - F0) * std::pow(Mathf::clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

Vec3 RayTracingSystem::fresnelSchlickRoughness(f32 cosTheta, const Vec3& F0, f32 roughness) {
    return F0 + (Vec3(1.0f) - F0) * std::pow(Mathf::clamp(1.0f - cosTheta, 0.0f, 1.0f),
                                               std::max((1.0f - roughness) * 10.0f, 0.0f));
}

// ============================================================================
// Denoising
// ============================================================================
void RayTracingSystem::temporalAccumulation() {
    u32 pixelCount = screenWidth_ * screenHeight_;
    if (historyColor_.size() != pixelCount) return;

    for (u32 i = 0; i < pixelCount && i < reflectionOutput_.size(); i++) {
        Vec3 current = reflectionOutput_[i];
        Vec3 history = historyColor_[i];
        f32 blendWeight = config_.denoise.temporalBlendWeight;
        denoisedOutput_[i] = history * blendWeight + current * (1.0f - blendWeight);
        historyColor_[i] = denoisedOutput_[i];
    }
}

void RayTracingSystem::spatialBlur() {
    if (!config_.denoise.useATrous) {
        u32 pixelCount = screenWidth_ * screenHeight_;
        for (u32 iter = 0; iter < config_.denoise.atrousIterations; iter++) {
            atrousWavelet(reinterpret_cast<const f32*>(denoisedOutput_.data()),
                          reinterpret_cast<f32*>(denoisedOutput_.data()),
                          screenWidth_, screenHeight_,
                          config_.denoise.atrousSigma, iter);
        }
    }
}

void RayTracingSystem::atrousWavelet(const f32* input, f32* output,
                                       u32 width, u32 height,
                                       f32 sigma, u32 iteration) {
    i32 radius = 2;
    f32 kernel[5] = { 1.0f / 16.0f, 4.0f / 16.0f, 6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f };
    f32 step = (f32)(1 << iteration);

    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            Vec3 sum = Vec3(0);
            f32 weightSum = 0;
            for (i32 ky = -radius; ky <= radius; ky++) {
                for (i32 kx = -radius; kx <= radius; kx++) {
                    i32 sx = Mathf::max(0, Mathf::min((i32)(x + kx * step), (i32)width - 1));
                    i32 sy = Mathf::max(0, Mathf::min((i32)(y + ky * step), (i32)height - 1));
                    u32 sIdx = sy * width + sx;
                    u32 kIdx = (u32)(ky + radius) * 5 + (u32)(kx + radius);
                    f32 w = kernel[kIdx < 5 ? kIdx : 0];
                    if (kIdx / 5 < 5 && kIdx % 5 < 5) {
                        w = kernel[(u32)(ky + radius)] * kernel[(u32)(kx + radius)];
                    }
                    Vec3 sampleVal = Vec3(input[sIdx * 3], input[sIdx * 3 + 1], input[sIdx * 3 + 2]);
                    sum = sum + sampleVal * w;
                    weightSum += w;
                }
            }
            if (weightSum > 0) sum = sum / weightSum;
            u32 idx = y * width + x;
            output[idx * 3 + 0] = sum.x;
            output[idx * 3 + 1] = sum.y;
            output[idx * 3 + 2] = sum.z;
        }
    }
}

void RayTracingSystem::varianceClipping(const f32* current, const f32* history,
                                          const f32* motionVec, u32 width, u32 height) {
    // Temporal variance clipping for stability
    for (u32 y = 1; y < height - 1; y++) {
        for (u32 x = 1; x < width - 1; x++) {
            u32 idx = y * width + x;
            Vec3 mu = Vec3(0);
            Vec3 sigma = Vec3(0);
            u32 count = 0;
            for (i32 ky = -1; ky <= 1; ky++) {
                for (i32 kx = -1; kx <= 1; kx++) {
                    u32 nIdx = (y + ky) * width + (x + kx);
                    mu = mu + Vec3(current[nIdx * 3], current[nIdx * 3 + 1], current[nIdx * 3 + 2]);
                    count++;
                }
            }
            mu = mu / (f32)count;
            for (i32 ky = -1; ky <= 1; ky++) {
                for (i32 kx = -1; kx <= 1; kx++) {
                    u32 nIdx = (y + ky) * width + (x + kx);
                    Vec3 diff = Vec3(current[nIdx * 3], current[nIdx * 3 + 1], current[nIdx * 3 + 2]) - mu;
                    sigma = sigma + Vec3(diff.x * diff.x, diff.y * diff.y, diff.z * diff.z);
                }
            }
            sigma = sigma / (f32)count;
            Vec3 minBound = mu - Vec3(std::sqrt(sigma.x), std::sqrt(sigma.y), std::sqrt(sigma.z)) * 1.0f;
            Vec3 maxBound = mu + Vec3(std::sqrt(sigma.x), std::sqrt(sigma.y), std::sqrt(sigma.z)) * 1.0f;
            Vec3 hist = Vec3(history[idx * 3], history[idx * 3 + 1], history[idx * 3 + 2]);
            hist.x = Mathf::clamp(hist.x, minBound.x, maxBound.x);
            hist.y = Mathf::clamp(hist.y, minBound.y, maxBound.y);
            hist.z = Mathf::clamp(hist.z, minBound.z, maxBound.z);
        }
    }
}

// ============================================================================
// Random Number Generation
// ============================================================================
Vec2 RayTracingSystem::hammersley(u32 i, u32 N) {
    u32 bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    f32 radicalInverse = (f32)bits * 2.3283064365386963e-10f;
    return Vec2((f32)i / (f32)N, radicalInverse);
}

Vec3 RayTracingSystem::cosineWeightedHemisphere(const Vec2& Xi, const Vec3& N) {
    f32 phi = Xi.x * Mathf::TWO_PI;
    f32 cosTheta = std::sqrt(1.0f - Xi.y);
    f32 sinTheta = std::sqrt(Xi.y);
    Vec3 up = std::abs(N.z) < 0.999f ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
    Vec3 tangent = up.cross(N).normalized();
    Vec3 bitangent = N.cross(tangent);
    return (tangent * (std::cos(phi) * sinTheta) + bitangent * (std::sin(phi) * sinTheta) + N * cosTheta).normalized();
}

Vec3 RayTracingSystem::uniformSphereDirection(const Vec2& Xi) {
    f32 phi = 2.0f * Mathf::PI * Xi.x;
    f32 cosTheta = 1.0f - 2.0f * Xi.y;
    f32 sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
    return Vec3(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);
}

// ============================================================================
// Advanced Path Tracing
// ============================================================================
void RayTracingSystem::pathTrace(u32 width, u32 height, u32 maxBounces) {
    reflectionOutput_.resize(width * height);

    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u32 idx = y * width + x;

            // Generate camera ray
            f32 ndcX = ((f32)x + 0.5f) / (f32)width * 2.0f - 1.0f;
            f32 ndcY = 1.0f - ((f32)y + 0.5f) / (f32)height * 2.0f;

            Vec4 nearPt = Vec4(ndcX, ndcY, 0.0f, 1.0f);
            Vec4 farPt = Vec4(ndcX, ndcY, 1.0f, 1.0f);

            Vec4 nearWorld = viewProjMatrix_ * nearPt;
            Vec4 farWorld = viewProjMatrix_ * farPt;

            Vec3 origin = Vec3(nearWorld.x, nearWorld.y, nearWorld.z) / nearWorld.w;
            Vec3 direction = Vec3(farWorld.x, farWorld.y, farWorld.z) / farWorld.w - origin;
            direction = direction.normalized();

            // Path trace
            Vec3 throughput = Vec3(1.0f);
            Vec3 radiance = Vec3(0);

            for (u32 bounce = 0; bounce < maxBounces; bounce++) {
                RayPayload payload;
                traceRay(origin, direction, nearPlane_, farPlane_, payload, bounce);

                if (!payload.hit) {
                    radiance = radiance + throughput * evaluateMiss(direction);
                    break;
                }

                // Get material properties
                Vec3 albedo, emission;
                f32 metallic, roughness;
                evaluateMaterial(payload, direction, albedo, metallic, roughness);

                // Add emission
                radiance = radiance + throughput * emission;

                // Sample direct light
                Vec3 lightDir = Vec3(0.5f, -0.8f, -0.3f).normalized();
                Vec3 lightColor = Vec3(1.0f, 0.96f, 0.9f) * 3.0f;

                ShadowPayload shadow;
                traceShadowRay(payload.position + payload.normal * 0.01f,
                               lightDir, 0.001f, 100.0f, shadow);

                if (!shadow.hit) {
                    Vec3 H = ((-direction).normalized() + lightDir).normalized();
                    f32 NdotL = Mathf::max(payload.normal.dot(lightDir), 0.0f);
                    f32 NdotH = Mathf::max(payload.normal.dot(H), 0.0f);

                    Vec3 F0 = Vec3(0.04f) + (albedo - Vec3(0.04f)) * metallic;
                    Vec3 F = fresnelSchlick(NdotH, F0);
                    f32 D = distributionGGX(payload.normal, H, roughness);
                    f32 G = geometrySmith(payload.normal, (-direction).normalized(), lightDir, roughness);

                    Vec3 spec = (F * (D * G)) / (4.0f * Mathf::max(payload.normal.dot((-direction).normalized()), 0.0f) * NdotL + 0.001f);
                    Vec3 kD = (Vec3(1.0f) - F) * (1.0f - metallic);

                    radiance = radiance + throughput * (kD * albedo / Mathf::PI + spec) * lightColor * NdotL;
                }

                // Sample next direction (importance sampling)
                Vec2 Xi = hammersley(x * 1000 + y + bounce * 100000 + frameIndex_ * 7919, 100000);
                Vec3 sampleDir = importanceSampleGGX(Xi, roughness, payload.normal);

                // Update throughput
                 Vec3 F = fresnelSchlick(Mathf::max(sampleDir.dot((-direction).normalized()), 0.0f),
                                         Vec3(0.04f) + (albedo - Vec3(0.04f)) * metallic);
                Vec3 kD = (Vec3(1.0f) - F) * (1.0f - metallic);

                throughput = throughput * kD * albedo;

                // Update ray
                origin = payload.position + sampleDir * 0.01f;
                direction = sampleDir;
            }

            reflectionOutput_[idx] = radiance;
        }
    }
}

// ============================================================================
// Advanced Denoising with Feature-Aware Filtering
// ============================================================================
void RayTracingSystem::featureAwareDenoise(const Vec3* normals, const f32* depths) {
    u32 pixelCount = screenWidth_ * screenHeight_;
    if (denoisedOutput_.size() != pixelCount) return;

    // Edge-stopping spatial filter
    f32 sigmaColor = 0.1f;
    f32 sigmaNormal = 0.3f;
    f32 sigmaDepth = 0.1f;
    f32 sigmaSpatial = 2.0f;

    for (u32 y = 0; y < screenHeight_; y++) {
        for (u32 x = 0; x < screenWidth_; x++) {
            u32 idx = y * screenWidth_ + x;

            Vec3 centerNormal = normals[idx];
            f32 centerDepth = depths[idx];
            Vec3 centerColor = reflectionOutput_[idx];

            Vec3 filteredColor = Vec3(0);
            f32 totalWeight = 0;

            i32 radius = 3;
            for (i32 ky = -radius; ky <= radius; ky++) {
                for (i32 kx = -radius; kx <= radius; kx++) {
                    i32 sx = Mathf::max(0, Mathf::min(x + kx, (i32)screenWidth_ - 1));
                    i32 sy = Mathf::max(0, Mathf::min(y + ky, (i32)screenHeight_ - 1));
                    u32 sIdx = sy * screenWidth_ + sx;

                    Vec3 sampleNormal = normals[sIdx];
                    f32 sampleDepth = depths[sIdx];
                    Vec3 sampleColor = reflectionOutput_[sIdx];

                    // Spatial weight
                    f32 spatialDist = std::sqrt((f32)(kx * kx + ky * ky));
                    f32 spatialWeight = std::exp(-spatialDist * spatialDist / (2.0f * sigmaSpatial * sigmaSpatial));

                    // Normal weight
                    f32 normalDot = centerNormal.dot(sampleNormal);
                    f32 normalWeight = std::pow(Mathf::max(normalDot, 0.0f), sigmaNormal * 10.0f);

                    // Depth weight
                    f32 depthDiff = std::abs(centerDepth - sampleDepth) / (centerDepth + 0.001f);
                    f32 depthWeight = std::exp(-depthDiff * depthDiff / (2.0f * sigmaDepth * sigmaDepth));

                    // Color weight (for edge preservation)
                    f32 colorDiff = (centerColor - sampleColor).length();
                    f32 colorWeight = std::exp(-colorDiff * colorDiff / (2.0f * sigmaColor * sigmaColor));

                    f32 weight = spatialWeight * normalWeight * depthWeight * colorWeight;

                    filteredColor = filteredColor + sampleColor * weight;
                    totalWeight += weight;
                }
            }

            if (totalWeight > 0) {
                denoisedOutput_[idx] = filteredColor / totalWeight;
            } else {
                denoisedOutput_[idx] = centerColor;
            }
        }
    }
}

// ============================================================================
// Progressive Path Tracing for convergence
// ============================================================================
void RayTracingSystem::progressivePathTrace(u32 width, u32 height, u32 sampleIndex) {
    u32 pixelCount = width * height;
    if (reflectionOutput_.size() != pixelCount) {
        reflectionOutput_.resize(pixelCount);
        for (auto& c : reflectionOutput_) c = Vec3(0);
    }

    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u32 idx = y * width + x;

            // Jittered camera ray
            f32 jitterX = hammersley(sampleIndex, 10000).x;
            f32 jitterY = hammersley(sampleIndex, 10000).y;

            f32 ndcX = ((f32)x + jitterX) / (f32)width * 2.0f - 1.0f;
            f32 ndcY = 1.0f - ((f32)y + jitterY) / (f32)height * 2.0f;

            Vec4 nearPt = Vec4(ndcX, ndcY, 0.0f, 1.0f);
            Vec4 farPt = Vec4(ndcX, ndcY, 1.0f, 1.0f);

            Vec4 nearWorld = viewProjMatrix_ * nearPt;
            Vec4 farWorld = viewProjMatrix_ * farPt;

            Vec3 origin = Vec3(nearWorld.x, nearWorld.y, nearWorld.z) / nearWorld.w;
            Vec3 direction = Vec3(farWorld.x, farWorld.y, farWorld.z) / farWorld.w - origin;
            direction = direction.normalized();

            // One-sample path trace
            Vec3 throughput = Vec3(1.0f);
            Vec3 radiance = Vec3(0);

            for (u32 bounce = 0; bounce < 4; bounce++) {
                RayPayload payload;
                traceRay(origin, direction, nearPlane_, farPlane_, payload, bounce);

                if (!payload.hit) {
                    radiance = radiance + throughput * evaluateMiss(direction);
                    break;
                }

                Vec3 albedo, emission;
                f32 metallic, roughness;
                evaluateMaterial(payload, direction, albedo, metallic, roughness);

                radiance = radiance + throughput * emission;

                // Direct light
                Vec3 lightDir = Vec3(0.5f, -0.8f, -0.3f).normalized();
                Vec3 lightColor = Vec3(1.0f, 0.96f, 0.9f) * 3.0f;

                ShadowPayload shadow;
                traceShadowRay(payload.position + payload.normal * 0.01f,
                               lightDir, 0.001f, 100.0f, shadow);

                if (!shadow.hit) {
                    f32 NdotL = Mathf::max(payload.normal.dot(lightDir), 0.0f);
                    radiance = radiance + throughput * albedo * lightColor * NdotL / Mathf::PI;
                }

                // Russian roulette
                f32 survivalProb = Mathf::max(Mathf::max(throughput.x, throughput.y), throughput.z);
                if (bounce > 0 && hammersley(sampleIndex + bounce * 1000, 10000).x > survivalProb) {
                    break;
                }
                throughput = throughput / survivalProb;

                // Next direction
                Vec2 Xi = hammersley(sampleIndex + bounce * 10000, 100000);
                Vec3 sampleDir = cosineWeightedHemisphere(Xi, payload.normal);

                origin = payload.position + sampleDir * 0.01f;
                direction = sampleDir;
            }

            // Progressive accumulation
            f32 weight = 1.0f / (f32)(sampleIndex + 1);
            reflectionOutput_[idx] = reflectionOutput_[idx] * (1.0f - weight) + radiance * weight;
        }
    }
}

// ============================================================================
// Performance optimization: adaptive ray budget
// ============================================================================
void RayTracingSystem::adaptiveRayBudget(u32 width, u32 height, f32 targetFrameTimeMs) {
    // Compute per-pixel ray budget based on importance
    f32 totalBudget = (f32)width * (f32)height * 1.0f; // 1 ray per pixel base

    // Reduce rays in areas with low variance
    for (u32 y = 0; y < height; y += 4) {
        for (u32 x = 0; x < width; x += 4) {
            u32 idx = y * width + x;

            // Check local variance
            f32 variance = 0.0f;
            Vec3 mean = reflectionOutput_[idx];
            u32 sampleCount = 0;

            for (i32 ky = -1; ky <= 1; ky++) {
                for (i32 kx = -1; kx <= 1; kx++) {
                    i32 sx = Mathf::max(0, Mathf::min(x + kx * 4, (i32)width - 1));
                    i32 sy = Mathf::max(0, Mathf::min(y + ky * 4, (i32)height - 1));
                    u32 sIdx = sy * width + sx;

                    Vec3 diff = reflectionOutput_[sIdx] - mean;
                    variance += diff.dot(diff);
                    sampleCount++;
                }
            }

            variance /= (f32)sampleCount;

            // Low variance = fewer rays needed
            if (variance < 0.01f) {
                totalBudget -= 0.5f; // Reduce budget in smooth areas
            }
        }
    }
}

// ============================================================================
// Shader binding table management
// ============================================================================
void RayTracingSystem::buildShaderBindingTable() {
    // Build the shader binding table that maps ray types to shaders
    // In a real implementation, this would create GPU buffers

    sbtEntries_.clear();

    // Ray generation
    SBTEntry rayGenEntry;
    rayGenEntry.shaderIndex = rayGenShaderIndex_;
    rayGenEntry.offset = 0;
    rayGenEntry.stride = 1;
    sbtEntries_.push_back(rayGenEntry);

    // Miss shaders
    for (u32 i = 0; i < missShaders_.size(); i++) {
        SBTEntry missEntry;
        missEntry.shaderIndex = i;
        missEntry.offset = i;
        missEntry.stride = 1;
        sbtEntries_.push_back(missEntry);
    }

    // Hit groups
    for (u32 i = 0; i < hitGroups_.size(); i++) {
        SBTEntry hitEntry;
        hitEntry.shaderIndex = i;
        hitEntry.offset = i;
        hitEntry.stride = 1;
        sbtEntries_.push_back(hitEntry);
    }
}

void RayTracingSystem::dispatchRaysFromSBT(u32 width, u32 height) {
    // Dispatch rays using the shader binding table
    // In a real implementation, this would call vkCmdTraceRaysKHR or similar

    // For CPU implementation, we trace rays manually
    dispatchRayGeneration(width, height);
}

// ============================================================================
// Software BVH Construction and Traversal
// ============================================================================
void RayTracingSystem::buildBVH(const Vector<Vec3>& triMin, const Vector<Vec3>& triMax, u32 leafCount) {
    bvhNodes_.clear();
    bvhPrimitiveIndices_.clear();

    u32 triCount = static_cast<u32>(triMin.size());
    if (triCount == 0) return;

    bvhPrimitiveIndices_.resize(triCount);
    for (u32 i = 0; i < triCount; i++) bvhPrimitiveIndices_[i] = i;

    Vector<Vec3> localMin(triMin.begin(), triMin.end());
    Vector<Vec3> localMax(triMax.begin(), triMax.end());

    struct BuildTask {
        u32 primStart;
        u32 primEnd;
        u32 depth;
    };

    Vector<BuildTask> stack;
    stack.push_back({0, triCount, 0});

    while (!stack.empty()) {
        BuildTask task = stack.back();
        stack.pop_back();

        u32 start = task.primStart;
        u32 end = task.primEnd;
        u32 count = end - start;
        if (count == 0) continue;

        BVHNode node;
        node.boundsMin = localMin[bvhPrimitiveIndices_[start]];
        node.boundsMax = localMax[bvhPrimitiveIndices_[start]];
        for (u32 i = start + 1; i < end; i++) {
            node.boundsMin = node.boundsMin.min(localMin[bvhPrimitiveIndices_[i]]);
            node.boundsMax = node.boundsMax.max(localMax[bvhPrimitiveIndices_[i]]);
        }

        if (count <= leafCount || task.depth >= 128) {
            node.isLeaf = true;
            node.leftFirst = start;
            node.primitiveCount = count;
            bvhNodes_.push_back(node);
            continue;
        }

        Vec3 extent = node.boundsMax - node.boundsMin;
        u32 axis = 0;
        if (extent.y > extent.x && extent.y > extent.z) axis = 1;
        else if (extent.z > extent.x && extent.z > extent.y) axis = 2;

        u32 mid = start + count / 2;

        struct CentroidRef {
            u32 index;
            f32 centroid;
        };

        u32 rangeCount = count;
        Vector<CentroidRef> refs;
        refs.resize(rangeCount);
        for (u32 i = 0; i < rangeCount; i++) {
            refs[i].index = bvhPrimitiveIndices_[start + i];
            Vec3 c = (localMin[refs[i].index] + localMax[refs[i].index]) * 0.5f;
            refs[i].centroid = (axis == 0) ? c.x : (axis == 1) ? c.y : c.z;
        }

        std::sort(refs.begin(), refs.begin() + rangeCount,
                  [](const CentroidRef& a, const CentroidRef& b) { return a.centroid < b.centroid; });

        for (u32 i = 0; i < rangeCount; i++) {
            bvhPrimitiveIndices_[start + i] = refs[i].index;
        }

        node.isLeaf = false;
        node.leftFirst = static_cast<u32>(bvhNodes_.size());
        node.primitiveCount = 0;
        bvhNodes_.push_back(node);

        stack.push_back({mid, end, task.depth + 1});
        stack.push_back({start, mid, task.depth + 1});
    }
}

bool RayTracingSystem::intersectsAABB(const Vec3& origin, const Vec3& invDir, const Vec3& bmin, const Vec3& bmax, f32& tNear, f32& tFar) const {
    f32 t1 = (bmin.x - origin.x) * invDir.x;
    f32 t2 = (bmax.x - origin.x) * invDir.x;
    tNear = Mathf::min(t1, t2);
    tFar = Mathf::max(t1, t2);

    f32 t3 = (bmin.y - origin.y) * invDir.y;
    f32 t4 = (bmax.y - origin.y) * invDir.y;
    tNear = Mathf::max(tNear, Mathf::min(t3, t4));
    tFar = Mathf::min(tFar, Mathf::max(t3, t4));

    f32 t5 = (bmin.z - origin.z) * invDir.z;
    f32 t6 = (bmax.z - origin.z) * invDir.z;
    tNear = Mathf::max(tNear, Mathf::min(t5, t6));
    tFar = Mathf::min(tFar, Mathf::max(t5, t6));

    return tNear <= tFar && tFar >= 0.0f;
}

bool RayTracingSystem::intersectTriangle(const Vec3& o, const Vec3& d, const Vec3& v0, const Vec3& v1, const Vec3& v2, f32& t, f32& u, f32& v) const {
    Vec3 edge1 = v1 - v0;
    Vec3 edge2 = v2 - v0;
    Vec3 h = d.cross(edge2);
    f32 a = edge1.dot(h);
    if (a > -0.00001f && a < 0.00001f) return false;
    f32 invA = 1.0f / a;
    Vec3 s = o - v0;
    u = s.dot(h) * invA;
    if (u < 0.0f || u > 1.0f) return false;
    Vec3 q = s.cross(edge1);
    v = d.dot(q) * invA;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = edge2.dot(q) * invA;
    return t > 0.00001f;
}

bool RayTracingSystem::traceBVH(const Vec3& origin, const Vec3& dir, f32 tMin, f32 tMax,
                                  const Vector<Vec3>& triVerts, const Vector<u32>& triIndices,
                                  Vec3& hitPos, Vec3& hitNormal, f32& hitT) const {
    if (bvhNodes_.empty()) return false;

    Vec3 invDir(1.0f / dir.x, 1.0f / dir.y, 1.0f / dir.z);

    hitT = tMax;
    bool hit = false;

    struct StackEntry {
        u32 nodeIndex;
    };

    static constexpr u32 kMaxStack = 128;
    StackEntry stack[kMaxStack];
    u32 stackSize = 0;

    stack[stackSize++] = {0};

    while (stackSize > 0) {
        u32 nodeIdx = stack[--stackSize].nodeIndex;
        const BVHNode& node = bvhNodes_[nodeIdx];

        f32 tNear, tFar;
        if (!intersectsAABB(origin, invDir, node.boundsMin, node.boundsMax, tNear, tFar)) continue;
        if (tNear > hitT) continue;

        if (node.isLeaf) {
            for (u32 i = 0; i < node.primitiveCount; i++) {
                u32 primIdx = bvhPrimitiveIndices_[node.leftFirst + i];
                u32 i0 = triIndices[primIdx * 3 + 0];
                u32 i1 = triIndices[primIdx * 3 + 1];
                u32 i2 = triIndices[primIdx * 3 + 2];

                f32 t, u, v;
                if (intersectTriangle(origin, dir, triVerts[i0], triVerts[i1], triVerts[i2], t, u, v)) {
                    if (t > tMin && t < hitT) {
                        hitT = t;
                        hitPos = origin + dir * t;
                        Vec3 edge1 = triVerts[i1] - triVerts[i0];
                        Vec3 edge2 = triVerts[i2] - triVerts[i0];
                        hitNormal = edge1.cross(edge2).normalized();
                        hit = true;
                    }
                }
            }
        } else {
            u32 left = node.leftFirst;
            u32 right = node.leftFirst + 1;
            if (stackSize < kMaxStack - 1) {
                f32 tNearL, tFarL, tNearR, tFarR;
                bool hitL = intersectsAABB(origin, invDir, bvhNodes_[left].boundsMin, bvhNodes_[left].boundsMax, tNearL, tFarL);
                bool hitR = intersectsAABB(origin, invDir, bvhNodes_[right].boundsMin, bvhNodes_[right].boundsMax, tNearR, tFarR);
                if (hitL && hitR) {
                    if (tNearL < tNearR) {
                        stack[stackSize++] = {right};
                        stack[stackSize++] = {left};
                    } else {
                        stack[stackSize++] = {left};
                        stack[stackSize++] = {right};
                    }
                } else if (hitL) {
                    stack[stackSize++] = {left};
                } else if (hitR) {
                    stack[stackSize++] = {right};
                }
            } else if (stackSize < kMaxStack) {
                stack[stackSize++] = {left};
            }
        }
    }

    return hit;
}

void RayTracingSystem::rebuildStats() {
    stats_.bvhNodes = static_cast<u32>(bvhNodes_.size());
    stats_.bvhLeaves = 0;
    for (u32 i = 0; i < bvhNodes_.size(); i++) {
        if (bvhNodes_[i].isLeaf) stats_.bvhLeaves++;
    }
}

} // namespace Frost
