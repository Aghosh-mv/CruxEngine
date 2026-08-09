// ============================================================================
// FrostEngine FrostCluster — Cluster-Based Virtualized Geometry
// ============================================================================
// Proprietary virtualized geometry system. Uses binary tree hierarchy with
// hybrid SW/HW rasterization and smaller 64-triangle clusters for finer
// LOD granularity. Different from Nanite's general DAG approach.
// ============================================================================

#include "FrostEngine/Renderer/FrostCluster.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <numeric>

namespace Frost {

// ============================================================================
// Construction / Destruction
// ============================================================================

FrostCluster::FrostCluster()
    : clusterCount_(0), treeNodeCount_(0), leafClusterCount_(0),
      visibleClusterCount_(0), maxDepthLevel_(0), avgHausdorffError_(0),
      screenWidth_(0), screenHeight_(0), materialBinCount_(0),
      worldBoundsMin_(1e30f), worldBoundsMax_(-1e30f), initialized_(false) {
}

FrostCluster::~FrostCluster() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool FrostCluster::init(u32 maxClusters) {
    clusters_.resize(maxClusters);
    treeNodes_.resize(maxClusters * 2);

    // Initialize visibility buffer for 1920x1080 (will resize as needed)
    screenWidth_ = 1920;
    screenHeight_ = 1080;
    visBuffer_.resize(screenWidth_ * screenHeight_);

    swDepthBuffer_.resize(screenWidth_ * screenHeight_);
    swCoverageBuffer_.resize(screenWidth_ * screenHeight_);

    clusterCount_ = 0;
    treeNodeCount_ = 0;
    initialized_ = true;

    return true;
}

void FrostCluster::shutdown() {
    clusters_.clear();
    treeNodes_.clear();
    visBuffer_.clear();
    swDepthBuffer_.clear();
    swCoverageBuffer_.clear();
    materialBins_.clear();
    initialized_ = false;
}

void FrostCluster::reset() {
    clusterCount_ = 0;
    treeNodeCount_ = 0;
    leafClusterCount_ = 0;
    visibleClusterCount_ = 0;
    materialBinCount_ = 0;
}

// ============================================================================
// Cluster Building — Split Meshes into 64-Triangle Clusters
// ============================================================================

void FrostCluster::buildClusters(const ClusterMeshInput& mesh) {
    buildClusters(&mesh, 1);
}

void FrostCluster::buildClusters(const ClusterMeshInput* meshes, u32 meshCount) {
    for (u32 m = 0; m < meshCount; m++) {
        const ClusterMeshInput& mesh = meshes[m];

        if (mesh.indices.size() < 3) continue;

        u32 triCount = (u32)mesh.indices.size() / 3;

        // Update global bounds
        for (u32 i = 0; i < mesh.positions.size(); i++) {
            worldBoundsMin_ = worldBoundsMin_.min(mesh.positions[i]);
            worldBoundsMax_ = worldBoundsMax_.max(mesh.positions[i]);
        }

        // Split triangles into clusters of CLUSTER_TRIANGLES
        Vector<u32> triIndices;
        triIndices.resize(triCount);
        for (u32 t = 0; t < triCount; t++) triIndices[t] = t;

        // Process triangles in groups of 64
        u32 numClusters = (triCount + CLUSTER_TRIANGLES - 1) / CLUSTER_TRIANGLES;

        for (u32 c = 0; c < numClusters; c++) {
            u32 startTri = c * CLUSTER_TRIANGLES;
            u32 endTri = std::min(startTri + CLUSTER_TRIANGLES, triCount);
            u32 triInCluster = endTri - startTri;

            u32 clusterIdx = clusterCount_++;
            Cluster& cluster = clusters_[clusterIdx];

            cluster.meshID = mesh.meshID;
            cluster.materialID = mesh.materialID;
            cluster.depthLevel = 0;
            cluster.parentNodeIndex = 0xFFFFFFFF;

            // Extract triangles and local vertices
            cluster.triangleCount = triInCluster;
            cluster.vertexCount = 0;

            // Track unique vertices
            Vector<u32> uniqueVerts;
            uniqueVerts.resize(mesh.positions.size());
            for (auto& v : uniqueVerts) v = 0xFFFFFFFF;

            for (u32 t = 0; t < triInCluster; t++) {
                u32 globalTri = startTri + t;
                ClusterTriangle& ct = cluster.triangles[t];

                for (u32 v = 0; v < 3; v++) {
                    u32 globalIdx = mesh.indices[globalTri * 3 + v];

                    if (uniqueVerts[globalIdx] == 0xFFFFFFFF) {
                        u32 localIdx = cluster.vertexCount++;
                        uniqueVerts[globalIdx] = localIdx;

                        cluster.positions[localIdx] = mesh.positions[globalIdx];
                        cluster.normals[localIdx] = mesh.normals[globalIdx];
                        cluster.uvs[localIdx] = mesh.uvs[globalIdx];
                    }

                    ct.indices[v] = (u16)uniqueVerts[globalIdx];
                }

                ct.materialSlot = (u8)mesh.materialID;
                ct.flags = 0;
            }

            // Compute cluster bounds
            computeClusterBounds(cluster);

            // Determine rasterization method
            assignRasterMethod(cluster);

            // Create tree node
            u32 nodeIdx = treeNodeCount_++;
            treeNodes_[nodeIdx].clusterIndex = clusterIdx;
            treeNodes_[nodeIdx].isLeaf = true;
            treeNodes_[nodeIdx].leftChild = 0xFFFFFFFF;
            treeNodes_[nodeIdx].rightChild = 0xFFFFFFFF;
            treeNodes_[nodeIdx].splitError = 0;
            treeNodes_[nodeIdx].screenSize = 0;

            leafClusterCount_++;
        }
    }

    // Build binary hierarchy
    buildBinaryHierarchy();

    // Bin clusters by material
    binClustersByMaterial();
}

// ============================================================================
// Binary Hierarchy Construction
// ============================================================================

void FrostCluster::buildBinaryHierarchy() {
    if (treeNodeCount_ <= 1) return;

    // Simple bottom-up binary tree construction
    // Merge pairs of nodes into parent nodes
    u32 currentLevel = treeNodeCount_;

    while (currentLevel > 1) {
        u32 nextLevel = 0;

        for (u32 i = 0; i < currentLevel; i += 2) {
            if (i + 1 >= currentLevel) {
                // Odd node out, promote to next level
                if (nextLevel != i) {
                    treeNodes_[nextLevel] = treeNodes_[i];
                }
                nextLevel++;
                continue;
            }

            // Create parent node
            u32 parentIdx = treeNodeCount_++;
            if (parentIdx >= MAX_CLUSTER_NODES) {
                treeNodeCount_--;
                break;
            }

            ClusterTreeNode& parent = treeNodes_[parentIdx];
            parent.leftChild = i;
            parent.rightChild = i + 1;
            parent.isLeaf = false;
            parent.clusterIndex = 0xFFFFFFFF;

            // Create a parent cluster that spans both children
            u32 parentClusterIdx = clusterCount_++;
            if (parentClusterIdx >= MAX_CLUSTERS) {
                clusterCount_--;
                treeNodeCount_--;
                break;
            }

            Cluster& parentCluster = clusters_[parentClusterIdx];
            parentCluster.depthLevel = 0;

            // Compute union bounds from children
            u32 leftCluster = treeNodes_[i].clusterIndex;
            u32 rightCluster = treeNodes_[i + 1].clusterIndex;

            if (leftCluster < clusterCount_ && rightCluster < clusterCount_) {
                parentCluster.boundsMin = clusters_[leftCluster].boundsMin.min(
                    clusters_[rightCluster].boundsMin);
                parentCluster.boundsMax = clusters_[leftCluster].boundsMax.max(
                    clusters_[rightCluster].boundsMax);
                parentCluster.boundsCenter = (parentCluster.boundsMin + parentCluster.boundsMax) * 0.5f;
                parentCluster.boundsRadius = (parentCluster.boundsMax - parentCluster.boundsMin).length() * 0.5f;

                // Compute Hausdorff error between parent and children
                parentCluster.hausdorffError = computeHausdorffDistance(
                    clusters_[leftCluster], clusters_[rightCluster]);
            }

            parentCluster.parentNodeIndex = 0xFFFFFFFF;
            parent.clusterIndex = parentClusterIdx;

            treeNodes_[i].parentIndex = parentIdx;
            treeNodes_[i + 1].parentIndex = parentIdx;

            treeNodes_[nextLevel] = parent;
            nextLevel++;
        }

        currentLevel = nextLevel;
    }

    // Update depth levels and max depth
    maxDepthLevel_ = 0;
    f32 totalError = 0;
    u32 errorCount = 0;

    for (u32 i = 0; i < treeNodeCount_; i++) {
        if (treeNodes_[i].isLeaf) continue;

        u32 clusterIdx = treeNodes_[i].clusterIndex;
        if (clusterIdx < clusterCount_) {
            clusters_[clusterIdx].depthLevel = 1;
            totalError += clusters_[clusterIdx].hausdorffError;
            errorCount++;
        }
    }

    avgHausdorffError_ = errorCount > 0 ? totalError / (f32)errorCount : 0;
}

void FrostCluster::buildBinaryHierarchyRecursive(u32 nodeIdx, const Vector<u32>& triIndices,
                                                  const ClusterMeshInput& mesh, u32 depth) {
    ClusterTreeNode& node = treeNodes_[nodeIdx];

    if (triIndices.size() <= CLUSTER_TRIANGLES || depth > 20) {
        node.isLeaf = true;
        return;
    }

    // Split triangles in half
    u32 mid = (u32)triIndices.size() / 2;
    Vector<u32> leftTris(triIndices.begin(), triIndices.begin() + mid);
    Vector<u32> rightTris(triIndices.begin() + mid, triIndices.end());

    // Create child nodes
    u32 leftIdx = treeNodeCount_++;
    u32 rightIdx = treeNodeCount_++;

    node.leftChild = leftIdx;
    node.rightChild = rightIdx;
    node.isLeaf = false;

    treeNodes_[leftIdx].parentIndex = nodeIdx;
    treeNodes_[rightIdx].parentIndex = nodeIdx;

    buildBinaryHierarchyRecursive(leftIdx, leftTris, mesh, depth + 1);
    buildBinaryHierarchyRecursive(rightIdx, rightTris, mesh, depth + 1);
}

// ============================================================================
// Cluster Bounds and Error Metrics
// ============================================================================

void FrostCluster::computeClusterBounds(Cluster& cluster) {
    cluster.boundsMin = Vec3(1e30f);
    cluster.boundsMax = Vec3(-1e30f);

    for (u32 v = 0; v < cluster.vertexCount; v++) {
        cluster.boundsMin = cluster.boundsMin.min(cluster.positions[v]);
        cluster.boundsMax = cluster.boundsMax.max(cluster.positions[v]);
    }

    cluster.boundsCenter = (cluster.boundsMin + cluster.boundsMax) * 0.5f;
    cluster.boundsRadius = (cluster.boundsMax - cluster.boundsMin).length() * 0.5f;
}

f32 FrostCluster::computeHausdorffDistance(const Cluster& a, const Cluster& b) const {
    // Bidirectional Hausdorff distance: max of point-to-set distances
    f32 maxDistA = 0;
    f32 maxDistB = 0;

    // For each vertex in A, find closest triangle in B
    for (u32 v = 0; v < a.vertexCount && v < 64; v++) {
        f32 minDist = 1e30f;
        for (u32 t = 0; t < b.triangleCount; t++) {
            u32 i0 = b.triangles[t].indices[0];
            u32 i1 = b.triangles[t].indices[1];
            u32 i2 = b.triangles[t].indices[2];

            if (i0 < CLUSTER_VERTICES && i1 < CLUSTER_VERTICES && i2 < CLUSTER_VERTICES) {
                f32 d = pointToTriangleDistance(a.positions[v],
                    b.positions[i0], b.positions[i1], b.positions[i2]);
                minDist = std::min(minDist, d);
            }
        }
        maxDistA = std::max(maxDistA, minDist);
    }

    // For each vertex in B, find closest triangle in A
    for (u32 v = 0; v < b.vertexCount && v < 64; v++) {
        f32 minDist = 1e30f;
        for (u32 t = 0; t < a.triangleCount; t++) {
            u32 i0 = a.triangles[t].indices[0];
            u32 i1 = a.triangles[t].indices[1];
            u32 i2 = a.triangles[t].indices[2];

            if (i0 < CLUSTER_VERTICES && i1 < CLUSTER_VERTICES && i2 < CLUSTER_VERTICES) {
                f32 d = pointToTriangleDistance(b.positions[v],
                    a.positions[i0], a.positions[i1], a.positions[i2]);
                minDist = std::min(minDist, d);
            }
        }
        maxDistB = std::max(maxDistB, minDist);
    }

    return std::max(maxDistA, maxDistB);
}

f32 FrostCluster::pointToTriangleDistance(Vec3 point, Vec3 a, Vec3 b, Vec3 c) const {
    // Compute closest point on triangle to given point
    Vec3 ab = b - a;
    Vec3 ac = c - a;
    Vec3 ap = point - a;

    f32 d1 = ab.dot(ap);
    f32 d2 = ac.dot(ap);
    if (d1 <= 0 && d2 <= 0) return (point - a).length();

    Vec3 bp = point - b;
    f32 d3 = ab.dot(bp);
    f32 d4 = ac.dot(bp);
    if (d3 >= 0 && d4 <= d3) return (point - b).length();

    f32 vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        f32 v = d1 / (d1 - d3);
        Vec3 closePt = a + ab * v;
        return (point - closePt).length();
    }

