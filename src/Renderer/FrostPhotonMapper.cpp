// ============================================================================
// FrostEngine FrostPhotonMapper — Photon Mapping Global Illumination
// ============================================================================
// Proprietary photon mapping system. Uses k-d tree based photon storage with
// progressive refinement for accurate caustics and multi-bounce indirect
// lighting. Fundamentally different from both Lumen and FrostRadiance.
// ============================================================================

#include "FrostEngine/Renderer/FrostPhotonMapper.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <random>
#include <numeric>
#include <stack>

namespace Frost {

// ============================================================================
// Construction / Destruction
// ============================================================================

FrostPhotonMapper::FrostPhotonMapper()
    : photonCount_(0), kdTreeNodeCount_(0), nextPhotonSlot_(0),
      meshCount_(0), materialCount_(0), lightCount_(0),
      filterType_(PhotonFilter::Gaussian), gatherRadius_(1.0f),
      maxGatherPhotons_(64), globalPhotonPower_(1.0f),
      progressiveFrame_(0), photonsPerFrame_(100000),
      progressiveMode_(false), initialized_(false) {
    memset(&stats_, 0, sizeof(stats_));
}

FrostPhotonMapper::~FrostPhotonMapper() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool FrostPhotonMapper::init(u32 maxPhotons) {
    photons_.resize(maxPhotons);
    kdTree_.resize(maxPhotons * 2);

    photonCount_ = 0;
    kdTreeNodeCount_ = 0;
    nextPhotonSlot_ = 0;

    stats_.emitted = 0;
    stats_.absorbed = 0;
    stats_.causticCount = 0;
    stats_.indirectCount = 0;
    stats_.directCount = 0;
    stats_.avgBounces = 0;

    initialized_ = true;
    return true;
}

void FrostPhotonMapper::shutdown() {
    photons_.clear();
    kdTree_.clear();
    sceneMeshes_.clear();
    sceneMaterials_.clear();
    lightSources_.clear();
    initialized_ = false;
}

void FrostPhotonMapper::reset() {
    photonCount_ = 0;
    kdTreeNodeCount_ = 0;
    nextPhotonSlot_ = 0;
    progressiveFrame_ = 0;
    memset(&stats_, 0, sizeof(stats_));
}

// ============================================================================
// Scene Setup
// ============================================================================

void FrostPhotonMapper::setSceneMeshes(const PhotonMeshData* meshes, u32 meshCount,
                                        const PhotonMaterial* materials, u32 materialCount) {
    sceneMeshes_.resize(meshCount);
    sceneMaterials_.resize(materialCount);

    for (u32 i = 0; i < meshCount; i++) {
        sceneMeshes_[i] = meshes[i];
    }
    for (u32 i = 0; i < materialCount; i++) {
        sceneMaterials_[i] = materials[i];
    }

    meshCount_ = meshCount;
    materialCount_ = materialCount;
}

void FrostPhotonMapper::setLightSources(const PhotonLightSource* lights, u32 lightCount) {
    lightSources_.resize(lightCount);
    for (u32 i = 0; i < lightCount; i++) {
        lightSources_[i] = lights[i];
    }
    lightCount_ = lightCount;
}

// ============================================================================
// Main Update
// ============================================================================

void FrostPhotonMapper::update(f32 deltaTime, const Mat4& viewProj, u32 screenW, u32 screenH) {
    if (!initialized_) return;

    beginProgressiveFrame();

    // Emit photons from all light sources
    emitPhotons();

    // Build k-d tree from accumulated photons
    buildKDTree();

    endProgressiveFrame();
}

// ============================================================================
// Progressive Refinement
// ============================================================================

void FrostPhotonMapper::beginProgressiveFrame() {
    progressiveFrame_++;
}

void FrostPhotonMapper::endProgressiveFrame() {
    // Update statistics
    f32 totalBounces = 0;
    for (u32 i = 0; i < photonCount_; i++) {
        totalBounces += (f32)photons_[i].bounceCount;
    }
    stats_.avgBounces = photonCount_ > 0 ? totalBounces / (f32)photonCount_ : 0;
}

// ============================================================================
// Photon Emission
// ============================================================================

void FrostPhotonMapper::emitPhotons() {
    for (u32 l = 0; l < lightCount_; l++) {
        const PhotonLightSource& light = lightSources_[l];
        if (!light.enabled) continue;

        emitPhotonsFromSource(light);
    }
}

void FrostPhotonMapper::emitPhotonsFromSource(const PhotonLightSource& light) {
    u32 photonsToEmit = light.photonsToEmit;
    if (progressiveMode_) {
        photonsToEmit = photonsPerFrame_;
    }

    std::mt19937 rng(42 + progressiveFrame_ * 1000 + &light - &lightSources_[0]);
    std::uniform_real_distribution<f32> dist(0.0f, 1.0f);

    for (u32 i = 0; i < photonsToEmit; i++) {
        if (nextPhotonSlot_ >= photons_.size()) {
            // Wrap around, overwriting old photons
            nextPhotonSlot_ = 0;
            photonCount_ = (u32)photons_.size();
        }

        Photon& photon = photons_[nextPhotonSlot_];

        // Generate emission position and direction
        photon.position = generateEmissionPosition(light);
        photon.direction = generateEmissionDirection(light);

        // Set initial power
        f32 powerScale = light.intensity / (f32)photonsToEmit;
        photon.power = light.color * powerScale;
        photon.bounceCount = 0;
        photon.flags = 0;
        photon.wavelengthR = 1.0f;
        photon.wavelengthG = 1.0f;
        photon.wavelengthB = 1.0f;

        // Trace photon through scene
        bool absorbed = tracePhoton(photon, MAX_PHOTON_BOUNCES);

        if (absorbed) {
            stats_.absorbed++;
        }

        stats_.emitted++;
        nextPhotonSlot_++;
        photonCount_ = std::max(photonCount_, nextPhotonSlot_);
    }
}

Vec3 FrostPhotonMapper::generateEmissionDirection(const PhotonLightSource& light) const {
    if (light.isDirectional) {
        return light.direction.normalized();
    }

    // Cosine-weighted hemisphere emission
    std::mt19937 rng(42);
    std::uniform_real_distribution<f32> dist(0.0f, 1.0f);

    f32 u1 = dist(rng);
    f32 u2 = dist(rng);

    f32 r = sqrtf(u1);
    f32 theta = 6.28318530718f * u2;
    f32 x = r * cosf(theta);
    f32 y = r * sinf(theta);
    f32 z = sqrtf(std::max(0.0f, 1.0f - u1));

    Vec3 up = fabsf(light.direction.y) < 0.999f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    Vec3 tangent = up.cross(light.direction).normalized();
    Vec3 bitangent = light.direction.cross(tangent);

    return tangent * x + bitangent * y + light.direction * z;
}

Vec3 FrostPhotonMapper::generateEmissionPosition(const PhotonLightSource& light) const {
    if (light.radius <= 0.001f) return light.position;

    // Disk area light
    std::mt19937 rng(43);
    std::uniform_real_distribution<f32> dist(0.0f, 1.0f);

    f32 r = light.radius * sqrtf(dist(rng));
    f32 theta = 6.28318530718f * dist(rng);

    Vec3 up = fabsf(light.direction.y) < 0.999f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    Vec3 tangent = up.cross(light.direction).normalized();
    Vec3 bitangent = light.direction.cross(tangent);

    Vec3 offset = tangent * (r * cosf(theta)) + bitangent * (r * sinf(theta));
    return light.position + offset;
}

// ============================================================================
// Photon Tracing
// ============================================================================

bool FrostPhotonMapper::tracePhoton(Photon& photon, u32 maxBounces) {
    for (u32 bounce = 0; bounce < maxBounces; bounce++) {
        // Intersect scene
        Vec3 hitPos, hitNormal;
        f32 t;
        u32 materialID, meshID;

        if (!intersectScene(photon.position, photon.direction, t,
                            hitPos, hitNormal, materialID, meshID)) {
            return false;  // photon escaped scene
        }

        // Move to hit point
        photon.position = hitPos + hitNormal * 0.001f;

        // Get material
        if (materialID >= materialCount_) return false;
        const PhotonMaterial& mat = sceneMaterials_[materialID];

        // Store photon at diffuse surfaces
        if (!mat.isSpecular || bounce > 0) {
            // Record direct lighting photons
            if (bounce == 0) {
                stats_.directCount++;
            } else if (isCausticPath(photon)) {
                stats_.causticCount++;
                photon.flags |= 2;  // mark as caustic
            } else {
                stats_.indirectCount++;
            }
        }

        photon.bounceCount = (u16)(bounce + 1);
        photon.normal = hitNormal;

        // Decide bounce type
        f32 randVal = (f32)(std::rand() % 1000) / 1000.0f;

        if (mat.isSpecular) {
            // Specular: reflect or refract
            if (mat.isTransmissive && randVal < mat.isTransmissive) {
                if (!traceRefraction(photon, hitPos, hitNormal, mat, mat.ior)) {
                    return true;
                }
            } else {
                if (!traceReflection(photon, hitPos, hitNormal, mat)) {
                    return true;
                }
            }
        } else {
            // Diffuse: absorb or scatter
            if (randVal > mat.albedo.x) {
                return true;  // absorbed
            }
            if (!traceDiffuse(photon, hitPos, hitNormal, mat)) {
                return true;
            }
        }

        // Russian roulette after first bounce
        if (bounce > 2) {
            f32 survivalProb = std::max({photon.power.x, photon.power.y, photon.power.z});
            survivalProb = std::min(survivalProb, 0.9f);
            if (!russianRoulette(survivalProb)) {
                return true;
            }
        }
    }

    return true;
}

bool FrostPhotonMapper::traceReflection(Photon& photon, Vec3 hitPos, Vec3 hitNormal,
                                         const PhotonMaterial& mat) {
    Vec3 reflDir = computeSpecularDirection(photon.direction, hitNormal, mat.ior);
    photon.direction = reflDir;
    photon.position = hitPos;
    return true;
}

bool FrostPhotonMapper::traceRefraction(Photon& photon, Vec3 hitPos, Vec3 hitNormal,
                                         const PhotonMaterial& mat, f32 eta) {
    f32 cosI = -hitNormal.dot(photon.direction);
    f32 sin2T = eta * eta * (1.0f - cosI * cosI);

    if (sin2T > 1.0f) {
        // Total internal reflection
        Vec3 reflDir = photon.direction + hitNormal * (2.0f * cosI);
        photon.direction = reflDir.normalized();
    } else {
        f32 cosT = sqrtf(1.0f - sin2T);
        Vec3 refrDir = photon.direction * eta + hitNormal * (eta * cosI - cosT);
        photon.direction = refrDir.normalized();
    }

    photon.position = hitPos;
    return true;
}

bool FrostPhotonMapper::traceDiffuse(Photon& photon, Vec3 hitPos, Vec3 hitNormal,
                                      const PhotonMaterial& mat) {
    // Cosine-weighted hemisphere sampling
    std::mt19937 rng(std::rand());
    std::uniform_real_distribution<f32> dist(0.0f, 1.0f);

    f32 u1 = dist(rng);
    f32 u2 = dist(rng);

    f32 r = sqrtf(u1);
    f32 theta = 6.28318530718f * u2;
    f32 x = r * cosf(theta);
    f32 y = r * sinf(theta);
    f32 z = sqrtf(std::max(0.0f, 1.0f - u1));

    Vec3 up = fabsf(hitNormal.y) < 0.999f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    Vec3 tangent = up.cross(hitNormal).normalized();
    Vec3 bitangent = hitNormal.cross(tangent);

    Vec3 scatterDir = tangent * x + bitangent * y + hitNormal * z;
    photon.direction = scatterDir.normalized();
    photon.position = hitPos;

    // Attenuate power by albedo
    photon.power = photon.power * mat.albedo;

    return true;
}

Vec3 FrostPhotonMapper::computeSpecularDirection(Vec3 incident, Vec3 normal, f32 ior) const {
    f32 cosI = -normal.dot(incident);
    f32 eta = 1.0f / ior;
    f32 sin2T = eta * eta * (1.0f - cosI * cosI);
    f32 cosT = sqrtf(std::max(0.0f, 1.0f - sin2T));

    return incident * eta + normal * (eta * cosI - cosT);
}

f32 FrostPhotonMapper::fresnelReflectance(Vec3 incident, Vec3 normal, f32 eta) const {
    f32 cosI = -normal.dot(incident);
    f32 sin2T = eta * eta * (1.0f - cosI * cosI);

    if (sin2T >= 1.0f) return 1.0f;

    f32 cosT = sqrtf(1.0f - sin2T);
    f32 rS = (eta * cosI - cosT) / (eta * cosI + cosT);
    f32 rP = (cosI - eta * cosT) / (cosI + eta * cosT);

    return (rS * rS + rP * rP) * 0.5f;
}

bool FrostPhotonMapper::russianRoulette(f32 survivalProb) const {
    f32 randVal = (f32)(std::rand() % 1000) / 1000.0f;
    return randVal < survivalProb;
}

// ============================================================================
// K-d Tree Building
// ============================================================================

void FrostPhotonMapper::buildKDTree() {
    if (photonCount_ == 0) {
        kdTreeNodeCount_ = 0;
        return;
    }

    Vector<u32> indices;
    indices.resize(photonCount_);
    for (u32 i = 0; i < photonCount_; i++) indices[i] = i;

    kdTreeNodeCount_ = 1;
    buildKDTreeRecursive(0, indices, 0, photonCount_, 0);
    computeSubtreeBounds(0);
}

void FrostPhotonMapper::buildKDTreeRecursive(u32 nodeIdx, Vector<u32>& indices,
                                              u32 start, u32 end, u32 depth) {
    if (start >= end || kdTreeNodeCount_ >= kdTree_.size()) return;

    KDTreeNode& node = kdTree_[nodeIdx];

    if (end - start <= 1 || depth >= KD_TREE_MAX_DEPTH) {
        // Leaf node
        node.photonIndex = indices[start];
        node.photonCount = end - start;
        node.leftChild = 0xFFFFFFFF;
        node.rightChild = 0xFFFFFFFF;
        return;
    }

    // Find best split axis and position
    f32 bestCost = 1e30f;
    u32 bestAxis = 0;
    f32 bestSplit = 0;

    for (u32 axis = 0; axis < 3; axis++) {
        f32 splitCost = 0;
        f32 splitPos = evaluateSAH(indices, start, end, axis, 0);
        if (splitCost < bestCost) {
            bestCost = splitCost;
            bestAxis = axis;
            bestSplit = splitPos;
        }
    }

    node.splitAxis = bestAxis;
    node.splitPoint = Vec3(0);
    node.splitPoint[bestAxis] = bestSplit;

    // Partition photons
    u32 mid = start;
    for (u32 i = start; i < end; i++) {
        if (photons_[indices[i]].position[bestAxis] < bestSplit) {
            std::swap(indices[i], indices[mid]);
            mid++;
        }
    }

    // Ensure progress
    if (mid == start || mid == end) {
        mid = (start + end) / 2;
    }

    // Create children
    u32 leftIdx = kdTreeNodeCount_++;
    u32 rightIdx = kdTreeNodeCount_++;

    node.leftChild = leftIdx;
    node.rightChild = rightIdx;
    node.photonCount = end - start;

    buildKDTreeRecursive(leftIdx, indices, start, mid, depth + 1);
    buildKDTreeRecursive(rightIdx, indices, mid, end, depth + 1);
}

void FrostPhotonMapper::computeSubtreeBounds(u32 nodeIdx) {
    KDTreeNode& node = kdTree_[nodeIdx];

    if (node.leftChild == 0xFFFFFFFF) {
        // Leaf: bounds from photon
        if (node.photonIndex < photonCount_) {
            const Photon& p = photons_[node.photonIndex];
            node.subtreeMin = p.position;
            node.subtreeMax = p.position;
        }
        return;
    }

    // Recurse
    computeSubtreeBounds(node.leftChild);
    computeSubtreeBounds(node.rightChild);

    // Merge children bounds
    node.subtreeMin = kdTree_[node.leftChild].subtreeMin.min(
        kdTree_[node.rightChild].subtreeMin);
    node.subtreeMax = kdTree_[node.leftChild].subtreeMax.max(
        kdTree_[node.rightChild].subtreeMax);
}

u32 FrostPhotonMapper::findBestSplitAxis(const Vector<u32>& indices, u32 start, u32 end,
                                          f32& bestCost) const {
    bestCost = 1e30f;
    u32 bestAxis = 0;

    for (u32 axis = 0; axis < 3; axis++) {
        f32 cost = evaluateSAH(indices, start, end, axis, 0);
        if (cost < bestCost) {
            bestCost = cost;
            bestAxis = axis;
        }
    }

    return bestAxis;
}

f32 FrostPhotonMapper::evaluateSAH(const Vector<u32>& indices, u32 start, u32 end,
                                    u32 axis, f32 splitPos) const {
    // Surface Area Heuristic for k-d tree
    Vec3 boundsMin(1e30f);
    Vec3 boundsMax(-1e30f);

    for (u32 i = start; i < end; i++) {
        boundsMin = boundsMin.min(photons_[indices[i]].position);
        boundsMax = boundsMax.max(photons_[indices[i]].position);
    }

    u32 count = end - start;
    Vec3 extent = boundsMax - boundsMin;
    f32 surfaceArea = 2.0f * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x);

