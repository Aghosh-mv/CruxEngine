#include "Scene/SceneStreaming.h"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frost {

SceneStreaming::SceneStreaming()
    : nextChunkId_(1), nextTerrainId_(1), nextCellId_(1), lastUpdate_(0), lastCameraDist_(0) {
    memset(&stats_, 0, sizeof(stats_));
}

SceneStreaming::~SceneStreaming() { shutdown(); }

bool SceneStreaming::init(const StreamingConfig& config) {
    config_ = config;
    chunks_.clear();
    terrainBlocks_.clear();
    worldCells_.clear();
    for (auto& lod : lodLevels_) lod.clear();
    stats_ = {};
    FROST_LOG_INFO("[SceneStreaming] Initialized (budget=%uMB, loadDist=%.1f, async=%s)",
        config_.memoryBudgetMB, config_.loadDistance, config_.asyncLoading ? "true" : "false");
    return true;
}

void SceneStreaming::shutdown() {
    chunks_.clear();
    terrainBlocks_.clear();
    worldCells_.clear();
    for (auto& lod : lodLevels_) lod.clear();
    while (!legacyLoadQueue_.empty()) legacyLoadQueue_.pop();
    while (!legacyUnloadQueue_.empty()) legacyUnloadQueue_.pop();
    FROST_LOG_INFO("[SceneStreaming] Shutdown");
}

void SceneStreaming::update(f32 dt, const Vec3& cameraPosition, const Mat4& viewMatrix) {
    lastUpdate_ = dt;
    lastCameraPos_ = cameraPosition;

    f32 moved = (cameraPosition - lastCameraPos_).length();
    bool cameraMoved = moved > 0.01f || lastUpdate_ > 0.5f;

    if (cameraMoved) {
        for (auto& chunk : chunks_) {
            chunk.priority = computeChunkPriority(chunk, cameraPosition);
        }

        for (auto& block : terrainBlocks_) {
            block.priority = computeTerrainPriority(block, cameraPosition);
        }
    }

    processLoadQueue();
    processUnloadQueue();
    updateChunkStates();
    handleFailedLoads();
    cleanupCompletedLoads();
    enforceMemoryBudget();

    stats_.memoryUsedMB = getCurrentMemoryUsage();
    stats_.memoryBudgetMB = config_.memoryBudgetMB;
    stats_.averagePriority = 0;
    f32 totalPriority = 0;
    u32 count = 0;
    for (const auto& chunk : chunks_) {
        if (chunk.state == StreamingState::Loaded || chunk.state == StreamingState::Streaming) {
            totalPriority += chunk.priority;
            count++;
        }
    }
    if (count > 0) stats_.averagePriority = totalPriority / count;
}

u32 SceneStreaming::loadChunk(const AABB& bounds, u32 dataSize, StreamingPriority priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    StreamingChunk chunk;
    chunk.chunkId = nextChunkId_++;
    chunk.bounds = bounds;
    chunk.dataSize = dataSize;
    chunk.state = StreamingState::Queued;
    chunk.priority = static_cast<f32>(priority);
    chunk.lodLevel = 0;
    chunk.dirty = false;
    chunk.loadProgress = 0.0f;
    chunk.dependencyCount = 0;
    chunks_.push_back(chunk);
    legacyLoadQueue_.push(&chunks_.back());
    stats_.totalRequests++;
    stats_.queueSize++;
    return chunk.chunkId;
}

bool SceneStreaming::unloadChunk(u32 chunkId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& chunk : chunks_) {
        if (chunk.chunkId == chunkId) {
            if (chunk.state == StreamingState::Loaded || chunk.state == StreamingState::Streaming) {
                chunk.state = StreamingState::Unloading;
                chunk.priority = -1.0f;
                stats_.chunksUnloaded++;
                return true;
            }
            return false;
        }
    }
    return false;
}

bool SceneStreaming::isChunkLoaded(u32 chunkId) const {
    for (const auto& chunk : chunks_) {
        if (chunk.chunkId == chunkId) return chunk.state == StreamingState::Loaded;
    }
    return false;
}

StreamingState SceneStreaming::getChunkState(u32 chunkId) const {
    for (const auto& chunk : chunks_) {
        if (chunk.chunkId == chunkId) return chunk.state;
    }
    return StreamingState::Unloaded;
}

StreamingChunk* SceneStreaming::getChunk(u32 chunkId) {
    for (auto& chunk : chunks_) {
        if (chunk.chunkId == chunkId) return &chunk;
    }
    return nullptr;
}

