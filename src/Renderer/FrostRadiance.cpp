// ============================================================================
// FrostEngine FrostRadiance — Surfel-Based Global Illumination
// ============================================================================
// Proprietary surfel radiance system. Uses surface element accumulation
// with hierarchical octree gathering for multi-bounce indirect lighting.
// Fundamentally different from Lumen's screen-trace + distance-field approach.
// ============================================================================

#include "FrostEngine/Renderer/FrostRadiance.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <random>
#include <numeric>

namespace Frost {

// ============================================================================
// Construction / Destruction
// ============================================================================

FrostRadiance::FrostRadiance()
    : quality_(SurfQuality::Medium), maxSurfels_(200000), baseSurfelRadius_(0.1f),
      bounceWeight_(0.65f), temporalBlendFactor_(0.85f), splatRadiusFactor_(2.0f),
      nodeCount_(0), initialized_(false), lastUpdateTimeMs_(0), frameNumber_(0),
      bounceCount_(2), irradianceResolution_(16), probesUpdatedThisFrame_(0),
      probesUpdated_(0), irradianceQueries_(0), cacheUpdateMs_(0) {
}

FrostRadiance::~FrostRadiance() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool FrostRadiance::init(SurfQuality quality) {
    quality_ = quality;
    maxSurfels_ = targetSurfelCount();

    pool_.init(maxSurfels_);

    octree_.resize(MAX_OCTREE_NODES);
    nodeCount_ = 0;

    irrVolume_.init(Vec3(-50, -5, -50), Vec3(100, 30, 100), IRRI_VOLUME_RES);

    prevScreenPositions_.resize(maxSurfels_);
    prevDepthBuffer_.resize(1024 * 1024);

    // Initialize irradiance volume
    u32 irrSize = irradianceResolution_ * irradianceResolution_ * irradianceResolution_;
    irradianceVolume_.resize(irrSize);
    for (auto& v : irradianceVolume_) v = Vec3(0);

    frameNumber_ = 0;
    bounceCount_ = 2;
    initialized_ = true;

    return true;
}

void FrostRadiance::shutdown() {
    pool_.surfels.clear();
    octree_.clear();
    prevScreenPositions_.clear();
    prevDepthBuffer_.clear();
    probeCache_.clear();
    irradianceVolume_.clear();
    surfelToProbe_.clear();
    initialized_ = false;
}

void FrostRadiance::reset() {
    pool_.activeCount = 0;
    pool_.freeListHead = 0;
    nodeCount_ = 0;
    frameNumber_ = 0;
    probesUpdated_ = 0;
    irradianceQueries_ = 0;
    cacheUpdateMs_ = 0;
}

// ============================================================================
// Quality
// ============================================================================

void FrostRadiance::setQuality(SurfQuality quality) {
    quality_ = quality;
    maxSurfels_ = targetSurfelCount();
    pool_.init(maxSurfels_);
}

u32 FrostRadiance::targetSurfelCount() const {
    switch (quality_) {
        case SurfQuality::Low:    return 50000;
        case SurfQuality::Medium: return 200000;
        case SurfQuality::High:   return 500000;
        case SurfQuality::Epic:   return 1024 * 1024;
        default: return 200000;
    }
}

// ============================================================================
// Main Update
// ============================================================================

void FrostRadiance::update(f32 deltaTime, const Mat4& viewProj, u32 screenW, u32 screenH) {
    if (!initialized_) return;

    updateDirtySurfels();
    evictOldSurfels(300.0f);
    buildOctree();

    if (frameNumber_ > 0) {
        reproject(viewProj, screenW, screenH);
    }

    computeBounces(bounceCount_);
    updateIrradianceVolume();

    frameNumber_++;
}

// ============================================================================
// Surfel Generation — Detailed Mesh Processing
// ============================================================================

void FrostRadiance::injectSurfels(const SurfelMeshData& mesh) {
    generateSurfelsForMesh(mesh);
}

void FrostRadiance::injectSurfelsFromMeshes(const SurfelMeshData* meshes, u32 count) {
    for (u32 i = 0; i < count; i++) {
        generateSurfelsForMesh(meshes[i]);
    }
}

void FrostRadiance::generateSurfelsForMesh(const SurfelMeshData& mesh) {
    if (mesh.indices.size() < 3) return;

    u32 triCount = (u32)mesh.indices.size() / 3;

    // Compute total surface area for proportional surfel density
    f32 totalArea = 0.0f;
    Vector<f32> triAreas;
    triAreas.resize(triCount);

    for (u32 t = 0; t < triCount; t++) {
        u32 i0 = mesh.indices[t * 3 + 0];
        u32 i1 = mesh.indices[t * 3 + 1];
        u32 i2 = mesh.indices[t * 3 + 2];

        Vec3 a = mesh.positions[i0];
        Vec3 b = mesh.positions[i1];
        Vec3 c = mesh.positions[i2];

        f32 area = computeTriangleArea(a, b, c);
        triAreas[t] = area;
        totalArea += area;
    }

    if (totalArea < 0.0001f) return;

    f32 targetDensity = (f32)maxSurfels_ / totalArea;
    f32 baseRadius = sqrtf(totalArea / (f32)maxSurfels_) * splatRadiusFactor_;

    // Stratified random sampling across triangles
    std::mt19937 rng(42 + mesh.meshID);
    std::uniform_real_distribution<f32> dist(0.0f, 1.0f);

    for (u32 t = 0; t < triCount; t++) {
        u32 i0 = mesh.indices[t * 3 + 0];
        u32 i1 = mesh.indices[t * 3 + 1];
        u32 i2 = mesh.indices[t * 3 + 2];

        Vec3 a = mesh.positions[i0];
        Vec3 b = mesh.positions[i1];
        Vec3 c = mesh.positions[i2];

        Vec3 na = mesh.normals[i0];
        Vec3 nb = mesh.normals[i1];
        Vec3 nc = mesh.normals[i2];

        Vec2 ua = mesh.uvs[i0];
        Vec2 ub = mesh.uvs[i1];
        Vec2 uc = mesh.uvs[i2];

        u32 surfelCount = (u32)(triAreas[t] * targetDensity);
        surfelCount = std::max(surfelCount, 1u);

        // Limit per-triangle surfels to avoid hot spots
        surfelCount = std::min(surfelCount, 64u);

        for (u32 s = 0; s < surfelCount; s++) {
            u32 idx = pool_.allocate();
            if (idx == 0xFFFFFFFF) return;

            // Stratified barycentric sampling
            f32 u = dist(rng);
            f32 v = dist(rng);
            if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }
            f32 w = 1.0f - u - v;

            Surfel& surfel = pool_[idx];
            surfel.position = a * w + b * u + c * v;
            surfel.normal = interpolateNormal(na, nb, nc, u, v).normalized();

            // Compute surface albedo (simplified: would sample texture in production)
            surfel.albedo = Vec3(0.8f, 0.8f, 0.8f);

            // Compute adaptive radius
            f32 curvature = computeCurvature(surfel.position, surfel.normal, mesh);
            surfel.radius = computeAdaptiveRadius(0.0f, curvature, baseRadius);

            surfel.age = 0.0f;
            surfel.triangleID = t;
            surfel.meshID = mesh.meshID;
            surfel.flags = 1;

            // Compute initial direct lighting
            surfel.radiance = evaluateDirectLight(surfel);
            surfel.flux = surfel.radiance * surfel.albedo;

            // Store previous position for reprojection
            surfel.prevWorldPos = surfel.position;
        }
    }
}

Vec3 FrostRadiance::samplePointOnTriangle(const Vec3& a, const Vec3& b, const Vec3& c,
                                          f32 u, f32 v) const {
    f32 w = 1.0f - u - v;
    return a * w + b * u + c * v;
}

Vec3 FrostRadiance::interpolateNormal(const Vec3& a, const Vec3& b, const Vec3& c,
                                      f32 u, f32 v) const {
    f32 w = 1.0f - u - v;
    Vec3 n = a * w + b * u + c * v;
    f32 l = n.length();
    return l > 0.0001f ? n / l : Vec3(0, 1, 0);
}

f32 FrostRadiance::computeTriangleArea(const Vec3& a, const Vec3& b, const Vec3& c) const {
    return (b - a).cross(c - a).length() * 0.5f;
}

f32 FrostRadiance::computeCurvature(const Vec3& pos, const Vec3& normal,
                                    const SurfelMeshData& mesh) const {
    f32 maxAngle = 0.0f;
    u32 sampleCount = std::min((u32)mesh.normals.size(), 100u);

    for (u32 i = 0; i < sampleCount; i++) {
        f32 d = (mesh.positions[i] - pos).length();
        if (d < 0.01f || d > 2.0f) continue;

        f32 cosAngle = normal.dot(mesh.normals[i]);
        cosAngle = Mathf::clamp(cosAngle, -1.0f, 1.0f);
        f32 angle = acosf(cosAngle);
        if (angle > maxAngle) maxAngle = angle;
    }

    return maxAngle;
}

f32 FrostRadiance::computeAdaptiveRadius(f32 distance, f32 curvature, f32 baseRadius) const {
    // Larger radius on flat areas, smaller on curved surfaces
    f32 curvatureFactor = 1.0f / (1.0f + curvature * 3.0f);
    f32 distanceFactor = 1.0f + distance * 0.01f;
    return baseRadius * curvatureFactor * distanceFactor;
}

// ============================================================================
// Octree Construction — Full Hierarchical Spatial Structure
// ============================================================================