    if (surfaceArea < 0.0001f) return 1e30f;

    // SAH cost: C = traversal + pL * nL * costLeaf + pR * nR * costLeaf
    f32 invArea = 1.0f / surfaceArea;
    f32 cost = (f32)count * invArea;

    return cost;
}

// ============================================================================
// K-d Tree Queries
// ============================================================================

void FrostPhotonMapper::queryNearest(Vec3 point, u32 nodeIdx, u32& nearestIdx,
                                      f32& nearestDistSq, u32& foundCount,
                                      u32 maxCount) const {
    if (nodeIdx >= kdTreeNodeCount_) return;

    const KDTreeNode& node = kdTree_[nodeIdx];

    // AABB distance test
    Vec3 closest;
    closest.x = Mathf::clamp(point.x, node.subtreeMin.x, node.subtreeMax.x);
    closest.y = Mathf::clamp(point.y, node.subtreeMin.y, node.subtreeMax.y);
    closest.z = Mathf::clamp(point.z, node.subtreeMin.z, node.subtreeMax.z);

    f32 aabbDistSq = (closest - point).lengthSquared();
    if (aabbDistSq > nearestDistSq) return;

    if (node.leftChild == 0xFFFFFFFF) {
        // Leaf: test photons
        for (u32 i = 0; i < node.photonCount; i++) {
            u32 pIdx = node.photonIndex + i;
            if (pIdx >= photonCount_) continue;

            f32 distSq = (photons_[pIdx].position - point).lengthSquared();
            if (distSq < nearestDistSq) {
                nearestDistSq = distSq;
                nearestIdx = pIdx;
                foundCount++;
            }
        }
        return;
    }

    // Traverse children
    queryNearest(point, node.leftChild, nearestIdx, nearestDistSq, foundCount, maxCount);
    queryNearest(point, node.rightChild, nearestIdx, nearestDistSq, foundCount, maxCount);
}

