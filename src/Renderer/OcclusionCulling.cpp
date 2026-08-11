#include "OcclusionCulling.h"

#include "Texture.h"
#include <algorithm>
#include <cmath>

namespace Frost {
namespace Renderer {

void Frustum::update(const Mat4& viewProj) {
    planes[0] = {viewProj.m[3] + viewProj.m[0], viewProj.m[4] + viewProj.m[1], viewProj.m[5] + viewProj.m[2], 1.0f};
    planes[1] = {viewProj.m[3] - viewProj.m[0], viewProj.m[4] - viewProj.m[1], viewProj.m[5] - viewProj.m[2], 1.0f};
    planes[2] = {viewProj.m[3] + viewProj.m[6], viewProj.m[4] + viewProj.m[7], viewProj.m[5] + viewProj.m[8], 1.0f};
    planes[3] = {viewProj.m[3] - viewProj.m[6], viewProj.m[4] - viewProj.m[7], viewProj.m[5] - viewProj.m[8], 1.0f};
    planes[4] = {viewProj.m[3] + viewProj.m[9], viewProj.m[4] + viewProj.m[10], viewProj.m[5] + viewProj.m[11], 1.0f};
    planes[5] = {viewProj.m[3] - viewProj.m[9], viewProj.m[4] - viewProj.m[10], viewProj.m[5] - viewProj.m[11], 1.0f};
    for (auto& p : planes) {
        f32 len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z + p.w * p.w);
        if (len > 0.0f) {
            p.x /= len; p.y /= len; p.z /= len; p.w /= len;
        }
    }
}

bool Frustum::intersects(const AABB& box) const {
    for (u32 i = 0; i < 6; i++) {
        const auto& plane = planes[i];
        f32 denom = plane.w;
        if (std::abs(plane.x * box.min.x + plane.y * box.min.y + plane.z * box.min.z - denom) > 0.001f) {
            if (plane.x * box.min.x + plane.y * box.min.y + plane.z * box.min.z > denom) return false;
        }
        if (std::abs(plane.x * box.max.x + plane.y * box.max.y + plane.z * box.max.z - denom) > 0.001f) {
            if (plane.x * box.max.x + plane.y * box.max.y + plane.z * box.max.z < denom) return false;
        }
    }
    return true;
}

bool Frustum::contains(const AABB& box) const {
    for (u32 i = 0; i < 6; i++) {
        const auto& p = planes[i];
        if (p.w > 0.0f) {
            f32 d = p.x * box.min.x + p.y * box.min.y + p.z * box.min.z - p.w;
            if (d > 0.0f) return false;
        } else {
            f32 d = p.x * box.min.x + p.y * box.min.y + p.z * box.min.z + p.w;
            if (d < 0.0f) return false;
        }
    }
    return true;
}

bool Frustum::contains(const Vec3& point) const {
    for (u32 i = 0; i < 6; i++) {
        const auto& p = planes[i];
        f32 d = p.x * point.x + p.y * point.y + p.z * point.z - p.w;
        if (p.w > 0.0f && d > 0.0f) return false;
        if (p.w <= 0.0f && d < 0.0f) return false;
    }
    return true;
}

void HiZBuffer::initialize(u32 width, u32 height) {
    width_ = width;
    height_ = height;
    mipLevels_ = 0;
    while ((1u << mipLevels_) < width || (1u << mipLevels_) < height) mipLevels_++;
    mipLevels_ = std::max(mipLevels_, 1u);
    data_.resize(width * height * mipLevels_);
    std::fill(data_.begin(), data_.end(), 0.0f);
}

void HiZBuffer::shutdown() {
    if (gpuBuffer_) {
        gpuBuffer_ = nullptr;
    }
    data_.clear();
    mipData_.clear();
    width_ = 0;
    height_ = 0;
    mipLevels_ = 0;
}

void HiZBuffer::buildFromDepth(void* cmd, FrameBuffer* depthTexture) {
    if (!depthTexture) return;
    // Stub: In real implementation, would read depth from FrameBuffer's depth texture
    // For now, just initialize with dummy data
    f32* pixelData = new f32[width_ * height_];
    std::fill(pixelData, pixelData + width_ * height_, 1.0f);
    buildFromDepthCPU(pixelData, width_, height_);
    delete[] pixelData;
}

void HiZBuffer::buildFromDepthCPU(const f32* depthData, u32 w, u32 h) {
    width_ = w;
    height_ = h;
    mipLevels_ = 0;
    while ((1u << mipLevels_) < w || (1u << mipLevels_) < h) mipLevels_++;
    mipLevels_ = std::max(mipLevels_, 1u);
    data_.resize(w * h * mipLevels_);
    mipData_.clear();
    for (u32 mip = 0; mip < mipLevels_; mip++) {
        HiZMipLevel level;
        level.width = w;
        level.height = h;
        level.offset = mip > 0 ? data_.size() / mipLevels_ : 0;
        mipData_.push_back(level);
    }
}

void HiZBuffer::generateMipsCPU() {
    for (u32 mip = 1; mip < mipLevels_; mip++) {
        u32 w = width_ >> mip;
        u32 h = height_ >> mip;
        f32* src = data_.data() + (mip - 1) * w * h * sizeof(f32);
        f32* dst = data_.data() + mip * w * h * sizeof(f32);
        for (u32 y = 0; y < h; y++) {
            for (u32 x = 0; x < w; x++) {
                f32 minVal = 1e30f;
                for (u32 dy = 0; dy < 2; dy++) {
                    for (u32 dx = 0; dx < 2; dx++) {
                        u32 sx = x * 2 + dx;
                        u32 sy = y * 2 + dy;
                        if (sx < width_ && sy < height_) {
                            minVal = std::min(minVal, src[sy * width_ + sx]);
                        }
                    }
                }
                dst[y * w + x] = minVal;
            }
        }
    }
}

f32 HiZBuffer::sampleMipLevel(u32 mip, f32 u, f32 v) const {
    u32 w = width_ >> mip;
    u32 h = height_ >> mip;
    u32 sx = (u32)(u * w);
    u32 sy = (u32)(v * h);
    if (sx >= w || sy >= h) return 1e30f;
    return data_[(mip * w * h) + sy * w + sx];
}

bool HiZBuffer::testAABB(const AABB& box, const Mat4& viewProj) const {
    for (u32 mip = 0; mip < mipLevels_; mip++) {
        u32 w = width_ >> mip;
        u32 h = height_ >> mip;
        f32 sampleSize = 1.0f / (1u << mip);
        f32 minU = box.min.x / (w + 1) * sampleSize;
        f32 maxU = box.max.x / (w + 1) * sampleSize;
        f32 minV = box.min.y / (h + 1) * sampleSize;
        f32 maxV = box.max.y / (h + 1) * sampleSize;
        f32 minW = box.min.z / (width_ + 1) * sampleSize;
        f32 maxW = box.max.z / (width_ + 1) * sampleSize;
        f32 depth = sampleMipLevel(mip, maxU, maxV);
        if (depth >= minW) return false;
    }
    return true;
}

bool HiZBuffer::testSphere(const Vec3& center, f32 radius, const Mat4& viewProj) const {
    return testAABB(AABB{center - Vec3(radius, radius, radius), center + Vec3(radius, radius, radius)}, viewProj);
}

SoftwareRasterizer::SoftwareRasterizer(u32 width, u32 height) {
    width_ = width;
    height_ = height;
    tileWidth_ = 64;
    tileHeight_ = 64;
    clear();
}

SoftwareRasterizer::~SoftwareRasterizer() {}

void SoftwareRasterizer::clear() {
    triangles_.clear();
    tiles_.clear();
    depthBuffer_.clear();
    objectIdBuffer_.clear();
    setupTileGrid();
}

void SoftwareRasterizer::setViewProjection(const Mat4& viewProj) {
    viewProj_ = viewProj;
}

void SoftwareRasterizer::addMesh(const OcclusionMesh& mesh, const Mat4& transform, u32 objectId) {
    // Use the mesh's bounds directly since we don't have vertex buffer access here
    // This is a simplified implementation for occlusion testing
    if (mesh.indexCount == 0) return;
    
    // Create a bounding box test triangle for the mesh
    Vec3 center = mesh.bounds.center();
    Vec3 extents = mesh.bounds.extents();
    
    // Add 8 corner triangles for the AABB
    Vec3 corners[8] = {
        {mesh.bounds.min.x, mesh.bounds.min.y, mesh.bounds.min.z},
        {mesh.bounds.max.x, mesh.bounds.min.y, mesh.bounds.min.z},
        {mesh.bounds.min.x, mesh.bounds.max.y, mesh.bounds.min.z},
        {mesh.bounds.max.x, mesh.bounds.max.y, mesh.bounds.min.z},
        {mesh.bounds.min.x, mesh.bounds.min.y, mesh.bounds.max.z},
        {mesh.bounds.max.x, mesh.bounds.min.y, mesh.bounds.max.z},
        {mesh.bounds.min.x, mesh.bounds.max.y, mesh.bounds.max.z},
        {mesh.bounds.max.x, mesh.bounds.max.y, mesh.bounds.max.z}
    };
    
    // Add 12 triangles for the box
    u32 triIndices[36] = {
        0, 1, 2,  1, 3, 2,  // bottom
        4, 6, 5,  5, 6, 7,  // top
        0, 2, 4,  2, 6, 4,  // left
        1, 5, 3,  3, 5, 7,  // right
        0, 4, 1,  1, 4, 5,  // front
        2, 3, 6,  3, 7, 6   // back
    };
    
    for (u32 i = 0; i < 36; i += 3) {
        Vertex v0, v1, v2;
        v0.position = transform * Vec4(corners[triIndices[i]].x, corners[triIndices[i]].y, corners[triIndices[i]].z, 1.0f);
        v1.position = transform * Vec4(corners[triIndices[i+1]].x, corners[triIndices[i+1]].y, corners[triIndices[i+1]].z, 1.0f);
        v2.position = transform * Vec4(corners[triIndices[i+2]].x, corners[triIndices[i+2]].y, corners[triIndices[i+2]].z, 1.0f);
        triangles_.push_back({v0, v1, v2, objectId});
    }
}

void SoftwareRasterizer::rasterize() {
    for (u32 y = 0; y < height_; y++) {
        for (u32 x = 0; x < width_; x++) {
            f32 minDepth = 1e30f;
            u32 bestObj = 0;
            for (auto& tri : triangles_) {
                for (u32 i = 0; i < 3; i++) {
                    f32 tx = tri.v[i].position.x;
                    f32 ty = tri.v[i].position.y;
                    f32 tz = tri.v[i].position.z;
                    f32 wx = x + (1.0f / (float)width_) * tx;
                    f32 wy = y + (1.0f / (float)height_) * ty;
                    f32 wz = (1.0f / (float)width_) * tz;
                    if (wz >= 0.0f && wz < (float)width_ && wy >= 0.0f && wy < (float)height_) {
                        if (wz < minDepth) {
                            minDepth = wz;
                            bestObj = tri.objectId;
                        }
                    }
                }
            }
        }
    }
}

bool SoftwareRasterizer::testOcclusion(const AABB& box, const Mat4& viewProj) {
    f32* depth = getDepthBuffer();
    for (u32 x = 0; x < width_; x++) {
        for (u32 y = 0; y < height_; y++) {
            if (box.contains(Vec3((f32)x / (float)width_, (f32)y / (float)height_, 0.0f))) {
                f32 depthVal = depth[y * width_ + x];
                if (depthVal < box.min.z) return false;
            }
        }
    }
    return true;
}

bool SoftwareRasterizer::testOcclusion(const Vec3& center, f32 radius, const Mat4& viewProj) {
    f32* depth = getDepthBuffer();
    f32 sx = center.x / (float)width_;
    f32 sy = center.y / (float)height_;
    f32 sr = radius / (float)width_;
    u32 minX = (u32)(sx - sr) * width_;
    u32 maxX = (u32)(sx + sr) * width_;
    u32 minY = (u32)(sy - sr) * height_;
    u32 maxY = (u32)(sy + sr) * height_;
    for (u32 y = minY; y < maxY; y++) {
        for (u32 x = minX; x < maxX; x++) {
            if (depth[y * width_ + x] < center.z) return false;
        }
    }
    return true;
}

void SoftwareRasterizer::rasterizeTriangle(const Triangle& tri) {
    f32 d0 = tri.v[0].depth;
    f32 d1 = tri.v[1].depth;
    f32 d2 = tri.v[2].depth;
    f32 minD = std::min({d0, d1, d2});
    f32 maxD = std::max({d0, d1, d2});
    for (u32 y = 0; y < tileHeight_; y++) {
        for (u32 x = 0; x < tileWidth_; x++) {
            f32 px = (f32)(x + 0.5f) / (float)tileWidth_;
            f32 py = (f32)(y + 0.5f) / (float)tileHeight_;
            if (px > 0.0f && py > 0.0f && px < 1.0f && py < 1.0f) {
                f32 val = minD + (maxD - minD) * px * py;
                u32 idx = y * tileWidth_ + x;
                if (val < depthBuffer_[idx]) {
                    depthBuffer_[idx] = val;
                    objectIdBuffer_[idx] = tri.objectId;
                }
            }
        }
    }
}

void SoftwareRasterizer::setupTileGrid() {
    u32 numTilesX = (width_ + tileWidth_ - 1) / tileWidth_;
    u32 numTilesY = (height_ + tileHeight_ - 1) / tileHeight_;
    tiles_.resize(numTilesX * numTilesY);
}

void SoftwareRasterizer::processTile(u32 tileX, u32 tileY) {
    u32 baseX = tileX * tileWidth_;
    u32 baseY = tileY * tileHeight_;
    for (u32 y = 0; y < tileHeight_; y++) {
        for (u32 x = 0; x < tileWidth_; x++) {
            u32 idx = (baseY + y) * width_ + (baseX + x);
            depthBuffer_[idx] = 1e30f;
            objectIdBuffer_[idx] = 0;
        }
    }
}

OcclusionCullingSystem::OcclusionCullingSystem() {}

OcclusionCullingSystem::~OcclusionCullingSystem() {
    shutdown();
}

bool OcclusionCullingSystem::initialize(const Config& config) {
    config_ = config;
    currentCmd_ = nullptr;
    currentFrame_ = 0;
    resetStats();
    return true;
}

void OcclusionCullingSystem::shutdown() {
    objects_.clear();
    occlusionMeshes_.clear();
    gpuQueries_.clear();
    staticObjectIndices_.clear();
    dynamicObjectIndices_.clear();
    queryBuffer_ = nullptr;
    queryTexture_ = nullptr;
}

void OcclusionCullingSystem::beginFrame(u64 frameIndex, void* cmd) {
    currentFrame_ = frameIndex;
    currentCmd_ = cmd;
}

void OcclusionCullingSystem::endFrame() {
    currentCmd_ = nullptr;
}

u32 OcclusionCullingSystem::registerObject(const AABB& bounds, const Mat4& transform, u32 meshIndex, bool staticObject) {
    Object obj;
    obj.id = objects_.size() + 1;
    obj.bounds = bounds;
    obj.transform = transform;
    obj.meshIndex = meshIndex;
    obj.staticObject = staticObject;
    obj.lastVisibleFrame = 0;
    obj.wasVisible = true;
    objects_.push_back(obj);
    return obj.id;
}

void OcclusionCullingSystem::unregisterObject(u32 objectId) {
    for (usize i = 0; i < objects_.size(); ++i) {
        if (objects_[i].id == objectId) {
            objects_.erase(i);
            return;
        }
    }
}

void OcclusionCullingSystem::updateObjectTransform(u32 objectId, const Mat4& transform) {
    for (auto& obj : objects_) {
        if (obj.id == objectId) {
            obj.transform = transform;
            return;
        }
    }
}

void OcclusionCullingSystem::updateObjectBounds(u32 objectId, const AABB& bounds) {
    for (auto& obj : objects_) {
        if (obj.id == objectId) {
            obj.bounds = bounds;
            return;
        }
    }
}

void OcclusionCullingSystem::setCamera(const Mat4& view, const Mat4& proj) {
    view_ = view;
    proj_ = proj;
    viewProj_ = view * proj;
    frustum_.update(viewProj_);
}

void OcclusionCullingSystem::setCamera(const Mat4& viewProj) {
    view_ = viewProj;
    viewProj_ = viewProj;
    frustum_.update(viewProj);
}

void OcclusionCullingSystem::addOcclusionMesh(u32 meshId, const Vec3* vertices, u32 vertexCount, const u32* indices, u32 indexCount) {
    OcclusionMesh mesh;
    mesh.meshId = meshId;
    mesh.vertexOffset = 0;
    mesh.indexOffset = 0;
    mesh.indexCount = indexCount;
    mesh.bounds = AABB{Vec3(1e30f, 1e30f, 1e30f), Vec3(-1e30f, -1e30f, -1e30f)};
    for (u32 i = 0; i < indexCount; i++) {
        if (indices[i] < vertexCount) {
            mesh.bounds.min.x = std::min(mesh.bounds.min.x, vertices[indices[i]].x);
            mesh.bounds.max.x = std::max(mesh.bounds.max.x, vertices[indices[i]].x);
            mesh.bounds.min.y = std::min(mesh.bounds.min.y, vertices[indices[i]].y);
            mesh.bounds.max.y = std::max(mesh.bounds.max.y, vertices[indices[i]].y);
            mesh.bounds.min.z = std::min(mesh.bounds.min.z, vertices[indices[i]].z);
            mesh.bounds.max.z = std::max(mesh.bounds.max.z, vertices[indices[i]].z);
        }
    }
    occlusionMeshes_.push_back(mesh);
}

void OcclusionCullingSystem::removeOcclusionMesh(u32 meshId) {
    for (usize i = 0; i < occlusionMeshes_.size(); ++i) {
        if (occlusionMeshes_[i].meshId == meshId) {
            occlusionMeshes_.erase(i);
            return;
        }
    }
}

void OcclusionCullingSystem::cullObjects(Vector<u32>& outVisible, Vector<u32>& outCulled) {
    for (auto& obj : objects_) {
        bool visible = false;
        if (config_.useHiZ) {
            visible = testObjectHiZ(obj);
        }
        if (!visible && config_.useSoftwareRasterizer) {
            visible = testObjectSoftwareRaster(obj);
        }
        if (visible) {
            outVisible.push_back(obj.id);
            obj.lastVisibleFrame = currentFrame_;
            obj.wasVisible = true;
        } else {
            outCulled.push_back(obj.id);
            obj.lastVisibleFrame = 0;
            obj.wasVisible = false;
        }
    }
}

void OcclusionCullingSystem::cullObjects(Vector<Object*>& outVisible, Vector<Object*>& outCulled) {
    for (auto& obj : objects_) {
        bool visible = false;
        if (config_.useHiZ) {
            visible = testObjectHiZ(obj);
        }
        if (!visible && config_.useSoftwareRasterizer) {
            visible = testObjectSoftwareRaster(obj);
        }
        if (visible) {
            outVisible.push_back(&obj);
            obj.lastVisibleFrame = currentFrame_;
            obj.wasVisible = true;
        } else {
            outCulled.push_back(&obj);
            obj.lastVisibleFrame = 0;
            obj.wasVisible = false;
        }
    }
}

bool OcclusionCullingSystem::isObjectVisible(u32 objectId) const {
    for (const auto& obj : objects_) {
        if (obj.id == objectId) {
            return obj.wasVisible;
        }
    }
    return false;
}

bool OcclusionCullingSystem::isAABBVisible(const AABB& box) const {
    return frustum_.contains(box);
}

bool OcclusionCullingSystem::isSphereVisible(const Vec3& center, f32 radius) const {
    return frustum_.contains(center);
}

void OcclusionCullingSystem::buildHiZFromDepth(FrameBuffer* depthTexture) {
    if (!depthTexture) return;
    // Stub: In real implementation, would read depth from FrameBuffer's depth texture
    f32* depthData = new f32[hiZBuffer_.getWidth() * hiZBuffer_.getHeight()];
    std::fill(depthData, depthData + hiZBuffer_.getWidth() * hiZBuffer_.getHeight(), 1.0f);
    hiZBuffer_.buildFromDepthCPU(depthData, hiZBuffer_.getWidth(), hiZBuffer_.getHeight());
    delete[] depthData;
}

void OcclusionCullingSystem::buildSoftwareRasterizerDepth() {
    if (softwareRasterizer_) {
        softwareRasterizer_->clear();
    }
}

bool OcclusionCullingSystem::testObjectHiZ(const Object& obj) const {
    stats_.hiZTests++;
    return hiZBuffer_.testAABB(obj.bounds, viewProj_);
}

bool OcclusionCullingSystem::testObjectSoftwareRaster(const Object& obj) const {
    stats_.softwareRasterTests++;
    return softwareRasterizer_->testOcclusion(obj.bounds, viewProj_);
}

bool OcclusionCullingSystem::testObjectGPUQuery(const Object& obj) const {
    stats_.gpuQueryTests++;
    return false;
}

bool OcclusionCullingSystem::testObjectFrustum(const Object& obj) const {
    return frustum_.intersects(obj.bounds);
}

void OcclusionCullingSystem::updateObjectVisibility(Object& obj, bool visible) {
    if (visible) {
        obj.wasVisible = true;
        obj.lastVisibleFrame = currentFrame_;
    } else {
        obj.wasVisible = false;
        obj.lastVisibleFrame = 0;
    }
}

void OcclusionCullingSystem::processStaticObjects() {
    for (u32 i = 0; i < staticObjectIndices_.size(); i++) {
        u32 idx = staticObjectIndices_[i];
        if (idx < objects_.size()) {
            objects_[idx].wasVisible = true;
            objects_[idx].lastVisibleFrame = currentFrame_;
        }
    }
}

void OcclusionCullingSystem::processDynamicObjects() {
    for (u32 i = 0; i < dynamicObjectIndices_.size(); i++) {
        u32 idx = dynamicObjectIndices_[i];
        if (idx < objects_.size()) {
            objects_[idx].wasVisible = true;
            objects_[idx].lastVisibleFrame = currentFrame_;
        }
    }
}

void OcclusionCullingSystem::buildHiZ(const f32* depthBuffer, u32 width, u32 height) {
    hizBuffer_.width = width;
    hizBuffer_.height = height;
    hizBuffer_.mipLevels = 0;
    u32 dim = std::max(width, height);
    while ((1u << hizBuffer_.mipLevels) < dim) hizBuffer_.mipLevels++;
    hizBuffer_.mipLevels = std::max(hizBuffer_.mipLevels, 1u);

    u32 totalSize = 0;
    for (u32 mip = 0; mip < hizBuffer_.mipLevels; mip++) {
        u32 w = std::max(width >> mip, 1u);
        u32 h = std::max(height >> mip, 1u);
        totalSize += w * h;
    }
    hizBuffer_.depths.resize(totalSize);

    u32 baseW = width;
    u32 baseH = height;
    for (u32 i = 0; i < baseW * baseH; i++) {
        hizBuffer_.depths[i] = depthBuffer[i];
    }

    for (u32 mip = 1; mip < hizBuffer_.mipLevels; mip++) {
        downsampleHiZ(mip);
    }
}

void OcclusionCullingSystem::downsampleHiZ(u32 mipLevel) {
    if (mipLevel == 0 || mipLevel >= hizBuffer_.mipLevels) return;

    u32 prevMip = mipLevel - 1;
    u32 srcW = std::max(hizBuffer_.width >> prevMip, 1u);
    u32 srcH = std::max(hizBuffer_.height >> prevMip, 1u);
    u32 dstW = std::max(hizBuffer_.width >> mipLevel, 1u);
    u32 dstH = std::max(hizBuffer_.height >> mipLevel, 1u);

    u32 srcOffset = 0;
    for (u32 m = 0; m < prevMip; m++) {
        srcOffset += std::max(hizBuffer_.width >> m, 1u) * std::max(hizBuffer_.height >> m, 1u);
    }
    u32 dstOffset = srcOffset + srcW * srcH;

    for (u32 y = 0; y < dstH; y++) {
        for (u32 x = 0; x < dstW; x++) {
            f32 maxDepth = 0.0f;
            for (u32 dy = 0; dy < 2; dy++) {
                for (u32 dx = 0; dx < 2; dx++) {
                    u32 sx = x * 2 + dx;
                    u32 sy = y * 2 + dy;
                    if (sx < srcW && sy < srcH) {
                        f32 d = hizBuffer_.depths[srcOffset + sy * srcW + sx];
                        maxDepth = std::max(maxDepth, d);
                    }
                }
            }
            hizBuffer_.depths[dstOffset + y * dstW + x] = maxDepth;
        }
    }
}

bool OcclusionCullingSystem::testAABB(const Vec3& aabbMin, const Vec3& aabbMax, const Mat4& viewProj) {
    testedCount_++;

    if (hizBuffer_.depths.size() == 0) return false;

    Vec3 corners[8] = {
        {aabbMin.x, aabbMin.y, aabbMin.z},
        {aabbMax.x, aabbMin.y, aabbMin.z},
        {aabbMin.x, aabbMax.y, aabbMin.z},
        {aabbMax.x, aabbMax.y, aabbMin.z},
        {aabbMin.x, aabbMin.y, aabbMax.z},
        {aabbMax.x, aabbMin.y, aabbMax.z},
        {aabbMin.x, aabbMax.y, aabbMax.z},
        {aabbMax.x, aabbMax.y, aabbMax.z}
    };

    f32 minZ = 1e30f;
    f32 minU = 1e30f, maxU = 0.0f;
    f32 minV = 1e30f, maxV = 0.0f;
    bool behindCamera = false;

    for (u32 i = 0; i < 8; i++) {
        Vec4 clip = viewProj * Vec4(corners[i].x, corners[i].y, corners[i].z, 1.0f);
        if (clip.w <= 0.0f) {
            behindCamera = true;
            continue;
        }
        f32 ndcx = clip.x / clip.w;
        f32 ndcy = clip.y / clip.w;
        f32 ndcz = clip.z / clip.w;

        f32 u = (ndcx + 1.0f) * 0.5f;
        f32 v = (1.0f - ndcy) * 0.5f;

        minU = std::min(minU, u);
        maxU = std::max(maxU, u);
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
        minZ = std::min(minZ, ndcz);
    }

    if (behindCamera) return false;
    if (minU >= 1.0f || maxU <= 0.0f || minV >= 1.0f || maxV <= 0.0f) return false;

    minU = std::max(minU, 0.0f);
    maxU = std::min(maxU, 1.0f);
    minV = std::max(minV, 0.0f);
    maxV = std::min(maxV, 1.0f);

    u32 screenW = (u32)((maxU - minU) * hizBuffer_.width + 0.5f);
    u32 screenH = (u32)((maxV - minV) * hizBuffer_.height + 0.5f);
    if (screenW == 0) screenW = 1;
    if (screenH == 0) screenH = 1;

    u32 mipLevel = 0;
    u32 mipW = hizBuffer_.width;
    u32 mipH = hizBuffer_.height;
    while (mipLevel + 1 < hizBuffer_.mipLevels && mipW / 2 >= screenW && mipH / 2 >= screenH) {
        mipLevel++;
        mipW = std::max(mipW / 2, 1u);
        mipH = std::max(mipH / 2, 1u);
    }

    u32 mipOffset = 0;
    for (u32 m = 0; m < mipLevel; m++) {
        mipOffset += std::max(hizBuffer_.width >> m, 1u) * std::max(hizBuffer_.height >> m, 1u);
    }
    u32 sampleW = std::max(hizBuffer_.width >> mipLevel, 1u);
    u32 sampleH = std::max(hizBuffer_.height >> mipLevel, 1u);

    u32 sx0 = (u32)(minU * sampleW);
    u32 sy0 = (u32)(minV * sampleH);
    u32 sx1 = (u32)(maxU * sampleW) + 1;
    u32 sy1 = (u32)(maxV * sampleH) + 1;
    sx0 = std::min(sx0, sampleW - 1);
    sy0 = std::min(sy0, sampleH - 1);
    sx1 = std::min(sx1, sampleW);
    sy1 = std::min(sy1, sampleH);

    f32 maxSampleDepth = 0.0f;
    for (u32 sy = sy0; sy < sy1; sy++) {
        for (u32 sx = sx0; sx < sx1; sx++) {
            f32 d = hizBuffer_.depths[mipOffset + sy * sampleW + sx];
            maxSampleDepth = std::max(maxSampleDepth, d);
        }
    }

    if (maxSampleDepth < minZ) {
        occludedCount_++;
        return true;
    }
    return false;
}

bool OcclusionCullingSystem::testSphere(const Vec3& center, f32 radius, const Mat4& viewProj) {
    return testAABB(
        Vec3(center.x - radius, center.y - radius, center.z - radius),
        Vec3(center.x + radius, center.y + radius, center.z + radius),
        viewProj
    );
}

void OcclusionCullingSystem::temporalReproject() {
    if (!enableTemporal_) return;
    if (hizBuffer_.depths.size() == 0) return;

    if (prevHzBuffer_.depths.size() != hizBuffer_.depths.size()) {
        prevHzBuffer_ = hizBuffer_;
        return;
    }

    for (usize i = 0; i < hizBuffer_.depths.size(); i++) {
        hizBuffer_.depths[i] = std::max(hizBuffer_.depths[i], prevHzBuffer_.depths[i]);
    }
    prevHzBuffer_ = hizBuffer_;
}

void OcclusionCullingSystem::setNearFar(f32 near, f32 far) {
    zNear_ = near;
    zFar_ = far;
}

void OcclusionCullingSystem::resetStats() {
    stats_ = Stats{};
    occludedCount_ = 0;
    testedCount_ = 0;
    cullTimeMs_ = 0.0f;
}

}
}