void FrostRadiance::buildOctree() {
    if (pool_.activeCount == 0) {
        nodeCount_ = 0;
        return;
    }

    // Find bounds of all active surfels
    Vec3 boundsMin(1e30f);
    Vec3 boundsMax(-1e30f);
    u32 validCount = 0;

    for (u32 i = 0; i < pool_.activeCount; i++) {
        const Surfel& s = pool_[i];
        if (s.radius < 0.0001f && s.age > 0) continue;
        boundsMin = boundsMin.min(s.position);
        boundsMax = boundsMax.max(s.position);
        validCount++;
    }

    if (validCount == 0) {
        nodeCount_ = 0;
        return;
    }

    // Expand bounds to cube
    Vec3 extent = boundsMax - boundsMin;
    f32 maxExtent = std::max(extent.x, std::max(extent.y, extent.z));
    if (maxExtent < 0.001f) maxExtent = 10.0f;

    Vec3 center = (boundsMin + boundsMax) * 0.5f;
    Vec3 halfSize = Vec3(maxExtent * 0.55f);

    nodeCount_ = 1;
    SurfelOctreeNode& root = octree_[0];
    root.center = center;
    root.boundsMin = center - halfSize;
    root.boundsMax = center + halfSize;
    root.firstChild = 0xFFFFFFFF;
    root.firstSurfel = 0;
    root.surfelCount = pool_.activeCount;
    root.parentIndex = 0xFFFFFFFF;
    root.splitThreshold = 16.0f;

    buildOctreeRecursive(0, 0);
}

void FrostRadiance::buildOctreeRecursive(u32 nodeIdx, u32 depth) {
    SurfelOctreeNode& node = octree_[nodeIdx];

    if (node.surfelCount <= 8 || depth > 12) return;

    Vec3 center = (node.boundsMin + node.boundsMax) * 0.5f;

    // Count surfels per octant
    u32 childCounts[8] = {};
    for (u32 i = 0; i < node.surfelCount; i++) {
        u32 surfelIdx = node.firstSurfel + i;
        if (surfelIdx >= pool_.activeCount) continue;

        const Surfel& s = pool_[surfelIdx];
        u32 octant = 0;
        if (s.position.x >= center.x) octant |= 1;
        if (s.position.y >= center.y) octant |= 2;
        if (s.position.z >= center.z) octant |= 4;
        childCounts[octant]++;
    }

    // Create 8 children
    u32 baseChild = nodeCount_;
    nodeCount_ += 8;
    if (nodeCount_ > MAX_OCTREE_NODES) {
        nodeCount_ -= 8;
        return;
    }

    node.firstChild = baseChild;

    for (u32 oct = 0; oct < 8; oct++) {
        SurfelOctreeNode& child = octree_[baseChild + oct];
        child.boundsMin = node.boundsMin;
        child.boundsMax = node.boundsMax;
        child.parentIndex = nodeIdx;
        child.surfelCount = childCounts[oct];
        child.firstChild = 0xFFFFFFFF;
        child.firstSurfel = 0;
        child.splitThreshold = node.splitThreshold * 0.5f;

        if (oct & 1) child.boundsMin.x = center.x; else child.boundsMax.x = center.x;
        if (oct & 2) child.boundsMin.y = center.y; else child.boundsMax.y = center.y;
        if (oct & 4) child.boundsMin.z = center.z; else child.boundsMax.z = center.z;

        child.center = (child.boundsMin + child.boundsMax) * 0.5f;
    }

    // Partition surfels into children
    u32 offsets[8] = {};
    for (u32 i = 0; i < node.surfelCount; i++) {
        u32 surfelIdx = node.firstSurfel + i;
        if (surfelIdx >= pool_.activeCount) continue;

        const Surfel& s = pool_[surfelIdx];
        u32 octant = 0;
        if (s.position.x >= center.x) octant |= 1;
        if (s.position.y >= center.y) octant |= 2;
        if (s.position.z >= center.z) octant |= 4;

        u32 childIdx = baseChild + octant;
        if (offsets[octant] == 0 && octree_[childIdx].firstSurfel == 0) {
            octree_[childIdx].firstSurfel = surfelIdx;
        }
        offsets[octant]++;
    }

    // Recurse on populated children
    for (u32 oct = 0; oct < 8; oct++) {
        u32 childIdx = baseChild + oct;
        if (octree_[childIdx].surfelCount > 8 && depth < 11) {
            buildOctreeRecursive(childIdx, depth + 1);
        }
    }
}

void FrostRadiance::splitOctreeNode(u32 nodeIdx) {
    SurfelOctreeNode& node = octree_[nodeIdx];
    if (node.firstChild != 0xFFFFFFFF) return;

    Vec3 center = node.center;

    u32 baseChild = nodeCount_;
    nodeCount_ += 8;
    if (nodeCount_ > MAX_OCTREE_NODES) {
        nodeCount_ -= 8;
        return;
    }

    node.firstChild = baseChild;

    for (u32 oct = 0; oct < 8; oct++) {
        SurfelOctreeNode& child = octree_[baseChild + oct];
        child.boundsMin = node.boundsMin;
        child.boundsMax = node.boundsMax;
        child.parentIndex = nodeIdx;
        child.firstChild = 0xFFFFFFFF;
        child.surfelCount = 0;
        child.firstSurfel = 0;
        child.splitThreshold = node.splitThreshold * 0.5f;

        if (oct & 1) child.boundsMin.x = center.x; else child.boundsMax.x = center.x;
        if (oct & 2) child.boundsMin.y = center.y; else child.boundsMax.y = center.y;
        if (oct & 4) child.boundsMin.z = center.z; else child.boundsMax.z = center.z;

        child.center = (child.boundsMin + child.boundsMax) * 0.5f;
    }
}

// ============================================================================
// Octree Queries — Radius and Cone Gathering
// ============================================================================

void FrostRadiance::gatherSurfelsInRadius(Vec3 center, f32 radius,
                                          Vector<u32>& result) const {
    result.clear();
    if (nodeCount_ == 0) return;

    struct StackEntry { u32 nodeIdx; };
    Vector<StackEntry> stack;
    stack.push_back({0});

    while (stack.size() > 0) {
        StackEntry entry = stack[stack.size() - 1];
        stack.pop();

        const SurfelOctreeNode& node = octree_[entry.nodeIdx];

        // AABB-sphere overlap test
        Vec3 closest;
        closest.x = Mathf::clamp(center.x, node.boundsMin.x, node.boundsMax.x);
        closest.y = Mathf::clamp(center.y, node.boundsMin.y, node.boundsMax.y);
        closest.z = Mathf::clamp(center.z, node.boundsMin.z, node.boundsMax.z);

        f32 distSq = (closest - center).lengthSquared();
        if (distSq > radius * radius) continue;

        if (node.firstChild == 0xFFFFFFFF) {
            // Leaf node: test individual surfels
            for (u32 i = 0; i < node.surfelCount; i++) {
                u32 surfelIdx = node.firstSurfel + i;
                if (surfelIdx >= pool_.activeCount) continue;

                f32 d = (pool_[surfelIdx].position - center).length();
                if (d <= radius) {
                    result.push_back(surfelIdx);
                }
            }
        } else {
            // Internal node: push children
            for (u32 c = 0; c < 8; c++) {
                stack.push_back({node.firstChild + c});
            }
        }
    }
}

void FrostRadiance::gatherSurfelsInCone(Vec3 origin, Vec3 dir, f32 halfAngle,
                                         f32 maxDist, Vector<u32>& result) const {
    result.clear();
    if (nodeCount_ == 0) return;

    f32 cosHalfAngle = cosf(halfAngle);

    struct StackEntry { u32 nodeIdx; };
    Vector<StackEntry> stack;
    stack.push_back({0});

    while (stack.size() > 0) {
        StackEntry entry = stack[stack.size() - 1];
        stack.pop();

        const SurfelOctreeNode& node = octree_[entry.nodeIdx];

        Vec3 toNode = node.center - origin;
        f32 dist = toNode.length();
        if (dist > maxDist) continue;

        Vec3 toNodeDir = dist > 0.0001f ? toNode / dist : Vec3(0);
        f32 cosAngle = dir.dot(toNodeDir);
        if (cosAngle < cosHalfAngle - 0.1f) continue;

        if (node.firstChild == 0xFFFFFFFF) {
            for (u32 i = 0; i < node.surfelCount; i++) {
                u32 surfelIdx = node.firstSurfel + i;
                if (surfelIdx >= pool_.activeCount) continue;

                Vec3 toSurfel = pool_[surfelIdx].position - origin;
                f32 d = toSurfel.length();
                if (d > maxDist || d < 0.0001f) continue;

                Vec3 toSurfelDir = toSurfel / d;
                if (dir.dot(toSurfelDir) >= cosHalfAngle) {
                    result.push_back(surfelIdx);
                }
            }
        } else {
            for (u32 c = 0; c < 8; c++) {
                stack.push_back({node.firstChild + c});
            }
        }
    }
}

// ============================================================================
// Bounce Computation — Multi-Bounce Radiance Accumulation
// ============================================================================

void FrostRadiance::computeBounces(u32 bounceCount) {
    for (u32 b = 0; b < bounceCount; b++) {
        computeSingleBounce(b);
    }
}