void FrostPhotonMapper::queryRadius(Vec3 point, f32 radius, u32 nodeIdx,
                                     Vector<u32>& result) const {
    if (nodeIdx >= kdTreeNodeCount_) return;

    const KDTreeNode& node = kdTree_[nodeIdx];

    // AABB-sphere test
    Vec3 closest;
    closest.x = Mathf::clamp(point.x, node.subtreeMin.x, node.subtreeMax.x);
    closest.y = Mathf::clamp(point.y, node.subtreeMin.y, node.subtreeMax.y);
    closest.z = Mathf::clamp(point.z, node.subtreeMin.z, node.subtreeMax.z);

    f32 distSq = (closest - point).lengthSquared();
    if (distSq > radius * radius) return;

    if (node.leftChild == 0xFFFFFFFF) {
        for (u32 i = 0; i < node.photonCount; i++) {
            u32 pIdx = node.photonIndex + i;
            if (pIdx >= photonCount_) continue;

            f32 d = (photons_[pIdx].position - point).length();
            if (d <= radius) {
                result.push_back(pIdx);
            }
        }
        return;
    }

    queryRadius(point, radius, node.leftChild, result);
    queryRadius(point, radius, node.rightChild, result);
}

// ============================================================================
// Final Gathering — Compute Irradiance at Surface Point
// ============================================================================

