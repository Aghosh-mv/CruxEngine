#include "Core/SystemManager.h"

#include <chrono>

namespace Frost {

using Clock = std::chrono::high_resolution_clock;
using Ms = std::chrono::duration<f32, std::milli>;

bool SystemManager::initialize() {
    if (initialized_) return true;

    FrostMonoWorld::Config monoCfg;
    world_.initialize(monoCfg);

    TimeConfig timeCfg;
    chronos_.initialize(timeCfg);

    Noema::Config noemaCfg;
    noema_.initialize(noemaCfg);

    SimLODSystem::Config simCfg;
    simLod_.initialize(simCfg);

    GridConfig gridCfg;
    worldPartition_.initialize(gridCfg);

    Renderer::OcclusionCullingSystem::Config occCfg;
    occlusion_.initialize(occCfg);

    zenith_.initialize(4, 1920, 1080, Renderer::SplitMode::Halves);

    initialized_ = true;
    return true;
}

void SystemManager::shutdown() {
    if (!initialized_) return;

    zenith_.shutdown();
    occlusion_.shutdown();
    worldPartition_.shutdown();
    noema_.shutdown();
    chronos_.shutdown();
    world_.shutdown();

    initialized_ = false;
}

bool SystemManager::isInitialized() const {
    return initialized_;
}

void SystemManager::update(f32 dt, u64 frameIndex) {
    if (!initialized_) return;

    auto simStart = Clock::now();
    u32 ticks = chronos_.update(dt);

    for (u32 i = 0; i < ticks; ++i) {
        chronos_.advanceTick();
        world_.update(dt, frameIndex);
        simLod_.update(dt);
        noema_.update(dt);
        worldPartition_.update(cameraPos_, frameIndex);
    }
    auto simEnd = Clock::now();
    stats_.simulationMs = Ms(simEnd - simStart).count();
    stats_.worldUpdateMs = 0.0f;
    stats_.ticksThisFrame = ticks;

    auto renderStart = Clock::now();
    occlusion_.beginFrame(frameIndex, nullptr);
    occlusion_.endFrame();
    zenith_.submitFrame(frameIndex, zenithRenderCallback, nullptr);
    zenith_.waitForFrame();
    auto renderEnd = Clock::now();
    stats_.renderMs = Ms(renderEnd - renderStart).count();
}

void SystemManager::zenithRenderCallback(const Renderer::RenderRegion&,
                                         u32, void*) {
}

} // namespace Frost