void FrostRadiance::computeSingleBounce(u32 bounceIndex) {
    Vector<u32> neighbors;
    neighbors.reserve(256);

    for (u32 i = 0; i < pool_.activeCount; i++) {
        Surfel& surfel = pool_[i];
        if (surfel.flags == 0 && surfel.age > 1) continue;

        f32 gatherRadius = surfel.radius * 4.0f * (f32)(bounceIndex + 1);
        gatherSurfelsInRadius(surfel.position, gatherRadius, neighbors);

        if (neighbors.size() == 0) continue;

        Vec3 incomingRadiance(0);
        f32 totalWeight = 0.0f;

        for (u32 n = 0; n < neighbors.size(); n++) {
            u32 neighborIdx = neighbors[n];
            if (neighborIdx == i) continue;

            const Surfel& neighbor = pool_[neighborIdx];

            Vec3 diff = neighbor.position - surfel.position;
            f32 dist = diff.length();
            if (dist < 0.0001f || dist > gatherRadius) continue;

            // Inverse-square distance falloff
            f32 distFalloff = 1.0f / (1.0f + dist * dist);

            // Cosine-weighted geometric term
            f32 NdotL = Mathf::max(surfel.normal.dot(diff.normalized()), 0.0f);

            // Visibility approximation
            f32 visibility = 1.0f;

            // Radiance transfer weight
            f32 weight = distFalloff * NdotL * visibility;
            incomingRadiance += neighbor.flux * weight;
            totalWeight += weight;
        }

        if (totalWeight > 0.0001f) {
            incomingRadiance = incomingRadiance / totalWeight;
        }

        // Apply bounce falloff and surface albedo
        Vec3 indirectLight = incomingRadiance * surfel.albedo * bounceWeight_;

        // Accumulate with temporal blending
        surfel.radiance = surfel.radiance * (1.0f - temporalBlendFactor_) +
                          (surfel.radiance + indirectLight) * temporalBlendFactor_;
        surfel.flux = surfel.radiance * surfel.albedo;
        surfel.flags |= 1;
    }
}

Vec3 FrostRadiance::gatherRadianceAtSurfel(u32 surfelIdx, f32 gatherRadius) const {
    const Surfel& surfel = pool_[surfelIdx];
    Vector<u32> neighbors;

    gatherSurfelsInRadius(surfel.position, gatherRadius, neighbors);

    Vec3 totalRadiance(0);
    f32 totalWeight = 0.0f;

    for (u32 n = 0; n < neighbors.size(); n++) {
        if (neighbors[n] == surfelIdx) continue;
        const Surfel& neighbor = pool_[neighbors[n]];

        Vec3 diff = neighbor.position - surfel.position;
        f32 dist = diff.length();
        if (dist < 0.0001f) continue;

        f32 NdotL = Mathf::max(surfel.normal.dot(diff.normalized()), 0.0f);
        f32 weight = NdotL / (1.0f + dist * dist);

        totalRadiance += neighbor.flux * weight;
        totalWeight += weight;
    }

    return totalWeight > 0.0001f ? totalRadiance / totalWeight : Vec3(0);
}

Vec3 FrostRadiance::evaluateDirectLight(const Surfel& s) const {
    // Sun contribution
    Vec3 sunDir = Vec3(0.5f, -0.8f, -0.3f).normalized();
    Vec3 sunColor = Vec3(1.0f, 0.96f, 0.9f);
    f32 sunIntensity = 3.0f;

    f32 NdotL = Mathf::max(s.normal.dot(-sunDir), 0.0f);
    Vec3 direct = sunColor * sunIntensity * NdotL;

    // Sky ambient
    f32 skyFactor = s.normal.y * 0.5f + 0.5f;
    Vec3 skyAmbient = Vec3(0.1f, 0.15f, 0.25f) * skyFactor * 0.3f;

    return direct + skyAmbient;
}

f32 FrostRadiance::visibilityTest(Vec3 from, Vec3 to) const {
    Vec3 dir = to - from;
    f32 dist = dir.length();
    if (dist < 0.001f) return 1.0f;
    return 1.0f / (1.0f + dist * 0.01f);
}

// ============================================================================
// Surfel Reprojection — Temporal Stability Across Frames
// ============================================================================

void FrostRadiance::reproject(const Mat4& prevViewProj, u32 screenW, u32 screenH) {
    for (u32 i = 0; i < pool_.activeCount; i++) {
        Surfel& s = pool_[i];
        if (!reprojectSurfel(s, prevViewProj, screenW, screenH)) {
            s.flags |= 1;
        }
    }
}

bool FrostRadiance::reprojectSurfel(Surfel& s, const Mat4& prevViewProj,
                                    u32 screenW, u32 screenH) const {
    Vec4 prevClip = prevViewProj * Vec4(s.prevWorldPos, 1.0f);
    if (prevClip.w <= 0.0f) return false;

    f32 ndcX = prevClip.x / prevClip.w;
    f32 ndcY = prevClip.y / prevClip.w;

    if (ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f) return false;

    f32 screenX = (ndcX * 0.5f + 0.5f) * (f32)screenW;
    f32 screenY = (1.0f - (ndcY * 0.5f + 0.5f)) * (f32)screenH;

    s.prevScreenUV = Vec2(screenX / (f32)screenW, screenY / (f32)screenH);
    return true;
}

// ============================================================================
// Surfel Splatting — Screen-Space Radiance Transfer
// ============================================================================

void FrostRadiance::splatToScreen(const Mat4& viewProj, u32 screenW, u32 screenH,
                                  Vector<Vec3>& screenBuffer) const {
    screenBuffer.resize(screenW * screenH);
    for (auto& p : screenBuffer) p = Vec3(0);

    Vector<f32> depthBuffer;
    depthBuffer.resize(screenW * screenH);
    for (auto& d : depthBuffer) d = 1e30f;

    for (u32 i = 0; i < pool_.activeCount; i++) {
        const Surfel& s = pool_[i];
        if (s.flags == 0 && s.age > 2) continue;
        rasterizeSurfel(s, viewProj, screenW, screenH, screenBuffer, depthBuffer);
    }
}

void FrostRadiance::rasterizeSurfel(const Surfel& s, const Mat4& viewProj,
                                    u32 screenW, u32 screenH,
                                    Vector<Vec3>& screenBuffer,
                                    Vector<f32>& depthBuffer) const {
    Vec4 clipPos = viewProj * Vec4(s.position, 1.0f);
    if (clipPos.w <= 0.0f) return;

    f32 ndcX = clipPos.x / clipPos.w;
    f32 ndcY = clipPos.y / clipPos.w;
    f32 ndcZ = clipPos.z / clipPos.w;

    f32 screenX = (ndcX * 0.5f + 0.5f) * (f32)screenW;
    f32 screenY = (1.0f - (ndcY * 0.5f + 0.5f)) * (f32)screenH;

    f32 projRadius = s.radius * (f32)screenW / (clipPos.w * 2.0f);
    i32 pixelRadius = (i32)(projRadius + 0.5f);
    pixelRadius = std::min(pixelRadius, (i32)64);

    for (i32 dy = -pixelRadius; dy <= pixelRadius; dy++) {
        for (i32 dx = -pixelRadius; dx <= pixelRadius; dx++) {
            i32 px = (i32)screenX + dx;
            i32 py = (i32)screenY + dy;

            if (px < 0 || px >= (i32)screenW || py < 0 || py >= (i32)screenH) continue;

            f32 dist = sqrtf((f32)(dx * dx + dy * dy));
            if (dist > projRadius) continue;

            f32 t = dist / projRadius;
            f32 weight = expf(-t * t * 4.0f);

            u32 pixelIdx = (u32)(py * screenW + px);
            if (ndcZ < depthBuffer[pixelIdx]) {
                depthBuffer[pixelIdx] = ndcZ;
                screenBuffer[pixelIdx] = s.radiance * weight;
            } else {
                screenBuffer[pixelIdx] = screenBuffer[pixelIdx] + s.radiance * weight * 0.3f;
            }
        }
    }
}

// ============================================================================
// Irradiance Volume — Distant Object Lighting
// ============================================================================

void FrostRadiance::updateIrradianceVolume() {
    for (u32 i = 0; i < pool_.activeCount; i++) {
        const Surfel& s = pool_[i];
        if (s.flags == 0 && s.age > 1) continue;
        irrVolume_.write(s.position, s.flux);
    }
}

Vec3 FrostRadiance::sampleIrradiance(Vec3 worldPos) const {
    return irrVolume_.sample(worldPos);
}

// ============================================================================
// Update Helpers
// ============================================================================

void FrostRadiance::updateDirtySurfels() {
    for (u32 i = 0; i < pool_.activeCount; i++) {
        Surfel& s = pool_[i];
        if (s.flags & 1) {
            s.prevWorldPos = s.position;
            s.age += 1.0f;
            s.flags &= ~1;
        }
    }
}

void FrostRadiance::evictOldSurfels(f32 maxAge) {
    for (u32 i = 0; i < pool_.activeCount; i++) {
        if (pool_[i].age > maxAge) {
            pool_.free(i);
        }
    }
}

// ============================================================================
// Math Helpers
// ============================================================================

