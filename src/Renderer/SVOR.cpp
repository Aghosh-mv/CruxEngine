#include "Renderer/SVOR.h"

#include <chrono>
#include <cmath>

namespace Frost {

static f32 currentTimeMs() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return (f32)std::chrono::duration_cast<std::chrono::microseconds>(now).count() * 1e-3f;
}

void SVORSystem::clear() {
    for (u32 i = 0; i < MAX_NODES; i++) legacyNodes_[i] = SVONode{};
    for (u32 i = 0; i < MAX_VOXELS; i++) voxels_[i] = SVoxel{};
    legacyNodeCount_ = 1;
    voxelCount_ = 1;

    nodes_.clear();
    voxelData_.clear();
    nodeCount_ = 0;
    maxDepth_ = 8;
    origin_ = Vec3(0.0f);
    voxelSize_ = 1.0f;
    voxelizedTriangles_ = 0;
    buildTimeMs_ = 0.0f;
}

void SVORSystem::init(u32 maxDepth, Vec3 origin, f32 voxelSize) {
    maxDepth_ = (u32)Mathf::clamp((f32)maxDepth, 1.0f, (f32)MAX_DEPTH);
    origin_ = origin;
    voxelSize_ = voxelSize > 0.0f ? voxelSize : 1.0f;

    nodes_.clear();
    nodes_.push_back(VoxelNode{0, 0, Vec3(0.0f), Vec3(0.0f), 0.0f});
    nodeCount_ = 1;

    voxelData_.clear();
    voxelizedTriangles_ = 0;
    buildTimeMs_ = 0.0f;
}

bool SVORSystem::insertVoxel(const VoxelData& data) {
    if (nodes_.empty() || maxDepth_ == 0 || voxelSize_ <= 0.0f) return false;

    Vec3 t = (data.position - origin_) / voxelSize_;
    if (t.x < -1.0f || t.x > 1.0f || t.y < -1.0f || t.y > 1.0f || t.z < -1.0f || t.z > 1.0f) return false;

    u32 path[16];
    u32 pathCount = 0;
    u32 nodeIdx = 0;
    Vec3 center(0.0f);
    f32 half = 0.5f;

    for (u32 depth = 0; depth < maxDepth_; depth++) {
        path[pathCount++] = nodeIdx;
        VoxelNode node = nodes_[nodeIdx];

        u32 octX = t.x >= center.x ? 1u : 0u;
        u32 octY = t.y >= center.y ? 1u : 0u;
        u32 octZ = t.z >= center.z ? 1u : 0u;
        u32 childIdx = (octZ << 2) | (octY << 1) | octX;

        center.x += octX ? half : -half;
        center.y += octY ? half : -half;
        center.z += octZ ? half : -half;
        half *= 0.5f;

        if (node.childMask & (1u << childIdx)) {
            nodeIdx = node.childOffset + childIdx;
        } else {
            u32 base = nodeCount_;
            for (u32 i = 0; i < 8; i++) nodes_.push_back(VoxelNode{0, 0, Vec3(0.0f), Vec3(0.0f), 0.0f});
            nodeCount_ = (u32)nodes_.size();
            nodes_[nodeIdx].childMask = node.childMask | (1u << childIdx);
            nodes_[nodeIdx].childOffset = base;
            nodeIdx = base + childIdx;
        }
    }
    path[pathCount++] = nodeIdx;

    VoxelNode& leaf = nodes_[nodeIdx];
    f32 d = data.density > 0.0f ? data.density : 1.0f;
    if (leaf.density > 0.0f) {
        f32 total = leaf.density + d;
        leaf.albedo = (leaf.albedo * leaf.density + data.color * d) / total;
        leaf.emission = (leaf.emission * leaf.density + data.color * d) / total;
        leaf.density = Mathf::min(1.0f, total);
    } else {
        leaf.albedo = data.color;
        leaf.emission = data.color;
        leaf.density = d;
    }

    voxelData_.push_back(data);
    aggregateUp(path, pathCount);
    return true;
}