Vec3 FrostPhotonMapper::finalGather(Vec3 position, Vec3 normal, f32 gatherRadius,
                                     u32 maxPhotons) const {
    if (photonCount_ == 0 || kdTreeNodeCount_ == 0) return Vec3(0);

    Vector<u32> nearbyPhotons;
    nearbyPhotons.reserve(maxPhotons);

    // Query k-d tree for nearby photons
    queryRadius(position, gatherRadius, 0, nearbyPhotons);

    if (nearbyPhotons.size() == 0) return Vec3(0);

    Vec3 irradiance(0);
    f32 totalWeight = 0;

    // Limit photon count
    u32 count = std::min((u32)nearbyPhotons.size(), maxPhotons);

    for (u32 i = 0; i < count; i++) {
        u32 pIdx = nearbyPhotons[i];
        const Photon& photon = photons_[pIdx];

        // Only consider photons hitting same side of surface
        f32 NdotDir = normal.dot(-photon.direction);
        if (NdotDir <= 0) continue;

        // Distance-based weight
        f32 dist = (photon.position - position).length();
        f32 distWeight = 1.0f / (1.0f + dist * dist);

        // Filter weight
        f32 filterWeight = evaluateFilter(dist, gatherRadius);

        // Final weight
        f32 weight = distWeight * filterWeight * NdotDir;

        irradiance += photon.power * weight;
        totalWeight += weight;
    }

    if (totalWeight > 0.0001f) {
        irradiance = irradiance / totalWeight;
    }

    // Scale by filter reconstruction
    f32 reconstructionScale = 1.0f / (3.14159f * gatherRadius * gatherRadius);
    return irradiance * reconstructionScale;
}

// ============================================================================
// Reconstruction Filters
// ============================================================================

f32 FrostPhotonMapper::coneFilter(f32 dist, f32 radius) const {
    return Mathf::max(0.0f, 1.0f - dist / radius);
}

