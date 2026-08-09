#pragma once

// ============================================================================
// FrostEngine ECS — Data-Oriented Entity Component System
// ============================================================================
// Design principles:
//   1. Components are plain data stored in contiguous arrays (cache-friendly)
//   2. Systems iterate over component arrays with zero indirection
//   3. Entities are lightweight IDs (generation + index), not objects
//   4. Maximum 64 component types (one bit per type in a bitmask)
//   5. All allocation is pool-based — zero malloc during gameplay
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include <cstring>
#include <typeindex>
#include <unordered_map>

namespace Frost {

// ---- Entity handle: generation counter + index into sparse set ----
using EntityID = u64;

static constexpr u32 MAX_ENTITIES     = 65536;
static constexpr u32 MAX_COMPONENTS   = 64;
static constexpr u32 CHUNK_SIZE       = 64;

inline u32 entityIndex(EntityID id) { return (u32)(id & 0xFFFFFFFF); }
inline u32 entityGeneration(EntityID id) { return (u32)(id >> 32); }
inline EntityID makeEntityID(u32 index, u32 generation) {
    return ((EntityID)generation << 32) | (EntityID)index;
}

// ---- Component type ID assigned at registration time ----
using ComponentTypeID = u32;

// ---- Bitmask: which component types an entity has ----
struct ComponentMask {
    u64 bits[1] = {};  // supports up to 64 component types

    void set(ComponentTypeID id)   { bits[0] |= (1ULL << id); }
    void clear(ComponentTypeID id) { bits[0] &= ~(1ULL << id); }
    bool test(ComponentTypeID id) const { return (bits[0] >> id) & 1ULL; }
    bool contains(const ComponentMask& other) const {
        return (bits[0] & other.bits[0]) == other.bits[0];
    }
    bool empty() const { return bits[0] == 0; }
    ComponentMask operator|(const ComponentMask& o) const {
        ComponentMask r; r.bits[0] = bits[0] | o.bits[0]; return r;
    }
    ComponentMask operator&(const ComponentMask& o) const {
        ComponentMask r; r.bits[0] = bits[0] & o.bits[0]; return r;
    }
    bool operator==(const ComponentMask& o) const { return bits[0] == o.bits[0]; }
};

// ---- Component storage: contiguous array with swap-remove ----
// Each ComponentArray<T> stores MAX_ENTITIES entries but only `count_` are alive.
// The alive entries are packed at the front. Swap-remove keeps the array dense.
struct IComponentArray {
    virtual ~IComponentArray() = default;
    virtual void remove(u32 index) = 0;
    virtual void move(u32 from, u32 to) = 0;
    virtual void* getRaw(u32 index) = 0;
    virtual u32 count() const = 0;
};

template<typename T>
struct ComponentArray : IComponentArray {
    T data[MAX_ENTITIES];
    u32 count_ = 0;

    T& operator[](u32 index) { return data[index]; }
    const T& operator[](u32 index) const { return data[index]; }
    u32 count() const override { return count_; }

    T& add(EntityID entity) {
        u32 idx = count_++;
        data[idx] = T{};
        return data[idx];
    }

    void remove(u32 index) override {
        if (index < count_ - 1) {
            data[index] = data[count_ - 1];
        }
        count_--;
    }

    void move(u32 from, u32 to) override {
        data[to] = data[from];
    }

    void* getRaw(u32 index) override { return &data[index]; }
};

// ---- World: owns all entities and component arrays ----
class World {
public:
    // ---- Entity lifecycle ----
    EntityID createEntity() {
        u32 index;
        if (freeList_.size() > 0) {
            index = freeList_.back();
            freeList_.popBack();
        } else {
            index = entityCount_++;
        }
        generations_[index]++;
        EntityID id = makeEntityID(index, generations_[index]);
        masks_[index] = ComponentMask{};
        entities_.pushBack(id);
        return id;
    }

    void destroyEntity(EntityID id) {
        u32 idx = entityIndex(id);
        // Remove all components
        for (u32 c = 0; c < componentCount_; c++) {
            if (masks_[idx].test(c)) {
                arrays_[c]->remove(sparseToDense_[idx]);
                denseToSparse_[sparseToDense_[idx]] = denseToSparse_[entities_.size() - 1];
                sparseToDense_[idx] = 0xFFFFFFFF;
            }
        }
        masks_[idx] = ComponentMask{};
        freeList_.pushBack(idx);
    }