const StreamingChunk* SceneStreaming::getChunk(u32 chunkId) const {
    for (const auto& chunk : chunks_) {
        if (chunk.chunkId == chunkId) return &chunk;
    }
    return nullptr;
}

u32 SceneStreaming::addTerrainBlock(u32 blockX, u32 blockZ, u32 size) {
    TerrainBlock block;
    block.blockX = blockX;
    block.blockZ = blockZ;
    block.bounds.min = Vec3(blockX * config_.terrainBlockSize, 0, blockZ * config_.terrainBlockSize);
    block.bounds.max = Vec3((blockX + 1) * config_.terrainBlockSize, 256, (blockZ + 1) * config_.terrainBlockSize);
    block.heightData = nullptr;
    block.heightDataSize = size;
    block.state = StreamingState::Queued;
    block.priority = 0;
    terrainBlocks_.push_back(block);
    return terrainBlocks_.size() - 1;
}

bool SceneStreaming::removeTerrainBlock(u32 blockX, u32 blockZ) {
    for (usize i = 0; i < terrainBlocks_.size(); i++) {
        if (terrainBlocks_[i].blockX == blockX && terrainBlocks_[i].blockZ == blockZ) {
            if (terrainBlocks_[i].heightData) delete[] terrainBlocks_[i].heightData;
            terrainBlocks_.erase(i);
            return true;
        }
    }
    return false;
}

void SceneStreaming::updateTerrainPriority(f32 dt, const Vec3& cameraPos) {
    for (auto& block : terrainBlocks_) {
        block.priority = computeTerrainPriority(block, cameraPos);
    }
}

u32 SceneStreaming::addWorldCell(const AABB& bounds, u32 partitionId) {
    WorldCell cell;
    cell.bounds = bounds;
    cell.cellId = nextCellId_++;
    cell.partitionId = partitionId;
    cell.state = StreamingState::Queued;
    cell.priority = 0;
    cell.distanceToCamera = 0;
    cell.dataSize = 0;
    cell.lodLevel = 0;
    worldCells_.push_back(cell);
    return cell.cellId;
}

void SceneStreaming::removeWorldCell(u32 cellId) {
    for (usize i = 0; i < worldCells_.size(); i++) {
        if (worldCells_[i].cellId == cellId) {
            worldCells_.erase(i);
            return;
        }
    }
}

void SceneStreaming::updateWorldPartition(const Vec3& center, f32 radius) {
    for (auto& cell : worldCells_) {
        f32 dist = (cell.bounds.center() - center).length();
        cell.distanceToCamera = dist;
        if (dist < radius) {
            cell.priority = 1.0f - (dist / radius);
            if (cell.state == StreamingState::Unloaded) cell.state = StreamingState::Queued;
        } else {
            cell.priority = 0;
            if (cell.state == StreamingState::Loaded) cell.state = StreamingState::Unloading;
        }
    }
}

u32 SceneStreaming::addLOD(u32 chunkId, const LODLevel& lod) {
    if (chunkId < 256) {
        lodLevels_[chunkId].push_back(lod);
        return lodLevels_[chunkId].size() - 1;
    }
    return 0;
}

void SceneStreaming::setLODLevel(u32 chunkId, u8 level) {
    for (auto& chunk : chunks_) {
        if (chunk.chunkId == chunkId) {
            chunk.lodLevel = level;
            return;
        }
    }
}

f32 SceneStreaming::computeLODDistance(const Vec3& cameraPos, const AABB& bounds) const {
    Vec3 closest;
    closest.x = Mathf::clamp(cameraPos.x, bounds.min.x, bounds.max.x);
    closest.y = Mathf::clamp(cameraPos.y, bounds.min.y, bounds.max.y);
    closest.z = Mathf::clamp(cameraPos.z, bounds.min.z, bounds.max.z);
    return (closest - cameraPos).length();
}

u32 SceneStreaming::getCurrentMemoryUsage() const {
    u32 total = 0;
    for (const auto& chunk : chunks_) {
        if (chunk.state == StreamingState::Loaded || chunk.state == StreamingState::Streaming) total += chunk.dataSize;
    }
    for (const auto& block : terrainBlocks_) {
        if (block.state == StreamingState::Loaded) total += block.heightDataSize;
    }
    return total / (1024 * 1024);
}

void SceneStreaming::setMemoryBudget(u32 budgetMB) { config_.memoryBudgetMB = budgetMB; }