    Vec3 cp = point - c;
    f32 d5 = ab.dot(cp);
    f32 d6 = ac.dot(cp);
    if (d6 >= 0 && d5 <= d6) return (point - c).length();

    f32 vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        f32 w = d2 / (d2 - d6);
        Vec3 closePt = a + ac * w;
        return (point - closePt).length();
    }

    f32 va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        f32 w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        Vec3 closePt = b + (c - b) * w;
        return (point - closePt).length();
    }

    f32 denom = 1.0f / (va + vb + vc);
    f32 v = vb * denom;
    f32 w = vc * denom;
    Vec3 closePt = a + ab * v + ac * w;
    return (point - closePt).length();
}

void FrostCluster::assignRasterMethod(Cluster& cluster) {
    // Heuristic: small clusters with sub-pixel triangles use software rasterization
    f32 avgTriSize = cluster.boundsRadius / sqrtf((f32)cluster.triangleCount);

    if (avgTriSize < 1.0f) {
        cluster.rasterMethod = RasterMethod::Software;
    } else if (avgTriSize > 10.0f) {
        cluster.rasterMethod = RasterMethod::Hardware;
    } else {
        cluster.rasterMethod = RasterMethod::Hybrid;
    }
}

// ============================================================================
// Material Binning
// ============================================================================