    bool alive(EntityID id) const {
        u32 idx = entityIndex(id);
        return generations_[idx] == entityGeneration(id);
    }

    u32 entityCount() const { return (u32)entities_.size(); }
    EntityID entityAt(u32 i) const { return entities_[i]; }

    // ---- Component type registration ----
    template<typename T>
    ComponentTypeID registerComponent() {
        ComponentTypeID id = componentCount_++;
        typeMap_[std::type_index(typeid(T))] = id;
        arrays_[id] = new ComponentArray<T>();
        return id;
    }

    template<typename T>
    ComponentTypeID getComponentID() const {
        auto it = typeMap_.find(std::type_index(typeid(T)));
        if (it != typeMap_.end()) return it->second;
        return 0xFFFFFFFF;
    }

    template<typename T>
    bool hasComponent(EntityID id) const {
        u32 idx = entityIndex(id);
        ComponentTypeID cid = getComponentID<T>();
        return masks_[idx].test(cid);
    }

    template<typename T>
    T& addComponent(EntityID id) {
        u32 idx = entityIndex(id);
        ComponentTypeID cid = getComponentID<T>();
        auto* arr = static_cast<ComponentArray<T>*>(arrays_[cid]);
        masks_[idx].set(cid);
        return arr->add(id);
    }

    template<typename T>
    T& getComponent(EntityID id) {
        u32 idx = entityIndex(id);
        ComponentTypeID cid = getComponentID<T>();
        auto* arr = static_cast<ComponentArray<T>*>(arrays_[cid]);
        return (*arr)[0]; // simplified: returns first entry
    }

    template<typename T>
    void removeComponent(EntityID id) {
        u32 idx = entityIndex(id);
        ComponentTypeID cid = getComponentID<T>();
        masks_[idx].clear(cid);
    }

    // ---- Query: get all entities with specific components ----
    template<typename... Ts>
    Vector<EntityID> query() {
        ComponentMask required;
        ((required.set(getComponentID<Ts>())), ...);
        Vector<EntityID> result;
        for (u32 i = 0; i < entities_.size(); i++) {
            u32 idx = entityIndex(entities_[i]);
            if (masks_[idx].contains(required)) {
                result.pushBack(entities_[i]);
            }
        }
        return result;
    }

    // ---- Direct component array access for systems ----
    template<typename T>
    ComponentArray<T>& getArray() {
        return *static_cast<ComponentArray<T>*>(arrays_[getComponentID<T>()]);
    }

    // ---- Clear all entities (for level load) ----
    void clear() {
        for (u32 i = 0; i < componentCount_; i++) {
            delete arrays_[i];
            arrays_[i] = nullptr;
        }
        for (auto& [k, v] : typeMap_) v = 0xFFFFFFFF;
        entities_.clear();
        freeList_.clear();
        entityCount_ = 0;
        componentCount_ = 0;
        std::memset(masks_, 0, sizeof(masks_));
    }

private:
    ComponentMask masks_[MAX_ENTITIES];
    u32 generations_[MAX_ENTITIES] = {};
    u32 sparseToDense_[MAX_ENTITIES] = {};
    u32 denseToSparse_[MAX_ENTITIES] = {};
    Vector<u32> freeList_;

    IComponentArray* arrays_[MAX_COMPONENTS] = {};
    u32 componentCount_ = 0;
    std::unordered_map<std::type_index, ComponentTypeID> typeMap_;

    u32 entityCount_ = 0;
    Vector<EntityID> entities_; // active entities list
};

// ---- System base class ----
// Systems implement game logic. They run each frame and operate on entities.
struct System {
    virtual ~System() = default;
    virtual void init(World& world) {}
    virtual void update(World& world, f32 dt) {}
    virtual void shutdown(World& world) {}
};

// ---- SystemManager: owns and runs all systems in order ----
class SystemManager {
public:
    void addSystem(System* system) {
        systems_.pushBack(system);
    }

    void initAll(World& world) {
        for (auto* sys : systems_) sys->init(world);
    }

    void updateAll(World& world, f32 dt) {
        for (auto* sys : systems_) sys->update(world, dt);
    }

    void shutdownAll(World& world) {
        for (auto* sys : systems_) sys->shutdown(world);
    }

private:
    Vector<System*> systems_;
};

} // namespace Frost
