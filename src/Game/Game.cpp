#include "Game/Game.h"
#include "Renderer/Gl.h"
#include "Assets/MeshFactory.h"
#include "Assets/TextureFactory.h"
#include "Core/Log.h"
#include <cstdio>
#include <ctime>
#include <algorithm>

namespace Crux {

Game* Game::s_instance = nullptr;

// ---------------------------------------------------------------------------
// World height function (static for physics callbacks)
// ---------------------------------------------------------------------------
f32 Game::worldHeight(const Vec3& pos) {
    if (!s_instance) return 0.0f;
    Game& g = *s_instance;
    f32 nx = pos.x * 0.008f;
    f32 nz = pos.z * 0.008f;

    f32 hills = g.terrainNoise_.fbm2(nx, nz, 5);
    f32 h = hills * 0.55f;

    f32 ridge = g.terrainNoise_.ridged(nx * 0.5f + 30.0f, nz * 0.5f, 4);
    f32 mountainMask = Mathf::smoothstep(0.45f, 0.75f, hills);
    h += ridge * 0.85f * mountainMask;

    f32 d = std::sqrt(pos.x * pos.x + pos.z * pos.z);
    f32 flatMask = 1.0f - Mathf::smoothstep(0.0f, 60.0f, d);
    h *= 1.0f - flatMask * 0.75f;

    // A ridge line to fly along, generating strong ridge lift
    f32 ridgeLift = g.terrainNoise_.ridged(pos.x * 0.01f + 80.0f, pos.z * 0.01f, 3);
    f32 ridgeMask = Mathf::smoothstep(0.55f, 0.8f, ridgeLift);
    h += ridgeLift * 0.5f * ridgeMask;

    f32 clamped = Mathf::clamp(h, 0.0f, 1.0f);
    return clamped * kHeightScale;
}

// Terrain renderer callbacks: pure functions of world coords so the renderer
// can stream chunks anywhere in the world.
f32 Game::terrainHeightCb(f32 x, f32 z, void* user) {
    Game& g = *(Game*)user;
    return g.worldHeight(Vec3(x, 0, z));
}

f32 Game::terrainBiomeCb(f32 x, f32 z, void* user) {
    Game& g = *(Game*)user;
    f32 b = g.terrainNoise_.fbm2(x * 0.0015f + 100.0f, z * 0.0015f + 200.0f, 3);
    return b * 6.0f;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
bool Game::init(Window& window, Renderer& renderer) {
    window_ = &window;
    renderer_ = &renderer;
    s_instance = this;

    // --- Textures
    TextureFactory tf;
    grassTex_ = tf.makeGrass();
    rockTex_ = tf.makeRock();
    snowTex_ = tf.makeSnow();

    // --- Heightmap
    generateTerrainHeightmap();

    // --- Materials
    gliderMat_ = Material::aluminum();
    gliderFrameMat_ = Material::titanium();
    gliderSailMat_ = Material::rubber();
    gliderSailMat_.baseColor = Color(0.05f, 0.55f, 0.85f, 1.0f);
    gliderSailMat_.roughness = 0.7f;
    gliderSailMat_.doubleSided = true;

    orbMat_ = Material::emerald();
    orbMat_.emission = Color(0.1f, 1.0f, 0.6f, 1.0f);
    orbMat_.emissionStrength = 1.8f;

    treeMat_ = Material::oak();
    leafMat_ = Material::leaf();
    rockMat_ = Material::granite();
    cloudMat_ = Material::ice();
    cloudMat_.emission = Color(1, 1, 1, 1);
    cloudMat_.emissionStrength = 0.4f;

    // --- Meshes
    gliderMesh_ = MeshFactory::makeConeTrunk(1.0f, 0.4f, 2.6f, 6);      // sail
    orbMesh_ = MeshFactory::makeOctahedron(0.6f);
    treeMesh_ = MeshFactory::makeConeTrunk(0.35f, 0.15f, 2.5f, 8);      // trunk
    rockMesh_ = MeshFactory::makeIcosphere(1.0f, 1);

    // --- Renderer scene setup
    renderer_->setTerrainGenerator(Game::terrainHeightCb, Game::terrainBiomeCb, this,
                                  1.0f, 0.0f, grassTex_, rockTex_, snowTex_,
                                  12.0f, 70.0f, 0.28f);
    renderer_->setWaterLevel(9.0f);
    renderer_->setWaterParams(Color(0.08f, 0.32f, 0.42f, 1.0f),
                             Color(0.04f, 0.15f, 0.28f, 1.0f), 1.2f);
    renderer_->setFog(0.0008f, Color(0.62f, 0.71f, 0.82f, 1.0f));
    renderer_->setAmbient(Color(0.6f, 0.72f, 0.92f, 1.0f),
                         Color(0.4f, 0.38f, 0.34f, 1.0f), 0.7f);
    renderer_->setExposure(2.5f);
    renderer_->setBloom(0.35f);
    renderer_->setVignette(0.18f);

    Light sun = Light::directional(Vec3(0.55f, -0.75f, -0.36f).normalized(),
                                   Color(1.0f, 0.94f, 0.86f), 3.2f);
    sun.castShadow = true;
    renderer_->setSun(sun);

    renderer_->addPointLight(Light::point(Vec3(0, 10, 0), Color(1.0f, 0.85f, 0.7f), 6.0f, 60.0f));

    // --- Camera
    camera_.setPerspective(70.0f, (f32)window.width() / (f32)window.height(), 0.5f, 1200.0f);

    // --- Physics
    physics_.setHeightFunction(Game::worldHeight, 0.0f);
    physics_.setGravity(9.81f);
    physics_.setWind(&wind_);

    // --- Wind model
    wind_.setBaseWind(Vec3(1, 0.05f, 0.3f).normalized(), 7.0f, 0.7f);

    // --- World
    generateWorld();

    cameraPos_ = gliderPos_ + Vec3(0, 2.2f, -9.0f);
    camera_.setPosition(cameraPos_);
    camera_.lookAt(gliderPos_);
    renderer_->setCamera(camera_);

    CRUX_LOG_INFO("[Game] Angin ready. Score targets: catch the green orbs!");
    return true;
}

void Game::shutdown() {
    s_instance = nullptr;
}

void Game::generateTerrainHeightmap() {
    const u32 res = kHeightRes;
    f32* heights = new f32[res * res];
    f32 minV = 1e9f, maxV = -1e9f;
    for (u32 z = 0; z < res; z++) {
        for (u32 x = 0; x < res; x++) {
            f32 wx = (f32)x / (f32)(res - 1) * kTerrainSize - kTerrainSize * 0.5f;
            f32 wz = (f32)z / (f32)(res - 1) * kTerrainSize - kTerrainSize * 0.5f;
            f32 h = worldHeight(Vec3(wx, 0, wz)) / kHeightScale;
            heights[z * res + x] = h;
            if (h < minV) minV = h;
            if (h > maxV) maxV = h;
        }
    }
    // Normalize to [0,1]
    f32 span = maxV - minV;
    u8* data = new u8[res * res * 4];
    for (u32 i = 0; i < res * res; i++) {
        f32 n = span > 1e-6f ? (heights[i] - minV) / span : 0.0f;
        u8 v = (u8)(Mathf::clamp(n, 0.0f, 1.0f) * 255.0f);
        data[i * 4] = v;
        data[i * 4 + 1] = v;
        data[i * 4 + 2] = v;
        data[i * 4 + 3] = 255;
    }
    heightMapTex_.create(res, res, data, true, TextureFilter::Trilinear, TextureWrap::Clamp);
    delete[] heights;
    delete[] data;
}

void Game::generateWorld() {
    spawnTrees();
    spawnRocks();
    spawnOrbs();
    spawnThermals();
    spawnWindParticles();
}

void Game::spawnTrees() {
    Random rng(777);
    treePositions_.clear();
    treeMatrices_.clear();
    for (u32 i = 0; i < 400; i++) {
        f32 x = rng.range(-280.0f, 280.0f);
        f32 z = rng.range(-280.0f, 280.0f);
        f32 h = worldHeight(Vec3(x, 0, z));
        // Keep trees on gentle, green terrain, away from the spawn pad
        Vec3 n = physics_.groundNormalAt(Vec3(x, 0, z));
        if (n.y < 0.85f) continue;                       // too steep
        if (h < 12.0f) continue;                         // in water
        f32 d = std::sqrt(x * x + z * z);
        if (d < 25.0f) continue;                         // clear spawn
        f32 s = rng.range(0.8f, 1.6f);
        Mat4 m = Mat4::translation(Vec3(x, h, z)) * Mat4::scaling(Vec3(s, s, s));
        treeMatrices_.pushBack(m);
        treePositions_.pushBack(Vec3(x, h, z));
    }
    CRUX_LOG_INFO("[World] spawned %zu trees", (usize)treeMatrices_.size());
}

void Game::spawnRocks() {
    Random rng(999);
    rockMatrices_.clear();
    for (u32 i = 0; i < 120; i++) {
        f32 x = rng.range(-290.0f, 290.0f);
        f32 z = rng.range(-290.0f, 290.0f);
        f32 h = worldHeight(Vec3(x, 0, z));
        if (h < 12.0f) continue;
        f32 d = std::sqrt(x * x + z * z);
        if (d < 20.0f) continue;
        f32 s = rng.range(0.5f, 2.8f);
        Quat rot(Quat::fromEuler(Vec3(rng.range(-0.5f, 0.5f), rng.range(0, 6.28f),
                                      rng.range(-0.5f, 0.5f))));
        Mat4 m = Mat4::translation(Vec3(x, h - s * 0.2f, z)) *
                 quatToMat4(rot) * Mat4::scaling(Vec3(s, s * 0.7f, s));
        rockMatrices_.pushBack(m);
    }
    CRUX_LOG_INFO("[World] spawned %zu rocks", (usize)rockMatrices_.size());
}

void Game::spawnOrbs() {
    Random rng(555);
    orbs_.clear();
    for (u32 i = 0; i < 14; i++) {
        f32 x = rng.range(-240.0f, 240.0f);
        f32 z = rng.range(-240.0f, 240.0f);
        f32 h = worldHeight(Vec3(x, 0, z));
        if (h < 14.0f) continue;
        Orb o;
        o.position = Vec3(x, h + rng.range(18.0f, 46.0f), z);
        o.bobPhase = rng.range(0.0f, 6.28f);
        o.collected = false;
        o.value = 100;
        orbs_.pushBack(o);
    }
    CRUX_LOG_INFO("[World] spawned %zu orbs", (usize)orbs_.size());
}

void Game::spawnThermals() {
    Random rng(313);
    thermalPositions_.clear();
    Vec3 thermalSpots[4] = {
        Vec3(-180, 0, 60),
        Vec3(150, 0, -120),
        Vec3(60, 0, 200),
        Vec3(-40, 0, -220),
    };
    for (u32 i = 0; i < 4; i++) {
        Vec3 p = thermalSpots[i] + Vec3(rng.range(-30, 30), 0, rng.range(-30, 30));
        f32 strength = rng.range(7.0f, 11.0f);
        wind_.addThermal(p, 26.0f, strength);
        thermalPositions_.pushBack(p);
    }
}

void Game::spawnWindParticles() {
    Random rng(2024);
    windParticles_.clear();
    for (u32 i = 0; i < 220; i++) {
        WindParticle p;
        p.pos = Vec3(rng.range(-300.0f, 300.0f), rng.range(5.0f, 160.0f),
                     rng.range(-300.0f, 300.0f));
        p.vel = Vec3(0, 0, 0);
        p.life = rng.range(1.0f, 4.0f);
        p.maxLife = p.life;
        p.size = rng.range(0.8f, 2.2f);
        windParticles_.pushBack(p);
    }
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void Game::update(f32 dt) {
    if (!running_) return;
    totalTime_ += dt;
    if (paused_) return;

    // Fixed-step physics for stability
    const f32 fixedDt = 1.0f / 60.0f;
    f32 accumulator = 0.0f;
    accumulator += std::min(dt, 0.05f);
    while (accumulator >= fixedDt) {
        physics_.step(fixedDt, totalTime_);
        accumulator -= fixedDt;
    }

    if (crashed_) {
        crashTimer_ -= dt;
        if (crashTimer_ <= 0.0f) {
            resetFlight();
        }
    } else {
        flightTime_ += dt;
    }

    handleInput(dt);
    updateGlider(dt);
    updateCamera(dt);
    updatePickups(dt);
    updateParticles(dt);
    updateHud();
    submitScene();
}

void Game::handleInput(f32 dt) {
    // Smooth control inputs
    f32 targetPitch = 0, targetRoll = 0, targetYaw = 0, targetThrottle = 0;
    if (window_->isKeyPressed(Key::W)) targetPitch = -1.0f;
    if (window_->isKeyPressed(Key::S)) targetPitch = 1.0f;
    if (window_->isKeyPressed(Key::A)) targetRoll = 1.0f;
    if (window_->isKeyPressed(Key::D)) targetRoll = -1.0f;
    if (window_->isKeyPressed(Key::Q)) targetYaw = 1.0f;
    if (window_->isKeyPressed(Key::E)) targetYaw = -1.0f;
    if (window_->isKeyPressed(Key::LeftShift) || window_->isKeyPressed(Key::RightShift))
        targetThrottle = 1.0f;
    if (window_->isKeyJustPressed(Key::R)) resetFlight();

    const f32 k = 1.0f - std::exp(-6.0f * dt);
    pitchInput_ += (targetPitch - pitchInput_) * k;
    rollInput_ += (targetRoll - rollInput_) * k;
    yawInput_ += (targetYaw - yawInput_) * k;
    throttleInput_ += (targetThrottle - throttleInput_) * k;

    if (window_->isKeyJustPressed(Key::Escape)) running_ = false;
    if (window_->isKeyJustPressed(Key::P)) togglePause();
}

void Game::updateGlider(f32 dt) {
    // Air density falls with altitude
    f32 rho = 1.225f * std::exp(-std::max(gliderPos_.y, 0.0f) / 8500.0f);
    f32 m = 72.0f;
    Vec3 gravity = Vec3(0, -9.81f * m, 0);

    // Wind velocity at the glider
    Vec3 wind = wind_.sample(gliderPos_, totalTime_);

    // Relative airflow (what the wing sees)
    Vec3 airflow = wind - gliderVel_;

    // Wing force
    Vec3 fwd = gliderRot_.forward();
    Vec3 up = gliderRot_.up();
    AeroSurface wing;
    wing.set(8.2f, 1.7f, 1.35f, 14.0f);
    Vec3 aero;
    f32 aoa;
    wing.computeForce(airflow, up, fwd, rho, aero, aoa);
    angleOfAttack_ = Mathf::degrees(aoa);

    // Fuselage + tail drag (parasitic)
    f32 fuselageDrag = airflow.lengthSquared() * 0.35f * rho * 0.5f;
    Vec3 drag = airflow.normalized() * -fuselageDrag;

    // Thrust (small boost motor)
    Vec3 thrust = fwd * throttleInput_ * 22.0f * m;

    Vec3 total = gravity + aero + drag + thrust;
    Vec3 accel = total / m;

    // --- Rotation control
    f32 pitchRate = 1.6f;
    f32 rollRate = 2.2f;
    f32 yawRate = 0.9f;

    // Pitch rotates around local right axis
    Vec3 localRight = gliderRot_.right();
    Vec3 pitchTorque = localRight * (pitchInput_ * pitchRate);
    Vec3 rollTorque = fwd * (rollInput_ * rollRate);
    Vec3 yawTorque = up * (yawInput_ * yawRate);

    gliderAngVel_ += (pitchTorque + rollTorque + yawTorque) * dt;

    // Damping: return toward velocity alignment (stability)
    Vec3 velDir = gliderVel_.length() > 0.1f ? gliderVel_.normalized() : fwd;
    f32 stability = 1.2f;
    Vec3 desiredUp = velDir.length() > 0.1f ? Vec3(0, 1, 0) : up;
    (void)desiredUp;

    gliderAngVel_ *= (1.0f - 1.8f * dt);

    // Integrate orientation
    if (gliderAngVel_.lengthSquared() > 1e-8f) {
        f32 angle = gliderAngVel_.length();
        Quat dq(gliderAngVel_.normalized(), angle * dt);
        gliderRot_ = (dq * gliderRot_).normalized();
    }

    // Slight self-righting when no input (hands off returns to level)
    Vec3 euler = gliderRot_.euler();
    f32 levelStrength = (std::abs(rollInput_) < 0.05f) ? 0.25f : 0.0f;
    Quat correction = Quat::fromEuler(Vec3(-euler.x * levelStrength * dt,
                                           -euler.y * 0.1f * dt,
                                           -euler.z * levelStrength * dt));
    gliderRot_ = (correction * gliderRot_).normalized();

    // Integrate velocity
    gliderVel_ += accel * dt;

    // Position
    gliderPos_ += gliderVel_ * dt;

    airspeed_ = (gliderVel_ - wind).length();
    bankAngle_ = gliderRot_.euler().z;

    // --- Ground / water collision
    f32 ground = worldHeight(gliderPos_);
    f32 sinkNow = gliderPos_.y - ground;
    sinkRate_ = sinkNow < 0.1f ? -gliderVel_.y : 0.0f;

    if (sinkNow <= 0.0f || gliderPos_.y < 9.0f) {
        f32 impactSpeed = Mathf::abs(gliderVel_.y);
        f32 horizontal = Mathf::sqrt(gliderVel_.x * gliderVel_.x + gliderVel_.z * gliderVel_.z);
        if (impactSpeed > 6.0f || horizontal > 16.0f) {
            crashed_ = true;
            crashTimer_ = 3.0f;
        } else {
            // Gentle touchdown: stop horizontal drift
            gliderPos_.y = ground + 0.5f;
            gliderVel_.y = 0;
            gliderVel_.x *= 0.8f;
            gliderVel_.z *= 0.8f;
            if (window_->isKeyJustPressed(Key::Space)) {
                gliderVel_ = fwd * 14.0f;
                gliderVel_.y = 4.0f;
            }
        }
    }

    // Hard ceiling
    if (gliderPos_.y > 320.0f) {
        gliderPos_.y = 320.0f;
        gliderVel_.y = Mathf::min(gliderVel_.y, 0.0f);
    }
}

void Game::updateCamera(f32 dt) {
    // Demo orbit camera: always orbit the world so the scene is always visible
    f32 t = totalTime_ * 0.12f;
    f32 radius = 300.0f;
    f32 cx = std::cos(t) * radius;
    f32 cz = std::sin(t) * radius;
    f32 groundH = worldHeight(Vec3(cx, 0, cz));
    f32 cy = Mathf::max(groundH + 80.0f, 120.0f);
    cameraPos_ = Vec3(cx, cy, cz);
    camera_.setPosition(cameraPos_);
    Vec3 lookTarget = Vec3(0, worldHeight(Vec3(0, 0, 0)) + 40.0f, 0);
    camera_.lookAt(lookTarget);
    f32 fov = 70.0f;
    camera_.setPerspective(fov, (f32)window_->width() / (f32)window_->height(), 0.5f, 3000.0f);
    renderer_->setCamera(camera_);
}

void Game::updatePickups(f32 dt) {
    for (Orb& o : orbs_) {
        if (o.collected) continue;
        o.bobPhase += dt * 2.0f;
        Vec3 p = o.position + Vec3(0, std::sin(o.bobPhase) * 0.5f, 0);
        Vec3 d = gliderPos_ - p;
        if (d.length() < 2.6f) {
            o.collected = true;
            score_ += o.value;
            orbsCollected_++;
            // Respawn after a delay: replace with a new random spot
            Random rng(clock() & 0xFFFF);
            f32 x = rng.range(-240.0f, 240.0f);
            f32 z = rng.range(-240.0f, 240.0f);
            f32 h = worldHeight(Vec3(x, 0, z));
            o.position = Vec3(x, h + rng.range(18.0f, 46.0f), z);
            o.collected = false;
        }
    }
}

void Game::updateParticles(f32 dt) {
    for (WindParticle& p : windParticles_) {
        p.life -= dt;
        // Advect by wind + slight noise
        Vec3 wind = wind_.sample(p.pos, totalTime_);
        p.vel += (wind - p.vel) * (1.0f - std::exp(-1.2f * dt));
        p.pos += p.vel * dt;
        if (p.pos.y > 170.0f) { p.pos.y = 8.0f; p.vel = Vec3(0); }
        if (p.pos.y < 5.0f) { p.pos.y = 170.0f; p.vel = Vec3(0); }
        if (p.life <= 0.0f) {
            Random rng(clock() & 0xFFFF);
            p.pos = Vec3(rng.range(-300.0f, 300.0f), rng.range(8.0f, 160.0f),
                         rng.range(-300.0f, 300.0f));
            p.life = rng.range(1.0f, 4.0f);
            p.maxLife = p.life;
            p.vel = Vec3(0);
        }
    }
}

// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------
void Game::updateHud() {
    char buf[128];

    Color white(1, 1, 1, 1);
    Color accent(0.2f, 0.9f, 0.7f, 1.0f);
    Color warn(1.0f, 0.3f, 0.25f, 1.0f);
    Color dim(0.85f, 0.9f, 1.0f, 0.85f);

    f32 s = 1.4f;

    std::snprintf(buf, sizeof(buf), "ANGIN");
    renderer_->drawText(buf, 16, 10, s * 1.6f, accent);

    std::snprintf(buf, sizeof(buf), "Score: %u", score_);
    renderer_->drawText(buf, 16, 42, s, white);

    std::snprintf(buf, sizeof(buf), "Orbs: %u", orbsCollected_);
    renderer_->drawText(buf, 16, 66, s, white);

    std::snprintf(buf, sizeof(buf), "Altitude: %.0f m", gliderPos_.y);
    renderer_->drawText(buf, 16, 90, s, white);

    std::snprintf(buf, sizeof(buf), "Airspeed: %.0f m/s", airspeed_);
    renderer_->drawText(buf, 16, 114, s, white);

    std::snprintf(buf, sizeof(buf), "Angle of attack: %+.1f deg", angleOfAttack_);
    renderer_->drawText(buf, 16, 138, s, dim);

    std::snprintf(buf, sizeof(buf), "Sink: %+.1f m/s", sinkRate_);
    Color sinkCol = (sinkRate_ > 4.0f) ? warn : dim;
    renderer_->drawText(buf, 16, 162, s, sinkCol);

    std::snprintf(buf, sizeof(buf), "Time: %.1f s", flightTime_);
    renderer_->drawText(buf, 16, 186, s, dim);

    // Wind strength indicator (top right)
    Vec3 windHere = wind_.sample(gliderPos_, totalTime_);
    f32 windKph = windHere.length() * 3.6f;
    std::snprintf(buf, sizeof(buf), "Wind: %.0f km/h", windKph);
    renderer_->drawText(buf, (f32)window_->width() - 200, 16, s, windKph > 30.0f ? warn : white);

    // Controls
    renderer_->drawText("W/S pitch   A/D roll   Q/E yaw   Shift boost   R reset   P pause   Esc quit",
                        16, (f32)window_->height() - 34, 1.0f, dim);

    if (crashed_) {
        std::snprintf(buf, sizeof(buf), "CRASHED - respawning in %.1f...", std::max(crashTimer_, 0.0f));
        renderer_->drawText(buf, (f32)window_->width() * 0.5f - 160, (f32)window_->height() * 0.5f, 2.0f, warn);
    }

    if (gliderPos_.y < 25.0f && !crashed_) {
        renderer_->drawText("LOW ALTITUDE - find a thermal!", (f32)window_->width() * 0.5f - 140,
                            140, 1.2f, warn);
    }
}

void Game::resetFlight() {
    gliderPos_ = Vec3(0, 80, 0);
    gliderRot_ = Quat::identity();
    gliderVel_ = Vec3(0, 0, 0);
    gliderAngVel_ = Vec3(0, 0, 0);
    cameraPos_ = gliderPos_ + Vec3(0, 2.2f, -9.0f);
    crashed_ = false;
    crashTimer_ = 0.0f;
}

// ---------------------------------------------------------------------------
// Scene submission
// ---------------------------------------------------------------------------
void Game::submitScene() {
    Renderer& r = *renderer_;
    r.beginFrame();

    // --- Glider assembly
    Mat4 glider = Mat4::translation(gliderPos_) * quatToMat4(gliderRot_);

    // Sail (main wing), swept shape
    Mat4 sail = glider * Mat4::translation(Vec3(0, 0.6f, 0.6f)) *
                Mat4::scaling(Vec3(3.4f, 0.08f, 1.7f));
    r.submit(gliderMesh_, gliderSailMat_, sail, gliderPos_, 6.0f);

    // Frame bars
    Mat4 bar1 = glider * Mat4::translation(Vec3(0, 0.2f, -0.5f)) *
                Mat4::scaling(Vec3(0.05f, 0.05f, 3.6f));
    r.submit(gliderMesh_, gliderFrameMat_, bar1, gliderPos_, 3.0f);

    // Keel / central tube
    Mat4 keel = glider * Mat4::translation(Vec3(0, 0.05f, 1.4f)) *
                Mat4::scaling(Vec3(0.06f, 0.06f, 2.2f));
    r.submit(gliderMesh_, gliderFrameMat_, keel, gliderPos_, 2.0f);

    // Pilot capsule (seat)
    Mat4 pilot = glider * Mat4::translation(Vec3(0, -0.5f, 1.0f)) *
                 Mat4::scaling(Vec3(0.5f, 0.7f, 0.5f));
    r.submit(orbMesh_, gliderMat_, pilot, gliderPos_, 1.0f);

    // --- Instanced trees (trunk) + foliage handled as second batch
    if (!treeMatrices_.empty()) {
        Vector<Mat4> foliage;
        foliage.reserve(treeMatrices_.size());
        for (const Mat4& m : treeMatrices_) {
            foliage.pushBack(m * Mat4::translation(Vec3(0, 3.0f, 0)) *
                             Mat4::scaling(Vec3(2.6f, 2.2f, 2.6f)));
        }
        r.submitInstanced(treeMesh_, treeMat_, treeMatrices_, true);
        r.submitInstanced(orbMesh_, leafMat_, foliage, true);
    }

    // --- Instanced rocks
    if (!rockMatrices_.empty()) {
        r.submitInstanced(rockMesh_, rockMat_, rockMatrices_, true);
    }

    // --- Orbs (emissive, bobbing)
    for (const Orb& o : orbs_) {
        if (o.collected) continue;
        Vec3 p = o.position + Vec3(0, std::sin(o.bobPhase) * 0.5f, 0);
        Mat4 m = Mat4::translation(p) *
                 Mat4::rotation(Vec3(0, 1, 0), totalTime_ * 1.5f + o.bobPhase) *
                 Mat4::scaling(Vec3(0.9f, 0.9f, 0.9f));
        r.submit(orbMesh_, orbMat_, m, p, 1.0f, false);
    }

    // --- Wind particles
    Vector<Renderer::Particle> particles;
    particles.reserve(windParticles_.size());
    for (const WindParticle& p : windParticles_) {
        Renderer::Particle pt;
        pt.position = p.pos;
        f32 fade = p.life / p.maxLife;
        f32 speed = p.vel.length();
        f32 t = Mathf::clamp(speed / 14.0f, 0.0f, 1.0f);
        pt.color = Vec3(0.7f + t * 0.3f, 0.85f, 1.0f);
        pt.size = p.size * (1.0f + t);
        pt.alpha = 0.12f + t * 0.5f;
        particles.pushBack(pt);
    }
    r.submitParticles(particles);

    // --- Debug: show thermals as translucent rings via lines
    for (const Vec3& tp : thermalPositions_) {
        r.drawSphere(tp + Vec3(0, 12, 0), 26.0f, Color(0.3f, 0.9f, 0.4f, 0.5f));
    }

    // --- Glider position marker (optional)
    r.drawLine(gliderPos_, gliderPos_ + gliderRot_.forward() * 3.0f, Color(1, 0.5f, 0.2f));

    r.endFrame();
}

}