f32 FrostRadiance::hashFloat(f32 x, f32 y, f32 z) const {
    u32 h = (u32)(x * 73856093u) ^ (u32)(y * 19349663u) ^ (u32)(z * 83492791u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (f32)(h & 0x7FFFFFFF) / (f32)0x7FFFFFFF;
}

Vec3 FrostRadiance::cosineWeightedHemisphere(Vec3 normal, f32 u1, f32 u2) const {
    f32 r = sqrtf(u1);
    f32 theta = 6.28318530718f * u2;
    f32 x = r * cosf(theta);
    f32 y = r * sinf(theta);
    f32 z = sqrtf(Mathf::max(0.0f, 1.0f - u1));

    Vec3 up = fabsf(normal.y) < 0.999f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    Vec3 tangent = up.cross(normal).normalized();
    Vec3 bitangent = normal.cross(tangent);

    return tangent * x + bitangent * y + normal * z;
}

// ============================================================================
// Surfel Pool Management — Detailed Allocation Strategies
// ============================================================================

void FrostRadiance::compactPool() {
    // Remove gaps in the surfel pool for cache-friendly access
    u32 writeIdx = 0;

    for (u32 readIdx = 0; readIdx < pool_.activeCount; readIdx++) {
        if (pool_[readIdx].radius > 0.0001f || pool_[readIdx].age <= 0) {
            if (writeIdx != readIdx) {
                pool_[writeIdx] = pool_[readIdx];

                // Update octree references if needed
                // (simplified: would need index remapping in production)
            }
            writeIdx++;
        }
    }

    pool_.activeCount = writeIdx;
}

u32 FrostRadiance::findClosestSurfel(Vec3 position, f32 maxDistance) const {
    f32 closestDist = maxDistance * maxDistance;
    u32 closestIdx = 0xFFFFFFFF;

    for (u32 i = 0; i < pool_.activeCount; i++) {
        f32 distSq = (pool_[i].position - position).lengthSquared();
        if (distSq < closestDist) {
            closestDist = distSq;
            closestIdx = i;
        }
    }

    return closestIdx;
}

u32 FrostRadiance::findSurfelInCone(Vec3 origin, Vec3 direction, f32 halfAngle,
                                     f32 maxDist) const {
    f32 cosHalfAngle = cosf(halfAngle);
    f32 closestDist = maxDist * maxDist;
    u32 closestIdx = 0xFFFFFFFF;

    for (u32 i = 0; i < pool_.activeCount; i++) {
        const Surfel& s = pool_[i];

        Vec3 toSurfel = s.position - origin;
        f32 distSq = toSurfel.lengthSquared();
        if (distSq > closestDist) continue;

        f32 dist = sqrtf(distSq);
        if (dist < 0.001f) continue;

        Vec3 dir = toSurfel / dist;
        if (direction.dot(dir) >= cosHalfAngle) {
            closestDist = distSq;
            closestIdx = i;
        }
    }

    return closestIdx;
}

f32 FrostRadiance::computeSurfelDensity(Vec3 position, f32 radius) const {
    Vector<u32> nearby;
    gatherSurfelsInRadius(position, radius, nearby);
    return (f32)nearby.size() / (4.0f / 3.0f * 3.14159f * radius * radius * radius);
}

Vec3 FrostRadiance::computeSurfelNormal(Vec3 position, f32 radius) const {
    Vector<u32> nearby;
    gatherSurfelsInRadius(position, radius, nearby);

    if (nearby.size() == 0) return Vec3(0, 1, 0);

    Vec3 avgNormal(0);
    f32 totalWeight = 0;

    for (u32 i = 0; i < nearby.size(); i++) {
        const Surfel& s = pool_[nearby[i]];
        f32 dist = (s.position - position).length();
        f32 weight = 1.0f / (1.0f + dist * dist);

        avgNormal += s.normal * weight;
        totalWeight += weight;
    }

    return totalWeight > 0 ? (avgNormal / totalWeight).normalized() : Vec3(0, 1, 0);
}

// ============================================================================
// Advanced Bounce Algorithms — Progressive Radiance Transfer
// ============================================================================

void FrostRadiance::computeProgressiveBounces(u32 maxBounces, f32 convergenceThreshold) {
    Vector<u32> neighbors;
    neighbors.reserve(256);

    for (u32 bounce = 0; bounce < maxBounces; bounce++) {
        f32 maxChange = 0;

        for (u32 i = 0; i < pool_.activeCount; i++) {
            Surfel& surfel = pool_[i];
            if (surfel.flags == 0 && surfel.age > 1) continue;

            Vec3 oldRadiance = surfel.radiance;

            // Adaptive gather radius based on bounce number
            f32 gatherRadius = surfel.radius * (2.0f + (f32)bounce * 2.0f);
            gatherSurfelsInRadius(surfel.position, gatherRadius, neighbors);

            if (neighbors.size() == 0) continue;

            // Form-factor weighted radiance gathering
            Vec3 incomingRadiance(0);
            f32 totalFormFactor = 0;

            for (u32 n = 0; n < neighbors.size(); n++) {
                u32 neighborIdx = neighbors[n];
                if (neighborIdx == i) continue;

                const Surfel& neighbor = pool_[neighborIdx];

                Vec3 diff = neighbor.position - surfel.position;
                f32 dist = diff.length();
                if (dist < 0.001f || dist > gatherRadius) continue;

                // Exact form factor between surfel disks
                Vec3 omega = diff.normalized();
                f32 NdotL_src = Mathf::max(neighbor.normal.dot(-omega), 0.0f);
                f32 NdotL_dst = Mathf::max(surfel.normal.dot(omega), 0.0f);

                // Distance-squared falloff
                f32 distSq = dist * dist;

                // Solid angle of source surfel as seen from destination
                f32 srcSolidAngle = (neighbor.radius * neighbor.radius * NdotL_src) /
                                    (distSq + neighbor.radius * neighbor.radius);

                // Form factor: how much of the source's hemisphere faces the destination
                f32 formFactor = srcSolidAngle * NdotL_dst;

                // Visibility (simplified)
                f32 visibility = 1.0f;

                // Transfer weight
                f32 weight = formFactor * visibility;
                incomingRadiance += neighbor.flux * weight;
                totalFormFactor += weight;
            }

            if (totalFormFactor > 0.0001f) {
                incomingRadiance = incomingRadiance / totalFormFactor;
            }

            // Apply bounce weight and surface properties
            Vec3 indirectLight = incomingRadiance * surfel.albedo * bounceWeight_;

            // Temporal blending for stability
            surfel.radiance = surfel.radiance * (1.0f - temporalBlendFactor_) +
                              (surfel.radiance + indirectLight) * temporalBlendFactor_;
            surfel.flux = surfel.radiance * surfel.albedo;

            // Track convergence
            f32 change = (surfel.radiance - oldRadiance).length();
            maxChange = std::max(maxChange, change);

            surfel.flags |= 1;
        }

        // Early termination if converged
        if (maxChange < convergenceThreshold && bounce > 0) {
            break;
        }
    }
}

Vec3 FrostRadiance::computeFormFactor(const Surfel& src, const Surfel& dst) const {
    Vec3 diff = dst.position - src.position;
    f32 dist = diff.length();
    if (dist < 0.001f) return Vec3(0);

    Vec3 omega = diff / dist;
    f32 NdotL_src = Mathf::max(src.normal.dot(-omega), 0.0f);
    f32 NdotL_dst = Mathf::max(dst.normal.dot(omega), 0.0f);

    f32 distSq = dist * dist;
    f32 srcSolidAngle = (src.radius * src.radius * NdotL_src) /
                        (distSq + src.radius * src.radius);

    return Vec3(srcSolidAngle * NdotL_dst);
}

void FrostRadiance::gatherHemicube(Vec3 position, Vec3 normal, f32 radius,
                                    Vec3& totalIrradiance) const {
    // Hemicube gathering: project surfels onto 5 faces of a hemicube
    totalIrradiance = Vec3(0);

    // Define hemicube faces (forward, left, right, up, down)
    Vec3 faces[5];
    faces[0] = normal;                          // forward
    faces[1] = normal.cross(Vec3(0, 1, 0));    // right
    faces[2] = -faces[1];                       // left
    faces[3] = Vec3(0, 1, 0);                   // up
    faces[4] = Vec3(0, -1, 0);                  // down

    for (u32 f = 0; f < 5; f++) {
        Vector<u32> surfelsOnFace;
        gatherSurfelsInCone(position, faces[f], 0.785398f, radius, surfelsOnFace);

        for (u32 i = 0; i < surfelsOnFace.size(); i++) {
            const Surfel& s = pool_[surfelsOnFace[i]];
            Vec3 toSurfel = s.position - position;
            f32 dist = toSurfel.length();

            if (dist < 0.001f || dist > radius) continue;

            f32 NdotL = Mathf::max(normal.dot(toSurfel / dist), 0.0f);
            f32 weight = NdotL / (dist * dist + 1.0f);

            totalIrradiance += s.flux * weight;
        }
    }
}

// ============================================================================
// Surfel Injection Strategies
// ============================================================================

void FrostRadiance::injectSurfelsAdaptive(const SurfelMeshData& mesh, u32 targetCount) {
    if (mesh.indices.size() < 3) return;

    u32 triCount = (u32)mesh.indices.size() / 3;

    // Compute importance per triangle based on:
    // 1. Screen coverage (would need camera info)
    // 2. Surface curvature
    // 3. Lighting complexity
    Vector<f32> importance;
    importance.resize(triCount);

    f32 totalImportance = 0;
    for (u32 t = 0; t < triCount; t++) {
        u32 i0 = mesh.indices[t * 3 + 0];
        u32 i1 = mesh.indices[t * 3 + 1];
        u32 i2 = mesh.indices[t * 3 + 2];

        Vec3 a = mesh.positions[i0];
        Vec3 b = mesh.positions[i1];
        Vec3 c = mesh.positions[i2];
        Vec3 n = mesh.normals[i0];

        // Curvature importance
        f32 curv = computeCurvature((a + b + c) / 3.0f, n, mesh);

        // Area importance
        f32 area = computeTriangleArea(a, b, c);

        // Combined importance
        importance[t] = area * (1.0f + curv * 2.0f);
        totalImportance += importance[t];
    }

    if (totalImportance < 0.0001f) return;

    // Distribute surfels proportional to importance
    std::mt19937 rng(42 + mesh.meshID + (u32)totalImportance);
    std::uniform_real_distribution<f32> dist(0.0f, 1.0f);

    for (u32 t = 0; t < triCount; t++) {
        u32 surfelCount = (u32)(importance[t] / totalImportance * (f32)targetCount);
        surfelCount = std::max(surfelCount, 1u);
        surfelCount = std::min(surfelCount, 64u);

        u32 i0 = mesh.indices[t * 3 + 0];
        u32 i1 = mesh.indices[t * 3 + 1];
        u32 i2 = mesh.indices[t * 3 + 2];

        Vec3 a = mesh.positions[i0];
        Vec3 b = mesh.positions[i1];
        Vec3 c = mesh.positions[i2];
        Vec3 na = mesh.normals[i0];
        Vec3 nb = mesh.normals[i1];
        Vec3 nc = mesh.normals[i2];

        for (u32 s = 0; s < surfelCount; s++) {
            u32 idx = pool_.allocate();
            if (idx == 0xFFFFFFFF) return;

            f32 u = dist(rng);
            f32 v = dist(rng);
            if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }
            f32 w = 1.0f - u - v;

            Surfel& surfel = pool_[idx];
            surfel.position = a * w + b * u + c * v;
            surfel.normal = interpolateNormal(na, nb, nc, u, v).normalized();
            surfel.albedo = Vec3(0.8f);

            f32 curvature = computeCurvature(surfel.position, surfel.normal, mesh);
            surfel.radius = computeAdaptiveRadius(0.0f, curvature,
                sqrtf(importance[t] / totalImportance) * splatRadiusFactor_);

            surfel.age = 0.0f;
            surfel.triangleID = t;
            surfel.meshID = mesh.meshID;
            surfel.flags = 1;
            surfel.radiance = evaluateDirectLight(surfel);
            surfel.flux = surfel.radiance * surfel.albedo;
            surfel.prevWorldPos = surfel.position;
        }
    }
}

