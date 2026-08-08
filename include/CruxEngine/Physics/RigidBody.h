#pragma once

#include "Core/Types.h"
#include "Core/Math.h"
#include "Core/Vector.h"

namespace Crux {

// -------------------------------------------------------------------------
// Collider shapes. A body may own a single collider; static geometry (the
// terrain heightfield) lives in the world and is queried directly.
// -------------------------------------------------------------------------
enum class ColliderType : u8 { Sphere, Box, Capsule, Plane };

struct Collider {
    ColliderType type = ColliderType::Sphere;
    f32 radius = 0.5f;         // sphere / capsule
    Vec3 halfExtents{ 0.5f, 0.5f, 0.5f };  // box
    f32 height = 1.0f;         // capsule cylinder height
    Vec3 offset{ 0, 0, 0 };    // local-space offset from body origin
    f32 friction = 0.6f;
    f32 restitution = 0.1f;
    bool isTrigger = false;

    static Collider sphere(f32 r) { Collider c; c.type = ColliderType::Sphere; c.radius = r; return c; }
    static Collider box(const Vec3& half) { Collider c; c.type = ColliderType::Box; c.halfExtents = half; return c; }
    static Collider capsule(f32 r, f32 h) { Collider c; c.type = ColliderType::Capsule; c.radius = r; c.height = h; return c; }
};

// -------------------------------------------------------------------------
// Rigid body. Kinematic state with linear/angular velocity and accumulated
// forces. Integration is explicit semi-implicit Euler.
// -------------------------------------------------------------------------
class RigidBody {
public:
    RigidBody() = default;

    void setTransform(const Vec3& pos, const Quat& rot) { position_ = pos; rotation_ = rot; }
    void setPosition(const Vec3& pos) { position_ = pos; }
    void setRotation(const Quat& rot) { rotation_ = rot; }
    void setVelocity(const Vec3& v) { linearVelocity_ = v; }
    void setAngularVelocity(const Vec3& w) { angularVelocity_ = w; }
    void setMass(f32 m) { mass_ = m; invMass_ = m > 0 ? 1.0f / m : 0.0f; }
    void setGravity(f32 g) { gravityScale_ = g; }
    void setKinematic(bool k) { isKinematic_ = k; }
    void setSleeping(bool s) { sleeping_ = s; }
    void setDamping(f32 linear, f32 angular) { linearDamping_ = linear; angularDamping_ = angular; }
    void setCollider(const Collider& c) { collider_ = c; }
    void setFriction(f32 f) { collider_.friction = f; }

    void applyForce(const Vec3& force) { force_ += force; }
    void applyForceAt(const Vec3& force, const Vec3& worldPoint);
    void applyImpulse(const Vec3& impulse) {
        if (invMass_ == 0) return;
        linearVelocity_ += impulse * invMass_;
        wake();
    }
    void applyTorque(const Vec3& t) { torque_ += t; }
    void clearForces() { force_ = Vec3(0); torque_ = Vec3(0); }

    void wake() { sleeping_ = false; }
    void update(f32 dt);

    Vec3 position() const { return position_; }
    Quat rotation() const { return rotation_; }
    Vec3 linearVelocity() const { return linearVelocity_; }
    Vec3 angularVelocity() const { return angularVelocity_; }
    f32 mass() const { return mass_; }
    f32 invMass() const { return invMass_; }
    const Collider& collider() const { return collider_; }
    Collider& collider() { return collider_; }
    bool isKinematic() const { return isKinematic_; }
    bool isSleeping() const { return sleeping_; }
    f32 speed() const { return linearVelocity_.length(); }
    Vec3 velocityProjectedOnPlane() const {
        Vec3 up = Vec3::up();
        return linearVelocity_ - up * linearVelocity_.dot(up);
    }
    void addAngularFromTorque(const Vec3& t, f32 dt);

    // Local -> world helpers
    Vec3 localToWorld(const Vec3& local) const { return position_ + rotation_ * local; }

private:
    Vec3 position_{ 0, 0, 0 };
    Quat rotation_{ Quat::identity() };
    Vec3 linearVelocity_{ 0, 0, 0 };
    Vec3 angularVelocity_{ 0, 0, 0 };
    Vec3 force_{ 0, 0, 0 };
    Vec3 torque_{ 0, 0, 0 };
    f32 mass_ = 1.0f;
    f32 invMass_ = 1.0f;
    f32 gravityScale_ = 1.0f;
    f32 linearDamping_ = 0.02f;
    f32 angularDamping_ = 0.05f;
    bool isKinematic_ = false;
    bool sleeping_ = false;
    Collider collider_;
};

}
