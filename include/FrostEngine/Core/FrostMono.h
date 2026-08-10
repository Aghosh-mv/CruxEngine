#pragma once

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Math.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/UniquePtr.h"
#include "Core/HashMap.h"

namespace Frost {

enum class WorldNodeType : u8 {
    Entity = 0,
    Component,
    System,
    Field,
    Event,
    Relationship,
    Count
};

enum class EdgeType : u8 {
    HasComponent = 0,
    ChildOf,
    DependsOn,
    Affects,
    Triggers,
    Observes,
    BindsTo,
    SpatialNear,
    TemporalAfter,
    CausalLink,
    Count
};

struct WorldNode {
    u64 id = 0;
    WorldNodeType type = WorldNodeType::Entity;
    String name;
    String archetype;
    u64 frameCreated = 0;
    u64 frameModified = 0;
    bool active = true;
    bool persistent = false;
};

struct WorldEdge {
    u64 from = 0;
    u64 to = 0;
    EdgeType type = EdgeType::HasComponent;
    f32 weight = 1.0f;
    u64 frameCreated = 0;
    bool active = true;
};

struct ComponentData {
    u64 entityId = 0;
    u32 typeHash = 0;
    Vector<u8> data;
    u64 version = 0;
    bool dirty = true;
};

struct FieldSample {
    Vec3 position;
    f32 value;
    f32 gradient[3];
    u64 frame;
};

class FrostMonoWorld {
public:
    struct Config {
        u32 maxNodes = 1000000;
        u32 maxEdges = 5000000;
        u32 maxComponents = 2000000;
        f32 spatialIndexCellSize = 10.0f;
        u32 temporalHistoryFrames = 60;
        bool enableCausalTracking = true;
        bool enableSpatialIndex = true;
        bool enableEventPropagation = true;
    };

    struct Stats {
        u64 nodeCount = 0;
        u64 edgeCount = 0;
        u64 componentCount = 0;
        u64 eventsProcessed = 0;
        mutable u64 causalTraversals = 0;
        f32 updateTimeMs = 0.0f;
        f32 spatialQueryTimeMs = 0.0f;
        f32 memoryMB = 0.0f;
    };

    FrostMonoWorld();
    ~FrostMonoWorld();

    bool initialize(const Config& config);
    void shutdown();

    void setConfig(const Config& config) { config_ = config; }
    const Config& getConfig() const { return config_; }

    u64 createEntity(const String& name = "", const String& archetype = "");
    void destroyEntity(u64 entityId);
    bool hasEntity(u64 entityId) const;

    template<typename T>
    u64 addComponent(u64 entityId, const T& component);

    template<typename T>
    T* getComponent(u64 entityId);

    template<typename T>
    const T* getComponent(u64 entityId) const;

    template<typename T>
    void removeComponent(u64 entityId);

    template<typename T>
    bool hasComponent(u64 entityId) const;

    u64 addRelationship(u64 from, u64 to, EdgeType type, f32 weight = 1.0f);
    void removeRelationship(u64 from, u64 to, EdgeType type);
    void setRelationshipWeight(u64 from, u64 to, EdgeType type, f32 weight);

    Vector<u64> queryNeighbors(u64 nodeId, EdgeType type = EdgeType::Affects, u32 maxDepth = 1) const;
    Vector<u64> queryComponents(u64 entityId) const;
    Vector<u64> queryByArchetype(const String& archetype) const;
    Vector<u64> querySpatial(const Vec3& center, f32 radius) const;
    Vector<u64> queryTemporal(u64 sinceFrame) const;

    void propagateEvent(u64 source, const String& eventName, const void* data = nullptr, u32 maxDepth = 3);
    void addCausalLink(u64 cause, u64 effect, f32 strength = 1.0f);
    Vector<u64> traceCausality(u64 effect, u32 maxDepth = 10) const;
    Vector<u64> traceConsequences(u64 cause, u32 maxDepth = 10) const;

    void setFieldValue(u64 fieldId, const Vec3& position, f32 value);
    f32 getFieldValue(u64 fieldId, const Vec3& position) const;
    void addFieldSample(u64 fieldId, const FieldSample& sample);

    void update(f32 dt, u64 frameIndex);
    void flushDirtyComponents();

    const Stats& getStats() const { return stats_; }
    void resetStats();

    u64 getNodeCount() const { return nodes_.size(); }
    u64 getEdgeCount() const { return edges_.size(); }

