// ============================================================================
// FrostEngine Nanite-like Virtualized Geometry - Implementation
// ============================================================================

#include "FrostEngine/Renderer/NaniteSystem.h"
#include "FrostEngine/Renderer/Camera.h"
#include "FrostEngine/Core/Math.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Frost {

// ============================================================================
// Constructor / Destructor
// ============================================================================
NaniteSystem::NaniteSystem() = default;

NaniteSystem::~NaniteSystem() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================
bool NaniteSystem::init() {
    if (initialized_) return true;

    // Initialize occlusion hierarchy
    hierarchyWidth_ = 0;
    hierarchyHeight_ = 0;

    initialized_ = true;
    return true;
}

void NaniteSystem::shutdown() {
    if (!initialized_) return;

    clusters_.clear();
    meshes_.clear();
    lodDescriptors_.clear();
    occlusionHierarchy_.clear();
    visibleClusters_.clear();
    materialBatches_.clear();
    fallbackMeshes_.clear();
    hierarchicalDepthBuffer_.clear();

    initialized_ = false;
}

// ============================================================================
// Mesh Management
// ============================================================================
u32 NaniteSystem::addMesh(const Vector<Vec3>& positions, const Vector<Vec3>& normals,
                           const Vector<Vec2>& uvs, const Vector<u32>& indices,
                           u32 materialIndex, const Mat4& initialTransform) {
    u32 meshId = static_cast<u32>(meshes_.size());

    NaniteMeshDescriptor desc;
    desc.meshId = meshId;
    desc.totalTriangles = static_cast<u32>(indices.size() / 3);
    desc.totalVertices = static_cast<u32>(positions.size());
    desc.lodCount = 1;

    // Compute bounds
    if (positions.size() > 0) {
        desc.boundsMin = positions[0];
        desc.boundsMax = positions[0];
        for (u32 i = 1; i < positions.size(); i++) {
            desc.boundsMin = desc.boundsMin.min(positions[i]);
            desc.boundsMax = desc.boundsMax.max(positions[i]);
        }
    }
    desc.boundsCenter = (desc.boundsMin + desc.boundsMax) * 0.5f;
    desc.boundsRadius = (desc.boundsMax - desc.boundsMin).length() * 0.5f;

    // Split mesh into clusters
    splitMeshIntoClusters(meshId, positions, normals, uvs, indices, materialIndex);

    // Build DAG hierarchy
    buildClusterHierarchy(meshId);

    // Build LOD hierarchy
    buildLODHierarchy(meshId);

    // Create fallback mesh
    FallbackMesh fallback;
    fallback.positions = positions;
    fallback.indices = indices;
    fallbackMeshes_.push_back(fallback);
    desc.fallbackIndexCount = static_cast<u32>(indices.size());
    desc.fallbackVertexCount = static_cast<u32>(positions.size());

    // Store descriptor
    desc.lods[0].clusterStart = 0;
    desc.lods[0].clusterCount = static_cast<u32>(clusters_.size());
    desc.lods[0].totalTriangles = desc.totalTriangles;
    desc.lods[0].totalVertices = desc.totalVertices;
    desc.lods[0].screenErrorThreshold = 1.0f;

    meshes_.push_back(desc);

    return meshId;
}

void NaniteSystem::removeMesh(u32 meshId) {
    if (meshId >= meshes_.size()) return;

    // Mark clusters as invalid
    for (auto& c : clusters_) {
        if (c.meshId == meshId) {
            c.triangleCount = 0;
            c.vertexCount = 0;
        }
    }
}

void NaniteSystem::updateMeshTransform(u32 meshId, const Mat4& transform) {
    if (meshId >= meshes_.size()) return;

    // Update all clusters for this mesh
    for (auto& c : clusters_) {
        if (c.meshId == meshId) {
            // Transform cluster bounds
            Vec3 newCenter = Vec3(
                transform.m[0] * c.bounds.center.x + transform.m[4] * c.bounds.center.y +
                transform.m[8] * c.bounds.center.z + transform.m[12],
                transform.m[1] * c.bounds.center.x + transform.m[5] * c.bounds.center.y +
                transform.m[9] * c.bounds.center.z + transform.m[13],
                transform.m[2] * c.bounds.center.x + transform.m[6] * c.bounds.center.y +
                transform.m[10] * c.bounds.center.z + transform.m[14]
            );
            c.bounds.center = newCenter;

            // Transform positions
            for (u32 v = 0; v < c.vertexCount; v++) {
                Vec3& pos = c.positions[v];
                Vec3 newPos = Vec3(
                    transform.m[0] * pos.x + transform.m[4] * pos.y + transform.m[8] * pos.z + transform.m[12],
                    transform.m[1] * pos.x + transform.m[5] * pos.y + transform.m[9] * pos.z + transform.m[13],
                    transform.m[2] * pos.x + transform.m[6] * pos.y + transform.m[10] * pos.z + transform.m[14]
                );
                pos = newPos;
            }

            computeClusterBounds(c);
        }
    }
}

