#include "Physics/PhysicsWorld.h"
#include "Core/Math.h"
#include "Core/Log.h"

namespace Crux {

PhysicsWorld::PhysicsWorld() {}

f32 PhysicsWorld::groundHeightAt(const Vec3& pos) const {
    if (!heightFn_) return 0.0f;
    return heightFn_(pos) + groundOffset_;
}

Vec3 PhysicsWorld::groundNormalAt(const Vec3& pos) const {
    if (!heightFn_) return Vec3::up();
    const f32 e = 0.5f;
    f32 h = groundHeightAt(pos);
    f32 hx = groundHeightAt(pos + Vec3(e, 0, 0));
    f32 hz = groundHeightAt(pos + Vec3(0, 0, e));
    Vec3 n = Vec3(h - hx, e, h - hz).normalized();
    return n;
}

bool PhysicsWorld::intersectSphereSphere(const Vec3& a, f32 ra, const Vec3& b, f32 rb,
                                         ContactInfo& out) {
    Vec3 d = b - a;
    f32 dist = d.length();
    f32 rad = ra + rb;
    if (dist >= rad) return false;
    if (dist < 1e-6f) {
        out.point = a;
        out.normal = Vec3(0, 1, 0);
        out.depth = rad;
        return true;
    }
    Vec3 n = d / dist;
    out.normal = n;
    out.depth = rad - dist;
    out.point = a + n * (ra - out.depth * 0.5f);
    return true;
}

bool PhysicsWorld::intersectSphereBox(const Vec3& sphereCenter, f32 radius,
                                      const Vec3& boxCenter, const Vec3& boxHalf,
                                      const Quat& boxRot, ContactInfo& out) {
    Vec3 rel = sphereCenter - boxCenter;
    Vec3 local = boxRot.conjugate() * rel;
    Vec3 closest(Mathf::clamp(local.x, -boxHalf.x, boxHalf.x),
                 Mathf::clamp(local.y, -boxHalf.y, boxHalf.y),
                 Mathf::clamp(local.z, -boxHalf.z, boxHalf.z));
    Vec3 delta = local - closest;
    f32 dist2 = delta.lengthSquared();
    if (dist2 > radius * radius) return false;

    Vec3 worldClosest = boxCenter + boxRot * closest;
    Vec3 normal = (sphereCenter - worldClosest);
    f32 nlen = normal.length();
    if (nlen > 1e-6f) {
        normal = normal / nlen;
        out.normal = normal;
        out.depth = radius - nlen;
    } else {
        // Center inside box: push out along smallest axis
        out.normal = Vec3::up();
        out.depth = radius + boxHalf.y;
    }
    out.point = worldClosest;
    return true;
}

void PhysicsWorld::resolveGround(RigidBody& body, f32 dt) {
    const Collider& c = body.collider();
    f32 groundY = groundHeightAt(body.position());
    f32 penetration;
    Vec3 normal = groundNormalAt(body.position());

    if (c.type == ColliderType::Sphere) {
        penetration = (body.position().y - c.radius) - groundY;
    } else if (c.type == ColliderType::Box) {
        penetration = body.position().y - groundY;
    } else if (c.type == ColliderType::Capsule) {
        penetration = body.position().y - (c.height * 0.5f + c.radius) - groundY;
    } else {
        penetration = body.position().y - groundY;
    }

    if (penetration < 0.0f) {
        // Positional correction
        Vec3 corrected = body.position() + normal * (-penetration);
        body.setPosition(corrected);

        // Velocity response with restitution + ground friction
        Vec3 vel = body.linearVelocity();
        f32 velNormal = vel.dot(normal);
        if (velNormal < 0.0f) {
            Vec3 velTangent = vel - normal * velNormal;
            f32 rest = c.restitution;
            vel = velTangent * (1.0f - c.friction * 0.5f) + normal * (-velNormal * rest);
            body.setVelocity(vel);
        }
        body.wake();
    }
}

void PhysicsWorld::resolveBodies(RigidBody& a, RigidBody& b) {
    ContactInfo contact;
    const Collider& ca = a.collider();
    const Collider& cb = b.collider();
    bool hit = false;

    if (ca.type == ColliderType::Sphere && cb.type == ColliderType::Sphere) {
        hit = intersectSphereSphere(a.position(), ca.radius, b.position(), cb.radius, contact);
    } else if (ca.type == ColliderType::Sphere && cb.type == ColliderType::Box) {
        hit = intersectSphereBox(a.position(), ca.radius, b.position(), cb.halfExtents, b.rotation(), contact);
    } else if (ca.type == ColliderType::Box && cb.type == ColliderType::Sphere) {
        hit = intersectSphereBox(b.position(), cb.radius, a.position(), ca.halfExtents, a.rotation(), contact);
        if (hit) contact.normal = -contact.normal;
    }

    if (!hit) return;
    if (ca.isTrigger || cb.isTrigger) return;

    // Impulse resolution (equal mass simplification)
    f32 invSum = a.invMass() + b.invMass();
    if (invSum < 1e-8f) return;
    Vec3 rel = a.linearVelocity() - b.linearVelocity();
    f32 velAlong = rel.dot(contact.normal);
    if (velAlong > 0.0f) return;
    f32 rest = Mathf::max(ca.restitution, cb.restitution);
    f32 j = -(1.0f + rest) * velAlong / invSum;
    Vec3 impulse = contact.normal * j;
    a.applyImpulse(impulse);
    b.applyImpulse(-impulse);
    // Positional correction
    Vec3 correction = contact.normal * contact.depth * 0.5f;
    a.setPosition(a.position() - correction);
    b.setPosition(b.position() + correction);
}

void PhysicsWorld::resolveStatic(RigidBody& body) {
    const Collider& c = body.collider();
    for (const StaticBox& sb : staticBoxes_) {
        ContactInfo contact;
        bool hit = false;
        if (c.type == ColliderType::Sphere) {
            hit = intersectSphereBox(body.position(), c.radius, sb.center, sb.half, sb.rotation, contact);
        }
        if (!hit) continue;
        if (c.isTrigger) continue;
        body.setPosition(body.position() + contact.normal * contact.depth);
        Vec3 vel = body.linearVelocity();
        f32 vn = vel.dot(contact.normal);
        if (vn < 0.0f) {
            vel -= contact.normal * vn * (1.0f + c.restitution);
            body.setVelocity(vel);
        }
        body.wake();
    }
}

void PhysicsWorld::step(f32 dt, f32 time) {
    if (dt <= 0.0f) return;

    // Wind force on dynamic bodies
    if (wind_) {
        for (RigidBody* b : bodies_) {
            if (b->isKinematic() || b->isSleeping()) continue;
            Vec3 windVel = wind_->sample(b->position(), time);
            // Simple drag-ish wind force scaled by body size
            f32 k = b->collider().radius * 0.8f + 0.2f;
            Vec3 rel = windVel - b->linearVelocity();
            b->applyForce(rel * k);
        }
    }

    for (RigidBody* b : bodies_) {
        b->update(dt);
    }

    // Collision + ground pass (iterated for stability)
    for (i32 iter = 0; iter < 2; iter++) {
        for (u32 i = 0; i < bodies_.size(); i++) {
            RigidBody* a = bodies_[i];
            if (a->isKinematic()) continue;
            resolveGround(*a, dt);
            resolveStatic(*a);
            for (u32 j = i + 1; j < bodies_.size(); j++) {
                resolveBodies(*a, *bodies_[j]);
            }
        }
    }

    // Sleep bodies below a speed threshold
    for (RigidBody* b : bodies_) {
        if (b->isKinematic()) continue;
        if (b->speed() < 0.05f) {
            // could sleep; keep simple: no auto-sleep to avoid jitter
        }
    }
}

}