    const WorldNode* getNode(u64 id) const;
    const WorldEdge* getEdge(u64 from, u64 to, EdgeType type) const;

private:
    struct SpatialHash {
        struct Cell {
            Vector<u64> nodeIds;
        };
        HashMap<u64, Cell> cells;
        f32 cellSize = 10.0f;
        u64 hash(const Vec3& pos) const;
    };

    struct TemporalIndex {
        Vector<Vector<u64>> frames;
        u32 maxFrames = 60;
        void add(u64 frame, u64 nodeId);
        Vector<u64> query(u64 sinceFrame) const;
    };

    u64 allocateNode(WorldNodeType type, const String& name, const String& archetype);
    void deallocateNode(u64 id);
    void updateSpatialIndex(u64 nodeId);
    void updateTemporalIndex(u64 nodeId);
    void processEventPropagation(u64 source, const String& eventName, const void* data, u32 depth);
    void processCausalEffects(u64 cause, u32 depth);

    Config config_;
    Stats stats_;

    Vector<WorldNode> nodes_;
    Vector<WorldEdge> edges_;
    Vector<ComponentData> components_;
    HashMap<u64, u32> nodeIndex_;
    HashMap<u64, Vector<u32>> nodeComponents_;
    HashMap<u64, Vector<u32>> outgoingEdges_;
    HashMap<u64, Vector<u32>> incomingEdges_;
    HashMap<String, Vector<u64>> archetypeIndex_;

    SpatialHash spatialIndex_;
    TemporalIndex temporalIndex_;

    Vector<u64> dirtyComponents_;
    Vector<u64> pendingEvents_;
    u64 nextNodeId_ = 1;
    u64 currentFrame_ = 0;
};

template<typename T>
u64 FrostMonoWorld::addComponent(u64 entityId, const T& component) {
    auto it = nodeIndex_.find(entityId);
    if (it == nodeIndex_.end()) return 0;

    u32 typeHash = typeid(T).hash_code();
    ComponentData comp;
    comp.entityId = entityId;
    comp.typeHash = typeHash;
    comp.data.resize(sizeof(T));
    memcpy(comp.data.data(), &component, sizeof(T));
    comp.version = 1;
    comp.dirty = true;

    components_.push_back(comp);
    u32 compIndex = components_.size() - 1;
    nodeComponents_[entityId].push_back(compIndex);
    dirtyComponents_.push_back(entityId);

    stats_.componentCount++;
    return entityId;
}

template<typename T>
T* FrostMonoWorld::getComponent(u64 entityId) {
    auto it = nodeComponents_.find(entityId);
    if (it == nodeComponents_.end()) return nullptr;

    u32 typeHash = typeid(T).hash_code();
    for (u32 compIndex : it.value()) {
        if (components_[compIndex].typeHash == typeHash) {
            return reinterpret_cast<T*>(components_[compIndex].data.data());
        }
    }
    return nullptr;
}

template<typename T>
const T* FrostMonoWorld::getComponent(u64 entityId) const {
    auto it = nodeComponents_.find(entityId);
    if (it == nodeComponents_.end()) return nullptr;

    u32 typeHash = typeid(T).hash_code();
    for (u32 compIndex : it.value()) {
        if (components_[compIndex].typeHash == typeHash) {
            return reinterpret_cast<const T*>(components_[compIndex].data.data());
        }
    }
    return nullptr;
}

template<typename T>
void FrostMonoWorld::removeComponent(u64 entityId) {
    auto it = nodeComponents_.find(entityId);
    if (it == nodeComponents_.end()) return;

    u32 typeHash = typeid(T).hash_code();
    auto& comps = it.value();
    for (auto cit = comps.begin(); cit != comps.end(); ++cit) {
        if (components_[*cit].typeHash == typeHash) {
            components_[*cit].data.clear();
            comps.erase((usize)(cit - comps.begin()));
            stats_.componentCount--;
            break;
        }
    }
}

template<typename T>
bool FrostMonoWorld::hasComponent(u64 entityId) const {
    auto it = nodeComponents_.find(entityId);
    if (it == nodeComponents_.end()) return false;

    u32 typeHash = typeid(T).hash_code();
    for (u32 compIndex : it.value()) {
        if (components_[compIndex].typeHash == typeHash) {
            return true;
        }
    }
    return false;
}

} // namespace Frost