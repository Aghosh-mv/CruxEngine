#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Math.h"
#include "Renderer/Mesh.h"
#include "Renderer/Material.h"
#include "Renderer/Light.h"
#include "Physics/RigidBody.h"

namespace Frost {

// Provides Mat4 from quat-based rotation for entity model matrices.
Mat4 quatToMat4(const Quat& q);

struct EntityID { u32 index; u32 generation; };

inline bool operator==(const EntityID& a, const EntityID& b) {
    return a.index == b.index && a.generation == b.generation;
}

// A game object: transform + render + physics + gameplay bits.
struct Entity {
    Vec3 position{ 0, 0, 0 };
    Quat rotation{ Quat::identity() };
    Vec3 scale{ 1, 1, 1 };
    Vec3 velocity{ 0, 0, 0 };

    const Mesh* mesh = nullptr;
    const Material* material = nullptr;
    RigidBody* body = nullptr;

    Vec3 boundsCenter{ 0, 0, 0 };
    f32 boundsRadius = 1.0f;
    bool castShadow = true;
    bool visible = true;
    bool active = true;
    bool isInstanced = false;
    u32 instanceIndex = 0;

    Vec3 prevPosition{ 0, 0, 0 };
    u32 generation = 0;
    u32 flags = 0;

    // Script-ish hook: called each frame with dt.
    void (*onUpdate)(Entity* self, f32 dt, void* userData) = nullptr;
    void* userData = nullptr;

    Mat4 modelMatrix() const {
        return Mat4::translation(position) * Mat4::scaling(scale) * quatToMat4(rotation);
    }
    Vec3 forward() const { return rotation.forward(); }
    Vec3 right() const { return rotation.right(); }
    Vec3 up() const { return rotation.up(); }
};

// Provides Mat4 from quat-based rotation for entity model matrices.
Mat4 quatToMat4(const Quat& q);

// Lightweight scene container: owns entities, batches instanced draws.
class Scene {
public:
    Scene() = default;

    Entity& createEntity() {
        Entity e;
        entities_.pushBack(e);
        return entities_.back();
    }
    void destroyEntity(usize index) {
        if (index < entities_.size()) {
            Entity& e = entities_[index];
            if (e.body) { e.body = nullptr; }
            e.active = false;
        }
    }
    void clear() { entities_.clear(); instanced_.clear(); }

    Vector<Entity>& entities() { return entities_; }
    const Vector<Entity>& entities() const { return entities_; }
    Entity& entity(usize i) { return entities_[i]; }
    usize entityCount() const { return entities_.size(); }

    struct InstanceBatch {
        const Mesh* mesh = nullptr;
        const Material* material = nullptr;
        Vector<Mat4> models;
        Vector<Vec3> boundsCenters;
        Vector<f32> boundsRadii;
    };
    void addInstanced(const Mesh* mesh, const Material* mat, const Mat4& model,
                      const Vec3& center, f32 radius) {
        for (auto& b : instanced_) {
            if (b.mesh == mesh && b.material == mat) {
                b.models.pushBack(model);
                b.boundsCenters.pushBack(center);
                b.boundsRadii.pushBack(radius);
                return;
            }
        }
        InstanceBatch b;
        b.mesh = mesh;
        b.material = mat;
        b.models.pushBack(model);
        b.boundsCenters.pushBack(center);
        b.boundsRadii.pushBack(radius);
        instanced_.pushBack(b);
    }
    void clearInstanced() { instanced_.clear(); }
    Vector<InstanceBatch>& instanced() { return instanced_; }

private:
    Vector<Entity> entities_;
    Vector<InstanceBatch> instanced_;
};

}
