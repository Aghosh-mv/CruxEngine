#pragma once
#include "Core/Types.h"
#include "Core/Math.h"
#include "Core/Vector.h"
#include <queue>

namespace Frost {

enum class FractureMode : u8 { Voronoi = 0, Radial, Planar, Procedural };
enum class DebrisType : u8 { Static = 0, Dynamic, Kinematic };

struct DestructionConfig {
    bool enabled = true;
    f32 maxDamage = 100.0f;
    f32 fractureThreshold = 50.0f;
    f32 debrisLifetime = 10.0f;
    u32 maxDebris = 256;
    f32 debrisGravity = -9.81f;
    f32 debrisDamping = 0.98f;
    f32 minDebrisSize = 0.01f;
    f32 maxDebrisSize = 1.0f;
    f32 structuralIntegrityThreshold = 0.3f;
    bool enableCascading = true;
    f32 cascadeDelay = 0.1f;
    f32 cascadeDamageRadius = 5.0f;
    f32 cascadeDamageFalloff = 2.0f;
    u32 maxFracturePoints = 16;
    f32 fractureRadius = 2.0f;
    f32 fractureRandomness = 0.3f;
    bool generateDebris = true;
    f32 debrisImpulse = 5.0f;
    f32 debrisSpin = 2.0f;
};

struct DebrisPiece {
    Vec3 position;
    Vec3 velocity;
    Vec3 angularVelocity;
    Vec3 size;
    f32 mass;
    f32 lifetime;
    f32 age;
    Mat4 transform;
    u32 parentObjectId;
    DebrisType type;
    bool active;
    f32 drag;
    f32 angularDamping;
};

struct FracturePoint {
    Vec3 position;
    f32 radius;
    f32 damage;
    f32 randomness;
    u32 seed;
};

struct FractureVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    u32 originalIndex;
};

struct FractureTriangle {
    u32 indices[3];
    Vec3 normal;
    f32 area;
    u32 materialIndex;
};

struct StructuralNode {
    u32 nodeId;
    Vec3 position;
    f32 integrity;
    f32 maxIntegrity;
    f32 damage;
    Vector<u32> connections;
    Vector<u32> children;
    u32 parentNodeId;
    bool fractured;
};

struct DestructionObject {
    u32 objectId;
    Vec3 position;
    Mat4 transform;
    f32 totalDamage;
    f32 maxHealth;
    f32 currentHealth;
    bool destroyed;
    bool active;
    Vector<FractureVertex> vertices;
    Vector<FractureTriangle> triangles;
    Vector<StructuralNode> structuralNodes;
    f32 mass;
    f32 inverseMass;
    Vec3 centerOfMass;
    f32 boundingRadius;
};

struct DestructionEvent {
    u32 objectId;
    Vec3 impactPoint;
    Vec3 impactNormal;
    f32 damage;
    f32 radius;
    f32 time;
};

struct DestructionStats {
    u32 totalObjects;
    u32 destroyedObjects;
    u32 activeDebris;
    u32 totalFractures;
    u32 totalTriangles;
    f32 update_time_ms;
};

class DestructionSystem {
public:
    DestructionSystem();
    ~DestructionSystem();

    bool init(const DestructionConfig& config);
    void shutdown();
    void update(f32 dt);

    u32 createObject(const Vec3& position, f32 health);
    void destroyObject(u32 objectId);
    void applyDamage(u32 objectId, const Vec3& point, f32 damage, f32 radius);
    void applyDamage(u32 objectId, const Vec3& point, const Vec3& normal, f32 damage, f32 radius);

    void fractureObject(u32 objectId, const FracturePoint& point);
    void fractureVoronoi(u32 objectId, const Vec3& center, f32 radius, u32 pointCount);
    void fractureRadial(u32 objectId, const Vec3& center, const Vec3& normal, f32 radius, u32 segments);
    void fracturePlanar(u32 objectId, const Vec3& point, const Vec3& normal);
    void fractureProcedural(u32 objectId, const Vec3& center, f32 radius);

    void generateDebris(u32 objectId, const Vec3& center, f32 radius, u32 count);
    void applyDebrisImpulse(u32 debrisIndex, const Vec3& impulse, const Vec3& angularImpulse);
    void updateDebris(f32 dt);
    void cleanupDebris();

    void computeStructuralIntegrity(u32 objectId);
    void propagateDamage(u32 objectId, const Vec3& point, f32 damage, f32 radius);
    void checkCascadingFailure(u32 objectId, f32 dt);
    bool isStructurallySound(u32 objectId) const;
    f32 computeNodeIntegrity(const StructuralNode& node) const;

    void applyForce(u32 objectId, const Vec3& force);
    void applyTorque(u32 objectId, const Vec3& torque);
    void applyImpulse(u32 objectId, const Vec3& impulse, const Vec3& point);
    void updatePhysics(f32 dt);

    void setFractureMode(FractureMode mode);
    void setMaxDamage(f32 damage);
    void setFractureThreshold(f32 threshold);
    void setStructuralIntegrityThreshold(f32 threshold);
    void setEnableCascading(bool enable);
    void setMaxDebris(u32 max);

    DestructionObject* getObject(u32 objectId);
    const DestructionObject* getObject(u32 objectId) const;
    DebrisPiece* getDebris(u32 index);
    const DebrisPiece* getDebris(u32 index) const;
    u32 getDebrisCount() const;
    u32 getObjectCount() const;

    DestructionStats getStats() const;
    void resetStats();
    void printStats() const;

    void generateVoronoiPoints(Vec3* points, u32 count, const Vec3& center, f32 radius, u32 seed) const;
    u32 computeVoronoiCell(const Vec3& point, const Vec3* sites, u32 siteCount) const;
    void clipTriangleToCell(FractureTriangle& tri, const Vec3& cellCenter, f32 cellRadius) const;

    void computeRadialSegments(Vec3* vertices, u32 segments, const Vec3& center, const Vec3& normal, f32 radius) const;
    void computePlanarCut(Vec3* outVertices, u32& outCount, const Vec3* inVertices, u32 inCount, const Vec3& point, const Vec3& normal) const;

    void addStructuralNode(u32 objectId, const Vec3& position, f32 integrity);
    void connectStructuralNodes(u32 objectId, u32 nodeA, u32 nodeB);
    void removeStructuralNode(u32 objectId, u32 nodeId);

    void queueDestructionEvent(const DestructionEvent& event);
    void processDestructionEvents(f32 dt);

private:
    DestructionConfig config_;
    Vector<DestructionObject> objects_;
    Vector<DebrisPiece> debris_;
    Vector<DestructionEvent> eventQueue_;
    DestructionStats stats_;
    FractureMode fractureMode_;
    u32 nextObjectId_;
};

}