// ============================================================================
// Cluster Generation
// ============================================================================
void NaniteSystem::splitMeshIntoClusters(u32 meshId, const Vector<Vec3>& positions,
                                          const Vector<Vec3>& normals, const Vector<Vec2>& uvs,
                                          const Vector<u32>& indices, u32 materialIndex) {
    u32 totalTriangles = static_cast<u32>(indices.size() / 3);

    // Simple greedy clustering: group triangles spatially
    u32 clusterIndex = static_cast<u32>(clusters_.size());

    for (u32 triStart = 0; triStart < totalTriangles; triStart += NANITE_CLUSTER_TRIANGLES) {
        Cluster cluster;
        cluster.clusterId = clusterIndex;
        cluster.meshId = meshId;
        cluster.lodLevel = 0;
        cluster.parentClusterIndex = UINT32_MAX;
        cluster.childIndices[0] = UINT32_MAX;
        cluster.childIndices[1] = UINT32_MAX;
        cluster.vboHandle = 0;
        cluster.eboHandle = 0;
        cluster.vaoHandle = 0;
        cluster.visible = true;
        cluster.occluded = false;
        cluster.rasterized = false;
        cluster.screenError = 0.0f;

        u32 triEnd = Mathf::min(triStart + NANITE_CLUSTER_TRIANGLES, totalTriangles);
        cluster.triangleCount = triEnd - triStart;

        // Extract unique vertices for this cluster
        Vector<u32> uniqueIndices;
        Vector<u32> localIndices;
        Vector<u32> indexRemap;

        for (u32 t = triStart; t < triEnd; t++) {
            for (u32 v = 0; v < 3; v++) {
                u32 globalIdx = indices[t * 3 + v];

                // Check if already in this cluster
                i32 existing = -1;
                for (u32 u = 0; u < uniqueIndices.size(); u++) {
                    if (uniqueIndices[u] == globalIdx) {
                        existing = (i32)u;
                        break;
                    }
                }

                if (existing >= 0) {
                    localIndices.push_back((u32)existing);
                } else {
                    u32 localIdx = static_cast<u32>(uniqueIndices.size());
                    uniqueIndices.push_back(globalIdx);
                    localIndices.push_back(localIdx);

                    // Copy vertex data
                    if (globalIdx < positions.size()) {
                        cluster.positions[localIdx] = positions[globalIdx];
                        cluster.normals[localIdx] = (globalIdx < normals.size()) ?
                                                     normals[globalIdx] : Vec3(0, 1, 0);
                        cluster.uvs[localIdx] = (globalIdx < uvs.size()) ?
                                                  uvs[globalIdx] : Vec2(0, 0);
                        cluster.tangents[localIdx] = Vec4(1, 0, 0, 1);
                    }
                }
            }
        }

        cluster.vertexCount = static_cast<u32>(uniqueIndices.size());

        // Build triangle index data
        for (u32 t = 0; t < cluster.triangleCount; t++) {
            ClusterTriangle& tri = cluster.triangles[t];
            tri.indices[0] = localIndices[t * 3 + 0];
            tri.indices[1] = localIndices[t * 3 + 1];
            tri.indices[2] = localIndices[t * 3 + 2];
            tri.materialIndex = materialIndex;
        }

        computeClusterBounds(cluster);
        clusters_.push_back(cluster);
        clusterIndex++;
    }
}

void NaniteSystem::computeClusterBounds(Cluster& cluster) {
    if (cluster.vertexCount == 0) return;

    cluster.bounds.center = cluster.positions[0];
    cluster.bounds.boundsMin = cluster.positions[0];
    cluster.bounds.boundsMax = cluster.positions[0];

    for (u32 v = 1; v < cluster.vertexCount; v++) {
        cluster.bounds.boundsMin = cluster.bounds.boundsMin.min(cluster.positions[v]);
        cluster.bounds.boundsMax = cluster.bounds.boundsMax.max(cluster.positions[v]);
    }

    cluster.bounds.center = (cluster.bounds.boundsMin + cluster.bounds.boundsMax) * 0.5f;
    cluster.bounds.radius = (cluster.bounds.boundsMax - cluster.bounds.boundsMin).length() * 0.5f;

    // Compute normal cone for backface culling
    Vec3 avgNormal = Vec3(0);
    f32 maxCos = -1.0f;
    f32 minCos = 1.0f;

    for (u32 t = 0; t < cluster.triangleCount; t++) {
        Vec3 v0 = cluster.positions[cluster.triangles[t].indices[0]];
        Vec3 v1 = cluster.positions[cluster.triangles[t].indices[1]];
        Vec3 v2 = cluster.positions[cluster.triangles[t].indices[2]];

        Vec3 edge1 = v1 - v0;
        Vec3 edge2 = v2 - v0;
        Vec3 normal = edge1.cross(edge2).normalized();

        avgNormal = avgNormal + normal;
    }

    if (cluster.triangleCount > 0) {
        cluster.bounds.coneAxis = avgNormal / (f32)cluster.triangleCount;
        cluster.bounds.coneAxis = cluster.bounds.coneAxis.normalized();
    } else {
        cluster.bounds.coneAxis = Vec3(0, 1, 0);
    }

    cluster.bounds.coneCutoff = 0.0f;
}

// ============================================================================
// DAG Hierarchy
// ============================================================================
void NaniteSystem::buildClusterHierarchy(u32 meshId) {
    // Find all clusters for this mesh
    Vector<u32> meshClusters;
    for (u32 i = 0; i < clusters_.size(); i++) {
        if (clusters_[i].meshId == meshId && clusters_[i].lodLevel == 0) {
            meshClusters.push_back(i);
        }
    }

    // Build bottom-up hierarchy
    u32 level = 0;
    while (meshClusters.size() > 1) {
        Vector<u32> parentClusters;

        for (u32 i = 0; i < meshClusters.size(); i += NANITE_DAG_FANOUT) {
            Cluster parentCluster;
            parentCluster.clusterId = static_cast<u32>(clusters_.size());
            parentCluster.meshId = meshId;
            parentCluster.lodLevel = level + 1;
            parentCluster.parentClusterIndex = UINT32_MAX;
            parentCluster.vboHandle = 0;
            parentCluster.eboHandle = 0;
            parentCluster.vaoHandle = 0;
            parentCluster.visible = true;
            parentCluster.occluded = false;
            parentCluster.rasterized = false;

            // Merge children
            Vec3 boundsMin = Vec3(1e30f);
            Vec3 boundsMax = Vec3(-1e30f);
            Vec3 avgNormal = Vec3(0);
            u32 totalTriangles = 0;
            u32 totalVertices = 0;

            u32 childCount = Mathf::min(NANITE_DAG_FANOUT,
                                        (u32)(meshClusters.size() - i));
            for (u32 c = 0; c < childCount; c++) {
                u32 childIdx = meshClusters[i + c];
                Cluster& child = clusters_[childIdx];

                boundsMin = boundsMin.min(child.bounds.boundsMin);
                boundsMax = boundsMax.max(child.bounds.boundsMax);
                avgNormal = avgNormal + child.bounds.coneAxis;
                totalTriangles += child.triangleCount;
                totalVertices += child.vertexCount;

                parentCluster.childIndices[c] = childIdx;
                child.parentClusterIndex = parentCluster.clusterId;
            }

            parentCluster.bounds.boundsMin = boundsMin;
            parentCluster.bounds.boundsMax = boundsMax;
            parentCluster.bounds.center = (boundsMin + boundsMax) * 0.5f;
            parentCluster.bounds.radius = (boundsMax - boundsMin).length() * 0.5f;
            parentCluster.bounds.coneAxis = avgNormal / (f32)childCount;
            parentCluster.bounds.coneAxis = parentCluster.bounds.coneAxis.normalized();
            parentCluster.bounds.coneCutoff = 0.0f;
            parentCluster.triangleCount = totalTriangles;
            parentCluster.vertexCount = totalVertices;

            // Compute screen-size threshold
            parentCluster.bounds.screenSizeThreshold =
                parentCluster.bounds.radius / (parentCluster.bounds.radius + 1.0f) * 100.0f;

            clusters_.push_back(parentCluster);
            parentClusters.push_back(parentCluster.clusterId);
        }

        meshClusters = parentClusters;
        level++;
    }
}