void FrostCluster::binClustersByMaterial() {
    materialBins_.clear();

    // Count unique materials
    u32 maxMaterial = 0;
    for (u32 i = 0; i < clusterCount_; i++) {
        maxMaterial = std::max(maxMaterial, clusters_[i].materialID);
    }

    materialBins_.resize(maxMaterial + 1);
    materialBinCount_ = maxMaterial + 1;

    for (u32 m = 0; m <= maxMaterial; m++) {
        materialBins_[m].materialID = m;
        materialBins_[m].totalTriangles = 0;
    }

    // Assign clusters to bins
    for (u32 i = 0; i < clusterCount_; i++) {
        u32 matID = clusters_[i].materialID;
        if (matID < materialBins_.size()) {
            materialBins_[matID].clusterIndices.push_back(i);
            materialBins_[matID].totalTriangles += clusters_[i].triangleCount;
        }
    }
}

const Vector<u32>& FrostCluster::getMaterialBin(u32 materialID) const {
    if (materialID < materialBins_.size()) {
        return materialBins_[materialID].clusterIndices;
    }
    static Vector<u32> empty;
    return empty;
}

// ============================================================================
// LOD Selection — Screen-Space Coverage Based
// ============================================================================

void FrostCluster::selectLODs(const Mat4& viewProj, Vec3 cameraPos,
                               u32 screenW, u32 screenH) {
    screenWidth_ = screenW;
    screenHeight_ = screenH;

    // Resize visibility buffer
    visBuffer_.resize(screenW * screenH);
    for (auto& v : visBuffer_) v = VisibilityEntry();

    visibleClusterCount_ = 0;

    // Traverse tree and select LODs based on screen coverage
    for (u32 i = 0; i < treeNodeCount_; i++) {
        if (treeNodes_[i].parentIndex != 0xFFFFFFFF) continue;  // root nodes only

        f32 coverage = 1.0f;
        selectLODsRecursive(i, viewProj, cameraPos, screenW, screenH, coverage);
    }
}

void FrostCluster::selectLODsRecursive(u32 nodeIdx, const Mat4& viewProj,
                                        Vec3 cameraPos, u32 screenW, u32 screenH,
                                        f32 parentCoverage) {
    ClusterTreeNode& node = treeNodes_[nodeIdx];

    if (node.clusterIndex >= clusterCount_) return;

    Cluster& cluster = clusters_[node.clusterIndex];

    // Compute screen coverage
    f32 coverage = computeScreenCoverage(cluster, viewProj, screenW, screenH);
    node.screenSize = coverage;

    // Frustum culling
    if (!frustumCullCluster(cluster, viewProj)) {
        cluster.isVisible = false;
        return;
    }

    cluster.isVisible = true;

    // If leaf node, mark for rasterization
    if (node.isLeaf || node.leftChild == 0xFFFFFFFF) {
        cluster.needsRasterize = true;
        visibleClusterCount_++;
        return;
    }

    // If coverage is small enough, use this LOD level (don't recurse to children)
    f32 pixelSize = coverage * (f32)(screenW * screenH);
    if (pixelSize < 16.0f) {
        cluster.needsRasterize = true;
        visibleClusterCount_++;
        return;
    }

    // Otherwise, recurse to children for higher detail
    if (node.leftChild != 0xFFFFFFFF) {
        selectLODsRecursive(node.leftChild, viewProj, cameraPos, screenW, screenH, coverage);
    }
    if (node.rightChild != 0xFFFFFFFF) {
        selectLODsRecursive(node.rightChild, viewProj, cameraPos, screenW, screenH, coverage);
    }
}