f32 FrostPhotonMapper::cylinderFilter(f32 dist, f32 radius) const {
    return dist <= radius ? 1.0f : 0.0f;
}

f32 FrostPhotonMapper::gaussianFilter(f32 dist, f32 radius) const {
    f32 sigma = radius / 3.0f;
    f32 expArg = -dist * dist / (2.0f * sigma * sigma);
    return expf(expArg);
}

f32 FrostPhotonMapper::evaluateFilter(f32 dist, f32 radius) const {
    switch (filterType_) {
        case PhotonFilter::Cone:     return coneFilter(dist, radius);
        case PhotonFilter::Cylinder: return cylinderFilter(dist, radius);
        case PhotonFilter::Gaussian: return gaussianFilter(dist, radius);
        default: return gaussianFilter(dist, radius);
    }
}

// ============================================================================
// Scene Intersection
// ============================================================================

bool FrostPhotonMapper::intersectScene(Vec3 origin, Vec3 direction, f32& t,
                                         Vec3& hitPos, Vec3& hitNormal,
                                         u32& materialID, u32& meshID) const {
    f32 closestT = 1e30f;
    bool hit = false;

    for (u32 m = 0; m < meshCount_; m++) {
        const PhotonMeshData& mesh = sceneMeshes_[m];
        u32 triCount = (u32)mesh.indices.size() / 3;

        for (u32 t2 = 0; t2 < triCount; t2++) {
            u32 i0 = mesh.indices[t2 * 3 + 0];
            u32 i1 = mesh.indices[t2 * 3 + 1];
            u32 i2 = mesh.indices[t2 * 3 + 2];

            Vec3 a = mesh.positions[i0];
            Vec3 b = mesh.positions[i1];
            Vec3 c = mesh.positions[i2];

            f32 triT, u, v;
            if (intersectTriangle(origin, direction, a, b, c, triT, u, v)) {
                if (triT > 0.001f && triT < closestT) {
                    closestT = triT;
                    hitPos = origin + direction * triT;
                    hitNormal = mesh.normals[i0];  // simplified
                    materialID = mesh.materialID;
                    meshID = mesh.meshID;
                    hit = true;
                }
            }
        }
    }

    t = closestT;
    return hit;
}

bool FrostPhotonMapper::intersectTriangle(Vec3 origin, Vec3 direction,
                                            Vec3 a, Vec3 b, Vec3 c,
                                            f32& t, f32& u, f32& v) const {
    Vec3 ab = b - a;
    Vec3 ac = c - a;
    Vec3 pvec = direction.cross(ac);
    f32 det = ab.dot(pvec);

    if (det > -0.0001f && det < 0.0001f) return false;

    f32 invDet = 1.0f / det;
    Vec3 tvec = origin - a;
    u = tvec.dot(pvec) * invDet;
    if (u < 0 || u > 1) return false;

    Vec3 qvec = tvec.cross(ab);
    v = direction.dot(qvec) * invDet;
    if (v < 0 || u + v > 1) return false;

    t = ac.dot(qvec) * invDet;
    return t > 0.001f;
}

// ============================================================================
// Caustic Detection
// ============================================================================

bool FrostPhotonMapper::isCausticPath(const Photon& photon) const {
    return (photon.flags & 1) != 0;
}

// ============================================================================
// Photon Splatting (Visualization)
// ============================================================================

void FrostPhotonMapper::splatPhotonsToScreen(const Mat4& viewProj, u32 screenW, u32 screenH,
                                              Vector<Vec3>& screenBuffer) const {
    screenBuffer.resize(screenW * screenH);
    for (auto& p : screenBuffer) p = Vec3(0);

    for (u32 i = 0; i < photonCount_; i++) {
        const Photon& p = photons_[i];

        Vec4 clipPos = viewProj * Vec4(p.position, 1.0f);
        if (clipPos.w <= 0) continue;

        f32 ndcX = clipPos.x / clipPos.w;
        f32 ndcY = clipPos.y / clipPos.w;

        i32 px = (i32)((ndcX * 0.5f + 0.5f) * (f32)screenW);
        i32 py = (i32)((1.0f - (ndcY * 0.5f + 0.5f)) * (f32)screenH);

        if (px >= 0 && px < (i32)screenW && py >= 0 && py < (i32)screenH) {
            u32 idx = (u32)(py * screenW + px);
            screenBuffer[idx] = screenBuffer[idx] + p.power;
        }
    }
}

// ============================================================================
// Advanced Photon Mapping Algorithms
// ============================================================================

Vec3 FrostPhotonMapper::computeCausticIrradiance(Vec3 position, Vec3 normal,
                                                    f32 radius) const {
    // Specialized caustic gathering with directional filtering
    Vector<u32> nearbyPhotons;
    queryRadius(position, radius, 0, nearbyPhotons);

    Vec3 causticIrradiance(0);
    f32 totalWeight = 0;

    for (u32 i = 0; i < nearbyPhotons.size(); i++) {
        u32 pIdx = nearbyPhotons[i];
        const Photon& photon = photons_[pIdx];

        // Only consider caustic photons (specular -> diffuse paths)
        if (!(photon.flags & 2)) continue;

        f32 dist = (photon.position - position).length();
        if (dist < 0.001f) continue;

        // Directional weight: photons arriving from specular surfaces
        Vec3 toPhoton = (photon.position - position) / dist;
        f32 NdotL = Mathf::max(normal.dot(-photon.direction), 0.0f);

        // Cone filter for sharper caustics
        f32 filterWeight = coneFilter(dist, radius);

        f32 weight = NdotL * filterWeight;
        causticIrradiance += photon.power * weight;
        totalWeight += weight;
    }

    return totalWeight > 0 ? causticIrradiance / totalWeight : Vec3(0);
}