void NaniteSystem::buildLODHierarchy(u32 meshId) {
    // Compute screen-space error thresholds for each LOD level
    for (auto& mesh : meshes_) {
        if (mesh.meshId != meshId) continue;

        f32 baseError = 1.0f;
        for (u32 lod = 0; lod < NANITE_MAX_LODS; lod++) {
            mesh.lods[lod].screenErrorThreshold = baseError * (1 << lod);
        }
    }
}

// ============================================================================
// Frame Processing
// ============================================================================
void NaniteSystem::beginFrame(const Camera& camera, u32 screenWidth, u32 screenHeight) {
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
    camera_ = &camera;
    cameraPosition_ = camera.position();
    viewProjMatrix_ = camera.viewProj();
    frameIndex_++;

    // Reset stats
    stats_.visibleClusters = 0;
    stats_.culledClusters = 0;
    stats_.occludedClusters = 0;
    stats_.renderedTriangles = 0;
    stats_.materialBatches = 0;
    stats_.drawCalls = 0;

    // Rebuild occlusion hierarchy if needed
    if (hierarchyWidth_ != screenWidth || hierarchyHeight_ != screenHeight) {
        hierarchyWidth_ = screenWidth;
        hierarchyHeight_ = screenHeight;
        buildOcclusionHierarchy();
    }
}

void NaniteSystem::cull() {
    f32 cullStart = 0.0f; // Would use GPU timer

    // Select LODs based on screen-space error
    for (auto& mesh : meshes_) {
        selectLODs(mesh.meshId, cameraPosition_, viewProjMatrix_);
    }

    // Frustum culling
    frustumCull(*camera_);

    // Occlusion culling
    hierarchicalOcclusionCull(*camera_);

    stats_.cullingTimeMs = 0.0f; // Would be measured with GPU timer
}

void NaniteSystem::selectLODs(u32 meshId, const Vec3& viewPos, const Mat4& viewProj) {
    for (auto& mesh : meshes_) {
        if (mesh.meshId != meshId) continue;

        // For each cluster, compute screen-space error
        for (auto& c : clusters_) {
            if (c.meshId != meshId || c.lodLevel > 0) continue;

            f32 screenError = computeScreenSpaceError(c, viewPos, viewProj);

            // Determine which LOD this cluster should be at
            u32 targetLOD = 0;
            for (u32 lod = 0; lod < mesh.lodCount; lod++) {
                if (screenError < mesh.lods[lod].screenErrorThreshold) {
                    targetLOD = lod;
                }
            }

            c.screenError = screenError;

            // Visibility based on LOD selection
            c.visible = (targetLOD == 0); // Simplified: show cluster if at highest LOD
        }
    }
}

f32 NaniteSystem::computeScreenSpaceError(const Cluster& cluster, const Vec3& viewPos,
                                           const Mat4& viewProj) const {
    f32 dist = (cluster.bounds.center - viewPos).length();
    if (dist < 0.001f) dist = 0.001f;

    // Screen-space error = (world error / distance) * screen height
    f32 worldError = cluster.bounds.radius;
    f32 screenError = (worldError / dist) * (f32)screenHeight_;

    return screenError;
}

void NaniteSystem::buildRenderLists() {
    visibleClusters_.clear();

    for (auto& c : clusters_) {
        if (!c.visible || c.occluded || c.lodLevel > 0) continue;
        if (c.triangleCount == 0) continue;

        VisibleCluster vc;
        vc.clusterIndex = c.clusterId;
        vc.meshId = c.meshId;
        vc.materialIndex = c.triangles[0].materialIndex;
        vc.screenError = c.screenError;
        vc.triangleStart = 0;
        vc.triangleCount = c.triangleCount;

        visibleClusters_.push_back(vc);
    }

    stats_.visibleClusters = static_cast<u32>(visibleClusters_.size());
}

void NaniteSystem::batchByMaterial() {
    sortClustersByMaterial();
    buildBatchTable();
}

void NaniteSystem::sortClustersByMaterial() {
    // Simple insertion sort by material index
    for (u32 i = 1; i < visibleClusters_.size(); i++) {
        VisibleCluster key = visibleClusters_[i];
        u32 j = i - 1;
        while (j < visibleClusters_.size() && visibleClusters_[j].materialIndex > key.materialIndex) {
            visibleClusters_[j + 1] = visibleClusters_[j];
            if (j == 0) break;
            j--;
        }
        visibleClusters_[j + (j < visibleClusters_.size() ? 1 : 0)] = key;
    }
}