f32 FrostCluster::computeScreenCoverage(const Cluster& cluster, const Mat4& viewProj,
                                         u32 screenW, u32 screenH) const {
    // Project cluster bounds to screen and compute screen-space area
    Vec4 center = viewProj * Vec4(cluster.boundsCenter, 1.0f);
    if (center.w <= 0.0f) return 0.0f;

    // Project radius to screen
    f32 scale = (f32)screenW / (center.w * 2.0f);
    f32 screenRadius = cluster.boundsRadius * scale;

    // Coverage as fraction of screen
    f32 screenArea = 3.14159f * screenRadius * screenRadius;
    f32 totalScreenArea = (f32)(screenW * screenH);

    return screenArea / totalScreenArea;
}

// ============================================================================
// Rasterization — Hybrid SW/HW Path
// ============================================================================

void FrostCluster::rasterize(const Mat4& viewProj, u32 screenW, u32 screenH) {
    screenWidth_ = screenW;
    screenHeight_ = screenH;

    visBuffer_.resize(screenW * screenH);
    for (auto& v : visBuffer_) v = VisibilityEntry();

    swDepthBuffer_.resize(screenW * screenH);
    for (auto& d : swDepthBuffer_) d = 1e30f;

    // Rasterize visible clusters
    for (u32 i = 0; i < clusterCount_; i++) {
        Cluster& cluster = clusters_[i];
        if (!cluster.needsRasterize || !cluster.isVisible) continue;

        switch (cluster.rasterMethod) {
            case RasterMethod::Software:
                softwareRasterizeCluster(cluster, viewProj, screenW, screenH);
                break;
            case RasterMethod::Hardware:
                hardwareRasterizeCluster(cluster);
                break;
            case RasterMethod::Hybrid:
                // Decide based on triangle screen size
                f32 coverage = computeScreenCoverage(cluster, viewProj, screenW, screenH);
                if (coverage * (f32)(screenW * screenH) < 64.0f) {
                    softwareRasterizeCluster(cluster, viewProj, screenW, screenH);
                } else {
                    hardwareRasterizeCluster(cluster);
                }
                break;
        }
    }
}

void FrostCluster::softwareRasterizeCluster(const Cluster& cluster, const Mat4& viewProj,
                                             u32 screenW, u32 screenH) {
    for (u32 t = 0; t < cluster.triangleCount; t++) {
        const ClusterTriangle& tri = cluster.triangles[t];

        if (tri.indices[0] >= cluster.vertexCount ||
            tri.indices[1] >= cluster.vertexCount ||
            tri.indices[2] >= cluster.vertexCount) continue;

        // Transform vertices to clip space
        Vec3 v0 = cluster.positions[tri.indices[0]];
        Vec3 v1 = cluster.positions[tri.indices[1]];
        Vec3 v2 = cluster.positions[tri.indices[2]];

        Vec4 c0 = viewProj * Vec4(v0, 1.0f);
        Vec4 c1 = viewProj * Vec4(v1, 1.0f);
        Vec4 c2 = viewProj * Vec4(v2, 1.0f);

        // Perspective divide
        if (c0.w <= 0 || c1.w <= 0 || c2.w <= 0) continue;

        Vec3 ndc0(c0.x / c0.w, c0.y / c0.w, c0.z / c0.w);
        Vec3 ndc1(c1.x / c1.w, c1.y / c1.w, c1.z / c1.w);
        Vec3 ndc2(c2.x / c2.w, c2.y / c2.w, c2.z / c2.w);

        // Convert to screen space
        Vec2 s0(ndc0.x * 0.5f + 0.5f, 1.0f - (ndc0.y * 0.5f + 0.5f));
        Vec2 s1(ndc1.x * 0.5f + 0.5f, 1.0f - (ndc1.y * 0.5f + 0.5f));
        Vec2 s2(ndc2.x * 0.5f + 0.5f, 1.0f - (ndc2.y * 0.5f + 0.5f));

        s0 = Vec2(s0.x * (f32)screenW, s0.y * (f32)screenH);
        s1 = Vec2(s1.x * (f32)screenW, s1.y * (f32)screenH);
        s2 = Vec2(s2.x * (f32)screenW, s2.y * (f32)screenH);

        SWRasterTriangle rasterTri;
        rasterTri.screenPos[0] = Vec3(s0.x, s0.y, ndc0.z);
        rasterTri.screenPos[1] = Vec3(s1.x, s1.y, ndc1.z);
        rasterTri.screenPos[2] = Vec3(s2.x, s2.y, ndc2.z);
        rasterTri.depths[0] = ndc0.z;
        rasterTri.depths[1] = ndc1.z;
        rasterTri.depths[2] = ndc2.z;
        rasterTri.clusterID = &cluster - &clusters_[0];
        rasterTri.triangleID = t;
        rasterTri.materialID = cluster.materialID;

        swRasterizeTriangle(rasterTri, visBuffer_, screenW, screenH);
    }
}