void FrostRadiance::injectSurfelsOnEdges(const SurfelMeshData& mesh, u32 edgeSamples) {
    if (mesh.indices.size() < 3) return;

    u32 triCount = (u32)mesh.indices.size() / 3;

    // Find edge triangles (triangles with a free edge)
    Vector<bool> isEdge;
    isEdge.resize(triCount, false);

    for (u32 t = 0; t < triCount; t++) {
        u32 i0 = mesh.indices[t * 3 + 0];
        u32 i1 = mesh.indices[t * 3 + 1];
        u32 i2 = mesh.indices[t * 3 + 2];

        // Check if any vertex has only one adjacent triangle (boundary vertex)
        // Simplified: just check if normal faces away from mesh center
        Vec3 center(0);
        for (u32 v = 0; v < mesh.positions.size(); v++) {
            center += mesh.positions[v];
        }
        center = center / (f32)mesh.positions.size();

        Vec3 triCenter = (mesh.positions[i0] + mesh.positions[i1] + mesh.positions[i2]) / 3.0f;
        Vec3 toCenter = (center - triCenter).normalized();
        Vec3 normal = mesh.normals[i0];

        if (normal.dot(toCenter) < 0.3f) {
            isEdge[t] = true;
        }
    }

    // Inject surfels on edge triangles with higher density
    std::mt19937 rng(42 + mesh.meshID + 1000);
    std::uniform_real_distribution<f32> dist(0.0f, 1.0f);

    for (u32 t = 0; t < triCount; t++) {
        if (!isEdge[t]) continue;

        u32 i0 = mesh.indices[t * 3 + 0];
        u32 i1 = mesh.indices[t * 3 + 1];
        u32 i2 = mesh.indices[t * 3 + 2];

        Vec3 a = mesh.positions[i0];
        Vec3 b = mesh.positions[i1];
        Vec3 c = mesh.positions[i2];
        Vec3 na = mesh.normals[i0];
        Vec3 nb = mesh.normals[i1];
        Vec3 nc = mesh.normals[i2];

        for (u32 s = 0; s < edgeSamples; s++) {
            u32 idx = pool_.allocate();
            if (idx == 0xFFFFFFFF) return;

            // Sample more densely near edges
            f32 u = dist(rng) * 0.5f;
            f32 v = dist(rng) * 0.5f;
            if (u + v > 0.5f) { u = 0.5f - u; v = 0.5f - v; }
            f32 w = 1.0f - u - v;

            Surfel& surfel = pool_[idx];
            surfel.position = a * w + b * u + c * v;
            surfel.normal = interpolateNormal(na, nb, nc, u, v).normalized();
            surfel.albedo = Vec3(0.8f);
            surfel.radius = baseSurfelRadius_ * 0.5f;  // smaller radius on edges
            surfel.age = 0.0f;
            surfel.triangleID = t;
            surfel.meshID = mesh.meshID;
            surfel.flags = 1;
            surfel.radiance = evaluateDirectLight(surfel);
            surfel.flux = surfel.radiance * surfel.albedo;
            surfel.prevWorldPos = surfel.position;
        }
    }
}

// ============================================================================
// Advanced Octree Operations
// ============================================================================

void FrostRadiance::rebuildOctreeIncremental() {
    // Incrementally update octree by only modifying changed regions
    // This is more efficient than full rebuild each frame

    // Find surfels that moved significantly
    Vector<u32> movedSurfels;
    for (u32 i = 0; i < pool_.activeCount; i++) {
        Surfel& s = pool_[i];
        f32 movement = (s.position - s.prevWorldPos).length();
        if (movement > s.radius * 2.0f) {
            movedSurfels.push_back(i);
            s.flags |= 1;
        }
    }

    // If few surfels moved, do incremental update
    if (movedSurfels.size() < pool_.activeCount / 10) {
        // Remove moved surfels from their current octree nodes
        // Re-insert them into correct locations
        // (simplified implementation)
    } else {
        // Too many changes, do full rebuild
        buildOctree();
    }
}

void FrostRadiance::pruneEmptyNodes() {
    // Remove octree nodes with zero surfels
    for (u32 i = nodeCount_ - 1; i > 0; i--) {
        SurfelOctreeNode& node = octree_[i];

        if (node.surfelCount == 0 && node.firstChild == 0xFFFFFFFF) {
            // Mark parent's child reference as invalid
            if (node.parentIndex < nodeCount_) {
                SurfelOctreeNode& parent = octree_[node.parentIndex];
                if (parent.firstChild == i) {
                    parent.firstChild = 0xFFFFFFFF;
                }
            }
        }
    }
}

u32 FrostRadiance::computeOctreeDepth() const {
    if (nodeCount_ == 0) return 0;

    u32 maxDepth = 0;
    struct StackEntry { u32 nodeIdx; u32 depth; };
    Vector<StackEntry> stack;
    stack.push_back({0, 0});

    while (stack.size() > 0) {
        StackEntry entry = stack[stack.size() - 1];
        stack.pop();

        maxDepth = std::max(maxDepth, entry.depth);

        const SurfelOctreeNode& node = octree_[entry.nodeIdx];
        if (node.firstChild != 0xFFFFFFFF) {
            for (u32 c = 0; c < 8; c++) {
                stack.push_back({node.firstChild + c, entry.depth + 1});
            }
        }
    }

    return maxDepth;
}

f32 FrostRadiance::computeOctreeBalance() const {
    if (nodeCount_ == 0) return 1.0f;

    // Measure how balanced the octree is (1.0 = perfect balance)
    u32 minLeafDepth = 0xFFFFFFFF;
    u32 maxLeafDepth = 0;

    struct StackEntry { u32 nodeIdx; u32 depth; };
    Vector<StackEntry> stack;
    stack.push_back({0, 0});

    while (stack.size() > 0) {
        StackEntry entry = stack[stack.size() - 1];
        stack.pop();

        const SurfelOctreeNode& node = octree_[entry.nodeIdx];
        if (node.firstChild == 0xFFFFFFFF) {
            minLeafDepth = std::min(minLeafDepth, entry.depth);
            maxLeafDepth = std::max(maxLeafDepth, entry.depth);
        } else {
            for (u32 c = 0; c < 8; c++) {
                stack.push_back({node.firstChild + c, entry.depth + 1});
            }
        }
    }

    if (minLeafDepth == 0xFFFFFFFF || maxLeafDepth == 0) return 1.0f;
    return (f32)minLeafDepth / (f32)maxLeafDepth;
}

// ============================================================================
// Surfel Quality Metrics
// ============================================================================

f32 FrostRadiance::computeCoverageRatio() const {
    // Measure how well surfels cover the scene surface
    f32 totalArea = 0;
    f32 coveredArea = 0;

    for (u32 i = 0; i < pool_.activeCount; i++) {
        const Surfel& s = pool_[i];
        f32 surfelArea = 3.14159f * s.radius * s.radius;
        coveredArea += surfelArea;
    }

    // Approximate scene surface area from bounds
    // (would use mesh data in production)
    return coveredArea / (100.0f * 100.0f);  // normalized
}