void SVORSystem::aggregateUp(const u32* path, u32 pathCount) {
    if (pathCount < 2) return;
    for (i32 i = (i32)pathCount - 2; i >= 0; i--) {
        VoxelNode& node = nodes_[path[i]];
        Vec3 accAlbedo(0.0f), accEmission(0.0f);
        f32 accDensity = 0.0f;
        u32 childCount = 0;
        for (u32 c = 0; c < 8; c++) {
            if (node.childMask & (1u << c)) {
                const VoxelNode& child = nodes_[node.childOffset + c];
                childCount++;
                accAlbedo += child.albedo * child.density;
                accEmission += child.emission * child.density;
                accDensity += child.density;
            }
        }
        if (childCount > 0 && accDensity > 0.0f) {
            node.albedo = accAlbedo / accDensity;
            node.emission = accEmission / accDensity;
            node.density = Mathf::min(1.0f, accDensity / (f32)childCount);
        } else {
            node.albedo = Vec3(0.0f);
            node.emission = Vec3(0.0f);
            node.density = 0.0f;
        }
    }
}

void SVORSystem::sampleCone(u32 nodeIdx, Vec3 center, f32 half, Vec3 p, f32 r, u32 depth,
                            Vec3& accRadiance, f32& accDensity, f32& accWeight) const {
    if (depth > maxDepth_) return;
    const VoxelNode& node = nodes_[nodeIdx];

    Vec3 cellMin = center - Vec3(half);
    Vec3 cellMax = center + Vec3(half);
    Vec3 closest(Mathf::clamp(p.x, cellMin.x, cellMax.x),
                 Mathf::clamp(p.y, cellMin.y, cellMax.y),
                 Mathf::clamp(p.z, cellMin.z, cellMax.z));
    f32 dist = (p - closest).length();
    if (dist > r) return;

    f32 weight = 1.0f - dist / r;
    if (weight <= 0.0f) return;

    if (node.childMask == 0 || depth >= maxDepth_ || half <= r) {
        accRadiance += node.emission * weight;
        accDensity += node.density * weight;
        accWeight += weight;
        return;
    }

    f32 childHalf = half * 0.5f;
    for (u32 c = 0; c < 8; c++) {
        if (!(node.childMask & (1u << c))) continue;
        Vec3 cc = center;
        if (c & 1) cc.x += childHalf; else cc.x -= childHalf;
        if (c & 2) cc.y += childHalf; else cc.y -= childHalf;
        if (c & 4) cc.z += childHalf; else cc.z -= childHalf;
        sampleCone(node.childOffset + c, cc, childHalf, p, r, depth + 1,
                   accRadiance, accDensity, accWeight);
    }
}

Vec3 SVORSystem::traceCone(Vec3 origin, Vec3 direction, f32 aperture, f32 maxDist) {
    if (nodes_.empty() || maxDepth_ == 0 || voxelSize_ <= 0.0f) return Vec3(0.0f);

    Vec3 dir = direction.normalized();
    f32 leafHalf = voxelSize_ / (f32)(1u << maxDepth_);
    f32 apertureRadius = aperture > 0.0f ? aperture : 0.01f;

    Vec3 radiance(0.0f);
    f32 transmittance = 1.0f;
    f32 t = 0.0f;
    u32 guard = 0;

    while (t < maxDist && transmittance > 0.01f && guard++ < 512) {
        f32 radius = Mathf::max(apertureRadius * t, leafHalf);
        Vec3 p = origin + dir * t;

        Vec3 accRadiance(0.0f);
        f32 accDensity = 0.0f;
        f32 accWeight = 0.0f;
        sampleCone(0, Vec3(0.0f), 1.0f, (p - origin_) / voxelSize_, radius / voxelSize_, 0,
                   accRadiance, accDensity, accWeight);

        Vec3 sampleRadiance = accWeight > 0.0f ? accRadiance / accWeight : Vec3(0.0f);
        f32 opacity = accWeight > 0.0f ? Mathf::saturate(accDensity / accWeight) : 0.0f;
        radiance += sampleRadiance * opacity * transmittance;
        transmittance *= (1.0f - opacity);
        t += Mathf::max(radius * 1.5f, leafHalf * 2.0f);
    }
    return radiance;
}