void FrostCluster::swRasterizeTriangle(const SWRasterTriangle& tri,
                                        Vector<VisibilityEntry>& buffer,
                                        u32 screenW, u32 screenH) {
    // Compute triangle bounding box in screen space
    f32 minX = std::min({tri.screenPos[0].x, tri.screenPos[1].x, tri.screenPos[2].x});
    f32 maxX = std::max({tri.screenPos[0].x, tri.screenPos[1].x, tri.screenPos[2].x});
    f32 minY = std::min({tri.screenPos[0].y, tri.screenPos[1].y, tri.screenPos[2].y});
    f32 maxY = std::max({tri.screenPos[0].y, tri.screenPos[1].y, tri.screenPos[2].y});

    // Clamp to screen
    i32 x0 = (i32)std::max(minX, 0.0f);
    i32 x1 = (i32)std::min(maxX + 1.0f, (f32)screenW);
    i32 y0 = (i32)std::max(minY, 0.0f);
    i32 y1 = (i32)std::min(maxY + 1.0f, (f32)screenH);

    if (x0 >= x1 || y0 >= y1) return;

    // Compute edge functions
    f32 area;
    f32 bary1, bary2;
    edgeFunction(tri.screenPos[0].x, tri.screenPos[0].y,
                 tri.screenPos[1].x, tri.screenPos[1].y,
                 tri.screenPos[2].x, tri.screenPos[2].y,
                 area, bary1, bary2);

    if (area < 0.0001f) return;

    f32 invArea = 1.0f / area;

    // Rasterize
    for (i32 y = y0; y < y1; y++) {
        for (i32 x = x0; x < x1; x++) {
            f32 px = (f32)x + 0.5f;
            f32 py = (f32)y + 0.5f;

            // Compute barycentric coordinates
            f32 w0, w1, w2;
            f32 area2;
            edgeFunction(tri.screenPos[1].x, tri.screenPos[1].y,
                         tri.screenPos[2].x, tri.screenPos[2].y,
                         px, py, area2, w1, w2);
            w0 = area2 * invArea;

            edgeFunction(tri.screenPos[2].x, tri.screenPos[2].y,
                         tri.screenPos[0].x, tri.screenPos[0].y,
                         px, py, area2, w2, w0);
            w1 = area2 * invArea;

            w2 = 1.0f - w0 - w1;

            if (w0 < 0 || w1 < 0 || w2 < 0) continue;

            // Interpolate depth
            f32 depth = tri.depths[0] * w0 + tri.depths[1] * w1 + tri.depths[2] * w2;

            u32 pixelIdx = (u32)(y * screenW + x);

            // Depth test
            if (depth < swDepthBuffer_[pixelIdx] && depth > 0 && depth < 1) {
                swDepthBuffer_[pixelIdx] = depth;
                buffer[pixelIdx].clusterID = tri.clusterID;
                buffer[pixelIdx].triangleID = tri.triangleID;
                buffer[pixelIdx].depth = depth;
            }
        }
    }
}

void FrostCluster::edgeFunction(f32 ax, f32 ay, f32 bx, f32 by,
                                 f32 cx, f32 cy,
                                 f32& area, f32& bary1, f32& bary2) const {
    area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    bary1 = 0;
    bary2 = 0;
}

void FrostCluster::hardwareRasterizeCluster(const Cluster& cluster) {
    // In production, this would submit the cluster to the GPU pipeline
    // For now, we mark it as needing hardware rasterization
    // The actual GPU submission would happen through the render pipeline
}

// ============================================================================
// Frustum Culling
// ============================================================================

bool FrostCluster::frustumCullCluster(const Cluster& cluster, const Mat4& viewProj) const {
    // Test cluster AABB against view frustum
    // Simplified: test center + radius sphere
    return sphereInFrustum(cluster.boundsCenter, cluster.boundsRadius, viewProj);
}

bool FrostCluster::sphereInFrustum(Vec3 center, f32 radius, const Mat4& viewProj) const {
    // Extract frustum planes from view-projection matrix
    // Simplified: just check if center is within reasonable bounds
    Vec4 clipCenter = viewProj * Vec4(center, 1.0f);
    if (clipCenter.w <= 0) return false;

    f32 ndcZ = clipCenter.z / clipCenter.w;
    if (ndcZ < -1 || ndcZ > 1) return false;

    f32 ndcX = clipCenter.x / clipCenter.w;
    f32 ndcY = clipCenter.y / clipCenter.w;

    // Expand by radius in NDC
    f32 expandX = radius / clipCenter.w;
    f32 expandY = radius / clipCenter.w;

    if (ndcX + expandX < -1 && ndcX - expandX < -1) return false;
    if (ndcX - expandX > 1 && ndcX + expandX > 1) return false;
    if (ndcY + expandY < -1 && ndcY - expandY < -1) return false;
    if (ndcY - expandY > 1 && ndcY + expandY > 1) return false;

    return true;
}

// ============================================================================
// Cluster Analysis and Optimization
// ============================================================================

f32 FrostCluster::computeClusterSolidity(const Cluster& cluster) const {
    // Measure how solid/compact a cluster is (0 = very spread out, 1 = solid)
    Vec3 extent = cluster.boundsMax - cluster.boundsMin;
    f32 volume = extent.x * extent.y * extent.z;
    f32 expectedVolume = powf(cluster.boundsRadius * 2.0f, 3.0f);

    return expectedVolume > 0 ? volume / expectedVolume : 0;
}

f32 FrostCluster::computeClusterConvexity(const Cluster& cluster) const {
    // Approximate convexity by comparing volume to convex hull
    // Simplified: use ratio of actual bounds to sphere bounds
    f32 boxVolume = (cluster.boundsMax.x - cluster.boundsMin.x) *
                    (cluster.boundsMax.y - cluster.boundsMin.y) *
                    (cluster.boundsMax.z - cluster.boundsMin.z);
    f32 sphereVolume = (4.0f / 3.0f) * 3.14159f *
                       cluster.boundsRadius * cluster.boundsRadius * cluster.boundsRadius;

    return sphereVolume > 0 ? boxVolume / sphereVolume : 0;
}

Vec3 FrostCluster::computeClusterCentroid(const Cluster& cluster) const {
    Vec3 centroid(0);
    for (u32 v = 0; v < cluster.vertexCount; v++) {
        centroid += cluster.positions[v];
    }
    return cluster.vertexCount > 0 ? centroid / (f32)cluster.vertexCount : cluster.boundsCenter;
}

f32 FrostCluster::computeTriangleDensity(const Cluster& cluster) const {
    f32 volume = (cluster.boundsMax.x - cluster.boundsMin.x) *
                 (cluster.boundsMax.y - cluster.boundsMin.y) *
                 (cluster.boundsMax.z - cluster.boundsMin.z);
    return volume > 0 ? (f32)cluster.triangleCount / volume : 0;
}