f32 FrostRadiance::computeOverlapRatio() const {
    // Measure surfel overlap (higher = better coverage but more redundancy)
    u32 overlapCount = 0;
    u32 totalPairs = 0;

    Vector<u32> neighbors;
    neighbors.reserve(64);

    for (u32 i = 0; i < std::min(pool_.activeCount, 1000u); i++) {
        const Surfel& s = pool_[i];
        gatherSurfelsInRadius(s.position, s.radius * 2.0f, neighbors);

        for (u32 n = 0; n < neighbors.size(); n++) {
            if (neighbors[n] <= i) continue;
            totalPairs++;

            const Surfel& neighbor = pool_[neighbors[n]];
            f32 dist = (s.position - neighbor.position).length();
            if (dist < s.radius + neighbor.radius) {
                overlapCount++;
            }
        }
    }

    return totalPairs > 0 ? (f32)overlapCount / (f32)totalPairs : 0;
}

Vec3 FrostRadiance::computeAverageRadiance() const {
    Vec3 total(0);
    u32 count = 0;

    for (u32 i = 0; i < pool_.activeCount; i++) {
        if (pool_[i].radius > 0.0001f) {
            total += pool_[i].radiance;
            count++;
        }
    }

    return count > 0 ? total / (f32)count : Vec3(0);
}

f32 FrostRadiance::computeTemporalStability() const {
    // Measure how stable surfel radiance is across frames
    f32 totalChange = 0;
    u32 count = 0;

    for (u32 i = 0; i < pool_.activeCount; i++) {
        const Surfel& s = pool_[i];
        if (s.age > 1) {
            f32 change = (s.radiance - Vec3(s.flux.x / (s.albedo.x + 0.001f),
                                             s.flux.y / (s.albedo.y + 0.001f),
                                             s.flux.z / (s.albedo.z + 0.001f))).length();
            totalChange += change;
            count++;
        }
    }

    return count > 0 ? 1.0f - Mathf::clamp(totalChange / (f32)count, 0.0f, 1.0f) : 1.0f;
}

// ============================================================================
// Debug and Visualization
// ============================================================================

void FrostRadiance::debugDrawOctree(Vec3& boundsMin, Vec3& boundsMax,
                                     u32& nodeCount, u32& leafCount) const {
    boundsMin = Vec3(1e30f);
    boundsMax = Vec3(-1e30f);
    nodeCount = nodeCount_;
    leafCount = 0;

    for (u32 i = 0; i < nodeCount_; i++) {
        const SurfelOctreeNode& node = octree_[i];
        boundsMin = boundsMin.min(node.boundsMin);
        boundsMax = boundsMax.max(node.boundsMax);

        if (node.firstChild == 0xFFFFFFFF) {
            leafCount++;
        }
    }
}

void FrostRadiance::getSurfelStats(u32& active, u32& inactive, f32& avgRadius,
                                     f32& avgAge, Vec3& avgPosition) const {
    active = 0;
    inactive = 0;
    f32 totalRadius = 0;
    f32 totalAge = 0;
    Vec3 totalPos(0);

    for (u32 i = 0; i < pool_.activeCount; i++) {
        if (pool_[i].radius > 0.0001f) {
            active++;
            totalRadius += pool_[i].radius;
            totalAge += pool_[i].age;
            totalPos += pool_[i].position;
        } else {
            inactive++;
        }
    }

    avgRadius = active > 0 ? totalRadius / (f32)active : 0;
    avgAge = active > 0 ? totalAge / (f32)active : 0;
    avgPosition = active > 0 ? totalPos / (f32)active : Vec3(0);
}

Vector<Vec3> FrostRadiance::getSurfelPositions() const {
    Vector<Vec3> positions;
    positions.reserve(pool_.activeCount);

    for (u32 i = 0; i < pool_.activeCount; i++) {
        if (pool_[i].radius > 0.0001f) {
            positions.push_back(pool_[i].position);
        }
    }

    return positions;
}

Vector<Vec3> FrostRadiance::getSurfelNormals() const {
    Vector<Vec3> normals;
    normals.reserve(pool_.activeCount);

    for (u32 i = 0; i < pool_.activeCount; i++) {
        if (pool_[i].radius > 0.0001f) {
            normals.push_back(pool_[i].normal);
        }
    }

    return normals;
}

Vector<Vec3> FrostRadiance::getSurfelColors() const {
    Vector<Vec3> colors;
    colors.reserve(pool_.activeCount);

    for (u32 i = 0; i < pool_.activeCount; i++) {
        if (pool_[i].radius > 0.0001f) {
            colors.push_back(pool_[i].radiance);
        }
    }

    return colors;
}

// ============================================================================
// Surfel Pool Statistics and Debug
// ============================================================================

void FrostRadiance::debugPrintPoolState() const {
    u32 active = 0;
    u32 free = 0;
    u32 maxAge = 0;
    f32 avgAge = 0;

    for (u32 i = 0; i < pool_.activeCount; i++) {
        if (pool_[i].radius > 0.0001f) {
            active++;
            avgAge += pool_[i].age;
            maxAge = std::max(maxAge, (u32)pool_[i].age);
        } else {
            free++;
        }
    }

    avgAge = active > 0 ? avgAge / (f32)active : 0;
    // Statistics available for profiling
}

u32 FrostRadiance::computePoolFragmentation() const {
    // Count number of free list segments
    u32 segments = 0;
    u32 currentSegmentSize = 0;

    for (u32 i = 0; i < pool_.activeCount; i++) {
        if (pool_[i].radius < 0.0001f) {
            currentSegmentSize++;
        } else {
            if (currentSegmentSize > 0) {
                segments++;
                currentSegmentSize = 0;
            }
        }
    }

    if (currentSegmentSize > 0) segments++;
    return segments;
}

// ============================================================================
// Advanced Radiance Transfer
// ============================================================================

Vec3 FrostRadiance::computeViewDependentRadiance(Vec3 position, Vec3 normal,
                                                   Vec3 viewDir, f32 roughness) const {
    // Compute radiance that depends on viewing angle
    // Uses directional harmonics approximation

    Vector<u32> neighbors;
    gatherSurfelsInRadius(position, baseSurfelRadius_ * 6.0f, neighbors);

    Vec3 totalRadiance(0);
    f32 totalWeight = 0;

    for (u32 i = 0; i < neighbors.size(); i++) {
        const Surfel& s = pool_[neighbors[i]];

        Vec3 toSurfel = s.position - position;
        f32 dist = toSurfel.length();
        if (dist < 0.001f) continue;

        Vec3 dir = toSurfel / dist;
        f32 NdotL = Mathf::max(normal.dot(dir), 0.0f);

        // View-dependent weight (Glossy component)
        Vec3 halfVec = (viewDir + dir).normalized();
        f32 NdotH = Mathf::max(normal.dot(halfVec), 0.0f);
        f32 viewWeight = powf(NdotH, roughness * 128.0f);

        f32 weight = NdotL * viewWeight / (1.0f + dist * dist);
        totalRadiance += s.flux * weight;
        totalWeight += weight;
    }

    return totalWeight > 0 ? totalRadiance / totalWeight : Vec3(0);
}

void FrostRadiance::computeSpecularHighlights(Vec3 position, Vec3 normal,
                                                Vec3 viewDir, f32 roughness,
                                                Vec3& specularTerm) const {
    // Compute specular highlights from nearby light sources
    specularTerm = Vec3(0);

    // Sun specular
    Vec3 sunDir = Vec3(0.5f, -0.8f, -0.3f).normalized();
    Vec3 halfVec = (viewDir - sunDir).normalized();
    f32 NdotH = Mathf::max(normal.dot(halfVec), 0.0f);
    f32 specPower = powf(NdotH, 1.0f / (roughness * roughness + 0.01f));

    specularTerm = Vec3(1.0f, 0.96f, 0.9f) * specPower * 3.0f;
}

f32 FrostRadiance::computeFormFactorApproximation(const Surfel& a, const Surfel& b) const {
    // Approximate form factor between two surfels
    Vec3 diff = b.position - a.position;
    f32 dist = diff.length();
    if (dist < 0.001f) return 0;

    Vec3 omega = diff / dist;
    f32 NdotA = Mathf::max(a.normal.dot(omega), 0.0f);
    f32 NdotB = Mathf::max(b.normal.dot(-omega), 0.0f);

    // Simplified form factor
    return (NdotA * NdotB) / (dist * dist + 1.0f);
}

// ============================================================================
// Irradiance Volume Advanced Operations
// ============================================================================

void FrostRadiance::blurIrradianceVolume(f32 sigma) {
    // Gaussian blur the irradiance volume for smoother results
    Vector<Vec3> temp = irrVolume_.texels;

    for (u32 z = 0; z < irrVolume_.resZ; z++) {
        for (u32 y = 0; y < irrVolume_.resY; y++) {
            for (u32 x = 0; x < irrVolume_.resX; x++) {
                Vec3 sum(0);
                f32 totalWeight = 0;

                for (i32 dz = -1; dz <= 1; dz++) {
                    for (i32 dy = -1; dy <= 1; dy++) {
                        for (i32 dx = -1; dx <= 1; dx++) {
                            i32 sx = Mathf::clamp((i32)x + dx, 0, (i32)irrVolume_.resX - 1);
                            i32 sy = Mathf::clamp((i32)y + dy, 0, (i32)irrVolume_.resY - 1);
                            i32 sz = Mathf::clamp((i32)z + dz, 0, (i32)irrVolume_.resZ - 1);

                            f32 dist = sqrtf((f32)(dx * dx + dy * dy + dz * dz));
                            f32 weight = expf(-dist * dist / (2.0f * sigma * sigma));

                            sum += temp[sz * irrVolume_.resY * irrVolume_.resX +
                                       sy * irrVolume_.resX + sx] * weight;
                            totalWeight += weight;
                        }
                    }
                }

                irrVolume_.texels[z * irrVolume_.resY * irrVolume_.resX +
                                 y * irrVolume_.resX + x] = sum / totalWeight;
            }
        }
    }
}

