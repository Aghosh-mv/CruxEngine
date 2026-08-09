#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Math.h"
#include "Core/String.h"
#include "Renderer/Mesh.h"
#include <cmath>

namespace Frost {

struct Particle {
    Vec3 position{0, 0, 0};
    Vec3 velocity{0, 0, 0};
    Vec3 acceleration{0, 0, 0};
    Vec3 prevPosition{0, 0, 0};
    Color startColor{1, 1, 1, 1};
    Color midColor1{1, 1, 1, 1};
    Color midColor2{1, 1, 1, 1};
    Color endColor{1, 1, 1, 0};
    Color color{1, 1, 1, 1};
    f32 size = 1.0f;
    f32 startSize = 1.0f;
    f32 midSize1 = 1.0f;
    f32 midSize2 = 0.5f;
    f32 endSize = 0.0f;
    f32 life = 1.0f;
    f32 maxLife = 1.0f;
    f32 rotation = 0.0f;
    f32 rotationSpeed = 0.0f;
    f32 startRotation = 0.0f;
    f32 endRotation = 0.0f;
    f32 angularVelocity = 0.0f;
    u32 emitterIndex = 0;
    u32 trailStart = 0;
    u32 trailCount = 0;
    bool alive = false;
};

struct CurveKey {
    f32 time = 0.0f;
    f32 value = 0.0f;
};

struct GradientKey {
    f32 time = 0.0f;
    Color color{1, 1, 1, 1};
};

struct Curve {
    Vector<CurveKey> keys;
    f32 evaluate(f32 t) const;
    f32 evaluateCubic(f32 t) const;
    static Curve defaultSize();
};

struct Gradient {
    Vector<GradientKey> keys;
    Color evaluate(f32 t) const;
    static Gradient defaultColor();
};

enum class EmitterShape : u8 {
    Point,
    Cone,
    Box,
    Sphere,
    Hemisphere,
    Ring,
    Edge
};

enum class EmitterSpace : u8 {
    Local,
    World
};

struct EmitterModule {
    Vec3 position{0, 0, 0};
    Vec3 direction{0, 1, 0};
    Vec3 spread{360.0f, 360.0f, 360.0f};
    f32 speed = 5.0f;
    f32 speedVar = 0.0f;
    f32 lifetime = 2.0f;
    f32 lifetimeVar = 0.5f;
    f32 rate = 50.0f;
    f32 rateOverTime = 0.0f;
    i32 burstCount = 0;
    f32 burstTime = 0.0f;
    EmitterShape shape = EmitterShape::Cone;
    EmitterSpace space = EmitterSpace::World;
    f32 shapeRadius = 1.0f;
    Vec3 shapeSize{1, 1, 1};
    f32 shapeArc = 360.0f;
    f32 coneAngle = 25.0f;
    f32 startRotation = 0.0f;
    f32 startRotationVar = 0.0f;
    f32 rotationSpeed = 0.0f;
    f32 rotationSpeedVar = 0.0f;
    i32 maxParticles = 1000;
    bool enabled = true;
    bool looping = true;
    f32 duration = 5.0f;
    f32 time = 0.0f;
    f32 delay = 0.0f;
    f32 simulationSpeed = 1.0f;
    bool prewarm = false;
    f32 gravityModifier = 0.0f;
    f32 speedOverLife = 1.0f;
    f32 accelerationOverLife = 0.0f;
};

struct SizeModule {
    Curve sizeOverLife;
    f32 size = 1.0f;
    f32 sizeVar = 0.0f;
    f32 startMultiplier = 1.0f;
    f32 midMultiplier1 = 1.0f;
    f32 midMultiplier2 = 0.5f;
    f32 endMultiplier = 0.0f;
    bool enabled = true;
};

struct ColorModule {
    Gradient colorOverLife;
    Color startColor{1, 1, 1, 1};
    Color startColorVar{0, 0, 0, 0};
    Color endColor{1, 1, 1, 0};
    Color midColor1{1, 1, 0.5f, 0.8f};
    Color midColor2{1, 0.5f, 0, 0.4f};
    bool enabled = true;
};

struct RotationModule {
    f32 rotationSpeed = 0.0f;
    f32 rotationSpeedVar = 0.0f;
    f32 startRotation = 0.0f;
    f32 startRotationVar = 0.0f;
    Curve rotationOverLife;
    bool useAngularVelocity = false;
    bool enabled = true;
};

struct VelocityModule {
    Vec3 initialVelocity{0, 5, 0};
    Vec3 velocityVar{0, 1, 0};
    f32 speedMultiplier = 1.0f;
    Curve velocityOverLife;
    bool enabled = true;
};

struct ForceModule {
    Vec3 force{0, -9.81f, 0};
    Curve forceOverLife;
    bool enabled = true;
};

struct CollisionModule {
    bool enabled = false;
    f32 bounce = 0.5f;
    f32 lifetimeLoss = 0.1f;
    f32 minCollisionDepth = 0.01f;
    f32 friction = 0.3f;
    Vec3 groundNormal{0, 1, 0};
    f32(*groundHeightFn)(const Vec3& pos, void* user) = nullptr;
    void* groundUser = nullptr;
    enum class Shape : u8 { GroundPlane, Sphere, Box };
    Shape collisionShape = Shape::GroundPlane;
    Vec3 collisionCenter{0, 0, 0};
    Vec3 collisionExtents{100, 100, 100};
    f32 collisionRadius = 10.0f;
};

struct ForceField {
    enum class Type : u8 { Wind, Vortex, Turbulence, Point, Drag };
    Type type = Type::Wind;
    Vec3 position{0, 0, 0};
    Vec3 direction{1, 0, 0};
    Vec3 center{0, 0, 0};
    f32 strength = 1.0f;
    f32 radius = 10.0f;
    f32 turbulenceFreq = 0.5f;
    f32 turbulenceAmplitude = 1.0f;
    f32 turbulenceOctaves = 4;
    f32 drag = 0.1f;
    f32 innerRadius = 1.0f;
    f32 outerRadius = 10.0f;
    f32 vortexAxisStrength = 1.0f;
    Vec3 vortexAxis{0, 1, 0};
    bool enabled = true;
};

struct Trail {
    struct Segment {
        Vec3 position{0, 0, 0};
        f32 life = 0.0f;
        f32 width = 0.1f;
        Color color{1, 1, 1, 1};
    };
    u32 particleIndex = 0;
    Vector<Segment> segments;
    f32 lifetime = 0.3f;
    f32 width = 0.1f;
    f32 widthEnd = 0.01f;
    f32 lifetimeDecay = 1.0f;
    Curve widthCurve;
    bool enabled = false;
};

struct SubEmitter {
    enum class Trigger : u8 { Birth, Death, Collision, Trigger };
    Trigger trigger = Trigger::Birth;
    i32 emitterIndex = -1;
    f32 probability = 1.0f;
    i32 count = 1;
    bool enabled = true;
};

struct LODConfig {
    f32 distances[4] = {50.0f, 150.0f, 400.0f, 1000.0f};
    f32 qualityMultipliers[4] = {1.0f, 0.5f, 0.25f, 0.1f};
    bool enabled = true;
};

struct ParticleEmitter {
    EmitterModule emitter;
    SizeModule size;
    ColorModule color;
    RotationModule rotation;
    VelocityModule velocity;
    ForceModule force;
    CollisionModule collision;
    Vector<SubEmitter> subEmitters;
    Vector<Trail> trails;
    LODConfig lod;
    bool active = true;
    u32 emittedCount = 0;
};

class ParticlePool {
public:
    static constexpr u32 MAX_PARTICLES = 65536;