bool SceneStreaming::enforceMemoryBudget() {
    u32 used = getCurrentMemoryUsage();
    if (used <= config_.memoryBudgetMB) return false;
    u32 toFree = used - config_.memoryBudgetMB;
    u32 freed = 0;
    Vector<StreamingChunk*> candidates;
    for (auto& chunk : chunks_) {
        if (chunk.state == StreamingState::Loaded) candidates.push_back(&chunk);
    }
    std::sort(candidates.begin(), candidates.end(), [](StreamingChunk* a, StreamingChunk* b) { return a->priority < b->priority; });
    for (auto* chunk : candidates) {
        if (freed >= toFree) break;
        chunk->state = StreamingState::Unloading;
        freed += chunk->dataSize;
    }
    return freed >= toFree;
}

void SceneStreaming::setLoadDistance(f32 distance) { config_.loadDistance = distance; }
void SceneStreaming::setUnloadDistance(f32 distance) { config_.unloadDistance = distance; }
void SceneStreaming::setStreamingSpeed(f32 speed) { config_.streamingSpeed = speed; }
void SceneStreaming::setMaxConcurrentLoads(u32 max) { config_.maxConcurrentLoads = max; }
void SceneStreaming::setHysteresisMargin(f32 margin) { config_.hysteresisMargin = margin; }
void SceneStreaming::setStreamingCallback(StreamingCallback callback) { callback_ = callback; }

void SceneStreaming::cancelLoad(u32 chunkId) {
    for (auto& chunk : chunks_) {
        if (chunk.chunkId == chunkId && chunk.state == StreamingState::Queued) {
            chunk.state = StreamingState::Unloaded;
            return;
        }
    }
}

bool SceneStreaming::isLoading() const {
    for (const auto& chunk : chunks_) {
        if (chunk.state == StreamingState::Loading || chunk.state == StreamingState::Streaming) return true;
    }
    return false;
}

u32 SceneStreaming::pendingLoadCount() const {
    u32 count = 0;
    for (const auto& chunk : chunks_) {
        if (chunk.state == StreamingState::Queued) count++;
    }
    return count;
}

void SceneStreaming::prefetchChunks(const Vec3& position, f32 radius) {
    for (auto& chunk : chunks_) {
        f32 dist = (chunk.bounds.center() - position).length();
        if (dist < radius && chunk.state == StreamingState::Unloaded) {
            chunk.priority = 1.0f + config_.prefetchDistance / (dist + 1.0f);
            chunk.state = StreamingState::Queued;
        }
    }
}

void SceneStreaming::deprefetchChunks(const Vec3& position, f32 radius) {
    for (auto& chunk : chunks_) {
        f32 dist = (chunk.bounds.center() - position).length();
        if (dist > radius && chunk.state == StreamingState::Queued) {
            chunk.state = StreamingState::Unloaded;
        }
    }
}

StreamingStats SceneStreaming::getStats() const {
    StreamingStats result = stats_;
    result.activeLoads = activeLoads_;
    result.queueSize = (u32)pendingRequests_.size();
    return result;
}
void SceneStreaming::resetStats() { stats_ = {}; }

void SceneStreaming::printStats() const {
    FROST_LOG_INFO("[SceneStreaming] Stats: loaded=%u, streaming=%u, mem=%u/%uMB, avgPri=%.2f",
        stats_.chunksLoaded, stats_.chunksStreaming, stats_.memoryUsedMB, stats_.memoryBudgetMB, stats_.averagePriority);
}

f32 SceneStreaming::computeChunkPriority(const StreamingChunk& chunk, const Vec3& cameraPos) const {
    f32 dist = (chunk.bounds.center() - cameraPos).length();
    if (dist > config_.unloadDistance) return -1.0f;
    if (dist < config_.loadDistance) {
        f32 t = dist / config_.loadDistance;
        return 1.0f - t * t;
    }
    f32 t = (dist - config_.loadDistance) / (config_.unloadDistance - config_.loadDistance);
    return Mathf::max(0.0f, 0.5f - t * 0.5f);
}

f32 SceneStreaming::computeTerrainPriority(const TerrainBlock& block, const Vec3& cameraPos) const {
    f32 dist = (block.bounds.center() - cameraPos).length();
    if (dist > config_.unloadDistance) return -1.0f;
    if (dist < config_.loadDistance) return 1.0f;
    return Mathf::max(0.0f, 1.0f - (dist - config_.loadDistance) / (config_.unloadDistance - config_.loadDistance));
}

