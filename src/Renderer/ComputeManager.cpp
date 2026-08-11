#include "FrostEngine/Renderer/ComputeManager.h"
#include "FrostEngine/Core/Math.h"

#include <chrono>

namespace Frost {

namespace {

constexpr u32 kUavInvalid = 0xFFFFFFFF;
constexpr u32 kMaxBindingSlots = 16;

u32 findFreeBindingSlot(const Vector<ComputeManager::UAVResource>& pool) {
    for (u32 s = 0; s < kMaxBindingSlots; s++) {
        bool taken = false;
        for (usize i = 0; i < pool.size(); i++) {
            if (pool[i].inUse && pool[i].boundSlot == s) {
                taken = true;
                break;
            }
        }
        if (!taken) return s;
    }
    return 0;
}

} // anonymous namespace

u32 ComputeManager::allocateUAV(u32 sizeBytes) {
    if (sizeBytes == 0) {
        FROST_LOG_ERROR("[ComputeManager] allocateUAV: sizeBytes must be > 0");
        return kUavInvalid;
    }

    u32 id = kUavInvalid;
    for (usize i = 0; i < uavResources_.size(); i++) {
        if (!uavResources_[i].inUse) {
            id = static_cast<u32>(i);
            break;
        }
    }

    GLuint buf = 0;
    Gl::GenBuffers(1, &buf);
    if (buf == 0) {
        FROST_LOG_ERROR("[ComputeManager] allocateUAV: failed to generate buffer");
        return kUavInvalid;
    }
    Gl::BindBuffer(0x90D2, buf); // GL_SHADER_STORAGE_BUFFER
    Gl::BufferData(0x90D2, static_cast<GLsizeiptr>(sizeBytes), nullptr, 0x88E4); // GL_DYNAMIC_DRAW
    Gl::BindBuffer(0x90D2, 0);

    u32 slot = findFreeBindingSlot(uavResources_);
    if (id == kUavInvalid) {
        id = static_cast<u32>(uavResources_.size());
        uavResources_.pushBack(UAVResource{id, sizeBytes, true, slot});
        uavBuffers_.pushBack(buf);
    } else {
        uavResources_[id] = UAVResource{id, sizeBytes, true, slot};
        uavBuffers_[id] = buf;
    }

    totalGpuBytes_ += sizeBytes;
    if (totalGpuBytes_ > peakGpuBytes_) peakGpuBytes_ = totalGpuBytes_;
    stats_.uavsCreated++;
    return id;
}

void ComputeManager::freeUAV(u32 id) {
    if (id == kUavInvalid || id >= uavResources_.size()) return;
    UAVResource& res = uavResources_[id];
    if (!res.inUse) return;

    if (id < uavBuffers_.size() && uavBuffers_[id]) {
        Gl::DeleteBuffers(1, &uavBuffers_[id]);
        uavBuffers_[id] = 0;
    }
    if (res.sizeBytes <= totalGpuBytes_) totalGpuBytes_ -= res.sizeBytes;
    res.sizeBytes = 0;
    res.boundSlot = 0;
    res.inUse = false;
}

const ComputeManager::UAVResource& ComputeManager::getUAV(u32 id) const {
    static const UAVResource invalid = { kUavInvalid, 0, false, 0 };
    if (id == kUavInvalid || id >= uavResources_.size()) return invalid;
    const UAVResource& res = uavResources_[id];
    if (!res.inUse) return invalid;
    return res;
}

u32 ComputeManager::getPoolSize() const {
    return static_cast<u32>(uavResources_.size());
}

u32 ComputeManager::getTotalGpuBytes() const {
    return totalGpuBytes_;
}

u32 ComputeManager::getPeakGpuBytes() const {
    return peakGpuBytes_;
}

void ComputeManager::resetPeakTracking() {
    peakGpuBytes_ = totalGpuBytes_;
}

void ComputeManager::dispatchCompute(u32 shaderId, u32 gx, u32 gy, u32 gz) {
    if (shaderId >= shaderCount_) {
        FROST_LOG_ERROR("[ComputeManager] dispatchCompute: invalid shader id %u", shaderId);
        return;
    }
    if (gx == 0 || gy == 0 || gz == 0) {
        FROST_LOG_ERROR("[ComputeManager] dispatchCompute: work group counts must be > 0");
        return;
    }
    pendingDispatches_.pushBack(PendingDispatch{shaderId, gx, gy, gz});
}

u32 ComputeManager::flushDispatches() {
    if (pendingDispatches_.size() == 0) return 0;

    for (usize i = 0; i < uavResources_.size(); i++) {
        if (uavResources_[i].inUse && i < uavBuffers_.size() && uavBuffers_[i]) {
            Gl::BindBufferBase(0x90D2, uavResources_[i].boundSlot, uavBuffers_[i]);
        }
    }

    auto start = std::chrono::high_resolution_clock::now();
    u32 flushed = 0;
    for (usize i = 0; i < pendingDispatches_.size(); i++) {
        const PendingDispatch& pd = pendingDispatches_[i];
        if (pd.shaderId >= shaderCount_) continue;
        Gl::UseProgram(shaders_[pd.shaderId].program);
        Gl::DispatchCompute(pd.gx, pd.gy, pd.gz);
        Gl::MemoryBarrier(0x0040); // GL_SHADER_STORAGE_BARRIER_BIT
        dispatchCount_++;
        stats_.dispatches++;
        flushed++;
    }
    auto end = std::chrono::high_resolution_clock::now();
    f32 elapsedMs = std::chrono::duration<f32, std::milli>(end - start).count();
    dispatchTimeMs_ += elapsedMs;

    pendingDispatches_.clear();
    return flushed;
}

void ComputeManager::clearDispatches() {
    pendingDispatches_.clear();
}

void ComputeManager::beginFrame() {
    dispatchCount_ = 0;
    dispatchTimeMs_ = 0.0f;
}

void ComputeManager::endFrame() {
    if (pendingDispatches_.size() > 0) {
        flushDispatches();
    }
    dispatchTimeMs_ = Mathf::clamp(dispatchTimeMs_, 0.0f, 10000.0f);
}

u32 ComputeManager::getDispatchCount() const {
    return dispatchCount_;
}

f32 ComputeManager::getDispatchTimeMs() const {
    return dispatchTimeMs_;
}

} // namespace Frost
