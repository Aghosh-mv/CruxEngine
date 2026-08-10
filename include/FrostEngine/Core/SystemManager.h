#pragma once

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/FrostMono.h"
#include "Core/Chronos.h"
#include "Core/Noema.h"
#include "Simulation/SimLOD.h"
#include "Scene/WorldPartition.h"
#include "Renderer/OcclusionCulling.h"
#include "Renderer/FrostZenith.h"

#include <chrono>

namespace Frost {

class SystemManager {
public:
    struct Stats {
        f32 worldUpdateMs = 0.0f;
        f32 simulationMs = 0.0f;
        f32 renderMs = 0.0f;
        u32 ticksThisFrame = 0;
    };

    SystemManager() = default;
    ~SystemManager() = default;

    bool initialize();
    void shutdown();
    void update(f32 dt, u64 frameIndex);

    bool isInitialized() const;

    FrostMonoWorld& world() { return world_; }
    Chronos& chronos() { return chronos_; }
    Noema& noema() { return noema_; }
    SimLODSystem& simLod() { return simLod_; }
    WorldPartition& worldPartition() { return worldPartition_; }
    Renderer::OcclusionCullingSystem& occlusion() { return occlusion_; }
    Renderer::FrostZenith& zenith() { return zenith_; }

    void setCameraPosition(const Vec3& pos) { cameraPos_ = pos; }
    const Vec3& getCameraPosition() const { return cameraPos_; }

    const Stats& getStats() const { return stats_; }

private:
    static void zenithRenderCallback(const Renderer::RenderRegion& region,
                                     u32 workerId, void* userData);

    FrostMonoWorld world_;
    Chronos chronos_;
    Noema noema_;
    SimLODSystem simLod_;
    WorldPartition worldPartition_;
    Renderer::OcclusionCullingSystem occlusion_;
    Renderer::FrostZenith zenith_;

    Vec3 cameraPos_;
    Stats stats_{};
    bool initialized_ = false;
};

} // namespace Frost