void SceneStreaming::sortPendingQueue() {
    while (!legacyLoadQueue_.empty()) legacyLoadQueue_.pop();
    for (auto& chunk : chunks_) {
        if (chunk.state == StreamingState::Queued) legacyLoadQueue_.push(&chunk);
    }
}

void SceneStreaming::processLoadQueue() {
    u32 activeLoads = 0;
    for (const auto& chunk : chunks_) {
        if (chunk.state == StreamingState::Loading || chunk.state == StreamingState::Streaming) activeLoads++;
    }
    while (!legacyLoadQueue_.empty() && activeLoads < config_.maxConcurrentLoads) {
        StreamingChunk* chunk = legacyLoadQueue_.top();
        legacyLoadQueue_.pop();
        if (chunk->state == StreamingState::Queued) {
            chunk->state = StreamingState::Loading;
            chunk->loadProgress = 0.0f;
            activeLoads++;
            if (callback_) callback_(chunk->chunkId, StreamingStatus::InProgress);
        }
    }
}

void SceneStreaming::processUnloadQueue() {
    while (!legacyUnloadQueue_.empty()) {
        StreamingChunk* chunk = legacyUnloadQueue_.top();
        legacyUnloadQueue_.pop();
        if (chunk->state == StreamingState::Unloading) {
            chunk->state = StreamingState::Unloaded;
            chunk->loadProgress = 0.0f;
            if (callback_) callback_(chunk->chunkId, StreamingStatus::Completed);
        }
    }
}

void SceneStreaming::updateChunkStates() {
    for (auto& chunk : chunks_) {
        switch (chunk.state) {
            case StreamingState::Loading:
                chunk.loadProgress += config_.streamingSpeed * lastUpdate_ / chunk.dataSize;
                if (chunk.loadProgress >= 1.0f) {
                    chunk.state = StreamingState::Loaded;
                    chunk.loadProgress = 1.0f;
                    stats_.chunksLoaded++;
                    if (callback_) callback_(chunk.chunkId, StreamingStatus::Completed);
                }
                break;
            case StreamingState::Streaming:
                break;
            case StreamingState::Unloading:
                chunk.loadProgress -= config_.streamingSpeed * lastUpdate_ / chunk.dataSize;
                if (chunk.loadProgress <= 0.0f) {
                    chunk.state = StreamingState::Unloaded;
                    chunk.loadProgress = 0.0f;
                    stats_.chunksUnloaded++;
                }
                break;
            default:
                break;
        }
    }
}

void SceneStreaming::handleFailedLoads() {
    for (auto& chunk : chunks_) {
        if (chunk.state == StreamingState::Loading && chunk.loadProgress < 0) {
            chunk.state = StreamingState::Unloaded;
            stats_.failedLoads++;
            if (callback_) callback_(chunk.chunkId, StreamingStatus::Failed);
        }
    }
}

void SceneStreaming::cleanupCompletedLoads() {
    for (usize i = 0; i < chunks_.size(); ) {
        if (chunks_[i].state == StreamingState::Unloaded) {
            chunks_.erase(i);
        } else {
            ++i;
        }
    }
}

bool SceneStreaming::validateChunkData(u32 chunkId) const {
    const StreamingChunk* chunk = getChunk(chunkId);
    if (!chunk) return false;
    return chunk->state == StreamingState::Loaded && chunk->dataSize > 0;
}

void SceneStreaming::debugDraw() const {
    FROST_LOG_DEBUG("[SceneStreaming] Debug: %zu chunks, %zu terrain, %zu cells",
        chunks_.size(), terrainBlocks_.size(), worldCells_.size());
}

void SceneStreaming::requestStream(u32 assetId, f32 importance) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pendingRequests_.contains(assetId)) return;

    StreamRequest req;
    req.assetId = assetId;
    req.priority.distanceScore = 0.0f;
    req.priority.importanceScore = importance;
    req.priority.lodLevel = 0;
    req.requestedTime = lastUpdate_;
    req.inProgress = false;
    req.completed = false;
    pendingRequests_[assetId] = req;

    for (usize i = 0; i < loadQueue_.size(); i++) {
        if (loadQueue_[i] == assetId) return;
    }
    loadQueue_.push_back(assetId);

    for (usize i = 0; i + 1 < loadQueue_.size(); i++) {
        for (usize j = i + 1; j < loadQueue_.size(); j++) {
            auto itA = pendingRequests_.find(loadQueue_[i]);
            auto itB = pendingRequests_.find(loadQueue_[j]);
            if (itA != pendingRequests_.end() && itB != pendingRequests_.end()) {
                if (itB.value().priority < itA.value().priority) {
                    u32 tmp = loadQueue_[i];
                    loadQueue_[i] = loadQueue_[j];
                    loadQueue_[j] = tmp;
                }
            }
        }
    }
}

