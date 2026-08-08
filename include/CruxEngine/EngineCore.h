#pragma once

// ============================================================================
// CruxEngine Core — Main engine facade that wires all systems together
// ============================================================================
// This is the entry point for anyone building a game with CruxEngine.
// It owns: ECS world, scene graph, resource registry, rendering subsystems,
// input, physics, and all revolutionary tech systems.
//
// Usage:
//   EngineCore engine;
//   engine.init(1920, 1080, "My Game");
//   while (engine.running()) {
//       engine.beginFrame();
//       engine.update(dt);
//       engine.render();
//       engine.endFrame();
//   }
//   engine.shutdown();
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Math.h"
#include "Core/ECS.h"
#include "Scene/SceneGraph.h"
#include "Assets/ResourceSystem.h"
#include "Assets/ProceduralAssetSynthesis.h"
#include "Input/Input.h"
#include "Renderer/SVOR.h"
#include "Renderer/TCSM.h"
#include "Renderer/NRC.h"
#include "Renderer/GPUDriven.h"
#include "Renderer/BindlessMaterial.h"
#include "Renderer/SpectralRendering.h"

namespace Crux {

// ---- Engine configuration ----
struct EngineConfig {
    u32 windowWidth = 1920;
    u32 windowHeight = 1080;
    const char* windowTitle = "CruxEngine";
    bool fullscreen = false;
    bool hdr = true;
    bool vsync = true;
    u32 msaaSamples = 4;
    u32 maxFPS = 0; // 0 = uncapped

    // Rendering features
    bool enableSVOR = true;         // Sparse Voxel Octree Radiance (GI)
    bool enableTCSM = true;         // Temporal Cascade Shadow Maps
    bool enableNRC = true;          // Neural Radiance Caching
    bool enableGPUDriven = true;    // GPU-Driven Rendering
    bool enableBindless = true;     // Bindless Materials
    bool enableSpectral = false;    // Spectral Rendering (expensive)
    bool enablePAS = true;          // Procedural Asset Synthesis

    // World
    f32 worldSize = 1024.0f;       // half-size of the world
    u32 terrainResolution = 256;   // terrain vertices per side
};

// ---- Engine statistics ----
struct EngineStats {
    f32 frameTimeMs = 0;
    u32 fps = 0;
    u32 entityCount = 0;
    u32 drawCalls = 0;
    u32 triangles = 0;
    u32 texturesLoaded = 0;
    u64 gpuMemoryBytes = 0;

    // Revolutionary tech stats
    f32 svorVoxels = 0;
    f32 tcsmReprojectionRatio = 0;
    f32 nrcConvergence = 0;
    u32 gpuDrivenCulledCount = 0;
    u32 materialCount = 0;
};

// ---- EngineCore: the complete engine ----
class EngineCore {
public:
    EngineCore() = default;

    bool init(const EngineConfig& config = EngineConfig()) {
        config_ = config;

        // Initialize ECS
        world_ = new World();
        systems_ = new SystemManager();

        // Initialize scene graph
        scene_ = new Scene();

        // Initialize resource system
        resources_ = new ResourceRegistry();
        loader_ = new AsyncResourceLoader();

        // Initialize procedural synthesis
        pas_ = new ProceduralAssetSynthesis();

        // Initialize revolutionary rendering tech
        if (config.enableSVOR) {
            svor_ = new SVORSystem();
            svor_->setWorldSize(config.worldSize);
        }
        if (config.enableTCSM) {
            tcsm_ = new TCSMSystem();
            tcsm_->init(2048, 2);
        }
        if (config.enableNRC) {
            nrc_ = new NRCSystem();
            nrc_->init();
        }
        if (config.enableGPUDriven) {
            gpuDriven_ = new GPUDrivenRenderer();
            gpuDriven_->init();
        }
        if (config.enableBindless) {
            materials_ = new BindlessMaterialSystem();
            materials_->init();
        }

        // Initialize input
        input_ = new InputState();

        initialized_ = true;
        return true;
    }

