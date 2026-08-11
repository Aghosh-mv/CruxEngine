#include "Renderer/GPUDriven.h"

#include <algorithm>
#include <chrono>

namespace Frost {

u32 GPUDrivenRenderer::addInstance(const GPUInstance& instance) {
    if (instances_.size() >= maxInstances_) return 0xFFFFFFFF;
    instances_.push_back(instance);
    return (u32)instances_.size() - 1;
}

void GPUDrivenRenderer::removeInstance(u32 index) {
    instances_.erase(index);
}

void GPUDrivenRenderer::updateInstanceTransform(u32 index, const Vec3& pos, const Vec3& scale) {
    if (index >= instances_.size()) return;
    instances_[index].position = pos;
    instances_[index].scale = scale;
}

u32 GPUDrivenRenderer::getInstanceCount() const {
    return (u32)instances_.size();
}

u32 GPUDrivenRenderer::cullInstances(const Vector<Vec4>& cameraPlanes, const Mat4& viewProj) {
    (void)viewProj;
    auto start = std::chrono::high_resolution_clock::now();

    visibleInstances_.clear();
    culledCount_ = 0;

    const usize count = instances_.size();
    for (usize i = 0; i < count; i++) {
        const GPUInstance& inst = instances_[i];
        bool visible = true;

        if (enableGpuCulling_) {
            const f32 radius = inst.scale.length();
            for (usize p = 0; p < cameraPlanes.size(); p++) {
                const Vec4& plane = cameraPlanes[p];
                const f32 d = plane.x * inst.position.x + plane.y * inst.position.y +
                              plane.z * inst.position.z + plane.w;
                if (d < -radius) {
                    visible = false;
                    break;
                }
            }
        }

        if (visible) {
            visibleInstances_.push_back((u32)i);
        } else {
            culledCount_++;
        }
    }

    totalCulled_ += culledCount_;

    auto end = std::chrono::high_resolution_clock::now();
    cullTimeMs_ = std::chrono::duration<f32, std::milli>(end - start).count();
    return (u32)visibleInstances_.size();
}

u32 GPUDrivenRenderer::buildIndirectCommands(const Mat4& viewProj) {
    indirectCommands_.clear();

    const usize n = visibleInstances_.size();
    if (n == 0) return 0;

    Vector<u32> sorted;
    sorted.reserve(n);
    for (usize i = 0; i < n; i++) sorted.push_back(visibleInstances_[i]);

    std::stable_sort(sorted.begin(), sorted.end(), [this](u32 a, u32 b) {
        const GPUInstance& ia = instances_[a];
        const GPUInstance& ib = instances_[b];
        if (ia.materialIndex != ib.materialIndex) return ia.materialIndex < ib.materialIndex;
        if (ia.meshIndex != ib.meshIndex) return ia.meshIndex < ib.meshIndex;
        return ia.lodLevel < ib.lodLevel;
    });

    for (usize i = 0; i < n; i++) visibleInstances_[i] = sorted[i];

    Vector<IndirectCommand> commands;
    Vector<f32> depths;

    usize runStart = 0;
    while (runStart < n) {
        const GPUInstance& first = instances_[visibleInstances_[runStart]];
        const u32 mat = first.materialIndex;
        const u32 mesh = first.meshIndex;

        usize runEnd = runStart + 1;
        while (runEnd < n) {
            const GPUInstance& cur = instances_[visibleInstances_[runEnd]];
            if (cur.materialIndex != mat || cur.meshIndex != mesh) break;
            runEnd++;
        }

        IndirectCommand cmd;
        cmd.vertexCount = 0;
        cmd.instanceCount = (u32)(runEnd - runStart);
        cmd.firstVertex = 0;
        cmd.firstInstance = (u32)runStart;
        commands.push_back(cmd);

        Vec3 center(0.0f, 0.0f, 0.0f);
        for (usize k = runStart; k < runEnd; k++) {
            center = center + instances_[visibleInstances_[k]].position;
        }
        center = center / (f32)(runEnd - runStart);
        const Vec4 clip = viewProj * Vec4(center, 1.0f);
        const f32 depth = clip.w > 0.0f ? clip.z / clip.w : clip.z;
        depths.push_back(depth);

        runStart = runEnd;
    }

    const u32 cmdCount = (u32)commands.size();
    Vector<u32> order;
    order.reserve(cmdCount);
    for (u32 i = 0; i < cmdCount; i++) order.push_back(i);

    std::stable_sort(order.begin(), order.end(), [&depths](u32 a, u32 b) {
        return depths[a] < depths[b];
    });

    indirectCommands_.reserve(cmdCount);
    for (u32 i = 0; i < cmdCount; i++) {
        indirectCommands_.push_back(commands[order[i]]);
    }

    return cmdCount;
}

u32 GPUDrivenRenderer::countCulled() const {
    return culledCount_;
}

u32 GPUDrivenRenderer::getCulledTotal() const {
    return totalCulled_;
}

f32 GPUDrivenRenderer::getCullTimeMs() const {
    return cullTimeMs_;
}

const Vector<u32>& GPUDrivenRenderer::getVisibleInstances() const {
    return visibleInstances_;
}

const Vector<IndirectCommand>& GPUDrivenRenderer::getIndirectCommands() const {
    return indirectCommands_;
}

void GPUDrivenRenderer::setGpuCulling(bool enabled) {
    enableGpuCulling_ = enabled;
}

bool GPUDrivenRenderer::isGpuCullingEnabled() const {
    return enableGpuCulling_;
}

void GPUDrivenRenderer::resetCullingStats() {
    culledCount_ = 0;
    totalCulled_ = 0;
    cullTimeMs_ = 0.0f;
}

void GPUDrivenRenderer::clearInstances() {
    instances_.clear();
    visibleInstances_.clear();
    indirectCommands_.clear();
    culledCount_ = 0;
}

} // namespace Frost
