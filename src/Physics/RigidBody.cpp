#include "Physics/RigidBody.h"

namespace Crux {

void RigidBody::applyForceAt(const Vec3& force, const Vec3& worldPoint) {
    force_ += force;
    Vec3 r = worldPoint - position_;
    torque_ += r.cross(force);
    wake();
}

void RigidBody::addAngularFromTorque(const Vec3& t, f32 dt) {
    // crude: angular acceleration proportional to torque / mass
    angularVelocity_ += t * dt * 0.5f;
}

void RigidBody::update(f32 dt) {
    if (isKinematic_ || sleeping_) {
        clearForces();
        return;
    }

    // Linear integration (semi-implicit Euler)
    Vec3 accel = force_ * invMass_;
    accel.y -= 9.81f * gravityScale_;
    linearVelocity_ += accel * dt;

    // Angular integration
    if (angularVelocity_.lengthSquared() > 1e-10f) {
        f32 angle = angularVelocity_.length();
        Vec3 axis = angularVelocity_.normalized();
        Quat dq(axis, angle * dt);
        rotation_ = (dq * rotation_).normalized();
    }

    // Damping
    linearVelocity_ *= (1.0f - linearDamping_ * dt);
    angularVelocity_ *= (1.0f - angularDamping_ * dt);

    position_ += linearVelocity_ * dt;

    // Stability clamp
    const f32 maxSpeed = 300.0f;
    if (linearVelocity_.lengthSquared() > maxSpeed * maxSpeed)
        linearVelocity_ = linearVelocity_.normalized() * maxSpeed;

    clearForces();
}

}
