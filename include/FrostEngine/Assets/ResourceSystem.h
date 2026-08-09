#pragma once

// ============================================================================
// FrostEngine Resource System — Handle-based resource management
// ============================================================================
// Design:
//   - Resources are identified by 64-bit handles (type + generation + index)
//   - All resources live in a central registry with reference counting
//   - Handles are safe: using a stale handle returns a null/default resource
//   - Supports async loading, GPU upload queues, and hot-reloading
//   - Resource lifetime is automatic: when refcount hits 0, resource is freed
//   - Memory budget tracking per resource type
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Log.h"
#include <cstring>
#include <functional>

namespace Frost {

// ---- Resource handle: type(8) + generation(16) + index(40) ----
using ResourceHandle = u64;
static constexpr ResourceHandle NULL_RESOURCE = 0;

inline u32 resType(ResourceHandle h)     { return (u32)((h >> 56) & 0xFF); }
inline u32 resGen(ResourceHandle h)      { return (u32)((h >> 40) & 0xFFFF); }
inline u64 resIndex(ResourceHandle h)    { return h & 0xFFFFFFFFFFULL; }
inline ResourceHandle makeResHandle(u32 type, u32 gen, u64 index) {
    return ((u64)type << 56) | ((u64)gen << 40) | index;
}

// ---- Resource types registered by the engine ----
enum class ResType : u8 {
    Texture = 1,
    Mesh    = 2,
    Material = 3,
    Shader  = 4,
    Audio   = 5,
    Font    = 6,
    Scene   = 7,
    Animation = 8,
    Script  = 9,
    Custom  = 10,
};

// ---- Base resource interface ----
struct IResource {
    virtual ~IResource() = default;
    virtual void destroy() = 0;
    virtual u64 gpuMemoryBytes() const { return 0; }
    virtual bool isReady() const { return true; }
    virtual const char* typeName() const { return "IResource"; }
};

// ---- Resource slot: holds one resource + its metadata ----
struct ResourceSlot {
    IResource* resource = nullptr;
    u32 generation = 0;
    u32 refCount = 0;
    bool loading = false;
    bool pinned = false;  // pinned resources are never evicted
    String path;
    u64 gpuBytes = 0;
};

// ---- Resource registry: central database of all loaded resources ----
class ResourceRegistry {
public:
    static constexpr u32 MAX_SLOTS = 65536;

    ResourceRegistry() {
        // slots_ is a fixed-size array, no resize needed
        for (u32 i = 0; i < MAX_SLOTS; i++) {
            freeSlots_.pushBack(i);
        }
    }

    // ---- Acquire a handle to a new or existing resource slot ----
    ResourceHandle acquire(ResType type, const char* path = "") {
        u32 slot;
        if (freeSlots_.size() > 0) {
            slot = freeSlots_.back();
            freeSlots_.popBack();
        } else {
            FROST_LOG_ERROR("[ResourceRegistry] out of slots");
            return NULL_RESOURCE;
        }
        slots_[slot].generation++;
        slots_[slot].refCount = 0;
        slots_[slot].resource = nullptr;
        slots_[slot].loading = false;
        slots_[slot].pinned = false;
        slots_[slot].path = path;
        slots_[slot].gpuBytes = 0;
        totalSlots_++;
        return makeResHandle((u32)type, slots_[slot].generation, slot);
    }

    // ---- Reference counting ----
    void addRef(ResourceHandle h) {
        u32 idx = (u32)resIndex(h);
        if (idx < MAX_SLOTS && slots_[idx].generation == resGen(h))
            slots_[idx].refCount++;
    }

    void release(ResourceHandle h) {
        u32 idx = (u32)resIndex(h);
        if (idx < MAX_SLOTS && slots_[idx].generation == resGen(h)) {
            if (slots_[idx].refCount > 0) slots_[idx].refCount--;
            if (slots_[idx].refCount == 0 && !slots_[idx].pinned && slots_[idx].resource) {
                slots_[idx].resource->destroy();
                delete slots_[idx].resource;
                slots_[idx].resource = nullptr;
                freeSlots_.pushBack(idx);
                totalSlots_--;
            }
        }
    }

    // ---- Access resource ----
    template<typename T>
    T* get(ResourceHandle h) const {
        u32 idx = (u32)resIndex(h);
        if (idx < MAX_SLOTS && slots_[idx].generation == resGen(h) && slots_[idx].resource) {
            return static_cast<T*>(slots_[idx].resource);
        }
        return nullptr;
    }

    bool valid(ResourceHandle h) const {
        u32 idx = (u32)resIndex(h);
        return idx < MAX_SLOTS && slots_[idx].generation == resGen(h);
    }

