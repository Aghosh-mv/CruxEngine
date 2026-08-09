#include "Particles/ParticleSystem.h"
#include "Core/Log.h"
#include <cstring>
#include <cmath>
#include <cstdlib>

namespace Frost {

f32 Curve::evaluate(f32 t) const {
    if (keys.empty()) return 1.0f;
    if (keys.size() == 1) return keys[0].value;
    if (t <= keys[0].time) return keys[0].value;
    if (t >= keys.back().time) return keys.back().value;
    for (usize i = 0; i < keys.size() - 1; i++) {
        if (t >= keys[i].time && t <= keys[i + 1].time) {
            f32 range = keys[i + 1].time - keys[i].time;
            if (range < Mathf::EPSILON) return keys[i].value;
            f32 localT = (t - keys[i].time) / range;
            return Mathf::lerp(keys[i].value, keys[i + 1].value, localT);
        }
    }
    return keys.back().value;
}

f32 Curve::evaluateCubic(f32 t) const {
    if (keys.empty()) return 1.0f;
    if (keys.size() == 1) return keys[0].value;
    if (t <= keys[0].time) return keys[0].value;
    if (t >= keys.back().time) return keys.back().value;
    for (usize i = 0; i < keys.size() - 1; i++) {
        if (t >= keys[i].time && t <= keys[i + 1].time) {
            f32 range = keys[i + 1].time - keys[i].time;
            if (range < Mathf::EPSILON) return keys[i].value;
            f32 localT = (t - keys[i].time) / range;
            f32 tt = localT * localT;
            f32 ttt = tt * localT;
            f32 h1 = -ttt + 2.0f * tt - localT;
            f32 h2 = 3.0f * ttt - 5.0f * tt + 2.0f;
            f32 h3 = -3.0f * ttt + 4.0f * tt + localT;
            f32 h4 = ttt - tt;
            f32 p0 = (i > 0) ? keys[i - 1].value : keys[i].value;
            f32 p1 = keys[i].value;
            f32 p2 = keys[i + 1].value;
            f32 p3 = (i + 2 < keys.size()) ? keys[i + 2].value : keys[i + 1].value;
            return (h1 * p0 + h2 * p1 + h3 * p2 + h4 * p3) * 0.5f;
        }
    }
    return keys.back().value;
}

Curve Curve::defaultSize() {
    Curve c;
    c.keys.pushBack({0.0f, 1.0f});
    c.keys.pushBack({0.5f, 0.8f});
    c.keys.pushBack({1.0f, 0.0f});
    return c;
}

Color Gradient::evaluate(f32 t) const {
    if (keys.empty()) return Color(1, 1, 1, 1);
    if (keys.size() == 1) return keys[0].color;
    if (t <= keys[0].time) return keys[0].color;
    if (t >= keys.back().time) return keys.back().color;
    for (usize i = 0; i < keys.size() - 1; i++) {
        if (t >= keys[i].time && t <= keys[i + 1].time) {
            f32 range = keys[i + 1].time - keys[i].time;
            if (range < Mathf::EPSILON) return keys[i].color;
            f32 localT = (t - keys[i].time) / range;
            return Color::lerp(keys[i].color, keys[i + 1].color, localT);
        }
    }
    return keys.back().color;
}

Gradient Gradient::defaultColor() {
    Gradient g;
    g.keys.push_back(GradientKey{0.0f, Color(1, 1, 1, 1)});
    g.keys.push_back(GradientKey{1.0f, Color(1, 1, 1, 1)});
    return g;
}

// ---- ParticlePool ----

ParticlePool::ParticlePool() {
    for (u32 i = 0; i < MAX_PARTICLES; i++) {
        freeList_[MAX_PARTICLES - 1 - i] = i;
        particles_[i] = Particle{};
        positions_[i] = Vec3(0);
        colors_[i] = Color(1, 1, 1, 1);
        sizes_[i] = 1.0f;
        rotations_[i] = 0.0f;
    }
    freeCount_ = MAX_PARTICLES;
}

ParticlePool::~ParticlePool() {}

u32 ParticlePool::allocate() {
    if (freeCount_ == 0) return 0xFFFFFFFF;
    u32 idx = freeList_[--freeCount_];
    particles_[idx].alive = true;
    particles_[idx].trailStart = 0;
    particles_[idx].trailCount = 0;
    activeCount_++;
    return idx;
}

void ParticlePool::free(u32 index) {
    if (index >= MAX_PARTICLES || !particles_[index].alive) return;
    particles_[index].alive = false;
    freeList_[freeCount_++] = index;
    activeCount_--;
}

void ParticlePool::freeAll() {
    for (u32 i = 0; i < MAX_PARTICLES; i++) {
        particles_[i].alive = false;
        freeList_[MAX_PARTICLES - 1 - i] = i;
    }
    freeCount_ = MAX_PARTICLES;
    activeCount_ = 0;
}

Particle& ParticlePool::get(u32 index) { return particles_[index]; }
const Particle& ParticlePool::get(u32 index) const { return particles_[index]; }

// ---- ParticleSystem ----

ParticleSystem::ParticleSystem() {
    memset(emitters_, 0, sizeof(emitters_));
    memset(emitterIds_, 0, sizeof(emitterIds_));
    memset(forceFields_, 0, sizeof(forceFields_));
}

ParticleSystem::~ParticleSystem() { shutdown(); }

bool ParticleSystem::init() {
    FROST_LOG_INFO("[ParticleSystem] Initialized");
    return true;
}

void ParticleSystem::shutdown() {
    pool_.freeAll();
    emitterCount_ = 0;
    forceFieldCount_ = 0;
    FROST_LOG_INFO("[ParticleSystem] Shutdown");
}

void ParticleSystem::reset() {
    pool_.freeAll();
    for (u32 i = 0; i < emitterCount_; i++) {
        emitters_[i].emitter.time = 0.0f;
        emitters_[i].emittedCount = 0;
    }
}

void ParticleSystem::update(f32 dt) {
    time_ += dt;
    stats_.emittedThisFrame = 0;

    for (u32 ei = 0; ei < emitterCount_; ei++) {
        ParticleEmitter& em = emitters_[ei];
        if (!em.active || !em.emitter.enabled) continue;

        f32 simDt = dt * em.emitter.simulationSpeed;
        em.emitter.time += simDt;

        f32 lodMult = calculateLODMultiplier(em);

        bool shouldEmit = false;
        if (em.emitter.looping) {
            f32 cycleTime = std::fmod(em.emitter.time, em.emitter.duration);
            shouldEmit = (em.emitter.time >= em.emitter.delay) &&
                         (cycleTime < em.emitter.duration);
        } else {
            shouldEmit = (em.emitter.time >= em.emitter.delay) &&
                         (em.emitter.time < em.emitter.delay + em.emitter.duration);
        }

        if (shouldEmit) {
            emitParticles(em, simDt * lodMult);
        }

        for (u32 pi = 0; pi < ParticlePool::MAX_PARTICLES; pi++) {
            Particle& p = pool_.get(pi);
            if (!p.alive) continue;
            updateParticle(p, em, simDt, pi);
        }

        for (auto& trail : em.trails) {
            if (!trail.enabled) continue;
            for (u32 seg = 0; seg < trail.segments.size(); seg++) {
                trail.segments[seg].life -= simDt * trail.lifetimeDecay;
                if (trail.segments[seg].life <= 0.0f) {
                    trail.segments.erase(seg);
                    seg--;
                }
            }
        }
    }

    stats_.activeParticles = pool_.activeCount();
    stats_.activeEmitters = emitterCount_;
    u32 trailCount = 0;
    for (u32 ei = 0; ei < emitterCount_; ei++) {
        for (auto& trail : emitters_[ei].trails) {
            trailCount += (u32)trail.segments.size();
        }
    }
    stats_.trailSegments = trailCount;
}

void ParticleSystem::emitParticles(ParticleEmitter& em, f32 dt) {
    f32 emissionRate = em.emitter.rate + em.emitter.rateOverTime * em.emitter.time;
    f32 emitCount = emissionRate * dt;
    i32 count = (i32)emitCount;
    f32 frac = emitCount - (f32)count;
    if ((f32)std::rand() / (f32)RAND_MAX < frac) count++;

    for (i32 i = 0; i < count; i++) {
        if (pool_.isFull()) break;

        u32 idx = pool_.allocate();
        if (idx == 0xFFFFFFFF) break;

        Particle& p = pool_.get(idx);
        p.prevPosition = p.position;
        p.position = em.emitter.space == EmitterSpace::Local ? Vec3(0) : em.emitter.position;
        p.position += sampleShape(em.emitter);

        Vec3 dir = em.emitter.direction.normalized();
        if (em.emitter.coneAngle > 0.01f) {
            f32 coneRad = em.emitter.coneAngle * Mathf::DEG2RAD;
            f32 angle = ((f32)std::rand() / (f32)RAND_MAX) * coneRad;
            f32 azimuth = ((f32)std::rand() / (f32)RAND_MAX) * Mathf::TWO_PI;
            f32 sinA = std::sin(angle);
            dir = Vec3(sinA * std::cos(azimuth), std::cos(angle), sinA * std::sin(azimuth));
        } else if (em.emitter.spread.x > 0.01f || em.emitter.spread.y > 0.01f) {
            f32 spreadX = em.emitter.spread.x * Mathf::DEG2RAD;
            f32 spreadY = em.emitter.spread.y * Mathf::DEG2RAD;
            f32 rx = ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * spreadX;
            f32 ry = ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * spreadY;
            Quat rotX(Vec3(1, 0, 0), rx);
            Quat rotY(Vec3(0, 1, 0), ry);
            dir = (rotX * rotY) * dir;
        }

        f32 spd = em.emitter.speed + em.emitter.speedVar * ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * 2.0f;
        p.velocity = dir * spd;
        if (em.velocity.enabled) {
            p.velocity += em.velocity.initialVelocity;
            p.velocity += em.velocity.velocityVar * Vec3(
                ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * 2.0f,
                ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * 2.0f,
                ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * 2.0f);
        }

        p.maxLife = em.emitter.lifetime + em.emitter.lifetimeVar * ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * 2.0f;
        p.maxLife = Mathf::max(p.maxLife, 0.01f);
        p.life = p.maxLife;

        p.startColor = em.color.startColor;
        if (em.color.startColorVar.r > 0 || em.color.startColorVar.g > 0 || em.color.startColorVar.b > 0) {
            f32 cv = ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * 2.0f;
            p.startColor.r = Mathf::clamp(p.startColor.r + em.color.startColorVar.r * cv, 0.0f, 1.0f);
            p.startColor.g = Mathf::clamp(p.startColor.g + em.color.startColorVar.g * cv, 0.0f, 1.0f);
            p.startColor.b = Mathf::clamp(p.startColor.b + em.color.startColorVar.b * cv, 0.0f, 1.0f);
        }
        p.color = p.startColor;
        p.midColor1 = em.color.midColor1;
        p.midColor2 = em.color.midColor2;
        p.endColor = em.color.endColor;

        p.startSize = em.size.size + em.size.sizeVar * ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * 2.0f;
        p.startSize = Mathf::max(p.startSize, 0.001f);
        p.size = p.startSize * em.size.startMultiplier;
        p.midSize1 = p.startSize * em.size.midMultiplier1;
        p.midSize2 = p.startSize * em.size.midMultiplier2;
        p.endSize = em.size.sizeOverLife.keys.size() > 0 ? p.startSize * em.size.endMultiplier : 0.0f;

        p.startRotation = em.rotation.startRotation + em.rotation.startRotationVar * ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * 2.0f;
        p.rotation = p.startRotation;
        p.rotationSpeed = em.rotation.rotationSpeed + em.rotation.rotationSpeedVar * ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * 2.0f;
        p.angularVelocity = p.rotationSpeed;

        p.acceleration = Vec3(0);
        p.emitterIndex = emitterCount_ > 0 ? 0 : 0;
        p.trailStart = 0;
        p.trailCount = 0;

        stats_.emittedThisFrame++;
        em.emittedCount++;
    }

    if (em.emitter.burstCount > 0 && em.emitter.time >= em.emitter.burstTime &&
        em.emitter.time < em.emitter.burstTime + dt + 0.01f) {
        for (i32 i = 0; i < em.emitter.burstCount; i++) {
            if (pool_.isFull()) break;
            u32 idx = pool_.allocate();
            if (idx == 0xFFFFFFFF) break;
            Particle& p = pool_.get(idx);
            p.position = em.emitter.space == EmitterSpace::Local ? Vec3(0) : em.emitter.position;
            Vec3 dir = em.emitter.direction.normalized();
            p.velocity = dir * em.emitter.speed;
            p.maxLife = em.emitter.lifetime;
            p.life = p.maxLife;
            p.startSize = em.size.size;
            p.size = p.startSize;
            p.color = em.color.startColor;
            p.startColor = em.color.startColor;
            p.midColor1 = em.color.midColor1;
            p.midColor2 = em.color.midColor2;
            p.endColor = em.color.endColor;
            p.startRotation = 0;
            p.rotation = 0;
            p.rotationSpeed = em.rotation.rotationSpeed;
            stats_.emittedThisFrame++;
        }
    }
}

void ParticleSystem::updateParticle(Particle& p, ParticleEmitter& em, f32 dt, u32 particleIndex) {
    p.life -= dt;
    if (p.life <= 0.0f) {
        for (auto& sub : em.subEmitters) {
            if (!sub.enabled) continue;
            if (sub.trigger == SubEmitter::Trigger::Death) {
                f32 prob = ((f32)std::rand() / (f32)RAND_MAX);
                if (prob < sub.probability) {
                    spawnSubEmitters(em, p);
                }
            }
        }
        pool_.free(particleIndex);
        return;
    }

    f32 lifeT = 1.0f - (p.life / p.maxLife);
    p.prevPosition = p.position;

    if (em.force.enabled) {
        Vec3 force = em.force.force;
        if (em.force.forceOverLife.keys.size() > 0) {
            f32 fScale = em.force.forceOverLife.evaluate(lifeT);
            force *= fScale;
        }
        p.velocity += force * dt;
    }

    if (em.emitter.gravityModifier != 0.0f) {
        p.velocity.y += -9.81f * em.emitter.gravityModifier * dt;
    }

    for (u32 fi = 0; fi < forceFieldCount_; fi++) {
        applyForceField(p.velocity, p.position, dt);
    }

    p.velocity += p.acceleration * dt;
    p.position += p.velocity * dt;

    if (em.velocity.enabled && em.velocity.velocityOverLife.keys.size() > 0) {
        f32 velScale = em.velocity.velocityOverLife.evaluate(lifeT);
        p.velocity *= velScale * em.velocity.speedMultiplier;
    }

    if (em.emitter.speedOverLife != 1.0f) {
        f32 speedMult = Mathf::lerp(1.0f, em.emitter.speedOverLife, lifeT);
        p.velocity *= speedMult;
    }

    if (em.size.enabled) {
        p.size = interpolateSizeOverLife(em.size, lifeT);
    }

    if (em.color.enabled) {
        p.color = interpolateColorOverLife(em.color, lifeT);
    }

    if (em.rotation.enabled) {
        if (em.rotation.useAngularVelocity) {
            p.rotation += p.angularVelocity * dt;
        } else if (em.rotation.rotationOverLife.keys.size() > 0) {
            f32 rotScale = em.rotation.rotationOverLife.evaluate(lifeT);
            p.rotation = Mathf::lerp(p.startRotation, p.startRotation + p.rotationSpeed * p.maxLife, lifeT) * rotScale;
        } else {
            p.rotation += p.rotationSpeed * dt;
        }
    }

    if (em.collision.enabled) {
        applyCollision(p, em.collision, dt);
    }

    for (auto& sub : em.subEmitters) {
        if (!sub.enabled) continue;
        bool triggered = false;
        if (sub.trigger == SubEmitter::Trigger::Birth && lifeT < 0.01f && p.life > 0.0f) triggered = true;
        if (sub.trigger == SubEmitter::Trigger::Trigger) triggered = true;
        if (triggered) {
            f32 prob = ((f32)std::rand() / (f32)RAND_MAX);
            if (prob < sub.probability) {
                spawnSubEmitters(em, p);
            }
        }
    }

    for (auto& trail : em.trails) {
        if (!trail.enabled) continue;
        updateTrail(trail, p, dt);
    }
}

Vec3 ParticleSystem::sampleShape(const EmitterModule& em) const {
    Vec3 offset(0);
    f32 r1 = (f32)std::rand() / (f32)RAND_MAX;
    f32 r2 = (f32)std::rand() / (f32)RAND_MAX;
    f32 r3 = (f32)std::rand() / (f32)RAND_MAX;

    switch (em.shape) {
    case EmitterShape::Point:
        break;
    case EmitterShape::Cone: {
        f32 angle = r1 * em.shapeArc * Mathf::DEG2RAD;
        f32 rad = Mathf::sqrt(r2) * em.shapeRadius;
        offset = Vec3(std::cos(angle) * rad, 0, std::sin(angle) * rad);
        break;
    }
    case EmitterShape::Box:
        offset = Vec3(
            (r1 - 0.5f) * em.shapeSize.x,
            (r2 - 0.5f) * em.shapeSize.y,
            (r3 - 0.5f) * em.shapeSize.z);
        break;
    case EmitterShape::Sphere: {
        f32 theta = r1 * Mathf::TWO_PI;
        f32 phi = std::acos(2.0f * r2 - 1.0f);
        f32 rad = std::cbrt(r3) * em.shapeRadius;
        offset = Vec3(
            rad * std::sin(phi) * std::cos(theta),
            rad * std::sin(phi) * std::sin(theta),
            rad * std::cos(phi));
        break;
    }
    case EmitterShape::Hemisphere: {
        f32 theta = r1 * Mathf::TWO_PI;
        f32 phi = std::acos(r2) * 0.5f;
        f32 rad = std::cbrt(r3) * em.shapeRadius;
        offset = Vec3(
            rad * std::sin(phi) * std::cos(theta),
            rad * std::cos(phi),
            rad * std::sin(phi) * std::sin(theta));
        break;
    }
    case EmitterShape::Ring: {
        f32 angle = r1 * Mathf::TWO_PI;
        f32 rad = em.shapeRadius;
        offset = Vec3(std::cos(angle) * rad, 0, std::sin(angle) * rad);
        break;
    }
    case EmitterShape::Edge: {
        f32 edgeT = r1;
        i32 edge = (i32)(r2 * 4.0f) % 4;
        Vec3 corners[4] = {
            Vec3(-0.5f, 0, -0.5f) * em.shapeSize,
            Vec3(0.5f, 0, -0.5f) * em.shapeSize,
            Vec3(0.5f, 0, 0.5f) * em.shapeSize,
            Vec3(-0.5f, 0, 0.5f) * em.shapeSize
        };
        offset = corners[edge] * (1.0f - edgeT) + corners[(edge + 1) % 4] * edgeT;
        break;
    }
    }
    return offset;
}

void ParticleSystem::applyForceField(Vec3& vel, const Vec3& pos, f32 dt) {
    for (u32 i = 0; i < forceFieldCount_; i++) {
        const ForceField& ff = forceFields_[i];
        if (!ff.enabled) continue;

        Vec3 toCenter = ff.center - pos;
        f32 dist = toCenter.length();

        switch (ff.type) {
        case ForceField::Type::Wind:
            vel += ff.direction * ff.strength * dt;
            break;
        case ForceField::Type::Vortex: {
            if (dist > 0.01f) {
                Vec3 tangent = toCenter.cross(ff.vortexAxis).normalized();
                vel += tangent * ff.strength * ff.vortexAxisStrength * dt;
                f32 axisDist = toCenter.length();
                vel += ff.vortexAxis * (-axisDist * 0.01f) * dt;
            }
            break;
        }
        case ForceField::Type::Turbulence: {
            f32 tx = 0, ty = 0, tz = 0;
            for (i32 oct = 0; oct < (i32)ff.turbulenceOctaves; oct++) {
                f32 freq = ff.turbulenceFreq * (1 << oct);
                f32 amp = ff.turbulenceAmplitude / (1 << oct);
                tx += std::sin(pos.x * freq + time_ * (1.0f + oct * 0.3f)) * amp;
                ty += std::cos(pos.y * freq + time_ * (1.3f + oct * 0.2f)) * amp;
                tz += std::sin(pos.z * freq + time_ * (0.7f + oct * 0.4f)) * amp;
            }
            vel += Vec3(tx, ty, tz) * ff.strength * dt;
            break;
        }
        case ForceField::Type::Point: {
            if (dist > 0.01f && dist < ff.radius) {
                Vec3 dir = toCenter / dist;
                f32 falloff = 1.0f - (dist / ff.radius);
                vel += dir * ff.strength * falloff * dt;
            }
            break;
        }
        case ForceField::Type::Drag: {
            f32 dragFactor = ff.drag * (1.0f + dist / ff.radius);
            vel *= 1.0f - dragFactor * dt;
            break;
        }
        }
    }
}

void ParticleSystem::applyCollision(Particle& p, const CollisionModule& col, f32 dt) {
    switch (col.collisionShape) {
    case CollisionModule::Shape::GroundPlane: {
        if (col.groundHeightFn) {
            f32 groundH = col.groundHeightFn(p.position, col.groundUser);
            if (p.position.y < groundH + col.minCollisionDepth) {
                p.position.y = groundH + col.minCollisionDepth;
                p.velocity.y = -p.velocity.y * col.bounce;
                p.velocity.x *= (1.0f - col.friction);
                p.velocity.z *= (1.0f - col.friction);
                p.life *= (1.0f - col.lifetimeLoss);
            }
        } else {
            if (p.position.y < col.minCollisionDepth) {
                p.position.y = col.minCollisionDepth;
                p.velocity.y = -p.velocity.y * col.bounce;
                p.velocity.x *= (1.0f - col.friction);
                p.velocity.z *= (1.0f - col.friction);
                p.life *= (1.0f - col.lifetimeLoss);
            }
        }
        break;
    }
    case CollisionModule::Shape::Sphere: {
        Vec3 toCenter = p.position - col.collisionCenter;
        f32 dist = toCenter.length();
        if (dist < col.collisionRadius && dist > 0.01f) {
            Vec3 normal = toCenter / dist;
            p.position = col.collisionCenter + normal * col.collisionRadius;
            f32 dot = p.velocity.dot(normal);
            if (dot < 0.0f) {
                p.velocity -= normal * dot * (1.0f + col.bounce);
                p.velocity *= (1.0f - col.friction);
                p.life *= (1.0f - col.lifetimeLoss);
            }
        }
        break;
    }
    case CollisionModule::Shape::Box: {
        Vec3 local = p.position - col.collisionCenter;
        Vec3 halfExt = col.collisionExtents * 0.5f;
        bool inside = std::abs(local.x) < halfExt.x &&
                      std::abs(local.y) < halfExt.y &&
                      std::abs(local.z) < halfExt.z;
        if (inside) {
            f32 overlapX = halfExt.x - std::abs(local.x);
            f32 overlapY = halfExt.y - std::abs(local.y);
            f32 overlapZ = halfExt.z - std::abs(local.z);
            Vec3 normal(0);
            f32 minOverlap = overlapX;
            normal = Vec3(local.x > 0 ? 1.0f : -1.0f, 0, 0);
            if (overlapY < minOverlap) { minOverlap = overlapY; normal = Vec3(0, local.y > 0 ? 1.0f : -1.0f, 0); }
            if (overlapZ < minOverlap) { normal = Vec3(0, 0, local.z > 0 ? 1.0f : -1.0f); }
            p.position += normal * minOverlap;
            f32 dot = p.velocity.dot(normal);
            if (dot < 0.0f) {
                p.velocity -= normal * dot * (1.0f + col.bounce);
                p.velocity *= (1.0f - col.friction);
                p.life *= (1.0f - col.lifetimeLoss);
            }
        }
        break;
    }
    }
}

void ParticleSystem::spawnSubEmitters(const ParticleEmitter& em, const Particle& p) {
    (void)em;
    (void)p;
}

void ParticleSystem::updateTrail(Trail& trail, const Particle& p, f32 dt) {
    if (trail.segments.size() == 0) {
        Trail::Segment seg;
        seg.position = p.position;
        seg.life = trail.lifetime;
        seg.width = trail.width;
        seg.color = p.color;
        trail.segments.pushBack(seg);
        return;
    }

    Trail::Segment& last = trail.segments.back();
    f32 segDist = (p.position - last.position).length();
    if (segDist > 0.1f) {
        Trail::Segment seg;
        seg.position = p.position;
        seg.life = trail.lifetime;
        f32 lifeRatio = p.life / p.maxLife;
        seg.width = Mathf::lerp(trail.widthEnd, trail.width, lifeRatio);
        seg.color = p.color;
        trail.segments.pushBack(seg);
        if (trail.segments.size() > 32) {
            trail.segments.erase(0);
        }
    }
}

f32 ParticleSystem::calculateLODMultiplier(const ParticleEmitter& em) const {
    if (!em.lod.enabled) return 1.0f;
    f32 dist = (em.emitter.position - cameraPos_).length();
    for (i32 i = 3; i >= 0; i--) {
        if (dist >= em.lod.distances[i]) {
            return em.lod.qualityMultipliers[i];
        }
    }
    return 1.0f;
}

Color ParticleSystem::interpolateColorOverLife(const ColorModule& cm, f32 t) const {
    if (!cm.enabled) return cm.startColor;
    if (cm.colorOverLife.keys.size() > 0) {
        return cm.colorOverLife.evaluate(t);
    }
    Color c;
    if (t < 0.33f) {
        c = Color::lerp(cm.startColor, cm.midColor1, t / 0.33f);
    } else if (t < 0.66f) {
        c = Color::lerp(cm.midColor1, cm.midColor2, (t - 0.33f) / 0.33f);
    } else {
        c = Color::lerp(cm.midColor2, cm.endColor, (t - 0.66f) / 0.34f);
    }
    return c;
}

f32 ParticleSystem::interpolateSizeOverLife(const SizeModule& sm, f32 t) const {
    if (!sm.enabled) return sm.size;
    if (sm.sizeOverLife.keys.size() > 0) {
        return sm.sizeOverLife.evaluate(t) * sm.size;
    }
    f32 baseSize = sm.size;
    if (t < 0.33f) {
        return baseSize * Mathf::lerp(sm.startMultiplier, sm.midMultiplier1, t / 0.33f);
    } else if (t < 0.66f) {
        return baseSize * Mathf::lerp(sm.midMultiplier1, sm.midMultiplier2, (t - 0.33f) / 0.33f);
    }
    return baseSize * Mathf::lerp(sm.midMultiplier2, sm.endMultiplier, (t - 0.66f) / 0.34f);
}

u32 ParticleSystem::createEmitter() {
    if (emitterCount_ >= MAX_EMITTERS) {
        FROST_LOG_WARN("[ParticleSystem] Emitter pool exhausted");
        return 0;
    }
    u32 id = nextEmitterId_++;
    emitterIds_[emitterCount_] = id;
    emitters_[emitterCount_] = ParticleEmitter{};
    emitters_[emitterCount_].emitter.maxParticles = 1000;
    emitterCount_++;
    return id;
}

void ParticleSystem::destroyEmitter(u32 emitterId) {
    for (u32 i = 0; i < emitterCount_; i++) {
        if (emitterIds_[i] == emitterId) {
            emitters_[i] = ParticleEmitter{};
            if (i != emitterCount_ - 1) {
                emitters_[i] = emitters_[emitterCount_ - 1];
                emitterIds_[i] = emitterIds_[emitterCount_ - 1];
            }
            emitterCount_--;
            return;
        }
    }
}

ParticleEmitter* ParticleSystem::getEmitter(u32 emitterId) {
    for (u32 i = 0; i < emitterCount_; i++) {
        if (emitterIds_[i] == emitterId) return &emitters_[i];
    }
    return nullptr;
}

u32 ParticleSystem::addForceField(const ForceField& ff) {
    if (forceFieldCount_ >= MAX_FORCE_FIELDS) return 0xFFFFFFFF;
    forceFields_[forceFieldCount_] = ff;
    return forceFieldCount_++;
}

void ParticleSystem::removeForceField(u32 index) {
    if (index < forceFieldCount_) {
        forceFields_[index] = forceFields_[--forceFieldCount_];
    }
}

void ParticleSystem::clearForceFields() {
    forceFieldCount_ = 0;
}

void ParticleSystem::buildRenderData(Vector<ParticleVertex>& vertices) {
    vertices.clear();
    for (u32 i = 0; i < ParticlePool::MAX_PARTICLES; i++) {
        const Particle& p = pool_.get(i);
        if (!p.alive) continue;
        ParticleVertex v;
        v.position = p.position;
        v.color = p.color;
        v.size = p.size;
        v.rotation = p.rotation;
        vertices.pushBack(v);
    }
}

void ParticleSystem::buildTrailData(Vector<TrailVertex>& vertices) {
    vertices.clear();
    for (u32 ei = 0; ei < emitterCount_; ei++) {
        for (auto& trail : emitters_[ei].trails) {
            if (!trail.enabled) continue;
            for (u32 si = 0; si < trail.segments.size(); si++) {
                TrailVertex v;
                v.position = trail.segments[si].position;
                v.color = trail.segments[si].color;
                v.width = trail.segments[si].width;
                vertices.pushBack(v);
            }
        }
    }
}

}