void SceneStreaming::cancelRequest(u32 assetId) {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingRequests_.erase(assetId);
    for (usize i = 0; i < loadQueue_.size(); i++) {
        if (loadQueue_[i] == assetId) {
            loadQueue_.erase(i);
            break;
        }
    }
}

void SceneStreaming::processQueue() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (activeLoads_ < maxConcurrentLoads_ && !loadQueue_.empty()) {
        u32 assetId = loadQueue_[0];
        loadQueue_.erase(0);
        auto it = pendingRequests_.find(assetId);
        if (it == pendingRequests_.end() || it.value().completed || it.value().inProgress) continue;

        it.value().inProgress = true;
        activeLoads_++;
    }
}

void SceneStreaming::completeLoad(u32 assetId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pendingRequests_.find(assetId);
    if (it == pendingRequests_.end()) return;
    if (!it.value().inProgress) return;

    it.value().completed = true;
    it.value().inProgress = false;
    if (activeLoads_ > 0) activeLoads_--;
    pendingRequests_.erase(assetId);
}

void SceneStreaming::updateStreaming(f32 dt, const Vec3& cameraPos) {
    lastUpdate_ = dt;

    for (usize i = 0; i < loadQueue_.size(); i++) {
        auto it = pendingRequests_.find(loadQueue_[i]);
        if (it == pendingRequests_.end()) continue;
        StreamRequest& req = it.value();
        f32 dist = (cameraPos).length();
        req.priority.distanceScore = (dist < streamRadius_) ? 1.0f - (dist / streamRadius_) : 0.0f;
    }

    for (usize i = 0; i + 1 < loadQueue_.size(); i++) {
        for (usize j = i + 1; j < loadQueue_.size(); j++) {
            auto itA = pendingRequests_.find(loadQueue_[i]);
            auto itB = pendingRequests_.find(loadQueue_[j]);
            if (itA != pendingRequests_.end() && itB != pendingRequests_.end()) {
                if (itB.value().priority < itA.value().priority) {
                    u32 tmp = loadQueue_[i];
                    loadQueue_[i] = loadQueue_[j];
                    loadQueue_[j] = tmp;
                }
            }
        }
    }

    Vector<u32> toCancel;
    for (auto it = pendingRequests_.begin(); it != pendingRequests_.end(); ++it) {
        if (!it.value().inProgress) {
            f32 dist = (cameraPos).length();
            if (dist > unloadRadius_) {
                toCancel.push_back(it.key());
            }
        }
    }
    for (u32 id : toCancel) {
        cancelRequest(id);
    }

    processQueue();
}

u32 SceneStreaming::getLoadQueueSize() const {
    return (u32)loadQueue_.size();
}

u32 SceneStreaming::getActiveLoads() const {
    return activeLoads_;
}

u32 SceneStreaming::getTotalBytesLoaded() const {
    return totalBytesLoaded_;
}

u32 SceneStreaming::getTotalBytesUnloaded() const {
    return totalBytesUnloaded_;
}

void SceneStreaming::setStreamRadius(f32 radius) { streamRadius_ = radius; }
f32 SceneStreaming::getStreamRadius() const { return streamRadius_; }

void SceneStreaming::setUnloadRadius(f32 radius) { unloadRadius_ = radius; }
f32 SceneStreaming::getUnloadRadius() const { return unloadRadius_; }

u32 SceneStreaming::computeLODLevel(f32 distance) const {
    if (distance < streamRadius_ * 0.2f) return 0;
    if (distance < streamRadius_ * 0.4f) return 1;
    if (distance < streamRadius_ * 0.6f) return 2;
    if (distance < streamRadius_ * 0.8f) return 3;
    return 4;
}

Vector<u32> SceneStreaming::computePrefetchList(const Vec3& position, f32 radius) const {
    Vector<u32> list;
    f32 prefetchRadius = radius + config_.prefetchDistance;
    for (const auto& chunk : chunks_) {
        if (chunk.state == StreamingState::Unloaded) {
            f32 dist = (chunk.bounds.center() - position).length();
            if (dist < prefetchRadius) {
                list.push_back(chunk.chunkId);
            }
        }
    }
    return list;
}

}
