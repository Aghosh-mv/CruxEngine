#pragma once
#include "Core/Types.h"
#include "Core/Math.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include <functional>
#include <mutex>
#include <queue>

namespace Frost {

enum class StreamingPriority : u8 { Critical = 0, High, Medium, Low, Unnecessary };
enum class StreamingState : u8 { Unloaded = 0, Queued, Loading, Loaded, Streaming, Unloading };
enum class StreamingStatus : u8 { Pending, InProgress, Completed, Failed, Cancelled };

struct AABB {
    Vec3 min{0,0,0};
    Vec3 max{0,0,0};
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extents() const { return (max - min) * 0.5f; }
    bool intersects(const AABB& o) const { return min.x<=o.max.x&&max.x>=o.min.x&&min.y<=o.max.y&&max.y>=o.min.y&&min.z<=o.max.z&&max.z>=o.min.z; }
    bool contains(const Vec3& p) const { return p.x>=min.x&&p.x<=max.x&&p.y>=min.y&&p.y<=max.y&&p.z>=min.z&&p.z<=max.z; }
    f32 surfaceArea() const { Vec3 e=extents(); return 8.0f*(e.x*e.y+e.x*e.z+e.y*e.z); }
};

struct StreamingHandle {
    u32 id;
    StreamingState state;
    f32 priority;
    f32 lastAccessTime;
    u32 dataSize;
    bool operator<(const StreamingHandle& o) const { return priority < o.priority; }
};

struct StreamingConfig {
    u32 maxConcurrentLoads = 4;
    f32 loadDistance = 100.0f;
    f32 unloadDistance = 150.0f;
    u32 memoryBudgetMB = 2048;
    f32 streamingSpeed = 100.0f;
    bool asyncLoading = true;
    u32 priorityQueueSize = 256;
    f32 hysteresisMargin = 0.1f;
    u32 terrainBlockSize = 64;
    u32 worldPartitionSize = 256;
    f32 prefetchDistance = 50.0f;
    f32 lodDistanceScale = 1.0f;
    u32 maxChunksPerFrame = 8;
};

struct WorldCell {
    AABB bounds;
    u32 cellId;
    u32 partitionId;
    StreamingState state;
    f32 priority;
    f32 distanceToCamera;
    u32 dataSize;
    u8 lodLevel;
    bool operator<(const WorldCell& o) const { return priority < o.priority; }
};

struct LODLevel {
    f32 minDistance;
    f32 maxDistance;
    f32 scale;
    u32 meshIndex;
    u32 materialIndex;
    f32 alpha;
};

struct StreamingChunk {
    u32 chunkId;
    AABB bounds;
    u32 dataSize;
    StreamingState state;
    f32 priority;
    u32 lodLevel;
    bool dirty;
    f32 loadProgress;
    u32 dependencies[4];
    u32 dependencyCount;
    bool operator<(const StreamingChunk& o) const { return priority < o.priority; }
};

struct TerrainBlock {
    u32 blockX, blockZ;
    AABB bounds;
    u8* heightData;
    u32 heightDataSize;
    StreamingState state;
    f32 priority;
    bool operator<(const TerrainBlock& o) const { return priority < o.priority; }
};

struct StreamingStats {
    u32 chunksLoaded;
    u32 chunksUnloaded;
    u32 chunksStreaming;
    u32 memoryUsedMB;
    u32 memoryBudgetMB;
    f32 loadTimeMs;
    f32 unloadTimeMs;
    u32 activeLoads;
    u32 queueSize;
    u32 failedLoads;
    u32 totalRequests;
    f32 averagePriority;
};

using StreamingCallback = std::function<void(u32 chunkId, StreamingStatus status)>;

class SceneStreaming {
public:
    SceneStreaming();
    ~SceneStreaming();

    bool init(const StreamingConfig& config);
    void shutdown();
    void update(f32 dt, const Vec3& cameraPosition, const Mat4& viewMatrix);

    u32 loadChunk(const AABB& bounds, u32 dataSize, StreamingPriority priority);
    bool unloadChunk(u32 chunkId);
    bool isChunkLoaded(u32 chunkId) const;
    StreamingState getChunkState(u32 chunkId) const;
    StreamingChunk* getChunk(u32 chunkId);
    const StreamingChunk* getChunk(u32 chunkId) const;

    u32 addTerrainBlock(u32 blockX, u32 blockZ, u32 size);
    bool removeTerrainBlock(u32 blockX, u32 blockZ);
    void updateTerrainPriority(f32 dt, const Vec3& cameraPos);

    u32 addWorldCell(const AABB& bounds, u32 partitionId);
    void removeWorldCell(u32 cellId);
    void updateWorldPartition(const Vec3& center, f32 radius);

    u32 addLOD(u32 chunkId, const LODLevel& lod);
    void setLODLevel(u32 chunkId, u8 level);
    f32 computeLODDistance(const Vec3& cameraPos, const AABB& bounds) const;

    u32 getCurrentMemoryUsage() const;
    void setMemoryBudget(u32 budgetMB);
    bool enforceMemoryBudget();

    void setLoadDistance(f32 distance);
    void setUnloadDistance(f32 distance);
    void setStreamingSpeed(f32 speed);
    void setMaxConcurrentLoads(u32 max);
    void setHysteresisMargin(f32 margin);

    void setStreamingCallback(StreamingCallback callback);
    void cancelLoad(u32 chunkId);
    bool isLoading() const;
    u32 pendingLoadCount() const;

    void prefetchChunks(const Vec3& position, f32 radius);
    void deprefetchChunks(const Vec3& position, f32 radius);

    StreamingStats getStats() const;
    void resetStats();
    void printStats() const;

    f32 computeChunkPriority(const StreamingChunk& chunk, const Vec3& cameraPos) const;
    f32 computeTerrainPriority(const TerrainBlock& block, const Vec3& cameraPos) const;
    void sortPendingQueue();
    void processLoadQueue();
    void processUnloadQueue();
    void updateChunkStates();
    void handleFailedLoads();
    void cleanupCompletedLoads();

    bool validateChunkData(u32 chunkId) const;
    void debugDraw() const;

private:
    StreamingConfig config_;
    Vector<StreamingChunk> chunks_;
    Vector<LODLevel> lodLevels_[256];
    Vector<TerrainBlock> terrainBlocks_;
    Vector<WorldCell> worldCells_;
    std::priority_queue<StreamingChunk*> loadQueue_;
    std::priority_queue<StreamingChunk*> unloadQueue_;
    StreamingCallback callback_;
    StreamingStats stats_;
    u32 nextChunkId_;
    u32 nextTerrainId_;
    u32 nextCellId_;
    mutable std::mutex mutex_;
    f32 lastUpdate_;
    f32 lastCameraDist_;
    Vec3 lastCameraPos_;
};

}