void NaniteSystem::buildBatchTable() {
    materialBatches_.clear();

    for (u32 i = 0; i < visibleClusters_.size(); i++) {
        u32 matIdx = visibleClusters_[i].materialIndex;

        // Find existing batch
        i32 batchIdx = -1;
        for (u32 b = 0; b < materialBatches_.size(); b++) {
            if (materialBatches_[b].materialIndex == matIdx) {
                batchIdx = (i32)b;
                break;
            }
        }

        if (batchIdx >= 0) {
            materialBatches_[batchIdx].clusters.push_back(&visibleClusters_[i]);
            materialBatches_[batchIdx].totalTriangles += visibleClusters_[i].triangleCount;
        } else {
            MaterialBatch batch;
            batch.materialIndex = matIdx;
            batch.totalTriangles = visibleClusters_[i].triangleCount;
            batch.clusters.push_back(&visibleClusters_[i]);
            materialBatches_.push_back(batch);
        }
    }

    stats_.materialBatches = static_cast<u32>(materialBatches_.size());
}

void NaniteSystem::prepareDraws() {
    // Upload cluster data to GPU
    for (auto& c : clusters_) {
        if (c.visible && !c.occluded && c.vboHandle == 0) {
            uploadClusterToGPU(c);
        }
    }

    stats_.drawCalls = stats_.materialBatches;
}

// ============================================================================
// Frustum Culling
// ============================================================================
void NaniteSystem::frustumCull(const Camera& camera) {
    // Get frustum planes from camera
    Vec4 planes[6];
    // Simplified: extract frustum planes from view-projection matrix
    const f32* m = viewProjMatrix_.m;

    // Left
    planes[0] = Vec4(m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]);
    // Right
    planes[1] = Vec4(m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]);
    // Bottom
    planes[2] = Vec4(m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]);
    // Top
    planes[3] = Vec4(m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]);
    // Near
    planes[4] = Vec4(m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]);
    // Far
    planes[5] = Vec4(m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]);

    // Normalize planes
    for (u32 p = 0; p < 6; p++) {
        f32 len = Vec3(planes[p].x, planes[p].y, planes[p].z).length();
        if (len > 0) {
            planes[p] = planes[p] / len;
        }
    }

    for (auto& c : clusters_) {
        if (c.triangleCount == 0) continue;
        c.visible = isClusterInFrustum(c, planes);
        if (!c.visible) stats_.culledClusters++;
    }
}

bool NaniteSystem::isClusterInFrustum(const Cluster& cluster, const Vec4* planes) const {
    return isBoundsInFrustum(cluster.bounds.center, cluster.bounds.radius, planes);
}

bool NaniteSystem::isBoundsInFrustum(const Vec3& center, f32 radius, const Vec4* planes) const {
    for (u32 p = 0; p < 6; p++) {
        f32 dist = center.x * planes[p].x + center.y * planes[p].y +
                   center.z * planes[p].z + planes[p].w;
        if (dist < -radius) return false;
    }
    return true;
}

// ============================================================================
// Occlusion Culling
// ============================================================================
void NaniteSystem::buildOcclusionHierarchy() {
    // Build a quadtree for hierarchical occlusion testing
    occlusionHierarchy_.clear();

    if (hierarchyWidth_ == 0 || hierarchyHeight_ == 0) return;

    // Build hierarchy bottom-up
    u32 w = hierarchyWidth_;
    u32 h = hierarchyHeight_;

    while (w > 0 && h > 0) {
        u32 nodeCount = (w / 2) * (h / 2);
        if (nodeCount == 0) break;

        for (u32 i = 0; i < nodeCount; i++) {
            OcclusionNode node;
            node.boundsMin = Vec3(0);
            node.boundsMax = Vec3(0);
            node.clusterStart = 0;
            node.clusterCount = 0;
            node.visible = true;
            node.fullyOccluded = false;

            for (u32 c = 0; c < 4; c++) {
                node.children[c] = UINT32_MAX;
            }

            occlusionHierarchy_.push_back(node);
        }

        w /= 2;
        h /= 2;
    }

    // Link parent-child relationships
    u32 offset = 0;
    u32 levelStart = 0;
    while (levelStart < occlusionHierarchy_.size()) {
        u32 levelSize = 0;
        u32 childW = hierarchyWidth_ >> (levelStart > 0 ? 1 : 0);
        u32 childH = hierarchyHeight_ >> (levelStart > 0 ? 1 : 0);

        for (u32 i = 0; i < levelSize && (levelStart + i) < occlusionHierarchy_.size(); i++) {
            OcclusionNode& node = occlusionHierarchy_[levelStart + i];
            u32 childIdx = levelStart + levelSize + i * 4;
            for (u32 c = 0; c < 4; c++) {
                if (childIdx + c < occlusionHierarchy_.size()) {
                    node.children[c] = childIdx + c;
                }
            }
        }

        levelStart += levelSize;
        levelSize = (childW / 2) * (childH / 2);
    }
}

void NaniteSystem::hierarchicalOcclusionCull(const Camera& camera) {
    // Reset visibility
    for (auto& node : occlusionHierarchy_) {
        node.visible = true;
        node.fullyOccluded = false;
    }

    // Traverse hierarchy top-down, culling at each level
    for (auto& c : clusters_) {
        if (!c.visible || c.triangleCount == 0) continue;

        if (testBoundsOcclusion(c.bounds.boundsMin, c.bounds.boundsMax, viewProjMatrix_)) {
            c.occluded = true;
            stats_.occludedClusters++;
        } else {
            c.occluded = false;
        }
    }
}