Vec3 FrostPhotonMapper::computeIndirectIrradiance(Vec3 position, Vec3 normal,
                                                     f32 radius) const {
    // Indirect lighting from diffuse-diffuse paths
    Vector<u32> nearbyPhotons;
    queryRadius(position, radius, 0, nearbyPhotons);

    Vec3 indirectIrradiance(0);
    f32 totalWeight = 0;

    for (u32 i = 0; i < nearbyPhotons.size(); i++) {
        u32 pIdx = nearbyPhotons[i];
        const Photon& photon = photons_[pIdx];

        // Skip caustic photons
        if (photon.flags & 2) continue;

        f32 dist = (photon.position - position).length();
        if (dist < 0.001f) continue;

        f32 NdotL = Mathf::max(normal.dot(-photon.direction), 0.0f);
        f32 filterWeight = gaussianFilter(dist, radius);

        f32 weight = NdotL * filterWeight;
        indirectIrradiance += photon.power * weight;
        totalWeight += weight;
    }

    return totalWeight > 0 ? indirectIrradiance / totalWeight : Vec3(0);
}

// ============================================================================
// Photon Power Estimation
// ============================================================================

f32 FrostPhotonMapper::estimatePhotonDensity(Vec3 position, f32 radius) const {
    Vector<u32> nearbyPhotons;
    queryRadius(position, radius, 0, nearbyPhotons);

    f32 totalPower = 0;
    for (u32 i = 0; i < nearbyPhotons.size(); i++) {
        const Photon& p = photons_[nearbyPhotons[i]];
        totalPower += (p.power.x + p.power.y + p.power.z) / 3.0f;
    }

    // Normalize by volume
    f32 volume = (4.0f / 3.0f) * 3.14159f * radius * radius * radius;
    return totalPower / volume;
}

f32 FrostPhotonMapper::estimatePhotonPowerAt(Vec3 position, f32 radius) const {
    Vector<u32> nearbyPhotons;
    queryRadius(position, radius, 0, nearbyPhotons);

    Vec3 totalPower(0);
    for (u32 i = 0; i < nearbyPhotons.size(); i++) {
        totalPower += photons_[nearbyPhotons[i]].power;
    }

    return (totalPower.x + totalPower.y + totalPower.z) / 3.0f;
}

// ============================================================================
// K-d Tree Quality Metrics
// ============================================================================

u32 FrostPhotonMapper::computeKDTreeDepth() const {
    if (kdTreeNodeCount_ == 0) return 0;

    u32 maxDepth = 0;
    struct StackEntry { u32 nodeIdx; u32 depth; };
    Vector<StackEntry> stack;
    stack.push_back({0, 0});

    while (stack.size() > 0) {
        StackEntry entry = stack[stack.size() - 1];
        stack.pop();

        maxDepth = std::max(maxDepth, entry.depth);

        const KDTreeNode& node = kdTree_[entry.nodeIdx];
        if (node.leftChild != 0xFFFFFFFF) {
            stack.push_back({node.leftChild, entry.depth + 1});
        }
        if (node.rightChild != 0xFFFFFFFF) {
            stack.push_back({node.rightChild, entry.depth + 1});
        }
    }

    return maxDepth;
}

f32 FrostPhotonMapper::computeKDTreeBalance() const {
    if (kdTreeNodeCount_ == 0) return 1.0f;

    // Measure balance by comparing left and right subtree sizes
    u32 leftSize = 0, rightSize = 0;

    struct StackEntry { u32 nodeIdx; bool isLeft; };
    Vector<StackEntry> stack;

    if (kdTree_[0].leftChild != 0xFFFFFFFF) {
        stack.push_back({kdTree_[0].leftChild, true});
    }
    if (kdTree_[0].rightChild != 0xFFFFFFFF) {
        stack.push_back({kdTree_[0].rightChild, false});
    }

    while (stack.size() > 0) {
        StackEntry entry = stack[stack.size() - 1];
        stack.pop();

        if (entry.isLeft) leftSize++;
        else rightSize++;

        const KDTreeNode& node = kdTree_[entry.nodeIdx];
        if (node.leftChild != 0xFFFFFFFF) stack.push_back({node.leftChild, entry.isLeft});
        if (node.rightChild != 0xFFFFFFFF) stack.push_back({node.rightChild, entry.isLeft});
    }

    u32 total = leftSize + rightSize;
    return total > 0 ? (f32)std::min(leftSize, rightSize) / (f32)std::max(leftSize, rightSize) : 1.0f;
}

f32 FrostPhotonMapper::computeSAHCost() const {
    // Estimate SAH cost of the k-d tree
    f32 totalCost = 0;

    for (u32 i = 0; i < kdTreeNodeCount_; i++) {
        const KDTreeNode& node = kdTree_[i];
        if (node.photonCount > 0) {
            Vec3 extent = node.subtreeMax - node.subtreeMin;
            f32 surfaceArea = 2.0f * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x);
            totalCost += (f32)node.photonCount / (surfaceArea + 0.0001f);
        }
    }

    return totalCost;
}

// ============================================================================
// Photon Map Visualization
// ============================================================================

Vector<Vec3> FrostPhotonMapper::getPhotonPositions() const {
    Vector<Vec3> positions;
    positions.reserve(photonCount_);

    for (u32 i = 0; i < photonCount_; i++) {
        positions.push_back(photons_[i].position);
    }

    return positions;
}

Vector<Vec3> FrostPhotonMapper::getPhotonPowers() const {
    Vector<Vec3> powers;
    powers.reserve(photonCount_);

    for (u32 i = 0; i < photonCount_; i++) {
        powers.push_back(photons_[i].power);
    }

    return powers;
}

