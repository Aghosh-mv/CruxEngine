#pragma once

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Math.h"
#include "Core/Vector.h"

namespace Frost {

struct GridConfig {
    f32 cellSize = 100.0f;
    u32 viewDistance = 4;
    u32 maxStreamedCells = 64;
    u32 hlodLevels = 3;
};

struct WorldCell {
    i32 cx = 0;
    i32 cy = 0;
    i32 cz = 0;
    u32 state = 0;
    u64 lastLoadedFrame = 0;
    f32 priority = 0.0f;
    u32 hlodLevel = 0;
    Vector<u64> actorIds;
};

struct HLODNode {
    u32 cellKeyHi = 0;
    u32 cellKeyLo = 0;
    u32 level = 0;
    f32 screenSizeThreshold = 0.0f;
    u64 proxyMeshId = 0;
    bool active = false;
};

class WorldPartition {
public:
    static constexpr u32 kStateUnloaded = 0;
    static constexpr u32 kStateLoading = 1;
    static constexpr u32 kStateLoaded = 2;
    static constexpr u32 kStateUnloadPending = 3;

    struct Stats {
        u32 cellsLoaded = 0;
        u32 cellsUnloaded = 0;
        u32 cellsStreaming = 0;
        u32 hlodNodesActive = 0;
        u32 proxiesBuilt = 0;
    };

    WorldPartition() = default;
    ~WorldPartition() = default;

    bool initialize(const GridConfig& config);
    void shutdown();

    void setConfig(const GridConfig& config) { config_ = config; }
    const GridConfig& getConfig() const { return config_; }

    u64 cellKey(i32 cx, i32 cy, i32 cz) const;
    Vec3 cellCoordFromKey(u64 key) const;

    void update(const Vec3& cameraPos, u64 frameIndex);

    Vector<WorldCell>& getStreamedCells() { return cells_; }
    u32 getCellCount() const { return (u32)cells_.size(); }
    bool isCellLoaded(u64 key) const;

    bool selectHLOD(const Mat4& viewProj, f32 viewportHeight, const Vec3& cellCenter, f32 cellRadius, u32 level) const;
    void queueProxyForCell(u64 cellKey);
    u32 processProxyQueue(u32 maxProxiesPerFrame, u64 frameIndex);
    const Vector<HLODNode>& getHLODNodes() const { return hlodNodes_; }

    float priorityForCell(const Vec3& cameraPos, i32 cx, i32 cy, i32 cz) const;
    Vector<u64> getCellsToLoad(u32 maxToLoad);
    Vector<u64> getCellsToUnload(u32 maxToUnload);

    void attachActor(i32 cx, i32 cy, i32 cz, u64 actorId);
    void detachActor(u64 actorId);
    i64 findCellForActor(u64 actorId) const;

    const Stats& getStats() const { return stats_; }
    void reset();

private:
    i32 findCellIndex(i32 cx, i32 cy, i32 cz) const;
    f32 hlodThresholdForLevel(u32 level) const;
    u32 nextLevelForCell(u64 key) const;
    void refreshStats();

    GridConfig config_;
    Vector<WorldCell> cells_;
    Vector<HLODNode> hlodNodes_;
    Vector<u64> hlodProxyQueue_;
    Stats stats_;
    u64 nextProxyMeshId_ = 1;
    u32 proxiesBuilt_ = 0;
    f32 hlodBaseScreenThreshold_ = 32.0f;
};

} // namespace Frost
