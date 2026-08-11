#include "Scene/VirtualGeometry.h"
#include "Core/Math.h"

#include <cmath>

namespace Frost {

namespace {

constexpr u32 kLevelBits = 3;
constexpr u32 kCoordBits = 9;
constexpr u32 kCoordMask = 0x1FFu;
constexpr u32 kCoordSign = 0x100u;
constexpr u32 kCoordWrap = 0x200u;

struct PageCoords {
    i32 x = 0, y = 0, z = 0;
    u32 level = 0;
};

PageCoords decodePageId(u32 pageId) {
    PageCoords c;
    c.level = pageId & 0x7u;
    i32 x = (i32)((pageId >> kLevelBits) & kCoordMask);
    i32 y = (i32)((pageId >> (kLevelBits + kCoordBits)) & kCoordMask);
    i32 z = (i32)((pageId >> (kLevelBits + 2 * kCoordBits)) & kCoordMask);
    if (x & kCoordSign) x -= (i32)kCoordWrap;
    if (y & kCoordSign) y -= (i32)kCoordWrap;
    if (z & kCoordSign) z -= (i32)kCoordWrap;
    c.x = x;
    c.y = y;
    c.z = z;
    return c;
}

f32 pageCellSize(u32 level, f32 scale) {
    return scale * (f32)(1u << level);
}

}

void VirtualGeometrySystem::init(u32 clipmapLevels, f32 scale) {
    clipmapLevels_ = clipmapLevels > 0 ? clipmapLevels : 8u;
    clipmapScale_ = scale > 0.0f ? scale : 10.0f;
    pages_.clear();
    pendingRequests_.clear();
    residentPageCount_ = 0;
    requestedPages_ = 0;
    streamedPages_ = 0;
    lastUpdateTime_ = 0.0f;
}

u32 VirtualGeometrySystem::computeClipmapPage(const Vec3& worldPos, u32 level) const {
    u32 lvl = level;
    u32 maxLevel = clipmapLevels_ > 0 ? clipmapLevels_ - 1u : 0u;
    if (lvl > maxLevel) lvl = maxLevel;

    f32 cell = pageCellSize(lvl, clipmapScale_);
    i32 gx = (i32)std::floor(worldPos.x / cell);
    i32 gy = (i32)std::floor(worldPos.y / cell);
    i32 gz = (i32)std::floor(worldPos.z / cell);
    u32 cx = (u32)(gx & (i32)kCoordMask);
    u32 cy = (u32)(gy & (i32)kCoordMask);
    u32 cz = (u32)(gz & (i32)kCoordMask);
    return lvl | (cx << kLevelBits) |
                 (cy << (kLevelBits + kCoordBits)) |
                 (cz << (kLevelBits + 2 * kCoordBits));
}

void VirtualGeometrySystem::requestPage(u32 pageId, u32 priority) {
    for (usize i = 0; i < pendingRequests_.size(); ++i) {
        if (pendingRequests_[i].pageId == pageId) {
            if (priority > pendingRequests_[i].priority) {
                pendingRequests_[i].priority = priority;
            }
            return;
        }
    }

    GeometryRequest req;
    req.pageId = pageId;
    req.priority = priority;
    req.distanceToCamera = 0.0f;
    req.streaming = false;
    pendingRequests_.push_back(req);
    ++requestedPages_;
}

u32 VirtualGeometrySystem::processRequests(u32 maxPerFrame) {
    if (maxPerFrame == 0 || pendingRequests_.empty()) return 0;

    for (usize i = 1; i < pendingRequests_.size(); ++i) {
        GeometryRequest key = pendingRequests_[i];
        usize j = i;
        while (j > 0) {
            const GeometryRequest& prev = pendingRequests_[j - 1];
            bool higher = prev.priority < key.priority;
            bool closer = prev.priority == key.priority &&
                          prev.distanceToCamera > key.distanceToCamera;
            if (!higher && !closer) break;
            pendingRequests_[j] = prev;
            --j;
        }
        pendingRequests_[j] = key;
    }

    u32 processed = 0;
    usize i = 0;
    while (i < pendingRequests_.size() && processed < maxPerFrame) {
        GeometryRequest& req = pendingRequests_[i];
        bool ok = makeResident(req.pageId);
        if (ok) {
            if (req.streaming) ++streamedPages_;
            ++processed;
        }
        pendingRequests_.erase(i);
    }
    return processed;
}

bool VirtualGeometrySystem::makeResident(u32 pageId) {
    usize idx = findPage(pageId);
    if (idx != pages_.size()) {
        GeometryPage& page = pages_[idx];
        if (page.resident) return true;
        if (residentPageCount_ >= maxResidentPages_) return false;
        page.resident = true;
        page.loaded = true;
        ++residentPageCount_;
        return true;
    }

    if (residentPageCount_ >= maxResidentPages_) return false;

    PageCoords c = decodePageId(pageId);
    f32 cell = pageCellSize(c.level, clipmapScale_);
    GeometryPage page;
    page.pageId = pageId;
    page.min = Vec3((f32)c.x * cell, (f32)c.y * cell, (f32)c.z * cell);
    page.max = page.min + Vec3(cell);
    page.level = c.level;
    page.resident = true;
    page.loaded = true;
    page.meshCount = 0;
    pages_.push_back(page);
    ++residentPageCount_;
    return true;
}

bool VirtualGeometrySystem::evictPage(u32 pageId) {
    usize idx = findPage(pageId);
    if (idx == pages_.size()) return false;
    GeometryPage& page = pages_[idx];
    if (!page.resident) return false;
    page.resident = false;
    page.loaded = false;
    page.meshCount = 0;
    if (residentPageCount_ > 0) --residentPageCount_;
    return true;
}

const GeometryPage& VirtualGeometrySystem::getPage(u32 pageId) const {
    usize idx = findPage(pageId);
    if (idx != pages_.size()) return pages_[idx];
    static const GeometryPage kFallback = { 0u, Vec3(0), Vec3(0), 0u, false, false, 0u };
    return kFallback;
}

void VirtualGeometrySystem::update(const Vec3& cameraPos, f32 dt) {
    lastUpdateTime_ += dt;

    for (u32 level = 0; level < clipmapLevels_; ++level) {
        f32 cell = pageCellSize(level, clipmapScale_);
        i32 camCellX = (i32)std::floor(cameraPos.x / cell);
        i32 camCellY = (i32)std::floor(cameraPos.y / cell);
        i32 camCellZ = (i32)std::floor(cameraPos.z / cell);

        u32 remaining = clipmapLevels_ - level;
        i32 ring = 1 + (i32)((remaining + 1) / 2);
        i32 yRing = (level == 0) ? 1 : 0;

        for (i32 oy = -yRing; oy <= yRing; ++oy) {
            for (i32 ox = -ring; ox <= ring; ++ox) {
                for (i32 oz = -ring; oz <= ring; ++oz) {
                    if (ox * ox + oz * oz > ring * ring) continue;

                    Vec3 world((f32)(camCellX + ox) * cell,
                               (f32)(camCellY + oy) * cell,
                               (f32)(camCellZ + oz) * cell);
                    u32 pageId = computeClipmapPage(world, level);

                    usize pi = findPage(pageId);
                    if (pi != pages_.size() && pages_[pi].resident) continue;

                    requestPage(pageId, clipmapLevels_ - level);
                    Vec3 center = world + Vec3(cell * 0.5f);
                    f32 dist = (center - cameraPos).length();
                    for (usize i = 0; i < pendingRequests_.size(); ++i) {
                        if (pendingRequests_[i].pageId == pageId) {
                            pendingRequests_[i].distanceToCamera = dist;
                            pendingRequests_[i].streaming = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    while (residentPageCount_ > maxResidentPages_) {
        usize evictIdx = pages_.size();
        f32 farthest = -1.0f;
        for (usize i = 0; i < pages_.size(); ++i) {
            const GeometryPage& page = pages_[i];
            if (!page.resident) continue;
            Vec3 center = (page.min + page.max) * 0.5f;
            f32 dist = (center - cameraPos).lengthSquared();
            if (dist > farthest) {
                farthest = dist;
                evictIdx = i;
            }
        }
        if (evictIdx == pages_.size()) break;
        evictPage(pages_[evictIdx].pageId);
    }
}

void VirtualGeometrySystem::clearPages() {
    pages_.clear();
    pendingRequests_.clear();
    residentPageCount_ = 0;
    requestedPages_ = 0;
    streamedPages_ = 0;
}

usize VirtualGeometrySystem::findPage(u32 pageId) const {
    for (usize i = 0; i < pages_.size(); ++i) {
        if (pages_[i].pageId == pageId) return i;
    }
    return pages_.size();
}

}