    void setResource(ResourceHandle h, IResource* res) {
        u32 idx = (u32)resIndex(h);
        if (idx < MAX_SLOTS && slots_[idx].generation == resGen(h)) {
            slots_[idx].resource = res;
            slots_[idx].gpuBytes = res ? res->gpuMemoryBytes() : 0;
        }
    }

    void pin(ResourceHandle h, bool p = true) {
        u32 idx = (u32)resIndex(h);
        if (idx < MAX_SLOTS) slots_[idx].pinned = p;
    }

    void markLoading(ResourceHandle h, bool l = true) {
        u32 idx = (u32)resIndex(h);
        if (idx < MAX_SLOTS) slots_[idx].loading = l;
    }

    // ---- Memory budget tracking ----
    u64 totalGpuMemory() const {
        u64 total = 0;
        for (u32 i = 0; i < MAX_SLOTS; i++) {
            if (slots_[i].resource) total += slots_[i].gpuBytes;
        }
        return total;
    }

    u32 loadedCount() const {
        u32 count = 0;
        for (u32 i = 0; i < MAX_SLOTS; i++) {
            if (slots_[i].resource) count++;
        }
        return count;
    }

    // ---- Evict least-recently-used non-pinned resources to free memory ----
    u64 evictUntil(u64 targetBytes) {
        u64 freed = 0;
        for (u32 i = MAX_SLOTS; i > 0 && totalGpuMemory() > targetBytes; ) {
            i--;
            if (slots_[i].resource && !slots_[i].pinned && slots_[i].refCount == 0) {
                freed += slots_[i].gpuBytes;
                slots_[i].resource->destroy();
                delete slots_[i].resource;
                slots_[i].resource = nullptr;
                freeSlots_.pushBack(i);
                totalSlots_--;
            }
        }
        return freed;
    }

    void clear() {
        for (u32 i = 0; i < MAX_SLOTS; i++) {
            if (slots_[i].resource) {
                slots_[i].resource->destroy();
                delete slots_[i].resource;
                slots_[i].resource = nullptr;
            }
        }
        freeSlots_.clear();
        for (u32 i = 0; i < MAX_SLOTS; i++) freeSlots_.pushBack(i);
        totalSlots_ = 0;
    }

private:
    ResourceSlot slots_[MAX_SLOTS];
    Vector<u32> freeSlots_;
    u32 totalSlots_ = 0;
};

// ---- Typed resource handle wrapper (type-safe convenience) ----
template<typename T, ResType TYPE>
class ResHandle {
public:
    ResHandle() = default;
    explicit ResHandle(ResourceHandle h) : handle_(h) {}
    explicit ResHandle(ResourceHandle h, ResourceRegistry* reg) : handle_(h), registry_(reg) {}

    T* operator->() const { return registry_ ? registry_->get<T>(handle_) : nullptr; }
    T& operator*() const { return *registry_->get<T>(handle_); }
    bool valid() const { return registry_ && registry_->valid(handle_); }
    explicit operator bool() const { return valid(); }
    ResourceHandle raw() const { return handle_; }

    void release() {
        if (registry_ && handle_) registry_->release(handle_);
        handle_ = NULL_RESOURCE;
    }

private:
    ResourceHandle handle_ = NULL_RESOURCE;
    ResourceRegistry* registry_ = nullptr;
};

// ---- Async resource loader: queues loads and processes them across frames ----
struct LoadRequest {
    ResourceHandle handle;
    String path;
    ResType type;
    std::function<IResource*(const char*)> loader;
};

class AsyncResourceLoader {
public:
    void enqueue(ResourceHandle h, const char* path, ResType type,
                 std::function<IResource*(const char*)> loader) {
        LoadRequest req;
        req.handle = h;
        req.path = path;
        req.type = type;
        req.loader = std::move(loader);
        queue_.pushBack(std::move(req));
    }

    // Process up to N loads per frame (non-blocking)
    u32 processBatch(u32 maxPerFrame, ResourceRegistry& registry) {
        u32 processed = 0;
        while (queue_.size() > 0 && processed < maxPerFrame) {
            LoadRequest& req = queue_[0];
            registry.markLoading(req.handle, true);
            IResource* res = req.loader(req.path.data());
            if (res) {
                registry.setResource(req.handle, res);
                registry.addRef(req.handle);
            }
            registry.markLoading(req.handle, false);
            processed++;
            // Shift queue
            for (u32 i = 0; i < queue_.size() - 1; i++)
                queue_[i] = queue_[i + 1];
            queue_.popBack();
        }
        return processed;
    }

    u32 pendingCount() const { return (u32)queue_.size(); }

private:
    Vector<LoadRequest> queue_;
};

} // namespace Frost
