#include "Scene/WorldPartition.h"
#include <algorithm>
#include <cmath>

namespace Frost {

namespace {

void decodeCellKey(u64 key, i32& cx, i32& cy, i32& cz) {
    cx = (i32)((key & 0x1FFFFFull) << 11) >> 11;
    cy = (i32)(((key >> 21) & 0x1FFFFFull) << 11) >> 11;
    cz = (i32)(((key >> 42) & 0x1FFFFFull) << 11) >> 11;
}

} // namespace

bool WorldPartition::initialize(const GridConfig& config) {
    config_ = config;
    cells_.clear();
    hlodNodes_.clear();
    hlodProxyQueue_.clear();
    nextProxyMeshId_ = 1;
    proxiesBuilt_ = 0;
    stats_ = {};
    return true;
}

void WorldPartition::shutdown() {
    cells_.clear();
    hlodNodes_.clear();
    hlodProxyQueue_.clear();
    nextProxyMeshId_ = 1;
    proxiesBuilt_ = 0;
    stats_ = {};
}

u64 WorldPartition::cellKey(i32 cx, i32 cy, i32 cz) const {
    const u64 kx = (u64)(i64)cx & 0x1FFFFFull;
    const u64 ky = (u64)(i64)cy & 0x1FFFFFull;
    const u64 kz = (u64)(i64)cz & 0x1FFFFFull;
    return kx | (ky << 21) | (kz << 42);
}

Vec3 WorldPartition::cellCoordFromKey(u64 key) const {
    i32 cx, cy, cz;
    decodeCellKey(key, cx, cy, cz);
    const f32 half = config_.cellSize * 0.5f;
    return Vec3(cx * config_.cellSize + half, cy * config_.cellSize + half, cz * config_.cellSize + half);
}

i32 WorldPartition::findCellIndex(i32 cx, i32 cy, i32 cz) const {
    for (usize i = 0; i < cells_.size(); ++i) {
        const WorldCell& cell = cells_[i];
        if (cell.cx == cx && cell.cy == cy && cell.cz == cz) return (i32)i;
    }
    return -1;
}

float WorldPartition::priorityForCell(const Vec3& cameraPos, i32 cx, i32 cy, i32 cz) const {
    const Vec3 center = cellCoordFromKey(cellKey(cx, cy, cz));
    const f32 dist = (center - cameraPos).length();
    return 1.0f / (1.0f + dist);
}

void WorldPartition::update(const Vec3& cameraPos, u64 frameIndex) {
    if (config_.cellSize <= 0.0f) return;

    const i32 ccx = (i32)std::floor(cameraPos.x / config_.cellSize);
    const i32 ccy = (i32)std::floor(cameraPos.y / config_.cellSize);
    const i32 ccz = (i32)std::floor(cameraPos.z / config_.cellSize);
    const i32 vd = (i32)config_.viewDistance;

    for (i32 dx = -vd; dx <= vd; ++dx)
    for (i32 dy = -vd; dy <= vd; ++dy)
    for (i32 dz = -vd; dz <= vd; ++dz) {
        const i32 cx = ccx + dx, cy = ccy + dy, cz = ccz + dz;
        const f32 priority = priorityForCell(cameraPos, cx, cy, cz);
        const i32 idx = findCellIndex(cx, cy, cz);
        if (idx < 0) {
            WorldCell cell;
            cell.cx = cx;
            cell.cy = cy;
            cell.cz = cz;
            cell.state = kStateLoading;
            cell.lastLoadedFrame = frameIndex;
            cell.priority = priority;
            cells_.push_back(cell);
        } else {
            WorldCell& cell = cells_[(usize)idx];
            cell.priority = priority;
            if (cell.state == kStateUnloaded) {
                cell.state = kStateLoading;
                cell.lastLoadedFrame = frameIndex;
            } else if (cell.state == kStateUnloadPending) {
                cell.state = kStateLoaded;
                cell.lastLoadedFrame = frameIndex;
            }
        }
    }

    for (usize i = 0; i < cells_.size(); ) {
        const WorldCell& cell = cells_[i];
        const i32 maxOff = std::max(std::abs(cell.cx - ccx), std::max(std::abs(cell.cy - ccy), std::abs(cell.cz - ccz)));
        if (maxOff > vd) {
            if (cell.state == kStateLoaded) {
                cells_[i].state = kStateUnloadPending;
                ++i;
            } else if (cell.state == kStateUnloadPending) {
                ++i;
            } else {
                cells_.erase(i);
            }
        } else {
            ++i;
        }
    }

    u32 loadedCount = 0;
    for (usize i = 0; i < cells_.size(); ++i) {
        if (cells_[i].state == kStateLoaded) loadedCount++;
    }

    if (loadedCount > config_.maxStreamedCells) {
        Vector<usize> indices;
        for (usize i = 0; i < cells_.size(); ++i) {
            if (cells_[i].state == kStateLoaded) indices.push_back(i);
        }
        std::sort(indices.begin(), indices.end(), [this](usize a, usize b) {
            return cells_[a].priority < cells_[b].priority;
        });
        const u32 toUnload = loadedCount - config_.maxStreamedCells;
        for (u32 k = 0; k < toUnload && k < indices.size(); ++k) {
            cells_[indices[k]].state = kStateUnloadPending;
        }
    }

    refreshStats();
}

bool WorldPartition::isCellLoaded(u64 key) const {
    i32 cx, cy, cz;
    decodeCellKey(key, cx, cy, cz);
    const i32 idx = findCellIndex(cx, cy, cz);
    if (idx < 0) return false;
    return cells_[(usize)idx].state == kStateLoaded;
}

f32 WorldPartition::hlodThresholdForLevel(u32 level) const {
    return hlodBaseScreenThreshold_ * std::pow(0.5f, (f32)level);
}

bool WorldPartition::selectHLOD(const Mat4& viewProj, f32 viewportHeight, const Vec3& cellCenter, f32 cellRadius, u32 level) const {
    const Vec4 center = viewProj * Vec4(cellCenter, 1.0f);
    const Vec4 edgeX = viewProj * Vec4(cellCenter + Vec3(cellRadius, 0.0f, 0.0f), 1.0f);
    const Vec4 edgeY = viewProj * Vec4(cellCenter + Vec3(0.0f, cellRadius, 0.0f), 1.0f);
    if (center.w <= 0.0f || edgeX.w <= 0.0f || edgeY.w <= 0.0f) return true;
    const f32 cx = center.x / center.w;
    const f32 cy = center.y / center.w;
    const f32 dx = Mathf::abs(edgeX.x / edgeX.w - cx);
    const f32 dy = Mathf::abs(edgeY.y / edgeY.w - cy);
    const f32 screenRadiusPx = Mathf::max(dx, dy) * viewportHeight;
    return screenRadiusPx <= hlodThresholdForLevel(level);
}

void WorldPartition::queueProxyForCell(u64 cellKey) {
    for (usize i = 0; i < hlodProxyQueue_.size(); ++i) {
        if (hlodProxyQueue_[i] == cellKey) return;
    }
    hlodProxyQueue_.push_back(cellKey);
}

u32 WorldPartition::nextLevelForCell(u64 key) const {
    const u32 hi = (u32)(key >> 32);
    const u32 lo = (u32)(key & 0xFFFFFFFFu);
    u32 level = 0;
    for (const auto& node : hlodNodes_) {
        if (node.cellKeyHi == hi && node.cellKeyLo == lo) {
            level = std::max(level, node.level + 1);
        }
    }
    if (config_.hlodLevels > 0) {
        level = std::min(level, config_.hlodLevels - 1);
    }
    return level;
}

u32 WorldPartition::processProxyQueue(u32 maxProxiesPerFrame, u64 frameIndex) {
    (void)frameIndex;
    u32 processed = 0;
    while (processed < maxProxiesPerFrame && !hlodProxyQueue_.empty()) {
        const u64 key = hlodProxyQueue_[0];
        hlodProxyQueue_.erase(0);

        HLODNode node;
        node.cellKeyHi = (u32)(key >> 32);
        node.cellKeyLo = (u32)(key & 0xFFFFFFFFu);
        node.level = nextLevelForCell(key);
        node.screenSizeThreshold = hlodThresholdForLevel(node.level);
        node.proxyMeshId = nextProxyMeshId_++;
        node.active = true;
        hlodNodes_.push_back(node);
        proxiesBuilt_++;
        processed++;

        i32 cx, cy, cz;
        decodeCellKey(key, cx, cy, cz);
        const i32 idx = findCellIndex(cx, cy, cz);
        if (idx >= 0) cells_[(usize)idx].hlodLevel = node.level;
    }
    refreshStats();
    return processed;
}

Vector<u64> WorldPartition::getCellsToLoad(u32 maxToLoad) {
    Vector<u64> result;
    if (maxToLoad == 0) return result;

    Vector<usize> indices;
    for (usize i = 0; i < cells_.size(); ++i) {
        const WorldCell& cell = cells_[i];
        if (cell.state == kStateUnloaded || cell.state == kStateLoading) {
            indices.push_back(i);
        }
    }
    std::sort(indices.begin(), indices.end(), [this](usize a, usize b) {
        return cells_[a].priority > cells_[b].priority;
    });
    const u32 count = std::min(maxToLoad, (u32)indices.size());
    for (u32 k = 0; k < count; ++k) {
        const WorldCell& cell = cells_[indices[k]];
        result.push_back(cellKey(cell.cx, cell.cy, cell.cz));
    }
    return result;
}

Vector<u64> WorldPartition::getCellsToUnload(u32 maxToUnload) {
    Vector<u64> result;
    if (maxToUnload == 0) return result;

    Vector<usize> indices;
    for (usize i = 0; i < cells_.size(); ++i) {
        if (cells_[i].state == kStateUnloadPending) {
            indices.push_back(i);
        }
    }
    std::sort(indices.begin(), indices.end(), [this](usize a, usize b) {
        return cells_[a].priority < cells_[b].priority;
    });
    const u32 count = std::min(maxToUnload, (u32)indices.size());
    for (u32 k = 0; k < count; ++k) {
        const WorldCell& cell = cells_[indices[k]];
        result.push_back(cellKey(cell.cx, cell.cy, cell.cz));
    }
    return result;
}

void WorldPartition::attachActor(i32 cx, i32 cy, i32 cz, u64 actorId) {
    i32 idx = findCellIndex(cx, cy, cz);
    if (idx < 0) {
        WorldCell cell;
        cell.cx = cx;
        cell.cy = cy;
        cell.cz = cz;
        cell.state = kStateUnloaded;
        cell.priority = 0.0f;
        cells_.push_back(cell);
        idx = (i32)(cells_.size() - 1);
    }
    WorldCell& cell = cells_[(usize)idx];
    for (usize i = 0; i < cell.actorIds.size(); ++i) {
        if (cell.actorIds[i] == actorId) return;
    }
    cell.actorIds.push_back(actorId);
}

void WorldPartition::detachActor(u64 actorId) {
    for (usize i = 0; i < cells_.size(); ++i) {
        WorldCell& cell = cells_[i];
        for (usize j = 0; j < cell.actorIds.size(); ) {
            if (cell.actorIds[j] == actorId) {
                cell.actorIds.erase(j);
            } else {
                ++j;
            }
        }
    }
}

i64 WorldPartition::findCellForActor(u64 actorId) const {
    for (usize i = 0; i < cells_.size(); ++i) {
        const WorldCell& cell = cells_[i];
        for (usize j = 0; j < cell.actorIds.size(); ++j) {
            if (cell.actorIds[j] == actorId) {
                return (i64)cellKey(cell.cx, cell.cy, cell.cz);
            }
        }
    }
    return -1;
}

void WorldPartition::refreshStats() {
    stats_.cellsLoaded = 0;
    stats_.cellsUnloaded = 0;
    stats_.cellsStreaming = 0;
    for (const auto& cell : cells_) {
        if (cell.state == kStateLoading) stats_.cellsStreaming++;
        else if (cell.state == kStateLoaded) stats_.cellsLoaded++;
        else if (cell.state == kStateUnloadPending) stats_.cellsUnloaded++;
    }
    stats_.hlodNodesActive = 0;
    for (const auto& node : hlodNodes_) {
        if (node.active) stats_.hlodNodesActive++;
    }
    stats_.proxiesBuilt = proxiesBuilt_;
}

void WorldPartition::reset() {
    cells_.clear();
    hlodNodes_.clear();
    hlodProxyQueue_.clear();
    nextProxyMeshId_ = 1;
    proxiesBuilt_ = 0;
    stats_ = {};
}

} // namespace Frost