// ============================================================================
// LOD Quality Assessment
// ============================================================================

f32 FrostCluster::computeLODQuality(u32 nodeIdx, const Mat4& viewProj,
                                     u32 screenW, u32 screenH) const {
    if (nodeIdx >= treeNodeCount_) return 0;

    const ClusterTreeNode& node = treeNodes_[nodeIdx];
    if (node.clusterIndex >= clusterCount_) return 0;

    const Cluster& cluster = clusters_[node.clusterIndex];

    // Quality based on screen coverage vs error
    f32 coverage = computeScreenCoverage(cluster, viewProj, screenW, screenH);
    f32 error = cluster.hausdorffError;

    // Higher quality = more coverage with less error
    if (coverage < 0.0001f) return 1.0f;
    return 1.0f / (1.0f + error * error / coverage);
}

f32 FrostCluster::computeLODBalance() const {
    // Measure how balanced the LOD hierarchy is
    u32 leafCount = 0;
    u32 internalCount = 0;

    for (u32 i = 0; i < treeNodeCount_; i++) {
        if (treeNodes_[i].isLeaf) leafCount++;
        else internalCount++;
    }

    if (internalCount == 0) return 1.0f;
    return (f32)leafCount / (f32)internalCount;
}

u32 FrostCluster::computeMaxLODDepth() const {
    u32 maxDepth = 0;

    for (u32 i = 0; i < treeNodeCount_; i++) {
        if (treeNodes_[i].isLeaf) {
            u32 depth = 0;
            u32 parent = treeNodes_[i].parentIndex;
            while (parent != 0xFFFFFFFF && parent < treeNodeCount_) {
                depth++;
                parent = treeNodes_[parent].parentIndex;
            }
            maxDepth = std::max(maxDepth, depth);
        }
    }

    return maxDepth;
}

f32 FrostCluster::computeAverageTriangleSize() const {
    f32 totalSize = 0;
    u32 count = 0;

    for (u32 i = 0; i < clusterCount_; i++) {
        const Cluster& c = clusters_[i];
        for (u32 t = 0; t < c.triangleCount; t++) {
            u32 i0 = c.triangles[t].indices[0];
            u32 i1 = c.triangles[t].indices[1];
            u32 i2 = c.triangles[t].indices[2];

            if (i0 < c.vertexCount && i1 < c.vertexCount && i2 < c.vertexCount) {
                Vec3 a = c.positions[i0];
                Vec3 b = c.positions[i1];
                Vec3 cc = c.positions[i2];

                f32 area = (b - a).cross(cc - a).length() * 0.5f;
                totalSize += sqrtf(area);
                count++;
            }
        }
    }

    return count > 0 ? totalSize / (f32)count : 0;
}

// ============================================================================
// Material Binning Optimization
// ============================================================================

void FrostCluster::sortMaterialBins() {
    // Sort material bins by triangle count for better batching
    for (u32 i = 0; i < materialBinCount_; i++) {
        for (u32 j = i + 1; j < materialBinCount_; j++) {
            if (materialBins_[j].totalTriangles > materialBins_[i].totalTriangles) {
                MaterialBin temp = materialBins_[i];
                materialBins_[i] = materialBins_[j];
                materialBins_[j] = temp;
            }
        }
    }
}

u32 FrostCluster::computeMaterialBinCount() const {
    u32 count = 0;
    for (u32 i = 0; i < materialBinCount_; i++) {
        if (materialBins_[i].clusterIndices.size() > 0) count++;
    }
    return count;
}

u32 FrostCluster::computeBatchCount() const {
    // Estimate number of draw calls needed
    u32 batches = 0;
    for (u32 i = 0; i < materialBinCount_; i++) {
        u32 clusterCount = (u32)materialBins_[i].clusterIndices.size();
        batches += (clusterCount + 255) / 256;  // 256 clusters per batch
    }
    return batches;
}

// ============================================================================
// Visibility Buffer Analysis
// ============================================================================

u32 FrostCluster::countVisiblePixels() const {
    u32 count = 0;
    for (u32 i = 0; i < visBuffer_.size(); i++) {
        if (visBuffer_[i].clusterID != 0xFFFFFFFF) count++;
    }
    return count;
}

f32 FrostCluster::computeOverdrawRatio() const {
    u32 visiblePixels = countVisiblePixels();
    u32 totalPixels = (u32)visBuffer_.size();
    return totalPixels > 0 ? (f32)visiblePixels / (f32)totalPixels : 0;
}

u32 FrostCluster::countUniqueMaterials() const {
    Vector<bool> seen;
    seen.resize(materialBinCount_, false);
    u32 count = 0;

    for (u32 i = 0; i < clusterCount_; i++) {
        u32 matID = clusters_[i].materialID;
        if (matID < materialBinCount_ && !seen[matID]) {
            seen[matID] = true;
            count++;
        }
    }

    return count;
}

// ============================================================================
// Software Rasterization Improvements
// ============================================================================