    ParticlePool();
    ~ParticlePool();

    u32 allocate();
    void free(u32 index);
    void freeAll();
    Particle& get(u32 index);
    const Particle& get(u32 index) const;
    u32 activeCount() const { return activeCount_; }
    bool isFull() const { return activeCount_ >= MAX_PARTICLES; }

    Vec3* getPositions() { return positions_; }
    Color* getColors() { return colors_; }
    f32* getSizes() { return sizes_; }
    f32* getRotations() { return rotations_; }

private:
    Particle particles_[MAX_PARTICLES];
    Vec3 positions_[MAX_PARTICLES];
    Color colors_[MAX_PARTICLES];
    f32 sizes_[MAX_PARTICLES];
    f32 rotations_[MAX_PARTICLES];
    u32 freeList_[MAX_PARTICLES];
    u32 freeCount_ = 0;
    u32 activeCount_ = 0;
};

class ParticleSystem {
public:
    static constexpr u32 MAX_EMITTERS = 128;
    static constexpr u32 MAX_FORCE_FIELDS = 32;
    static constexpr u32 MAX_TRAIL_SEGMENTS = 65536;

    ParticleSystem();
    ~ParticleSystem();

    bool init();
    void shutdown();
    void update(f32 dt);
    void reset();

    u32 createEmitter();
    void destroyEmitter(u32 emitterId);
    ParticleEmitter* getEmitter(u32 emitterId);
    u32 emitterCount() const { return emitterCount_; }

    u32 addForceField(const ForceField& ff);
    void removeForceField(u32 index);
    void clearForceFields();

    ParticlePool& pool() { return pool_; }
    const ParticlePool& pool() const { return pool_; }

    void setCameraPosition(const Vec3& pos) { cameraPos_ = pos; }

    struct ParticleVertex {
        Vec3 position;
        Color color;
        f32 size;
        f32 rotation;
    };
    void buildRenderData(Vector<ParticleVertex>& vertices);

    struct TrailVertex {
        Vec3 position;
        Color color;
        f32 width;
    };
    void buildTrailData(Vector<TrailVertex>& vertices);

    struct Stats {
        u32 activeParticles = 0;
        u32 activeEmitters = 0;
        u32 emittedThisFrame = 0;
        u32 trailSegments = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    void emitParticles(ParticleEmitter& em, f32 dt);
    void updateParticle(Particle& p, ParticleEmitter& em, f32 dt, u32 particleIndex);
    Vec3 sampleShape(const EmitterModule& em) const;
    void applyForceField(Vec3& vel, const Vec3& pos, f32 dt);
    void applyCollision(Particle& p, const CollisionModule& col, f32 dt);
    void spawnSubEmitters(const ParticleEmitter& em, const Particle& p);
    void updateTrail(Trail& trail, const Particle& p, f32 dt);
    f32 calculateLODMultiplier(const ParticleEmitter& em) const;
    Color interpolateColorOverLife(const ColorModule& cm, f32 t) const;
    f32 interpolateSizeOverLife(const SizeModule& sm, f32 t) const;

    ParticlePool pool_;
    ParticleEmitter emitters_[MAX_EMITTERS];
    u32 emitterCount_ = 0;
    ForceField forceFields_[MAX_FORCE_FIELDS];
    u32 forceFieldCount_ = 0;
    f32 time_ = 0.0f;
    u32 nextEmitterId_ = 1;
    u32 emitterIds_[MAX_EMITTERS] = {};
    Vec3 cameraPos_{0, 0, 0};
    Stats stats_;
};

}