    void shutdown() {
        if (!initialized_) return;

        if (nrc_) { nrc_->shutdown(); delete nrc_; }
        if (tcsm_) { tcsm_->shutdown(); delete tcsm_; }
        delete svor_; delete gpuDriven_; delete materials_;
        delete pas_; delete loader_; delete resources_;
        delete scene_; delete world_; delete systems_;
        delete input_;
        initialized_ = false;
    }

    // ---- Frame lifecycle ----
    void beginFrame() {
        input_->newFrame();
        world_->beginFrame();
        frameStartTime_ = getCurrentTime();
    }

    void update(f32 dt) {
        if (!initialized_) return;
        systems_->updateAll(*world_, dt);
        scene_->propagateDirty();

        // Update revolutionary tech
        if (svor_) {
            // Get sun direction and color from the scene
            svor_->update(0.5f, -0.8f, -0.3f, 1.0f, 0.96f, 0.9f);
        }
        if (nrc_) {
            // Train the neural radiance cache
            nrc_->train();
        }
    }

    void render() {
        if (!initialized_) return;

        // Update GPU material buffer
        if (materials_ && materials_->dirty()) {
            materials_->uploadToGPU();
        }

        // GPU-driven culling (CPU fallback path)
        if (gpuDriven_) {
            gpuDriven_->clear();
            // Submit objects from the scene (placeholder: terrain chunks + entities)
        }

        // TCSM temporal reprojection
        if (tcsm_) {
            for (u32 c = 0; c < tcsm_->cascadeCount(); c++) {
                tcsm_->reproject(c);
            }
        }
    }

    void endFrame() {
        f32 now = getCurrentTime();
        stats_.frameTimeMs = (now - frameStartTime_) * 1000.0f;
        stats_.fps = (stats_.frameTimeMs > 0) ? (u32)(1000.0f / stats_.frameTimeMs) : 0;
        stats_.entityCount = world_->entityCount();
        if (gpuDriven_) stats_.gpuDrivenCulledCount = gpuDriven_->objectCount() - gpuDriven_->drawCount();
        if (svor_) stats_.svorVoxels = (f32)svor_->voxelCount();
        if (nrc_) stats_.nrcConvergence = nrc_->convergenceEstimate();
        if (materials_) stats_.materialCount = materials_->materialCount();
        if (resources_) stats_.gpuMemoryBytes = resources_->totalGpuMemory();
    }

    bool running() const { return initialized_; }

    // ---- Accessors ----
    World& world() { return *world_; }
    Scene& scene() { return *scene_; }
    ResourceRegistry& resources() { return *resources_; }
    InputState& input() { return *input_; }
    const EngineStats& stats() const { return stats_; }
    const EngineConfig& config() const { return config_; }

    SVORSystem* svor() const { return svor_; }
    TCSMSystem* tcsm() const { return tcsm_; }
    NRCSystem* nrc() const { return nrc_; }
    GPUDrivenRenderer* gpuDriven() const { return gpuDriven_; }
    BindlessMaterialSystem* materials() const { return materials_; }
    ProceduralAssetSynthesis* pas() const { return pas_; }

    void addSystem(System* sys) { systems_->addSystem(sys); }

private:
    f32 getCurrentTime() const {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (f32)ts.tv_sec + (f32)ts.tv_nsec / 1e9f;
    }

    EngineConfig config_;
    EngineStats stats_;
    bool initialized_ = false;
    f32 frameStartTime_ = 0;

    World* world_ = nullptr;
    SystemManager* systems_ = nullptr;
    Scene* scene_ = nullptr;
    ResourceRegistry* resources_ = nullptr;
    AsyncResourceLoader* loader_ = nullptr;
    InputState* input_ = nullptr;

    // Revolutionary tech
    SVORSystem* svor_ = nullptr;
    TCSMSystem* tcsm_ = nullptr;
    NRCSystem* nrc_ = nullptr;
    GPUDrivenRenderer* gpuDriven_ = nullptr;
    BindlessMaterialSystem* materials_ = nullptr;
    ProceduralAssetSynthesis* pas_ = nullptr;
};

} // namespace Crux