bool NaniteSystem::testBoundsOcclusion(const Vec3& boundsMin, const Vec3& boundsMax,
                                        const Mat4& viewProj) {
    // Project AABB to screen space and test against hierarchical depth buffer
    Vec3 corners[8] = {
        Vec3(boundsMin.x, boundsMin.y, boundsMin.z),
        Vec3(boundsMax.x, boundsMin.y, boundsMin.z),
        Vec3(boundsMin.x, boundsMax.y, boundsMin.z),
        Vec3(boundsMax.x, boundsMax.y, boundsMin.z),
        Vec3(boundsMin.x, boundsMin.y, boundsMax.z),
        Vec3(boundsMax.x, boundsMin.y, boundsMax.z),
        Vec3(boundsMin.x, boundsMax.y, boundsMax.z),
        Vec3(boundsMax.x, boundsMax.y, boundsMax.z)
    };

    f32 minDepth = 1e30f;
    f32 maxDepth = -1e30f;
    f32 minU = 1e30f, maxU = -1e30f;
    f32 minV = 1e30f, maxV = -1e30f;

    for (u32 i = 0; i < 8; i++) {
        Vec4 clip = viewProj * Vec4(corners[i], 1.0f);
        if (clip.w <= 0.0f) return false; // Behind camera

        f32 ndcZ = clip.z / clip.w;
        f32 ndcX = clip.x / clip.w;
        f32 ndcY = clip.y / clip.w;

        minDepth = Mathf::min(minDepth, ndcZ);
        maxDepth = Mathf::max(maxDepth, ndcZ);
        minU = Mathf::min(minU, ndcX);
        maxU = Mathf::max(maxU, ndcX);
        minV = Mathf::min(minV, ndcY);
        maxV = Mathf::max(maxV, ndcY);
    }

    // Convert to screen coordinates
    i32 sx0 = (i32)((minU * 0.5f + 0.5f) * (f32)screenWidth_);
    i32 sy0 = (i32)((minV * 0.5f + 0.5f) * (f32)screenHeight_);
    i32 sx1 = (i32)((maxU * 0.5f + 0.5f) * (f32)screenWidth_);
    i32 sy1 = (i32)((maxV * 0.5f + 0.5f) * (f32)screenHeight_);

    sx0 = Mathf::max(sx0, 0);
    sy0 = Mathf::max(sy0, 0);
    sx1 = Mathf::min(sx1, (i32)screenWidth_ - 1);
    sy1 = Mathf::min(sy1, (i32)screenHeight_ - 1);

    if (sx0 > sx1 || sy0 > sy1) return false;

    // Sample hierarchical depth buffer
    f32 maxSceneDepth = 0.0f;
    for (i32 y = sy0; y <= sy1; y += 4) {
        for (i32 x = sx0; x <= sx1; x += 4) {
            u32 idx = y * screenWidth_ + x;
            if (idx < hierarchicalDepthBuffer_.size()) {
                maxSceneDepth = Mathf::max(maxSceneDepth, hierarchicalDepthBuffer_[idx]);
            }
        }
    }

    // If all fragments behind scene geometry, it's occluded
    return maxDepth < maxSceneDepth - 0.001f;
}

void NaniteSystem::rasterizeOcclusionDepth(const Cluster& cluster, const Mat4& viewProj) {
    // Rasterize cluster triangles to hierarchical depth buffer
    for (u32 t = 0; t < cluster.triangleCount; t++) {
        const ClusterTriangle& tri = cluster.triangles[t];
        Vec3 v0 = cluster.positions[tri.indices[0]];
        Vec3 v1 = cluster.positions[tri.indices[1]];
        Vec3 v2 = cluster.positions[tri.indices[2]];

        softwareRasterizeTriangle(v0, v1, v2, tri.materialIndex, viewProj);
    }
}

// ============================================================================
// DAG Traversal
// ============================================================================
void NaniteSystem::traverseDAG(u32 rootClusterIndex, const Camera& camera, const Mat4& viewProj) {
    if (rootClusterIndex >= clusters_.size()) return;

    Cluster& root = clusters_[rootClusterIndex];

    // Compute screen-space error for root
    f32 screenError = computeScreenSpaceError(root, camera.position(), viewProj);

    // If root is visible at this error, traverse children
    if (screenError > root.bounds.screenSizeThreshold) {
        // Need higher detail - recurse into children
        for (u32 c = 0; c < NANITE_DAG_FANOUT; c++) {
            if (root.childIndices[c] != UINT32_MAX) {
                traverseDAG(root.childIndices[c], camera, viewProj);
            }
        }
    } else {
        // This LOD is sufficient - render this cluster
        root.visible = true;
        root.screenError = screenError;
    }
}

// ============================================================================
// Two-pass Rendering
// ============================================================================
void NaniteSystem::renderDepthPass() {
    // Depth-only pass: software rasterize small triangles
    for (auto& batch : materialBatches_) {
        for (auto* vc : batch.clusters) {
            Cluster& c = clusters_[vc->clusterIndex];

            if (c.triangleCount == 0) continue;

            // Software rasterize each triangle to depth buffer
            softwareRasterizeCluster(c, viewProjMatrix_);
        }
    }
}

void NaniteSystem::renderMaterialPass() {
    // Material pass: render visible clusters with their materials
    for (auto& batch : materialBatches_) {
        // In a real implementation, this would:
        // 1. Bind the material's shader
        // 2. Bind textures
        // 3. Issue draw calls for all clusters in this batch

        stats_.renderedTriangles += batch.totalTriangles;
    }
}

void NaniteSystem::softwareRasterizeCluster(const Cluster& cluster, const Mat4& viewProj) {
    for (u32 t = 0; t < cluster.triangleCount; t++) {
        const ClusterTriangle& tri = cluster.triangles[t];
        Vec3 v0 = cluster.positions[tri.indices[0]];
        Vec3 v1 = cluster.positions[tri.indices[1]];
        Vec3 v2 = cluster.positions[tri.indices[2]];

        softwareRasterizeTriangle(v0, v1, v2, tri.materialIndex, viewProj);
    }
}