Vector<Vec3> FrostPhotonMapper::getPhotonNormals() const {
    Vector<Vec3> normals;
    normals.reserve(photonCount_);

    for (u32 i = 0; i < photonCount_; i++) {
        normals.push_back(photons_[i].normal);
    }

    return normals;
}

// ============================================================================
// Progressive Photon Mapping
// ============================================================================

void FrostPhotonMapper::resetProgressive() {
    progressiveFrame_ = 0;
    photonCount_ = 0;
    nextPhotonSlot_ = 0;
    memset(&stats_, 0, sizeof(stats_));
}

f32 FrostPhotonMapper::computeProgressiveConvergence() const {
    if (progressiveFrame_ < 2) return 0;

    // Estimate convergence from photon density stability
    f32 densityVariance = 0;
    u32 sampleCount = 100;

    std::mt19937 rng(42);
    std::uniform_real_distribution<f32> posDist(-10.0f, 10.0f);

    for (u32 i = 0; i < sampleCount; i++) {
        Vec3 pos(posDist(rng), posDist(rng), posDist(rng));
        f32 density = estimatePhotonDensity(pos, 1.0f);
        densityVariance += density * density;
    }

    return 1.0f / (1.0f + densityVariance / (f32)sampleCount);
}

// ============================================================================
// Photon Splitting
// ============================================================================

void FrostPhotonMapper::splitPhoton(Photon& photon, u32& newPhotonIdx) {
    // Split photon at specular surfaces for better coverage
    if (photon.power.length() < 0.001f) return;

    // Create child photon
    newPhotonIdx = nextPhotonSlot_++;
    if (newPhotonIdx >= photons_.size()) {
        newPhotonIdx = 0xFFFFFFFF;
        return;
    }

    Photon& child = photons_[newPhotonIdx];
    child = photon;

    // Split power equally
    photon.power = photon.power * 0.5f;
    child.power = child.power * 0.5f;

    // Slightly perturb direction
    f32 perturbation = 0.1f;
    child.direction.x += (std::rand() % 1000 - 500) / 5000.0f * perturbation;
    child.direction.y += (std::rand() % 1000 - 500) / 5000.0f * perturbation;
    child.direction.z += (std::rand() % 1000 - 500) / 5000.0f * perturbation;
    child.direction = child.direction.normalized();
}

// ============================================================================
// Advanced Scene Intersection
// ============================================================================

bool FrostPhotonMapper::intersectSceneAABB(Vec3 origin, Vec3 dir,
                                             Vec3 bmin, Vec3 bmax,
                                             f32& tmin, f32& tmax) const {
    f32 invDirX = 1.0f / (fabsf(dir.x) + 0.0001f);
    f32 invDirY = 1.0f / (fabsf(dir.y) + 0.0001f);
    f32 invDirZ = 1.0f / (fabsf(dir.z) + 0.0001f);

    f32 t1 = (bmin.x - origin.x) * invDirX;
    f32 t2 = (bmax.x - origin.x) * invDirX;
    if (t1 > t2) std::swap(t1, t2);
    tmin = t1; tmax = t2;

    t1 = (bmin.y - origin.y) * invDirY;
    t2 = (bmax.y - origin.y) * invDirY;
    if (t1 > t2) std::swap(t1, t2);
    tmin = std::max(tmin, t1);
    tmax = std::min(tmax, t2);

    t1 = (bmin.z - origin.z) * invDirZ;
    t2 = (bmax.z - origin.z) * invDirZ;
    if (t1 > t2) std::swap(t1, t2);
    tmin = std::max(tmin, t1);
    tmax = std::min(tmax, t2);

    return tmin <= tmax && tmax > 0;
}

f32 FrostPhotonMapper::computeSceneBoundsVolume() const {
    Vec3 min(1e30f), max(-1e30f);
    for (u32 i = 0; i < meshCount_; i++) {
        for (u32 v = 0; v < sceneMeshes_[i].positions.size(); v++) {
            min = min.min(sceneMeshes_[i].positions[v]);
            max = max.max(sceneMeshes_[i].positions[v]);
        }
    }
    Vec3 extent = max - min;
    return extent.x * extent.y * extent.z;
}

u32 FrostPhotonMapper::getTriangleCount() const {
    u32 count = 0;
    for (u32 i = 0; i < meshCount_; i++) {
        count += (u32)sceneMeshes_[i].indices.size() / 3;
    }
    return count;
}

// ============================================================================
// Photon Map Query Optimization
// ============================================================================

void FrostPhotonMapper::computeNearestPhotons(Vec3 position, f32 radius,
                                                u32 maxPhotons,
                                                Vector<u32>& result) const {
    result.clear();
    result.reserve(maxPhotons);

    // Priority queue for k-nearest neighbors
    f32 maxDistSq = radius * radius;

    for (u32 i = 0; i < photonCount_; i++) {
        f32 distSq = (photons_[i].position - position).lengthSquared();
        if (distSq < maxDistSq) {
            result.push_back(i);
        }
    }

    // Sort by distance
    for (u32 i = 0; i < result.size() - 1; i++) {
        for (u32 j = i + 1; j < result.size(); j++) {
            f32 d1 = (photons_[result[i]].position - position).lengthSquared();
            f32 d2 = (photons_[result[j]].position - position).lengthSquared();
            if (d2 < d1) {
                u32 temp = result[i];
                result[i] = result[j];
                result[j] = temp;
            }
        }
    }

    // Limit to maxPhotons
    if (result.size() > maxPhotons) {
        result.resize(maxPhotons);
    }
}

// ============================================================================
// Photon Power Normalization
// ============================================================================