void FrostRasterTriangle(const Cluster& cluster, u32 clusterIdx, const ClusterTriangle& tri,
                          const Mat4& viewProj, u32 screenW, u32 screenH,
                          Vector<VisibilityEntry>& buffer, Vector<f32>& depthBuf) {
    // Full triangle setup with perspective-correct interpolation
    Vec3 v0 = cluster.positions[tri.indices[0]];
    Vec3 v1 = cluster.positions[tri.indices[1]];
    Vec3 v2 = cluster.positions[tri.indices[2]];

    Vec4 c0 = viewProj * Vec4(v0, 1.0f);
    Vec4 c1 = viewProj * Vec4(v1, 1.0f);
    Vec4 c2 = viewProj * Vec4(v2, 1.0f);

    if (c0.w <= 0 || c1.w <= 0 || c2.w <= 0) return;

    // Perspective divide
    Vec3 ndc0(c0.x / c0.w, c0.y / c0.w, c0.z / c0.w);
    Vec3 ndc1(c1.x / c1.w, c1.y / c1.w, c1.z / c1.w);
    Vec3 ndc2(c2.x / c2.w, c2.y / c2.w, c2.z / c2.w);

    // Screen space
    Vec2 s0(ndc0.x * 0.5f + 0.5f, 1.0f - (ndc0.y * 0.5f + 0.5f));
    Vec2 s1(ndc1.x * 0.5f + 0.5f, 1.0f - (ndc1.y * 0.5f + 0.5f));
    Vec2 s2(ndc2.x * 0.5f + 0.5f, 1.0f - (ndc2.y * 0.5f + 0.5f));

    s0 = Vec2(s0.x * (f32)screenW, s0.y * (f32)screenH);
    s1 = Vec2(s1.x * (f32)screenW, s1.y * (f32)screenH);
    s2 = Vec2(s2.x * (f32)screenW, s2.y * (f32)screenH);

    // Bounding box
    f32 minX = std::min({s0.x, s1.x, s2.x});
    f32 maxX = std::max({s0.x, s1.x, s2.x});
    f32 minY = std::min({s0.y, s1.y, s2.y});
    f32 maxY = std::max({s0.y, s1.y, s2.y});

    i32 x0 = (i32)std::max(minX, 0.0f);
    i32 x1 = (i32)std::min(maxX + 1.0f, (f32)screenW);
    i32 y0 = (i32)std::max(minY, 0.0f);
    i32 y1 = (i32)std::min(maxY + 1.0f, (f32)screenH);

    if (x0 >= x1 || y0 >= y1) return;

    // Edge functions
    auto edgeFunc = [](f32 ax, f32 ay, f32 bx, f32 by, f32 cx, f32 cy) -> f32 {
        return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    };

    f32 area = edgeFunc(s0.x, s0.y, s1.x, s1.y, s2.x, s2.y);
    if (area < 0.0001f) return;

    f32 invArea = 1.0f / area;

    // Scanline rasterization
    for (i32 y = y0; y < y1; y++) {
        for (i32 x = x0; x < x1; x++) {
            f32 px = (f32)x + 0.5f;
            f32 py = (f32)y + 0.5f;

            f32 w0 = edgeFunc(s1.x, s1.y, s2.x, s2.y, px, py) * invArea;
            f32 w1 = edgeFunc(s2.x, s2.y, s0.x, s0.y, px, py) * invArea;
            f32 w2 = 1.0f - w0 - w1;

            if (w0 < 0 || w1 < 0 || w2 < 0) continue;

            f32 depth = ndc0.z * w0 + ndc1.z * w1 + ndc2.z * w2;
            if (depth < 0 || depth > 1) continue;

            u32 pixelIdx = (u32)(y * screenW + x);

            if (depth < depthBuf[pixelIdx]) {
                depthBuf[pixelIdx] = depth;
                buffer[pixelIdx].clusterID = clusterIdx;
                buffer[pixelIdx].depth = depth;
            }
        }
    }
}

// ============================================================================
// Debug and Statistics
// ============================================================================

void FrostCluster::getHierarchyStats(u32& totalNodes, u32& leafNodes,
                                       u32& internalNodes, f32& avgChildren) const {
    totalNodes = treeNodeCount_;
    leafNodes = 0;
    internalNodes = 0;
    u32 totalChildren = 0;

    for (u32 i = 0; i < treeNodeCount_; i++) {
        if (treeNodes_[i].isLeaf) {
            leafNodes++;
        } else {
            internalNodes++;
            if (treeNodes_[i].leftChild != 0xFFFFFFFF) totalChildren++;
            if (treeNodes_[i].rightChild != 0xFFFFFFFF) totalChildren++;
        }
    }

    avgChildren = internalNodes > 0 ? (f32)totalChildren / (f32)internalNodes : 0;
}

void FrostCluster::getRasterStats(u32& swClusters, u32& hwClusters,
                                    u32& hybridClusters) const {
    swClusters = 0;
    hwClusters = 0;
    hybridClusters = 0;

    for (u32 i = 0; i < clusterCount_; i++) {
        switch (clusters_[i].rasterMethod) {
            case RasterMethod::Software: swClusters++; break;
            case RasterMethod::Hardware: hwClusters++; break;
            case RasterMethod::Hybrid: hybridClusters++; break;
        }
    }
}

f32 FrostCluster::computeMemoryUsage() const {
    f32 bytes = 0;

    // Clusters
    bytes += (f32)clusterCount_ * sizeof(Cluster);

    // Tree nodes
    bytes += (f32)treeNodeCount_ * sizeof(ClusterTreeNode);

    // Visibility buffer
    bytes += (f32)visBuffer_.size() * sizeof(VisibilityEntry);

    // Material bins
    bytes += (f32)materialBinCount_ * sizeof(MaterialBin);

    return bytes / (1024.0f * 1024.0f);  // Convert to MB
}

// ============================================================================
// Cluster Memory and Performance Analysis
// ============================================================================