void NaniteSystem::softwareRasterizeTriangle(const Vec3& v0, const Vec3& v1, const Vec3& v2,
                                              u32 materialIndex, const Mat4& viewProj) {
    // Project vertices to screen space
    auto projectVertex = [&](const Vec3& v) -> Vec3 {
        Vec4 clip = viewProj * Vec4(v, 1.0f);
        if (clip.w <= 0.0f) return Vec3(-1, -1, -1);
        f32 ndcX = clip.x / clip.w;
        f32 ndcY = clip.y / clip.w;
        f32 ndcZ = clip.z / clip.w;
        return Vec3(
            (ndcX * 0.5f + 0.5f) * (f32)screenWidth_,
            (ndcY * 0.5f + 0.5f) * (f32)screenHeight_,
            ndcZ
        );
    };

    Vec3 sv0 = projectVertex(v0);
    Vec3 sv1 = projectVertex(v1);
    Vec3 sv2 = projectVertex(v2);

    if (sv0.x < 0 || sv1.x < 0 || sv2.x < 0) return;

    // Compute bounding box
    f32 minX = Mathf::min(Mathf::min(sv0.x, sv1.x), sv2.x);
    f32 maxX = Mathf::max(Mathf::max(sv0.x, sv1.x), sv2.x);
    f32 minY = Mathf::min(Mathf::min(sv0.y, sv1.y), sv2.y);
    f32 maxY = Mathf::max(Mathf::max(sv0.y, sv1.y), sv2.y);

    i32 ix0 = Mathf::max((i32)std::floor(minX), 0);
    i32 ix1 = Mathf::min((i32)std::ceil(maxX), (i32)screenWidth_ - 1);
    i32 iy0 = Mathf::max((i32)std::floor(minY), 0);
    i32 iy1 = Mathf::min((i32)std::ceil(maxY), (i32)screenHeight_ - 1);

    // Compute edge functions
    auto edgeFunction = [](const Vec3& a, const Vec3& b, const Vec3& c) -> f32 {
        return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
    };

    f32 area = edgeFunction(sv0, sv1, sv2);
    if (std::abs(area) < 0.001f) return;

    // Rasterize
    for (i32 y = iy0; y <= iy1; y++) {
        for (i32 x = ix0; x <= ix1; x++) {
            Vec3 p((f32)x + 0.5f, (f32)y + 0.5f, 0);

            f32 w0 = edgeFunction(sv1, sv2, p);
            f32 w1 = edgeFunction(sv2, sv0, p);
            f32 w2 = edgeFunction(sv0, sv1, p);

            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                // Barycentric coordinates
                w0 /= area;
                w1 /= area;
                w2 /= area;

                // Interpolate depth
                f32 depth = sv0.z * w0 + sv1.z * w1 + sv2.z * w2;

                // Update depth buffer
                u32 idx = y * screenWidth_ + x;
                if (idx < hierarchicalDepthBuffer_.size()) {
                    if (depth < hierarchicalDepthBuffer_[idx]) {
                        hierarchicalDepthBuffer_[idx] = depth;
                    }
                }
            }
        }
    }
}

// ============================================================================
// GPU Resource Management
// ============================================================================
void NaniteSystem::uploadClusterToGPU(Cluster& cluster) {
    // In a real implementation, this would upload vertex/index data to GPU
    // For CPU simulation, we mark as uploaded
    cluster.vboHandle = 1;
    cluster.eboHandle = 1;
    cluster.vaoHandle = 1;
    residentClusterCount_++;
}

void NaniteSystem::evictClusterFromGPU(Cluster& cluster) {
    if (cluster.vboHandle != 0) {
        cluster.vboHandle = 0;
        cluster.eboHandle = 0;
        cluster.vaoHandle = 0;
        residentClusterCount_--;
    }
}

void NaniteSystem::managePersistence() {
    // Evict clusters that are far from camera
    for (auto& c : clusters_) {
        if (c.vboHandle == 0) continue;

        f32 dist = (c.bounds.center - cameraPosition_).length();
        if (dist > streamingDistance_) {
            evictClusterFromGPU(c);
        }
    }

    // Upload clusters that are close to camera
    for (auto& c : clusters_) {
        if (c.vboHandle != 0) continue;
        if (c.triangleCount == 0) continue;

        f32 dist = (c.bounds.center - cameraPosition_).length();
        if (dist < streamingDistance_ * 0.8f && residentClusterCount_ < maxResidentClusters_) {
            uploadClusterToGPU(c);
        }
    }
}

// ============================================================================
// Fallback Mesh
// ============================================================================
const Vector<Vec3>& NaniteSystem::getFallbackPositions(u32 meshId) const {
    if (meshId < fallbackMeshes_.size()) {
        return fallbackMeshes_[meshId].positions;
    }
    static Vector<Vec3> empty;
    return empty;
}

const Vector<u32>& NaniteSystem::getFallbackIndices(u32 meshId) const {
    if (meshId < fallbackMeshes_.size()) {
        return fallbackMeshes_[meshId].indices;
    }
    static Vector<u32> empty;
    return empty;
}

void NaniteSystem::endFrame() {
    managePersistence();
}

// ============================================================================
// Advanced Cluster Management
// ============================================================================
void NaniteSystem::updateClusterConnectivity() {
    // Update cluster connectivity information for seamless LOD transitions
    for (auto& c : clusters_) {
        if (c.triangleCount == 0) continue;

        // Find neighboring clusters (share vertices)
        for (auto& other : clusters_) {
            if (&other == &c) continue;
            if (other.meshId != c.meshId) continue;
            if (other.triangleCount == 0) continue;

            // Check if clusters share vertices
            bool sharesVertices = false;
            for (u32 v = 0; v < c.vertexCount && !sharesVertices; v++) {
                for (u32 ov = 0; ov < other.vertexCount && !sharesVertices; ov++) {
                    if ((c.positions[v] - other.positions[ov]).lengthSquared() < 0.001f) {
                        sharesVertices = true;
                    }
                }
            }

            if (sharesVertices) {
                // Update normal cone for seamless transition
                f32 normalDot = c.bounds.coneAxis.dot(other.bounds.coneAxis);
                if (normalDot > 0.9f) {
                    // Clusters are co-planar, merge normal cones
                    c.bounds.coneAxis = (c.bounds.coneAxis + other.bounds.coneAxis).normalized();
                }
            }
        }
    }
}

