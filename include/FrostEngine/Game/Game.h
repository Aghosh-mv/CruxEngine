#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Math.h"
#include "Core/Noise.h"
#include "Core/Random.h"
#include "Platform/Window.h"
#include "Renderer/Renderer.h"
#include "Renderer/Camera.h"
#include "Renderer/Material.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/WindField.h"
#include "Scene/Scene.h"

namespace Frost {

// Angin: a hang-glider flight game. Infinite procedural terrain, ridge lift
// and thermals, wind particles, orb pickups, and a lightweight crash/respawn
// loop. The terrain is sampled through pure world-coordinate callbacks so the
// renderer can stream chunks around the camera forever.
class Game {
public:
    bool init(Window& window, Renderer& renderer);
    void shutdown();

    void update(f32 dt);

    bool isRunning() const { return running_; }
    bool paused() const { return paused_; }
    void togglePause() { paused_ = !paused_; }

    // World callbacks shared with physics and the endless terrain renderer.
    static f32 worldHeight(const Vec3& pos);
    static f32 terrainHeightCb(f32 x, f32 z, void* user);
    static f32 terrainBiomeCb(f32 x, f32 z, void* user);

    static Game* s_instance;

private:
    struct Orb {
        Vec3 position{ 0, 0, 0 };
        f32 bobPhase = 0.0f;
        bool collected = false;
        i32 value = 100;
    };

    struct WindParticle {
        Vec3 pos{ 0, 0, 0 };
        Vec3 vel{ 0, 0, 0 };
        f32 life = 1.0f;
        f32 maxLife = 1.0f;
        f32 size = 1.0f;
    };

    // ---- lifecycle ----
    void generateTerrainHeightmap();
    void generateWorld();
    void spawnTrees();
    void spawnRocks();
    void spawnOrbs();
    void spawnThermals();
    void spawnWindParticles();

    // ---- gameplay ----
    void handleInput(f32 dt);
    void updateGlider(f32 dt);
    void updateCamera(f32 dt);
    void updatePickups(f32 dt);
    void updateParticles(f32 dt);
    void updateHud();
    void submitScene();
    void resetFlight();

    Window* window_ = nullptr;
    Renderer* renderer_ = nullptr;

    Camera camera_;
    Noise terrainNoise_;
    Random rng_;

    Texture2D heightMapTex_;
    Texture2D grassTex_, rockTex_, snowTex_;

    // Meshes / materials
    Mesh gliderMesh_, orbMesh_, treeMesh_, rockMesh_;
    Material gliderMat_, gliderFrameMat_, gliderSailMat_, orbMat_,
             treeMat_, leafMat_, rockMat_, cloudMat_;

    // Physics
    PhysicsWorld physics_;
    WindField wind_;

    // World data
    Vector<Vec3> treePositions_;
    Vector<Mat4> treeMatrices_, rockMatrices_;
    Vector<Orb> orbs_;
    Vector<Vec3> thermalPositions_;
    Vector<WindParticle> windParticles_;

    // Flight state
    Vec3 gliderPos_{ 0, 80, 0 };
    Vec3 gliderVel_{ 0, 0, 0 };
    Vec3 gliderAngVel_{ 0, 0, 0 };
    Quat gliderRot_{ Quat::identity() };

    Vec3 cameraPos_{ 0, 82, -9 };
    Vec3 cameraOffset_{ 0, 2.2f, -9.0f };

    f32 pitchInput_ = 0.0f;
    f32 rollInput_ = 0.0f;
    f32 yawInput_ = 0.0f;
    f32 throttleInput_ = 0.0f;

    f32 airspeed_ = 0.0f;
    f32 bankAngle_ = 0.0f;
    f32 angleOfAttack_ = 0.0f;
    f32 sinkRate_ = 0.0f;
    f32 flightTime_ = 0.0f;
    f32 crashTimer_ = 0.0f;
    f32 totalTime_ = 0.0f;
    bool crashed_ = false;
    bool running_ = true;
    bool paused_ = false;

    u32 score_ = 0;
    u32 orbsCollected_ = 0;

    static constexpr f32 kTerrainSize = 8000.0f;
    static constexpr f32 kHeightScale = 120.0f;
    static constexpr u32 kHeightRes = 513;
};

}