void FrostPhotonMapper::normalizePhotonPower() {
    if (photonCount_ == 0) return;

    // Compute total power
    Vec3 totalPower(0);
    for (u32 i = 0; i < photonCount_; i++) {
        totalPower += photons_[i].power;
    }

    // Normalize so average power = 1
    f32 avgPower = (totalPower.x + totalPower.y + totalPower.z) / 3.0f;
    if (avgPower > 0) {
        f32 normFactor = 1.0f / avgPower;
        for (u32 i = 0; i < photonCount_; i++) {
            photons_[i].power = photons_[i].power * normFactor;
        }
    }
}

f32 FrostPhotonMapper::computeAveragePhotonPower() const {
    if (photonCount_ == 0) return 0;

    Vec3 totalPower(0);
    for (u32 i = 0; i < photonCount_; i++) {
        totalPower += photons_[i].power;
    }

    return (totalPower.x + totalPower.y + totalPower.z) / (3.0f * (f32)photonCount_);
}

// ============================================================================
// Photon Map Density Estimation
// ============================================================================

f32 FrostPhotonMapper::estimateLocalDensity(Vec3 position, u32 k) const {
    // k-nearest neighbor density estimation
    Vector<u32> nearest;
    nearest.reserve(k);

    f32 maxDistSq = 1e30f;

    for (u32 i = 0; i < photonCount_; i++) {
        f32 distSq = (photons_[i].position - position).lengthSquared();

        if (nearest.size() < k) {
            nearest.push_back(i);
            if (distSq < maxDistSq) maxDistSq = distSq;
        } else if (distSq < maxDistSq) {
            // Replace farthest
            u32 farthestIdx = 0;
            f32 farthestDist = 0;
            for (u32 j = 0; j < nearest.size(); j++) {
                f32 d = (photons_[nearest[j]].position - position).lengthSquared();
                if (d > farthestDist) {
                    farthestDist = d;
                    farthestIdx = j;
                }
            }
            nearest[farthestIdx] = i;
            maxDistSq = distSq;
        }
    }

    if (nearest.size() < k) return 0;

    // Density = k / volume
    f32 radius = sqrtf(maxDistSq);
    f32 volume = (4.0f / 3.0f) * 3.14159f * radius * radius * radius;

    return volume > 0 ? (f32)k / volume : 0;
}

// ============================================================================
// Advanced Photon Tracing
// ============================================================================

bool FrostPhotonMapper::traceCausticPath(Photon& photon, u32 maxBounces) {
    // Specialized tracing for caustic paths (specular -> diffuse)
    bool hitSpecular = false;

    for (u32 bounce = 0; bounce < maxBounces; bounce++) {
        Vec3 hitPos, hitNormal;
        f32 t;
        u32 materialID, meshID;

        if (!intersectScene(photon.position, photon.direction, t,
                            hitPos, hitNormal, materialID, meshID)) {
            return false;
        }

        photon.position = hitPos + hitNormal * 0.001f;
        photon.normal = hitNormal;

        if (materialID >= materialCount_) return false;
        const PhotonMaterial& mat = sceneMaterials_[materialID];

        if (mat.isSpecular) {
            hitSpecular = true;
            // Reflect or refract
            Vec3 reflDir = computeSpecularDirection(photon.direction, hitNormal, mat.ior);
            photon.direction = reflDir;
        } else if (hitSpecular) {
            // Hit diffuse after specular: this is a caustic!
            photon.flags |= 2;  // mark as caustic
            stats_.causticCount++;
            return true;
        } else {
            // Diffuse without prior specular: regular path
            return false;
        }
    }

    return false;
}

Vec3 FrostPhotonMapper::computeRefractionDir(Vec3 incident, Vec3 normal,
                                               float eta) const {
    float cosI = -normal.dot(incident);
    float sin2T = eta * eta * (1.0f - cosI * cosI);

    if (sin2T >= 1.0f) return Vec3(0);  // total internal reflection

    float cosT = sqrtf(1.0f - sin2T);
    return incident * eta + normal * (eta * cosI - cosT);
}

float FrostPhotonMapper::computeFresnel(Vec3 incident, Vec3 normal,
                                          float ior) const {
    float cosI = -normal.dot(incident);
    float eta = 1.0f / ior;
    float sin2T = eta * eta * (1.0f - cosI * cosI);

    if (sin2T >= 1.0f) return 1.0f;

    float cosT = sqrtf(1.0f - sin2T);
    float rS = (eta * cosI - cosT) / (eta * cosI + cosT);
    float rP = (cosI - eta * cosT) / (cosI + eta * cosT);

    return (rS * rS + rP * rP) * 0.5f;
}

// ============================================================================
// Photon Map Statistics
// ============================================================================

void FrostPhotonMapper::getDetailedStats(u32& totalPhotons, u32& causticPhotons,
                                           u32& indirectPhotons, f32& avgBounces,
                                           f32& avgPower) const {
    totalPhotons = photonCount_;
    causticPhotons = stats_.causticCount;
    indirectPhotons = stats_.indirectCount;
    avgBounces = stats_.avgBounces;
    avgPower = computeAveragePhotonPower();
}

f32 FrostPhotonMapper::computePhotonMapCoverage() const {
    if (photonCount_ == 0) return 0;

    // Estimate coverage by checking photon spread
    Vec3 min(1e30f), max(-1e30f);
    for (u32 i = 0; i < photonCount_; i++) {
        min = min.min(photons_[i].position);
        max = max.max(photons_[i].position);
    }

    Vec3 extent = max - min;
    f32 volume = extent.x * extent.y * extent.z;

    // Coverage = photons per unit volume
    return volume > 0 ? (f32)photonCount_ / volume : 0;
}

} // namespace Frost
