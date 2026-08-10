#include "Core/FrostMono.h"

#include <algorithm>
#include <cstring>
#include <functional>

namespace Frost {

FrostMonoWorld::FrostMonoWorld() : nextNodeId_(1), currentFrame_(0) {}

FrostMonoWorld::~FrostMonoWorld() {
    shutdown();
}

bool FrostMonoWorld::initialize(const Config& config) {
    config_ = config;
    nodes_.reserve(config.maxNodes);
    edges_.reserve(config.maxEdges);
    components_.reserve(config.maxComponents);
    temporalIndex_.frames.resize(config.temporalHistoryFrames);
    temporalIndex_.maxFrames = config.temporalHistoryFrames;
    spatialIndex_.cellSize = config.spatialIndexCellSize;
    stats_ = {};
    return true;
}

void FrostMonoWorld::shutdown() {
    nodes_.clear();
    edges_.clear();
    components_.clear();
    nodeIndex_.clear();
    nodeComponents_.clear();
    outgoingEdges_.clear();
    incomingEdges_.clear();
    archetypeIndex_.clear();
    spatialIndex_.cells.clear();
    for (auto& frame : temporalIndex_.frames) frame.clear();
    dirtyComponents_.clear();
    pendingEvents_.clear();
    stats_ = {};
}

u64 FrostMonoWorld::createEntity(const String& name, const String& archetype) {
    u64 id = allocateNode(WorldNodeType::Entity, name, archetype);
    stats_.nodeCount++;
    return id;
}

void FrostMonoWorld::destroyEntity(u64 entityId) {
    auto it = nodeIndex_.find(entityId);
    if (it == nodeIndex_.end()) return;

    u32 nodeIdx = it.value();
    WorldNode& node = nodes_[nodeIdx];
    node.active = false;

    auto compIt = nodeComponents_.find(entityId);
    if (compIt != nodeComponents_.end()) {
        for (u32 compIdx : compIt.value()) {
            components_[compIdx].data.clear();
        }
        nodeComponents_.erase(entityId);
    }

    auto outIt = outgoingEdges_.find(entityId);
    if (outIt != outgoingEdges_.end()) {
        for (u32 edgeIdx : outIt.value()) {
            edges_[edgeIdx].active = false;
        }
    }

    auto inIt = incomingEdges_.find(entityId);
    if (inIt != incomingEdges_.end()) {
        for (u32 edgeIdx : inIt.value()) {
            edges_[edgeIdx].active = false;
        }
    }

    deallocateNode(entityId);
    stats_.nodeCount--;
}

bool FrostMonoWorld::hasEntity(u64 entityId) const {
    auto it = nodeIndex_.find(entityId);
    return it != nodeIndex_.end() && nodes_[it.value()].active;
}

u64 FrostMonoWorld::addRelationship(u64 from, u64 to, EdgeType type, f32 weight) {
    auto fromIt = nodeIndex_.find(from);
    auto toIt = nodeIndex_.find(to);
    if (fromIt == nodeIndex_.end() || toIt == nodeIndex_.end()) return 0;

    WorldEdge edge;
    edge.from = from;
    edge.to = to;
    edge.type = type;
    edge.weight = weight;
    edge.frameCreated = currentFrame_;
    edge.active = true;

    edges_.push_back(edge);
    u32 edgeIdx = edges_.size() - 1;

    outgoingEdges_[from].push_back(edgeIdx);
    incomingEdges_[to].push_back(edgeIdx);
    stats_.edgeCount++;
    return edgeIdx + 1;
}

void FrostMonoWorld::removeRelationship(u64 from, u64 to, EdgeType type) {
    auto outIt = outgoingEdges_.find(from);
    if (outIt == outgoingEdges_.end()) return;

    auto& edgeIndices = outIt.value();
    for (usize i = 0; i < edgeIndices.size(); ) {
        u32 edgeIdx = edgeIndices[i];
        WorldEdge& edge = edges_[edgeIdx];
        if (edge.to == to && edge.type == type && edge.active) {
            edge.active = false;
            auto inIt = incomingEdges_.find(to);
            if (inIt != incomingEdges_.end()) {
                auto& inEdges = inIt.value();
                for (usize j = 0; j < inEdges.size(); ) {
                    if (inEdges[j] == edgeIdx) {
                        inEdges.erase(j);
                    } else {
                        ++j;
                    }
                }
            }
            edgeIndices.erase(i);
            stats_.edgeCount--;
        } else {
            ++i;
        }
    }
}

void FrostMonoWorld::setRelationshipWeight(u64 from, u64 to, EdgeType type, f32 weight) {
    auto outIt = outgoingEdges_.find(from);
    if (outIt == outgoingEdges_.end()) return;

    for (u32 edgeIdx : outIt.value()) {
        WorldEdge& edge = edges_[edgeIdx];
        if (edge.to == to && edge.type == type && edge.active) {
            edge.weight = weight;
            break;
        }
    }
}

Vector<u64> FrostMonoWorld::queryNeighbors(u64 nodeId, EdgeType type, u32 maxDepth) const {
    Vector<u64> result;
    Vector<u64> current = {nodeId};
    Vector<u64> next;

    for (u32 depth = 0; depth < maxDepth && !current.empty(); ++depth) {
        for (u64 id : current) {
            auto it = outgoingEdges_.find(id);
            if (it == outgoingEdges_.end()) continue;
            for (u32 edgeIdx : it.value()) {
                const WorldEdge& edge = edges_[edgeIdx];
                if (edge.active && (type == EdgeType::Affects || edge.type == type)) {
                    if (std::find(result.begin(), result.end(), edge.to) == result.end()) {
                        result.push_back(edge.to);
                        next.push_back(edge.to);
                    }
                }
            }
        }
        current = next;
        next.clear();
    }
    return result;
}

Vector<u64> FrostMonoWorld::queryComponents(u64 entityId) const {
    auto it = nodeComponents_.find(entityId);
    if (it == nodeComponents_.end()) return {};
    Vector<u64> result;
    for (u32 compIdx : it.value()) {
        result.push_back(components_[compIdx].typeHash);
    }
    return result;
}

Vector<u64> FrostMonoWorld::queryByArchetype(const String& archetype) const {
    auto it = archetypeIndex_.find(archetype);
    if (it == archetypeIndex_.end()) return {};
    return it.value();
}

Vector<u64> FrostMonoWorld::querySpatial(const Vec3& center, f32 radius) const {
    if (!config_.enableSpatialIndex) return {};

    u64 cellKey = spatialIndex_.hash(center);
    u32 cellRadius = static_cast<u32>(radius / spatialIndex_.cellSize) + 1;

    Vector<u64> result;
    for (i32 dx = -static_cast<i32>(cellRadius); dx <= static_cast<i32>(cellRadius); ++dx) {
        for (i32 dy = -static_cast<i32>(cellRadius); dy <= static_cast<i32>(cellRadius); ++dy) {
            for (i32 dz = -static_cast<i32>(cellRadius); dz <= static_cast<i32>(cellRadius); ++dz) {
                u64 neighborKey = cellKey + (static_cast<u64>(dx) << 40) + (static_cast<u64>(dy) << 20) + static_cast<u64>(dz);
                auto it = spatialIndex_.cells.find(neighborKey);
                if (it != spatialIndex_.cells.end()) {
                    for (u64 nodeId : it.value().nodeIds) {
                        auto nodeIt = nodeIndex_.find(nodeId);
                        if (nodeIt != nodeIndex_.end()) {
                            const WorldNode& node = nodes_[nodeIt.value()];
                            if (node.active) {
                                Vec3 pos;
                                if (auto* transform = getComponent<Vec3>(nodeId)) {
                                    pos = *transform;
                                }
                                if ((pos - center).length() <= radius) {
                                    result.push_back(nodeId);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return result;
}

Vector<u64> FrostMonoWorld::queryTemporal(u64 sinceFrame) const {
    return temporalIndex_.query(sinceFrame);
}

void FrostMonoWorld::propagateEvent(u64 source, const String& eventName, const void* data, u32 maxDepth) {
    if (!config_.enableEventPropagation) return;

    Vector<u64> queue = {source};
    Vector<u64> next;
    Vector<u64> visited;

    for (u32 depth = 0; depth < maxDepth && !queue.empty(); ++depth) {
        for (u64 id : queue) {
            visited.push_back(id);
            auto it = outgoingEdges_.find(id);
            if (it == outgoingEdges_.end()) continue;

            for (u32 edgeIdx : it.value()) {
                const WorldEdge& edge = edges_[edgeIdx];
                if (edge.active && edge.type == EdgeType::Triggers) {
                    if (std::find(visited.begin(), visited.end(), edge.to) == visited.end()) {
                        next.push_back(edge.to);
                        processEventPropagation(edge.to, eventName, data, maxDepth - depth - 1);
                    }
                }
            }
        }
        queue = next;
        next.clear();
    }
    stats_.eventsProcessed += visited.size();
}

void FrostMonoWorld::addCausalLink(u64 cause, u64 effect, f32 strength) {
    addRelationship(cause, effect, EdgeType::CausalLink, strength);
}

Vector<u64> FrostMonoWorld::traceCausality(u64 effect, u32 maxDepth) const {
    Vector<u64> result;
    Vector<u64> current = {effect};

    for (u32 depth = 0; depth < maxDepth && !current.empty(); ++depth) {
        Vector<u64> next;
        for (u64 id : current) {
            auto it = incomingEdges_.find(id);
            if (it == incomingEdges_.end()) continue;
            for (u32 edgeIdx : it.value()) {
                const WorldEdge& edge = edges_[edgeIdx];
                if (edge.active && edge.type == EdgeType::CausalLink) {
                    if (std::find(result.begin(), result.end(), edge.from) == result.end()) {
                        result.push_back(edge.from);
                        next.push_back(edge.from);
                    }
                }
            }
        }
        current = next;
    }
    stats_.causalTraversals += result.size();
    return result;
}

Vector<u64> FrostMonoWorld::traceConsequences(u64 cause, u32 maxDepth) const {
    Vector<u64> result;
    Vector<u64> current = {cause};

    for (u32 depth = 0; depth < maxDepth && !current.empty(); ++depth) {
        Vector<u64> next;
        for (u64 id : current) {
            auto it = outgoingEdges_.find(id);
            if (it == outgoingEdges_.end()) continue;
            for (u32 edgeIdx : it.value()) {
                const WorldEdge& edge = edges_[edgeIdx];
                if (edge.active && edge.type == EdgeType::CausalLink) {
                    if (std::find(result.begin(), result.end(), edge.to) == result.end()) {
                        result.push_back(edge.to);
                        next.push_back(edge.to);
                    }
                }
            }
        }
        current = next;
    }
    stats_.causalTraversals += result.size();
    return result;
}

void FrostMonoWorld::setFieldValue(u64 fieldId, const Vec3& position, f32 value) {
    FieldSample sample;
    sample.position = position;
    sample.value = value;
    sample.frame = currentFrame_;
    addFieldSample(fieldId, sample);
}

f32 FrostMonoWorld::getFieldValue(u64 fieldId, const Vec3& position) const {
    return 0.0f;
}

void FrostMonoWorld::addFieldSample(u64 fieldId, const FieldSample& sample) {
}

void FrostMonoWorld::update(f32 dt, u64 frameIndex) {
    currentFrame_ = frameIndex;
    stats_.updateTimeMs = 0.0f;
    flushDirtyComponents();
    temporalIndex_.add(frameIndex, 0);
}

void FrostMonoWorld::flushDirtyComponents() {
    dirtyComponents_.clear();
}

void FrostMonoWorld::resetStats() {
    stats_ = {};
}

const WorldNode* FrostMonoWorld::getNode(u64 id) const {
    auto it = nodeIndex_.find(id);
    if (it == nodeIndex_.end()) return nullptr;
    return &nodes_[it.value()];
}

const WorldEdge* FrostMonoWorld::getEdge(u64 from, u64 to, EdgeType type) const {
    auto outIt = outgoingEdges_.find(from);
    if (outIt == outgoingEdges_.end()) return nullptr;
    for (u32 edgeIdx : outIt.value()) {
        const WorldEdge& edge = edges_[edgeIdx];
        if (edge.to == to && edge.type == type && edge.active) {
            return &edge;
        }
    }
    return nullptr;
}

u64 FrostMonoWorld::allocateNode(WorldNodeType type, const String& name, const String& archetype) {
    u64 id = nextNodeId_++;
    WorldNode node;
    node.id = id;
    node.type = type;
    node.name = name;
    node.archetype = archetype;
    node.frameCreated = currentFrame_;
    node.frameModified = currentFrame_;
    node.active = true;

    nodes_.push_back(node);
    u32 nodeIdx = nodes_.size() - 1;
    nodeIndex_[id] = nodeIdx;

    if (!archetype.empty()) {
        archetypeIndex_[archetype].push_back(id);
    }

    return id;
}

void FrostMonoWorld::deallocateNode(u64 id) {
    auto it = nodeIndex_.find(id);
    if (it == nodeIndex_.end()) return;

    u32 nodeIdx = it.value();
    WorldNode& node = nodes_[nodeIdx];

    if (!node.archetype.empty()) {
        auto archIt = archetypeIndex_.find(node.archetype);
        if (archIt != archetypeIndex_.end()) {
            auto& archVec = archIt.value();
            for (usize i = 0; i < archVec.size(); ) {
                if (archVec[i] == id) {
                    archVec.erase(i, i + 1);
                } else {
                    ++i;
                }
            }
        }
    }

    nodeIndex_.erase(id);
    node.active = false;
}

void FrostMonoWorld::updateSpatialIndex(u64 nodeId) {
    if (!config_.enableSpatialIndex) return;
}

void FrostMonoWorld::updateTemporalIndex(u64 nodeId) {
    temporalIndex_.add(currentFrame_, nodeId);
}

void FrostMonoWorld::processEventPropagation(u64 source, const String& eventName, const void* data, u32 depth) {
    if (depth == 0) return;
}

void FrostMonoWorld::processCausalEffects(u64 cause, u32 depth) {
}

u64 FrostMonoWorld::SpatialHash::hash(const Vec3& pos) const {
    i32 x = static_cast<i32>(pos.x / cellSize);
    i32 y = static_cast<i32>(pos.y / cellSize);
    i32 z = static_cast<i32>(pos.z / cellSize);
    return (static_cast<u64>(x) << 40) | (static_cast<u64>(y) << 20) | static_cast<u64>(z);
}

void FrostMonoWorld::TemporalIndex::add(u64 frame, u64 nodeId) {
    u32 idx = frame % maxFrames;
    if (idx < frames.size()) {
        frames[idx].push_back(nodeId);
    }
}

Vector<u64> FrostMonoWorld::TemporalIndex::query(u64 sinceFrame) const {
    Vector<u64> result;
    if (sinceFrame == 0) return result;
    for (const auto& frame : frames) {
        for (u64 nodeId : frame) {
            if (nodeId >= sinceFrame) {
                result.push_back(nodeId);
            }
        }
    }
    return result;
}

}