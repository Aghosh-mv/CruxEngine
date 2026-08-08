#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Physics/RigidBody.h"
#include "Physics/WindField.h"

namespace Crux {

struct ContactInfo {
    Vec3 point;
    Vec3 normal;
    f32 depth;
};

// Height sample signature used by the world for ground queries.
using HeightFunction = f32(*)(const Vec3& worldPos);

// Steps and resolves the dynamic bodies in the scene. Ground is supplied as a
// height function; static colliders can be registered as boxes/spheres.
class PhysicsWorld {
public:
    PhysicsWorld();

    void setHeightFunction(HeightFunction fn, f32 yOffset = 0.0f) {
        heightFn_ = fn;
        groundOffset_ = yOffset;
    }
    void setGravity(f32 g) { gravity_ = g; }
    void setWind(const WindField* wind) { wind_ = wind; }

    void addBody(RigidBody* body) { bodies_.pushBack(body); }
    void removeBody(RigidBody* body) {
        for (usize i = 0; i < bodies_.size(); i++) {
            if (bodies_[i] == body) { bodies_.eraseSwap(i); break; }
        }
    }
    u32 bodyCount() const { return (u32)bodies_.size(); }

    void step(f32 dt, f32 time);

    // Terrain queries
    f32 groundHeightAt(const Vec3& pos) const;
    Vec3 groundNormalAt(const Vec3& pos) const;

    void clearBodies() { bodies_.clear(); }

    // Static colliders (boxes) for obstacles
    struct StaticBox {
        Vec3 center{ 0, 0, 0 };
        Vec3 half{ 1, 1, 1 };
        Quat rotation{ Quat::identity() };
    };
    void addStaticBox(const Vec3& center, const Vec3& half, const Quat& rot = Quat::identity()) {
        staticBoxes_.pushBack({ center, half, rot });
    }
    void clearStatic() { staticBoxes_.clear(); }
    const Vector<StaticBox>& staticBoxes() const { return staticBoxes_; }

    // Query helpers
    static bool intersectSphereSphere(const Vec3& a, f32 ra, const Vec3& b, f32 rb, ContactInfo& out);
    static bool intersectSphereBox(const Vec3& sphereCenter, f32 radius,
                                   const Vec3& boxCenter, const Vec3& boxHalf,
                                   const Quat& boxRot, ContactInfo& out);

private:
    void resolveGround(RigidBody& body, f32 dt);
    void resolveBodies(RigidBody& a, RigidBody& b);
    void resolveStatic(RigidBody& body);

    Vector<RigidBody*> bodies_;
    HeightFunction heightFn_ = nullptr;
    f32 groundOffset_ = 0.0f;
    f32 gravity_ = -9.81f;
    const WindField* wind_ = nullptr;
    Vector<StaticBox> staticBoxes_;
};

}