void FrostRadiance::downsampleIrradianceVolume(u32 factor) {
    // Downsample irradiance volume for coarser lookups
    u32 newResX = irrVolume_.resX / factor;
    u32 newResY = irrVolume_.resY / factor;
    u32 newResZ = irrVolume_.resZ / factor;

    Vector<Vec3> temp;
    temp.resize(newResX * newResY * newResZ);

    for (u32 z = 0; z < newResZ; z++) {
        for (u32 y = 0; y < newResY; y++) {
            for (u32 x = 0; x < newResX; x++) {
                Vec3 sum(0);
                u32 count = 0;

                for (u32 dz = 0; dz < factor; dz++) {
                    for (u32 dy = 0; dy < factor; dy++) {
                        for (u32 dx = 0; dx < factor; dx++) {
                            u32 sx = x * factor + dx;
                            u32 sy = y * factor + dy;
                            u32 sz = z * factor + dz;

                            if (sx < irrVolume_.resX && sy < irrVolume_.resY && sz < irrVolume_.resZ) {
                                sum += irrVolume_.texels[sz * irrVolume_.resY * irrVolume_.resX +
                                                        sy * irrVolume_.resX + sx];
                                count++;
                            }
                        }
                    }
                }

                temp[z * newResY * newResX + y * newResX + x] = count > 0 ? sum / (f32)count : Vec3(0);
            }
        }
    }

    irrVolume_.resX = newResX;
    irrVolume_.resY = newResY;
    irrVolume_.resZ = newResZ;
    irrVolume_.texels = temp;
}

// ============================================================================
// Surfel Injection from Dynamic Objects
// ============================================================================

void FrostRadiance::injectDynamicSurfels(const SurfelMeshData& mesh,
                                           Vec3 velocity, f32 deltaTime) {
    // Inject surfels for moving objects with velocity-based radius scaling
    if (mesh.indices.size() < 3) return;

    u32 triCount = (u32)mesh.indices.size() / 3;
    f32 speed = velocity.length();

    // Scale radius based on motion (larger for faster objects)
    f32 motionScale = 1.0f + speed * deltaTime * 2.0f;

    std::mt19937 rng(42 + mesh.meshID);
    std::uniform_real_distribution<f32> dist(0.0f, 1.0f);

    for (u32 t = 0; t < triCount; t++) {
        u32 i0 = mesh.indices[t * 3 + 0];
        u32 i1 = mesh.indices[t * 3 + 1];
        u32 i2 = mesh.indices[t * 3 + 2];

        Vec3 a = mesh.positions[i0];
        Vec3 b = mesh.positions[i1];
        Vec3 c = mesh.positions[i2];
        Vec3 na = mesh.normals[i0];

        // Inject 1-4 surfels per triangle
        u32 surfelCount = 1 + (u32)(computeTriangleArea(a, b, c) * 2.0f);
        surfelCount = std::min(surfelCount, 4u);

        for (u32 s = 0; s < surfelCount; s++) {
            u32 idx = pool_.allocate();
            if (idx == 0xFFFFFFFF) return;

            f32 u = dist(rng);
            f32 v = dist(rng);
            if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }
            f32 w = 1.0f - u - v;

            Surfel& surfel = pool_[idx];
            surfel.position = a * w + b * u + c * v;
            surfel.normal = na;
            surfel.albedo = Vec3(0.8f);
            surfel.radius = baseSurfelRadius_ * motionScale;
            surfel.age = 0.0f;
            surfel.triangleID = t;
            surfel.meshID = mesh.meshID;
            surfel.flags = 1;
            surfel.radiance = evaluateDirectLight(surfel);
            surfel.flux = surfel.radiance * surfel.albedo;
            surfel.prevWorldPos = surfel.position;
        }
    }
}

void FrostRadiance::removeSurfelsForMesh(u32 meshID) {
    for (u32 i = 0; i < pool_.activeCount; i++) {
        if (pool_[i].meshID == meshID) {
            pool_.free(i);
        }
    }
}

void FrostRadiance::updateSurfelPositions(const SurfelMeshData& mesh) {
    // Update surfel positions when mesh deforms
    for (u32 i = 0; i < pool_.activeCount; i++) {
        Surfel& s = pool_[i];
        if (s.meshID != mesh.meshID) continue;

        // Find closest vertex and interpolate position
        f32 minDist = 1e30f;
        Vec3 newPos = s.position;

        for (u32 v = 0; v < mesh.positions.size(); v++) {
            f32 dist = (mesh.positions[v] - s.position).length();
            if (dist < minDist) {
                minDist = dist;
                newPos = mesh.positions[v];
                s.normal = mesh.normals[v];
            }
        }

        s.position = newPos;
        s.flags |= 1;  // mark dirty
    }
}

// ============================================================================
// Radiance Cache — Probe-Based Global Illumination
// ============================================================================

Vec3 FrostRadiance::traceProbeRay(const Vec3& origin, const Vec3& dir) const {
    Vector<u32> hitSurfels;
    gatherSurfelsInCone(origin, dir, 0.3f, radianceCfg_.maxTraceDistance, hitSurfels);

    Vec3 totalRadiance(0);
    f32 totalWeight = 0;

    for (u32 i = 0; i < hitSurfels.size(); i++) {
        const Surfel& s = pool_[hitSurfels[i]];
        Vec3 toSurfel = s.position - origin;
        f32 dist = toSurfel.length();
        if (dist < radianceCfg_.minTraceDistance || dist > radianceCfg_.maxTraceDistance) continue;

        f32 NdotL = Mathf::max(s.normal.dot(-dir), 0.0f);
        f32 weight = NdotL / (1.0f + dist * dist);
        totalRadiance += s.radiance * weight;
        totalWeight += weight;
    }

    if (totalWeight > 0.0001f) {
        return totalRadiance / totalWeight;
    }

    // Fallback: ambient sky contribution
    f32 skyFactor = dir.y * 0.5f + 0.5f;
    return Vec3(0.1f, 0.15f, 0.25f) * skyFactor;
}

u32 FrostRadiance::findNearestProbe(const Vec3& pos) const {
    if (probeCache_.size() == 0) return 0xFFFFFFFF;

    f32 closestDist = 1e30f;
    u32 closestIdx = 0;

    for (u32 i = 0; i < probeCache_.size(); i++) {
        f32 dist = (probeCache_[i].position - pos).lengthSquared();
        if (dist < closestDist) {
            closestDist = dist;
            closestIdx = i;
        }
    }

    return closestIdx;
}

void FrostRadiance::updateSurfelToProbeMapping() {
    surfelToProbe_.resize(pool_.activeCount);
    for (u32 i = 0; i < pool_.activeCount; i++) {
        surfelToProbe_[i] = findNearestProbe(pool_[i].position);
    }
}

void FrostRadiance::updateProbeCache(const Vec3& cameraPos) {
    probesUpdatedThisFrame_ = 0;

    u32 totalProbes = radianceCfg_.probesPerAxis * radianceCfg_.probesPerAxis *
                      radianceCfg_.probesPerAxis;

    // Rebuild probe grid if resolution changed
    if (probeCache_.size() != totalProbes) {
        probeCache_.resize(totalProbes);
        f32 halfExtent = radianceCfg_.probeSpacing * (f32)(radianceCfg_.probesPerAxis / 2);
        Vec3 gridOrigin = cameraPos - Vec3(halfExtent, halfExtent, halfExtent);

        for (u32 z = 0; z < radianceCfg_.probesPerAxis; z++) {
            for (u32 y = 0; y < radianceCfg_.probesPerAxis; y++) {
                for (u32 x = 0; x < radianceCfg_.probesPerAxis; x++) {
                    u32 idx = z * radianceCfg_.probesPerAxis * radianceCfg_.probesPerAxis +
                              y * radianceCfg_.probesPerAxis + x;
                    probeCache_[idx].position = gridOrigin + Vec3(
                        (f32)x * radianceCfg_.probeSpacing,
                        (f32)y * radianceCfg_.probeSpacing,
                        (f32)z * radianceCfg_.probeSpacing);
                    probeCache_[idx].lastUpdateFrame = 0;
                }
            }
        }
    }

    // Update probes closest to camera, limited per frame
    for (u32 z = 0; z < radianceCfg_.probesPerAxis &&
         probesUpdatedThisFrame_ < radianceCfg_.updateProbesPerFrame; z++) {
        for (u32 y = 0; y < radianceCfg_.probesPerAxis &&
             probesUpdatedThisFrame_ < radianceCfg_.updateProbesPerFrame; y++) {
            for (u32 x = 0; x < radianceCfg_.probesPerAxis &&
                 probesUpdatedThisFrame_ < radianceCfg_.updateProbesPerFrame; x++) {
                u32 idx = z * radianceCfg_.probesPerAxis * radianceCfg_.probesPerAxis +
                          y * radianceCfg_.probesPerAxis + x;
                RadianceProbe& probe = probeCache_[idx];

                if (probe.lastUpdateFrame == frameNumber_) continue;

                f32 dist = (probe.position - cameraPos).length();
                if (dist > radianceCfg_.probeRadius * (f32)radianceCfg_.probesPerAxis) continue;

                // Cast rays in 6 cardinal directions
                Vec3 dirs[6] = {
                    Vec3(1, 0, 0), Vec3(-1, 0, 0),
                    Vec3(0, 1, 0), Vec3(0, -1, 0),
                    Vec3(0, 0, 1), Vec3(0, 0, -1)
                };

                u32 raysToCast = std::min(radianceCfg_.maxRaysPerProbe, 6u);
                for (u32 d = 0; d < raysToCast; d++) {
                    Vec3 rayOrigin = probe.position + dirs[d] * radianceCfg_.minTraceDistance;
                    Vec3 hitRadiance = traceProbeRay(rayOrigin, dirs[d]);

                    // Temporal hysteresis
                    probe.radiance[d] = probe.radiance[d] * radianceCfg_.probeHysteresis +
                                        hitRadiance * (1.0f - radianceCfg_.probeHysteresis);
                }

                probe.lastUpdateFrame = frameNumber_;
                probesUpdatedThisFrame_++;
                probesUpdated_++;
            }
        }
    }
}