f32 FrostCluster::computeVertexCacheEfficiency() const {
    // Estimate vertex cache hit rate
    u32 cacheHits = 0;
    u32 cacheMisses = 0;
    u32 cacheSize = 32;  // typical GPU vertex cache size

    Vector<u32> cache;
    cache.resize(cacheSize);
    u32 cachePos = 0;

    for (u32 i = 0; i < clusterCount_; i++) {
        const Cluster& c = clusters_[i];
        for (u32 t = 0; t < c.triangleCount; t++) {
            for (u32 v = 0; v < 3; v++) {
                u32 vertIdx = c.triangles[t].indices[v];

                bool found = false;
                for (u32 c2 = 0; c2 < cacheSize; c2++) {
                    if (cache[c2] == vertIdx) {
                        cacheHits++;
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    cacheMisses++;
                    cache[cachePos] = vertIdx;
                    cachePos = (cachePos + 1) % cacheSize;
                }
            }
        }
    }

    u32 total = cacheHits + cacheMisses;
    return total > 0 ? (f32)cacheHits / (f32)total : 0;
}

f32 FrostCluster::computeIndexOverhead() const {
    // Compute overhead from index buffer vs vertex buffer
    u32 totalVertices = 0;
    u32 totalIndices = 0;

    for (u32 i = 0; i < clusterCount_; i++) {
        totalVertices += clusters_[i].vertexCount;
        totalIndices += clusters_[i].triangleCount * 3;
    }

    return totalVertices > 0 ? (f32)totalIndices / (f32)totalVertices : 0;
}

u32 FrostCluster::estimateGPUMemoryUsage() const {
    // Estimate GPU memory in bytes
    u32 bytes = 0;

    for (u32 i = 0; i < clusterCount_; i++) {
        const Cluster& c = clusters_[i];
        // Position (12) + Normal (12) + UV (8) + Tangent (16) per vertex
        bytes += c.vertexCount * (12 + 12 + 8 + 16);
        // Index buffer (6 bytes per triangle for u16 indices)
        bytes += c.triangleCount * 6;
    }

    // Visibility buffer
    bytes += (u32)visBuffer_.size() * 12;  // clusterID + triangleID + depth

    return bytes;
}

// ============================================================================
// Cluster Merging and Splitting
// ============================================================================

void FrostCluster::mergeClusters(u32 clusterA, u32 clusterB) {
    if (clusterA >= clusterCount_ || clusterB >= clusterCount_) return;

    Cluster& a = clusters_[clusterA];
    Cluster& b = clusters_[clusterB];

    // Merge triangles from B into A
    u32 newTriCount = a.triangleCount + b.triangleCount;
    if (newTriCount > CLUSTER_TRIANGLES) return;

    for (u32 t = 0; t < b.triangleCount; t++) {
        a.triangles[a.triangleCount + t] = b.triangles[t];
    }
    a.triangleCount = newTriCount;

    // Merge vertices
    for (u32 v = 0; v < b.vertexCount && a.vertexCount < CLUSTER_VERTICES; v++) {
        a.positions[a.vertexCount] = b.positions[v];
        a.normals[a.vertexCount] = b.normals[v];
        a.uvs[a.vertexCount] = b.uvs[v];
        a.vertexCount++;
    }

    // Update bounds
    a.boundsMin = a.boundsMin.min(b.boundsMin);
    a.boundsMax = a.boundsMax.max(b.boundsMax);
    a.boundsCenter = (a.boundsMin + a.boundsMax) * 0.5f;
    a.boundsRadius = (a.boundsMax - a.boundsMin).length() * 0.5f;

    // Clear cluster B
    b.triangleCount = 0;
    b.vertexCount = 0;
}

u32 FrostCluster::splitCluster(u32 clusterIdx) {
    if (clusterIdx >= clusterCount_) return 0xFFFFFFFF;

    Cluster& cluster = clusters_[clusterIdx];
    if (cluster.triangleCount <= CLUSTER_TRIANGLES / 2) return 0xFFFFFFFF;

    // Create new cluster for second half
    u32 newIdx = clusterCount_++;
    if (newIdx >= MAX_CLUSTERS) {
        clusterCount_--;
        return 0xFFFFFFFF;
    }

    Cluster& newCluster = clusters_[newIdx];
    u32 splitPoint = cluster.triangleCount / 2;

    // Move triangles to new cluster
    for (u32 t = splitPoint; t < cluster.triangleCount; t++) {
        newCluster.triangles[newCluster.triangleCount++] = cluster.triangles[t];
    }
    cluster.triangleCount = splitPoint;

    // Update bounds
    computeClusterBounds(cluster);
    computeClusterBounds(newCluster);

    return newIdx;
}

// ============================================================================
// Cluster Visibility Analysis
// ============================================================================

u32 FrostCluster::countVisibleTriangles() const {
    u32 count = 0;
    for (u32 i = 0; i < clusterCount_; i++) {
        if (clusters_[i].isVisible && clusters_[i].needsRasterize) {
            count += clusters_[i].triangleCount;
        }
    }
    return count;
}

f32 FrostCluster::computeOcclusionCullingEfficiency() const {
    u32 totalClusters = 0;
    u32 culledClusters = 0;

    for (u32 i = 0; i < clusterCount_; i++) {
        totalClusters++;
        if (!clusters_[i].isVisible) {
            culledClusters++;
        }
    }

    return totalClusters > 0 ? (f32)culledClusters / (f32)totalClusters : 0;
}

f32 FrostCluster::computeRasterizationEfficiency() const {
    u32 swPixels = 0;
    u32 hwPixels = 0;

    for (u32 i = 0; i < clusterCount_; i++) {
        if (clusters_[i].needsRasterize) {
            u32 triPixels = clusters_[i].triangleCount * 4;  // estimate
            if (clusters_[i].rasterMethod == RasterMethod::Software) {
                swPixels += triPixels;
            } else {
                hwPixels += triPixels;
            }
        }
    }

    u32 total = swPixels + hwPixels;
    return total > 0 ? (f32)hwPixels / (f32)total : 0;
}

// ============================================================================
// Material and Batch Analysis
// ============================================================================

u32 FrostCluster::computeMaxBatchSize() const {
    u32 maxSize = 0;
    for (u32 i = 0; i < materialBinCount_; i++) {
        maxSize = std::max(maxSize, (u32)materialBins_[i].clusterIndices.size());
    }
    return maxSize;
}

f32 FrostCluster::computeMaterialCoherency() const {
    // Measure how well clusters are sorted by material
    u32 materialChanges = 0;
    u32 lastMaterial = 0xFFFFFFFF;

    for (u32 i = 0; i < clusterCount_; i++) {
        if (clusters_[i].materialID != lastMaterial) {
            materialChanges++;
            lastMaterial = clusters_[i].materialID;
        }
    }

    return materialChanges > 0 ? 1.0f / (f32)materialChanges : 1.0f;
}

// ============================================================================
// Debug and Statistics
// ============================================================================

void FrostCluster::getFullStats(u32& clusters, u32& triangles, u32& vertices,
                                  u32& materials, f32& memoryMB) const {
    clusters = clusterCount_;
    triangles = 0;
    vertices = 0;

    for (u32 i = 0; i < clusterCount_; i++) {
        triangles += clusters_[i].triangleCount;
        vertices += clusters_[i].vertexCount;
    }

    materials = computeMaterialBinCount();
    memoryMB = computeMemoryUsage();
}

} // namespace Frost