// ============================================================================
// Persistent cluster streaming
// ============================================================================
void NaniteSystem::streamClustersForCamera(const Vec3& cameraPos, f32 streamDistance) {
    // Determine which clusters should be loaded based on camera position
    Vector<u32> clustersToLoad;
    Vector<u32> clustersToEvict;

    for (auto& c : clusters_) {
        if (c.triangleCount == 0) continue;

        f32 dist = (c.bounds.center - cameraPos).length();

        if (c.vboHandle == 0 && dist < streamDistance) {
            // Need to load
            clustersToLoad.push_back(c.clusterId);
        } else if (c.vboHandle != 0 && dist > streamDistance * 1.5f) {
            // Need to evict
            clustersToEvict.push_back(c.clusterId);
        }
    }

    // Evict first to free memory
    for (u32 idx : clustersToEvict) {
        if (idx < clusters_.size()) {
            evictClusterFromGPU(clusters_[idx]);
        }
    }

    // Load new clusters
    for (u32 idx : clustersToLoad) {
        if (idx < clusters_.size() && residentClusterCount_ < maxResidentClusters_) {
            uploadClusterToGPU(clusters_[idx]);
        }
    }
}

// ============================================================================
// GPU-driven culling helpers
// ============================================================================
void NaniteSystem::buildGPUCommandBuffer(Vector<u32>& drawCommands) {
    drawCommands.clear();

    for (auto& batch : materialBatches_) {
        for (auto* vc : batch.clusters) {
            Cluster& c = clusters_[vc->clusterIndex];
            if (c.vboHandle == 0) continue;

            // Draw command: {vertexCount, instanceCount, firstVertex, firstInstance}
            drawCommands.push_back(c.vertexCount);
            drawCommands.push_back(1); // instance count
            drawCommands.push_back(0); // first vertex
            drawCommands.push_back(vc->clusterIndex); // first instance (cluster ID)
        }
    }
}

void NaniteSystem::buildIndirectDrawBuffer(Vector<u32>& indirectBuffer) {
    indirectBuffer.clear();

    for (auto& batch : materialBatches_) {
        for (auto* vc : batch.clusters) {
            Cluster& c = clusters_[vc->clusterIndex];
            if (c.vboHandle == 0) continue;

            // Indirect draw command
            indirectBuffer.push_back(c.triangleCount * 3); // index count
            indirectBuffer.push_back(1); // instance count
            indirectBuffer.push_back(0); // first index
            indirectBuffer.push_back(0); // vertex offset
            indirectBuffer.push_back(vc->clusterIndex); // first instance
        }
    }
}

// ============================================================================
// Cluster memory management
// ============================================================================
void NaniteSystem::compactClusterMemory() {
    // Remove empty clusters to defragment memory
    Vector<u32> validClusters;

    for (u32 i = 0; i < clusters_.size(); i++) {
        if (clusters_[i].triangleCount > 0) {
            validClusters.push_back(i);
        }
    }

    // Rebuild cluster indices
    Vector<u32> indexRemap(clusters_.size(), UINT32_MAX);
    Vector<Cluster> compactedClusters;
    compactedClusters.reserve(validClusters.size());

    for (u32 i = 0; i < validClusters.size(); i++) {
        indexRemap[validClusters[i]] = i;
        compactedClusters.push_back(clusters_[validClusters[i]]);
    }

    // Update parent-child references
    for (auto& c : compactedClusters) {
        if (c.parentClusterIndex != UINT32_MAX) {
            c.parentClusterIndex = indexRemap[c.parentClusterIndex];
        }
        for (u32 i = 0; i < NANITE_DAG_FANOUT; i++) {
            if (c.childIndices[i] != UINT32_MAX) {
                c.childIndices[i] = indexRemap[c.childIndices[i]];
            }
        }
    }

    clusters_ = compactedClusters;
}

// ============================================================================
// Advanced occlusion culling with hierarchical HiZ
// ============================================================================
void NaniteSystem::hierarchicalHiZOcclusion(const Camera& camera) {
    // Build HiZ from depth buffer
    u32 mipWidth = screenWidth_;
    u32 mipHeight = screenHeight_;
    Vector<f32> hizBuffer;

    // Copy depth buffer to HiZ
    hizBuffer.resize(screenWidth_ * screenHeight_);

    // Build mip chain
    u32 mip = 0;
    while (mipWidth > 1 && mipHeight > 1) {
        u32 nextWidth = mipWidth / 2;
        u32 nextHeight = mipHeight / 2;

        for (u32 y = 0; y < nextHeight; y++) {
            for (u32 x = 0; x < nextWidth; x++) {
                u32 srcIdx = (y * 2) * mipWidth + (x * 2);
                f32 maxDepth = hizBuffer[srcIdx];
                maxDepth = Mathf::max(maxDepth, hizBuffer[srcIdx + 1]);
                maxDepth = Mathf::max(maxDepth, hizBuffer[srcIdx + mipWidth]);
                maxDepth = Mathf::max(maxDepth, hizBuffer[srcIdx + mipWidth + 1]);

                u32 dstIdx = y * nextWidth + x;
                if (dstIdx < hizBuffer.size()) {
                    hizBuffer[dstIdx] = maxDepth;
                }
            }
        }

        mipWidth = nextWidth;
        mipHeight = nextHeight;
        mip++;
    }

    // Use HiZ for cluster occlusion testing
    for (auto& c : clusters_) {
        if (!c.visible || c.triangleCount == 0) continue;

        // Project cluster bounds to screen
        Vec4 clipMin = viewProjMatrix_ * Vec4(c.bounds.boundsMin, 1.0f);
        Vec4 clipMax = viewProjMatrix_ * Vec4(c.bounds.boundsMax, 1.0f);

        if (clipMin.w <= 0 || clipMax.w <= 0) continue;

        f32 ndcMinX = clipMin.x / clipMin.w;
        f32 ndcMaxX = clipMax.x / clipMax.w;
        f32 ndcMinY = clipMin.y / clipMin.w;
        f32 ndcMaxY = clipMax.y / clipMax.w;
        f32 ndcMinZ = clipMin.z / clipMin.w;

        // Convert to screen coordinates
        i32 sx0 = (i32)((ndcMinX * 0.5f + 0.5f) * (f32)screenWidth_);
        i32 sy0 = (i32)((1.0f - ndcMaxY * 0.5f - 0.5f) * (f32)screenHeight_);
        i32 sx1 = (i32)((ndcMaxX * 0.5f + 0.5f) * (f32)screenWidth_);
        i32 sy1 = (i32)((1.0f - ndcMinY * 0.5f - 0.5f) * (f32)screenHeight_);

        sx0 = Mathf::max(sx0, 0);
        sy0 = Mathf::max(sy0, 0);
        sx1 = Mathf::min(sx1, (i32)screenWidth_ - 1);
        sy1 = Mathf::min(sy1, (i32)screenHeight_ - 1);

        if (sx0 > sx1 || sy0 > sy1) continue;

        // Sample HiZ at appropriate mip level
        i32 mipLevel = 0;
        i32 mipW = screenWidth_;
        i32 mipH = screenHeight_;

        while (mipW > 4 && mipH > 4) {
            i32 testW = (sx1 - sx0) >> (mipLevel + 1);
            i32 testH = (sy1 - sy0) >> (mipLevel + 1);
            if (testW < 2 && testH < 2) break;
            mipLevel++;
            mipW /= 2;
            mipH /= 2;
        }

        // Get max depth from HiZ at this mip
        i32 hizX = sx0 >> mipLevel;
        i32 hizY = sy0 >> mipLevel;
        u32 hizIdx = hizY * (screenWidth_ >> mipLevel) + hizX;

        if (hizIdx < hizBuffer.size()) {
            f32 hizDepth = hizBuffer[hizIdx];
            if (ndcMinZ > hizDepth + 0.001f) {
                c.occluded = true;
                stats_.occludedClusters++;
            }
        }
    }
}