// ============================================================================
// Dynamic Surfel Injection — Point-Based
// ============================================================================

void FrostRadiance::injectDynamicSurfels(const Vec3& pos, const Vec3& normal,
                                           const Vec3& albedo, u32 count) {
    std::mt19937 rng((u32)(pos.x * 73856093u) ^ (u32)(pos.y * 19349663u));
    std::uniform_real_distribution<f32> dist(-1.0f, 1.0f);

    for (u32 i = 0; i < count; i++) {
        u32 idx = pool_.allocate();
        if (idx == 0xFFFFFFFF) break;

        Surfel& surfel = pool_[idx];
        Vec3 offset(dist(rng), dist(rng), dist(rng));
        offset = offset.normalized() * baseSurfelRadius_ * 0.5f;
        surfel.position = pos + offset;
        surfel.normal = normal.normalized();
        surfel.albedo = albedo;
        surfel.radius = baseSurfelRadius_;
        surfel.age = 0.0f;
        surfel.triangleID = 0;
        surfel.meshID = 0xFFFFFFFF;
        surfel.flags = 1;
        surfel.radiance = evaluateDirectLight(surfel);
        surfel.flux = surfel.radiance * surfel.albedo;
        surfel.prevWorldPos = surfel.position;

        // Update surfel-to-probe mapping
        u32 probeIdx = findNearestProbe(surfel.position);
        if (probeIdx != 0xFFFFFFFF && idx < surfelToProbe_.size()) {
            surfelToProbe_[idx] = probeIdx;
        }
    }
}

// ============================================================================
// Surfel Position Update — Transform Recomputation
// ============================================================================

void FrostRadiance::updateSurfelPositions() {
    for (u32 i = 0; i < pool_.activeCount; i++) {
        Surfel& s = pool_[i];
        if (s.radius < 0.0001f) continue;

        f32 movement = (s.position - s.prevWorldPos).length();
        if (movement > 0.001f) {
            s.flags |= 1;
        }
        s.prevWorldPos = s.position;
    }

    updateSurfelToProbeMapping();
}

// ============================================================================
// Irradiance Volume — Blur, Downsample, Query
// ============================================================================

void FrostRadiance::blurIrradianceVolume(u32 passes) {
    u32 res = irradianceResolution_;
    u32 totalSize = res * res * res;
    if (irradianceVolume_.size() != totalSize) return;

    Vector<Vec3> temp;
    temp.resize(totalSize);

    for (u32 pass = 0; pass < passes; pass++) {
        for (u32 i = 0; i < totalSize; i++) {
            temp[i] = irradianceVolume_[i];
        }

        for (u32 z = 0; z < res; z++) {
            for (u32 y = 0; y < res; y++) {
                for (u32 x = 0; x < res; x++) {
                    Vec3 sum = temp[z * res * res + y * res + x] * 6.0f;
                    f32 totalWeight = 6.0f;

                    // 6-connected neighbors (Gaussian-like box blur)
                    for (u32 d = 0; d < 6; d++) {
                        i32 nx = (i32)x;
                        i32 ny = (i32)y;
                        i32 nz = (i32)z;

                        if (d == 0) nx++;
                        else if (d == 1) nx--;
                        else if (d == 2) ny++;
                        else if (d == 3) ny--;
                        else if (d == 4) nz++;
                        else if (d == 5) nz--;

                        nx = Mathf::clamp(nx, 0, (i32)res - 1);
                        ny = Mathf::clamp(ny, 0, (i32)res - 1);
                        nz = Mathf::clamp(nz, 0, (i32)res - 1);

                        sum += temp[(u32)nz * res * res + (u32)ny * res + (u32)nx];
                        totalWeight += 1.0f;
                    }

                    irradianceVolume_[z * res * res + y * res + x] = sum / totalWeight;
                }
            }
        }
    }
}

void FrostRadiance::downsampleIrradianceVolume() {
    u32 res = irradianceResolution_;
    if (res <= 2) return;

    u32 newRes = res / 2;
    Vector<Vec3> downsampled;
    downsampled.resize(newRes * newRes * newRes);

    for (u32 z = 0; z < newRes; z++) {
        for (u32 y = 0; y < newRes; y++) {
            for (u32 x = 0; x < newRes; x++) {
                Vec3 sum(0);
                for (u32 dz = 0; dz < 2; dz++) {
                    for (u32 dy = 0; dy < 2; dy++) {
                        for (u32 dx = 0; dx < 2; dx++) {
                            u32 sx = x * 2 + dx;
                            u32 sy = y * 2 + dy;
                            u32 sz = z * 2 + dz;
                            sum += irradianceVolume_[sz * res * res + sy * res + sx];
                        }
                    }
                }
                downsampled[z * newRes * newRes + y * newRes + x] = sum / 8.0f;
            }
        }
    }

    irradianceVolume_ = downsampled;
    irradianceResolution_ = newRes;
}

Vec3 FrostRadiance::queryIrradiance(const Vec3& pos, const Vec3& normal) const {
    u32 res = irradianceResolution_;
    u32 totalSize = res * res * res;
    if (totalSize == 0 || irradianceVolume_.size() != totalSize) return Vec3(0);

    irradianceQueries_++;

    // Trilinear sample using existing irradiance volume coordinate system
    Vec3 local = pos - irrVolume_.origin;
    f32 fx = local.x / irrVolume_.cellSize.x - 0.5f;
    f32 fy = local.y / irrVolume_.cellSize.y - 0.5f;
    f32 fz = local.z / irrVolume_.cellSize.z - 0.5f;

    i32 x0 = (i32)std::floor(fx);
    i32 y0 = (i32)std::floor(fy);
    i32 z0 = (i32)std::floor(fz);
    f32 tx = fx - (f32)x0;
    f32 ty = fy - (f32)y0;
    f32 tz = fz - (f32)z0;

    Vec3 irradiance(0);
    for (i32 dz = 0; dz <= 1; dz++) {
        for (i32 dy = 0; dy <= 1; dy++) {
            for (i32 dx = 0; dx <= 1; dx++) {
                i32 sx = Mathf::clamp(x0 + dx, 0, (i32)res - 1);
                i32 sy = Mathf::clamp(y0 + dy, 0, (i32)res - 1);
                i32 sz = Mathf::clamp(z0 + dz, 0, (i32)res - 1);
                f32 w = ((dx == 0) ? (1.0f - tx) : tx) *
                        ((dy == 0) ? (1.0f - ty) : ty) *
                        ((dz == 0) ? (1.0f - tz) : tz);
                irradiance += irradianceVolume_[(u32)sz * res * res +
                                                 (u32)sy * res + (u32)sx] * w;
            }
        }
    }

    // Dot with surface normal for directional irradiance
    f32 cosine = Mathf::max(normal.y, 0.0f);
    return irradiance * cosine;
}

// ============================================================================
// Cache Configuration
// ============================================================================

void FrostRadiance::setRadianceCacheConfig(const RadianceCacheConfig& cfg) {
    radianceCfg_ = cfg;

    // Invalidate probe cache if grid resolution changed
    u32 totalProbes = cfg.probesPerAxis * cfg.probesPerAxis * cfg.probesPerAxis;
    if (probeCache_.size() != totalProbes) {
        probeCache_.clear();
    }
}

const RadianceCacheConfig& FrostRadiance::getRadianceCacheConfig() const {
    return radianceCfg_;
}

// ============================================================================
// Cache Management
// ============================================================================

void FrostRadiance::clearCache() {
    // Clear irradiance volume
    for (auto& v : irradianceVolume_) v = Vec3(0);

    // Clear probe cache
    for (u32 i = 0; i < probeCache_.size(); i++) {
        for (u32 f = 0; f < 6; f++) {
            probeCache_[i].radiance[f] = Vec3(0);
        }
        probeCache_[i].lastUpdateFrame = 0;
    }

    // Clear surfel-to-probe mapping
    for (auto& m : surfelToProbe_) m = 0xFFFFFFFF;

    // Reset stats
    probesUpdated_ = 0;
    irradianceQueries_ = 0;
    cacheUpdateMs_ = 0;
    probesUpdatedThisFrame_ = 0;
}

} // namespace Frost
