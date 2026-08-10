#pragma once

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Math.h"
#include "Core/Vector.h"
#include "Core/HashMap.h"

namespace Frost {

enum class FluidDetail : u8 {
    Low = 0,
    Medium,
    High
};

struct SPHParticle {
    Vec3 position;
    Vec3 velocity;
    f32 density = 0.0f;
    f32 pressure = 0.0f;
    f32 mass = 1.0f;
};

struct SPHConfig {
    FluidDetail detail = FluidDetail::Medium;
    f32 particleRadius = 0.1f;
    f32 smoothingRadius = 0.4f;
    f32 restDensity = 1000.0f;
    f32 stiffness = 2000.0f;
    f32 viscosity = 0.02f;
    f32 gravity = -9.81f;
    u32 substeps = 1;
    f32 dtScale = 1.0f;
};

class SPHSolver {
public:
    struct Stats {
        u32 particleCount = 0;
        u32 neighborSearches = 0;
        f32 averageDensity = 0.0f;
    };

    SPHSolver() = default;

    void setConfig(const SPHConfig& config) { config_ = config; }
    const SPHConfig& getConfig() const { return config_; }

    void setBounds(const Vec3& halfExtents) { wallHalfExtents_ = halfExtents; }
    const Vec3& getBounds() const { return wallHalfExtents_; }

    u32 addParticle(const Vec3& pos);
    u32 addParticle(const Vec3& pos, const Vec3& vel);
    void addBox(const Vec3& center, const Vec3& halfExtents, f32 spacing);

    void step(f32 dt);

    u32 getParticleCount() const { return (u32)particles_.size(); }
    Vector<SPHParticle>& getParticles() { return particles_; }
    void clear() { particles_.clear(); }

    void setDetail(FluidDetail detail);

    const Stats& getStats() const { return stats_; }

private:
    u64 cellHash(const Vec3& p) const;
    void buildNeighbors();
    void computeDensityAndPressure();
    void computeForces(f32 dt);

    SPHConfig config_;
    Vec3 wallHalfExtents_{ 10.0f, 10.0f, 10.0f };
    Vector<SPHParticle> particles_;
    HashMap<u64, Vector<u32>> cells_;
    Vector<Vector<u32>> neighbors_;
    Stats stats_;
    u32 neighborUpdateInterval_ = 1;
    u32 neighborUpdateCounter_ = 0;
    bool enableViscosity_ = true;
};

struct Joint {
    Vec3 position;
    f32 boneLength = 0.0f;
    bool isTarget = false;
};

struct IKChain {
    Vector<Joint> joints;
    Vec3 target;
    f32 tolerance = 0.001f;
    u32 maxIterations = 20;
    bool solved = false;
};

class IKSolver {
public:
    struct Stats {
        u32 chains = 0;
        u32 totalIterations = 0;
    };

    IKSolver() = default;

    u64 createChain(Vector<Vec3> basePositions, const Vec3& target);
    void setTarget(u64 chainIndex, const Vec3& target);
    void solve(u64 chainIndex);

    IKChain& getChain(u64 chainIndex) { return chains_[chainIndex]; }
    u32 getChainCount() const { return (u32)chains_.size(); }
    void removeChain(u64 chainIndex);

    void solveAll();

    const Stats& getStats() const { return stats_; }

    void setIterationLimit(u32 limit);
    void setTolerance(f32 tolerance);

private:
    Vector<IKChain> chains_;
    Stats stats_;
    u32 iterationLimit_ = 20;
    f32 tolerance_ = 0.001f;
};

class SimLODSystem {
public:
    struct Config {
        SPHConfig fluid;
        u32 ikIterations = 20;
    };

    SimLODSystem() = default;

    void initialize(const Config& config);
    void initialize(const SPHConfig& fluidConfig, u32 ikIterations = 20);

    void update(f32 dt);

    SPHSolver& getFluid() { return fluid_; }
    IKSolver& getIK() { return ik_; }
    const Config& getConfig() const { return config_; }

private:
    Config config_;
    SPHSolver fluid_;
    IKSolver ik_;
};

} // namespace Frost
