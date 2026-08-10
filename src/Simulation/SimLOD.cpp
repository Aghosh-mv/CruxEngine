#include "Simulation/SimLOD.h"

#include <cmath>

namespace Frost {

namespace {
constexpr f32 kRestitution = 0.5f;
}

u32 SPHSolver::addParticle(const Vec3& pos) {
    SPHParticle p;
    p.position = pos;
    particles_.push_back(p);
    return (u32)(particles_.size() - 1);
}

u32 SPHSolver::addParticle(const Vec3& pos, const Vec3& vel) {
    SPHParticle p;
    p.position = pos;
    p.velocity = vel;
    particles_.push_back(p);
    return (u32)(particles_.size() - 1);
}

void SPHSolver::addBox(const Vec3& center, const Vec3& halfExtents, f32 spacing) {
    f32 s = spacing > 0.0f ? spacing : config_.particleRadius * 2.0f;
    i32 nx = (i32)std::floor(halfExtents.x * 2.0f / s);
    i32 ny = (i32)std::floor(halfExtents.y * 2.0f / s);
    i32 nz = (i32)std::floor(halfExtents.z * 2.0f / s);
    Vec3 min = center - halfExtents;
    for (i32 ix = 0; ix <= nx; ix++) {
        for (i32 iy = 0; iy <= ny; iy++) {
            for (i32 iz = 0; iz <= nz; iz++) {
                Vec3 pos(min.x + (f32)ix * s + s * 0.5f,
                         min.y + (f32)iy * s + s * 0.5f,
                         min.z + (f32)iz * s + s * 0.5f);
                addParticle(pos);
            }
        }
    }
}

void SPHSolver::setDetail(FluidDetail detail) {
    config_.detail = detail;
    switch (detail) {
        case FluidDetail::Low:
            neighborUpdateInterval_ = 2;
            enableViscosity_ = false;
            break;
        case FluidDetail::Medium:
        case FluidDetail::High:
        default:
            neighborUpdateInterval_ = 1;
            enableViscosity_ = true;
            break;
    }
}

u64 SPHSolver::cellHash(const Vec3& p) const {
    const f32 inv = 1.0f / config_.smoothingRadius;
    i32 cx = (i32)std::floor(p.x * inv);
    i32 cy = (i32)std::floor(p.y * inv);
    i32 cz = (i32)std::floor(p.z * inv);
    u64 hx = (u64)(u32)cx & 0x1FFFFFu;
    u64 hy = (u64)(u32)cy & 0x1FFFFFu;
    u64 hz = (u64)(u32)cz & 0x1FFFFFu;
    return (hx << 42) | (hy << 21) | hz;
}

void SPHSolver::buildNeighbors() {
    cells_.clear();
    neighbors_.resize(particles_.size());

    for (u32 i = 0; i < particles_.size(); i++) {
        cells_[cellHash(particles_[i].position)].push_back(i);
    }

    const f32 h = config_.smoothingRadius;
    const f32 h2 = h * h;
    const f32 inv = 1.0f / h;

    for (u32 i = 0; i < particles_.size(); i++) {
        Vector<u32>& list = neighbors_[i];
        list.clear();
        stats_.neighborSearches++;
        const Vec3& p = particles_[i].position;
        i32 cx = (i32)std::floor(p.x * inv);
        i32 cy = (i32)std::floor(p.y * inv);
        i32 cz = (i32)std::floor(p.z * inv);
        for (i32 dx = -1; dx <= 1; dx++) {
            for (i32 dy = -1; dy <= 1; dy++) {
                for (i32 dz = -1; dz <= 1; dz++) {
                    u64 key = (((u64)(u32)(cx + dx) & 0x1FFFFFu) << 42) |
                              (((u64)(u32)(cy + dy) & 0x1FFFFFu) << 21) |
                              ((u64)(u32)(cz + dz) & 0x1FFFFFu);
                    auto it = cells_.find(key);
                    if (it == cells_.end()) continue;
                    for (u32 j : it.value()) {
                        if (j == i) continue;
                        if ((particles_[j].position - p).lengthSquared() < h2) {
                            list.push_back(j);
                        }
                    }
                }
            }
        }
    }
}

void SPHSolver::computeDensityAndPressure() {
    const f32 h = config_.smoothingRadius;
    const f32 h2 = h * h;
    f32 h3 = h2 * h;
    f32 h9 = h3 * h3 * h3;
    const f32 poly6 = 315.0f / (64.0f * Mathf::PI * h9);

    f32 densitySum = 0.0f;
    for (u32 i = 0; i < particles_.size(); i++) {
        SPHParticle& p = particles_[i];
        f32 density = 0.0f;
        for (u32 j : neighbors_[i]) {
            const Vec3 delta = particles_[j].position - p.position;
            f32 r2 = delta.lengthSquared();
            if (r2 >= h2) continue;
            f32 d = h2 - r2;
            density += particles_[j].mass * poly6 * d * d * d;
        }
        density = density > 0.0001f ? density : 0.0001f;
        p.density = density;
        p.pressure = config_.stiffness * (density - config_.restDensity);
        densitySum += density;
    }
    stats_.averageDensity = particles_.size() > 0 ? densitySum / (f32)particles_.size() : 0.0f;
}

void SPHSolver::computeForces(f32 dt) {
    const f32 h = config_.smoothingRadius;
    const f32 h2 = h * h;
    f32 h3 = h2 * h;
    f32 h6 = h3 * h3;
    const f32 spikyGrad = 45.0f / (Mathf::PI * h6);
    const f32 viscKernel = 45.0f / (Mathf::PI * h6);

    for (u32 i = 0; i < particles_.size(); i++) {
        SPHParticle& p = particles_[i];
        Vec3 fp(0);
        Vec3 fv(0);
        for (u32 j : neighbors_[i]) {
            const Vec3 delta = p.position - particles_[j].position;
            f32 r = delta.length();
            if (r >= h || r <= 1e-6f) continue;
            f32 w = h - r;
            Vec3 dir = delta / r;
            f32 pTerm = particles_[j].mass * (p.pressure + particles_[j].pressure) * 0.5f / particles_[j].density;
            fp += dir * (pTerm * spikyGrad * w * w);
            if (enableViscosity_) {
                f32 vTerm = config_.viscosity * particles_[j].mass * viscKernel * w / particles_[j].density;
                fv += (particles_[j].velocity - p.velocity) * vTerm;
            }
        }
        Vec3 accel = fp + fv;
        accel.y += config_.gravity;
        p.velocity += accel * dt;
        p.position += p.velocity * dt;

        if (p.position.y < 0.0f) {
            p.position.y = 0.0f;
            if (p.velocity.y < 0.0f) p.velocity.y = -p.velocity.y * kRestitution;
        }
        f32 hx = wallHalfExtents_.x;
        f32 hy = wallHalfExtents_.y;
        f32 hz = wallHalfExtents_.z;
        if (p.position.x < -hx) { p.position.x = -hx; if (p.velocity.x < 0.0f) p.velocity.x = -p.velocity.x * kRestitution; }
        if (p.position.x >  hx) { p.position.x =  hx; if (p.velocity.x > 0.0f) p.velocity.x = -p.velocity.x * kRestitution; }
        if (p.position.y >  hy) { p.position.y =  hy; if (p.velocity.y > 0.0f) p.velocity.y = -p.velocity.y * kRestitution; }
        if (p.position.z < -hz) { p.position.z = -hz; if (p.velocity.z < 0.0f) p.velocity.z = -p.velocity.z * kRestitution; }
        if (p.position.z >  hz) { p.position.z =  hz; if (p.velocity.z > 0.0f) p.velocity.z = -p.velocity.z * kRestitution; }
    }
}

void SPHSolver::step(f32 dt) {
    stats_.particleCount = (u32)particles_.size();
    stats_.neighborSearches = 0;
    stats_.averageDensity = 0.0f;
    if (particles_.empty()) return;

    f32 scaledDt = dt * config_.dtScale;
    f32 subDt = scaledDt / (f32)config_.substeps;
    for (u32 s = 0; s < config_.substeps; s++) {
        neighborUpdateCounter_++;
        if (neighborUpdateCounter_ >= neighborUpdateInterval_) {
            neighborUpdateCounter_ = 0;
            buildNeighbors();
        }
        computeDensityAndPressure();
        computeForces(subDt);
    }
}

u64 IKSolver::createChain(Vector<Vec3> basePositions, const Vec3& target) {
    IKChain chain;
    chain.target = target;
    chain.tolerance = tolerance_;
    chain.maxIterations = iterationLimit_;
    chain.solved = false;
    for (usize i = 0; i < basePositions.size(); i++) {
        Joint joint;
        joint.position = basePositions[i];
        joint.boneLength = (i + 1 < basePositions.size())
                               ? (basePositions[i + 1] - basePositions[i]).length()
                               : 0.0f;
        joint.isTarget = (i + 1 == basePositions.size());
        chain.joints.push_back(joint);
    }
    chains_.push_back(chain);
    u64 index = chains_.size() - 1;
    stats_.chains = (u32)chains_.size();
    return index;
}

void IKSolver::setTarget(u64 chainIndex, const Vec3& target) {
    if (chainIndex >= chains_.size()) return;
    chains_[chainIndex].target = target;
    chains_[chainIndex].solved = false;
}

void IKSolver::solve(u64 chainIndex) {
    if (chainIndex >= chains_.size()) return;
    IKChain& chain = chains_[chainIndex];
    u32 n = (u32)chain.joints.size();
    if (n == 0) {
        chain.solved = false;
        return;
    }

    Vec3 root = chain.joints[0].position;
    f32 tol2 = chain.tolerance * chain.tolerance;
    u32 iterations = 0;
    for (; iterations < chain.maxIterations; iterations++) {
        chain.joints[n - 1].position = chain.target;
        for (i32 i = (i32)n - 2; i >= 0; i--) {
            Vec3 dir = (chain.joints[i].position - chain.joints[i + 1].position).normalized();
            chain.joints[i].position = chain.joints[i + 1].position + dir * chain.joints[i].boneLength;
        }

        chain.joints[0].position = root;
        for (u32 i = 1; i < n; i++) {
            Vec3 dir = (chain.joints[i].position - chain.joints[i - 1].position).normalized();
            chain.joints[i].position = chain.joints[i - 1].position + dir * chain.joints[i - 1].boneLength;
        }

        if ((chain.joints[n - 1].position - chain.target).lengthSquared() <= tol2) break;
    }

    stats_.totalIterations += iterations;
    chain.solved = (chain.joints[n - 1].position - chain.target).lengthSquared() <= tol2;
}

void IKSolver::removeChain(u64 chainIndex) {
    if (chainIndex >= chains_.size()) return;
    chains_.erase((usize)chainIndex);
    stats_.chains = (u32)chains_.size();
}

void IKSolver::solveAll() {
    for (u32 i = 0; i < chains_.size(); i++) {
        solve(i);
    }
}

void IKSolver::setIterationLimit(u32 limit) {
    iterationLimit_ = limit;
    for (u32 i = 0; i < chains_.size(); i++) {
        chains_[i].maxIterations = limit;
    }
}

void IKSolver::setTolerance(f32 tolerance) {
    tolerance_ = tolerance;
    for (u32 i = 0; i < chains_.size(); i++) {
        chains_[i].tolerance = tolerance;
    }
}

void SimLODSystem::initialize(const Config& config) {
    config_ = config;
    fluid_.setConfig(config.fluid);
    fluid_.setDetail(config.fluid.detail);
    ik_.setIterationLimit(config.ikIterations);
}

void SimLODSystem::initialize(const SPHConfig& fluidConfig, u32 ikIterations) {
    Config config;
    config.fluid = fluidConfig;
    config.ikIterations = ikIterations;
    initialize(config);
}

void SimLODSystem::update(f32 dt) {
    fluid_.step(dt);
    ik_.solveAll();
}

} // namespace Frost