// ============================================================================
// Material system integration
// ============================================================================
void NaniteSystem::updateMaterialTable() {
    // Build lookup table for material properties
    materialBatches_.clear();

    for (auto& vc : visibleClusters_) {
        u32 matIdx = vc.materialIndex;

        i32 batchIdx = -1;
        for (u32 b = 0; b < materialBatches_.size(); b++) {
            if (materialBatches_[b].materialIndex == matIdx) {
                batchIdx = (i32)b;
                break;
            }
        }

        if (batchIdx >= 0) {
            materialBatches_[batchIdx].clusters.push_back(&vc);
            materialBatches_[batchIdx].totalTriangles += vc.triangleCount;
        } else {
            MaterialBatch batch;
            batch.materialIndex = matIdx;
            batch.totalTriangles = vc.triangleCount;
            batch.clusters.push_back(&vc);
            materialBatches_.push_back(batch);
        }
    }
}

// ============================================================================
// Debug visualization
// ============================================================================
void NaniteSystem::visualizeClusters(Vec3* debugOutput, u32 width, u32 height) {
    if (!debugOutput) return;

    Vec3 colors[] = {
        Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1),
        Vec3(1, 1, 0), Vec3(1, 0, 1), Vec3(0, 1, 1)
    };

    for (auto& c : clusters_) {
        if (!c.visible || c.occluded || c.triangleCount == 0) continue;

        // Project cluster center to screen
        Vec4 clip = viewProjMatrix_ * Vec4(c.bounds.center, 1.0f);
        if (clip.w <= 0.0f) continue;

        f32 ndcX = clip.x / clip.w;
        f32 ndcY = clip.y / clip.w;

        i32 px = (i32)((ndcX * 0.5f + 0.5f) * (f32)width);
        i32 py = (i32)((1.0f - ndcY * 0.5f - 0.5f) * (f32)height);

        // Draw cluster indicator
        i32 clusterSize = (i32)(c.bounds.radius * 20.0f / (clip.w + 1.0f));
        clusterSize = Mathf::max(clusterSize, 2);

        Vec3 color = colors[c.lodLevel % 6];

        for (i32 dy = -clusterSize; dy <= clusterSize; dy++) {
            for (i32 dx = -clusterSize; dx <= clusterSize; dx++) {
                i32 sx = px + dx;
                i32 sy = py + dy;
                if (sx >= 0 && sx < (i32)width && sy >= 0 && sy < (i32)height) {
                    u32 idx = sy * width + sx;
                    debugOutput[idx] = color;
                }
            }
        }
    }
}

void NaniteSystem::visualizeLODs(Vec3* debugOutput, u32 width, u32 height) {
    if (!debugOutput) return;

    for (auto& c : clusters_) {
        if (!c.visible || c.triangleCount == 0) continue;

        Vec4 clip = viewProjMatrix_ * Vec4(c.bounds.center, 1.0f);
        if (clip.w <= 0.0f) continue;

        f32 ndcX = clip.x / clip.w;
        f32 ndcY = clip.y / clip.w;

        i32 px = (i32)((ndcX * 0.5f + 0.5f) * (f32)width);
        i32 py = (i32)((1.0f - ndcY * 0.5f - 0.5f) * (f32)height);

        // Color by screen error
        f32 errorNormalized = Mathf::saturate(c.screenError / 100.0f);
        Vec3 color = Vec3(errorNormalized, 1.0f - errorNormalized, 0);

        i32 dotSize = 3;
        for (i32 dy = -dotSize; dy <= dotSize; dy++) {
            for (i32 dx = -dotSize; dx <= dotSize; dx++) {
                i32 sx = px + dx;
                i32 sy = py + dy;
                if (sx >= 0 && sx < (i32)width && sy >= 0 && sy < (i32)height) {
                    u32 idx = sy * width + sx;
                    debugOutput[idx] = color;
                }
            }
        }
    }
}

} // namespace Frost