bool SVORSystem::queryLeaf(i32 ix, i32 iy, i32 iz, u32& outNodeIdx) const {
    u32 ux = (u32)ix, uy = (u32)iy, uz = (u32)iz;
    u32 nodeIdx = 0;
    for (u32 depth = 0; depth < maxDepth_; depth++) {
        u32 bit = 1u << (maxDepth_ - 1u - depth);
        u32 oct = ((uz & bit) ? 4u : 0u) | ((uy & bit) ? 2u : 0u) | ((ux & bit) ? 1u : 0u);
        const VoxelNode& node = nodes_[nodeIdx];
        if (!(node.childMask & (1u << oct))) return false;
        nodeIdx = node.childOffset + oct;
    }
    outNodeIdx = nodeIdx;
    return true;
}

Vec3 SVORSystem::traceRay(Vec3 origin, Vec3 dir, f32 maxDist) {
    if (nodes_.empty() || maxDepth_ == 0 || voxelSize_ <= 0.0f) return Vec3(0.0f);

    Vec3 d = dir.normalized();
    u32 n = 1u << maxDepth_;
    f32 leafSide = (2.0f * voxelSize_) / (f32)n;
    f32 invLeaf = 1.0f / leafSide;

    f32 px = origin.x + voxelSize_;
    f32 py = origin.y + voxelSize_;
    f32 pz = origin.z + voxelSize_;

    i32 ix = (i32)std::floor(px * invLeaf);
    i32 iy = (i32)std::floor(py * invLeaf);
    i32 iz = (i32)std::floor(pz * invLeaf);
    i32 maxI = (i32)n - 1;
    if (ix < 0) ix = 0; else if (ix > maxI) ix = maxI;
    if (iy < 0) iy = 0; else if (iy > maxI) iy = maxI;
    if (iz < 0) iz = 0; else if (iz > maxI) iz = maxI;

    i32 stepX = d.x >= 0.0f ? 1 : -1;
    i32 stepY = d.y >= 0.0f ? 1 : -1;
    i32 stepZ = d.z >= 0.0f ? 1 : -1;

    bool hasX = Mathf::abs(d.x) > 1e-6f;
    bool hasY = Mathf::abs(d.y) > 1e-6f;
    bool hasZ = Mathf::abs(d.z) > 1e-6f;

    f32 tDeltaX = hasX ? Mathf::abs(1.0f / d.x) * leafSide : 1e30f;
    f32 tDeltaY = hasY ? Mathf::abs(1.0f / d.y) * leafSide : 1e30f;
    f32 tDeltaZ = hasZ ? Mathf::abs(1.0f / d.z) * leafSide : 1e30f;

    f32 tMaxX = hasX ? ((d.x > 0.0f) ? ((f32)(ix + 1) * leafSide - px) / d.x : (px - (f32)ix * leafSide) / (-d.x)) : 1e30f;
    f32 tMaxY = hasY ? ((d.y > 0.0f) ? ((f32)(iy + 1) * leafSide - py) / d.y : (py - (f32)iy * leafSide) / (-d.y)) : 1e30f;
    f32 tMaxZ = hasZ ? ((d.z > 0.0f) ? ((f32)(iz + 1) * leafSide - pz) / d.z : (pz - (f32)iz * leafSide) / (-d.z)) : 1e30f;

    f32 t = 0.0f;
    u32 guard = 0;
    u32 maxGuard = n * 4;

    while (guard++ < maxGuard && t <= maxDist) {
        u32 leafIdx = 0;
        if (queryLeaf(ix, iy, iz, leafIdx)) {
            const VoxelNode& leaf = nodes_[leafIdx];
            if (leaf.density > 0.5f) return leaf.emission;
        }

        if (tMaxX <= tMaxY && tMaxX <= tMaxZ) {
            ix += stepX; t = tMaxX; tMaxX += tDeltaX;
        } else if (tMaxY <= tMaxZ) {
            iy += stepY; t = tMaxY; tMaxY += tDeltaY;
        } else {
            iz += stepZ; t = tMaxZ; tMaxZ += tDeltaZ;
        }

        if (ix < 0 || ix > maxI || iy < 0 || iy > maxI || iz < 0 || iz > maxI) break;
    }
    return Vec3(0.0f);
}

void SVORSystem::voxelizeTriangle(Vec3 a, Vec3 b, Vec3 c, Vec3 color) {
    if (maxDepth_ == 0 || voxelSize_ <= 0.0f) return;
    f32 start = currentTimeMs();

    u32 n = 1u << maxDepth_;
    f32 leafSide = (2.0f * voxelSize_) / (f32)n;
    f32 invLeaf = 1.0f / leafSide;

    Vec3 nrm = (b - a).cross(c - a).normalized();
    if (nrm.lengthSquared() <= 0.0f) {
        buildTimeMs_ += currentTimeMs() - start;
        return;
    }

    Vec3 worldMin = origin_ - Vec3(voxelSize_);
    Vec3 worldMax = origin_ + Vec3(voxelSize_);
    Vec3 bbMin = a.min(b).min(c).max(worldMin);
    Vec3 bbMax = a.max(b).max(c).min(worldMax);
    if (bbMin.x > bbMax.x || bbMin.y > bbMax.y || bbMin.z > bbMax.z) {
        buildTimeMs_ += currentTimeMs() - start;
        return;
    }

    i32 x0 = (i32)std::floor((bbMin.x + voxelSize_) * invLeaf);
    i32 x1 = (i32)std::floor((bbMax.x + voxelSize_) * invLeaf);
    i32 y0 = (i32)std::floor((bbMin.y + voxelSize_) * invLeaf);
    i32 y1 = (i32)std::floor((bbMax.y + voxelSize_) * invLeaf);
    i32 z0 = (i32)std::floor((bbMin.z + voxelSize_) * invLeaf);
    i32 z1 = (i32)std::floor((bbMax.z + voxelSize_) * invLeaf);
    i32 maxI = (i32)n - 1;
    if (x0 < 0) x0 = 0; else if (x0 > maxI) x0 = maxI;
    if (x1 < 0) x1 = 0; else if (x1 > maxI) x1 = maxI;
    if (y0 < 0) y0 = 0; else if (y0 > maxI) y0 = maxI;
    if (y1 < 0) y1 = 0; else if (y1 > maxI) y1 = maxI;
    if (z0 < 0) z0 = 0; else if (z0 > maxI) z0 = maxI;
    if (z1 < 0) z1 = 0; else if (z1 > maxI) z1 = maxI;

    Vec3 v0 = b - a;
    Vec3 v1 = c - a;
    f32 d00 = v0.dot(v0);
    f32 d01 = v0.dot(v1);
    f32 d11 = v1.dot(v1);
    f32 denom = d00 * d11 - d01 * d01;
    if (denom <= 0.0f) {
        buildTimeMs_ += currentTimeMs() - start;
        return;
    }

    for (i32 ix = x0; ix <= x1; ix++) {
        for (i32 iy = y0; iy <= y1; iy++) {
            for (i32 iz = z0; iz <= z1; iz++) {
                Vec3 p((f32)ix * leafSide - voxelSize_ + leafSide * 0.5f,
                       (f32)iy * leafSide - voxelSize_ + leafSide * 0.5f,
                       (f32)iz * leafSide - voxelSize_ + leafSide * 0.5f);

                f32 dist = nrm.dot(p - a);
                Vec3 proj = p - nrm * dist;
                Vec3 v2 = proj - a;
                f32 d20 = v2.dot(v0);
                f32 d21 = v2.dot(v1);
                f32 v = (d11 * d20 - d01 * d21) / denom;
                f32 w = (d00 * d21 - d01 * d20) / denom;
                f32 u = 1.0f - v - w;

                const f32 eps = 1e-4f;
                if (u < -eps || v < -eps || w < -eps) continue;

                VoxelData vd;
                vd.position = p;
                vd.color = color;
                vd.normal = nrm;
                vd.density = 1.0f;
                insertVoxel(vd);
            }
        }
    }

    voxelizedTriangles_++;
    buildTimeMs_ += currentTimeMs() - start;
}

} // namespace Frost
