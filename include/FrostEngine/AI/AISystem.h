#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Math.h"
#include "AI/BehaviorTree.h"
#include "AI/NavMesh.h"

namespace Frost {

struct SteeringOutput {
    Vec3 linear{0, 0, 0};
    f32 angular = 0.0f;
};

struct AIEntity {
    u32 id = 0;
    Vec3 position{0, 0, 0};
    Vec3 velocity{0, 0, 0};
    Vec3 heading{0, 0, -1};
    f32 speed = 5.0f;
    f32 maxSpeed = 10.0f;
    f32 maxForce = 20.0f;
    f32 radius = 0.5f;
    i32 currentTriangle = -1;
    u32 behaviorTreeIndex = 0xFFFFFFFF;
    bool active = true;

    NavPath navPath;
    Vec3 targetPosition{0, 0, 0};
    Vec3 wanderTarget{0, 0, 0};
    f32 wanderAngle = 0.0f;

    Vec3 lastKnownTargetPos{0, 0, 0};
    f32 alertLevel = 0.0f;
    f32 health = 100.0f;
    u32 squadId = 0;
    bool isLeader = false;
};

class AISystem {
public:
    static constexpr u32 MAX_ENTITIES = 1024;
    static constexpr u32 MAX_TREES = 64;
    static constexpr u32 MAX_OBSTACLES = 256;
    static constexpr u32 MAX_SQUADS = 32;

    AISystem();
    ~AISystem();

    bool init();
    void shutdown();
    void update(f32 dt);

    u32 createEntity();
    void destroyEntity(u32 entityId);
    AIEntity* getEntity(u32 entityId);
    u32 entityCount() const { return entityCount_; }

    void setNavMesh(const NavMesh* nav) { navMesh_ = nav; }
    NavMesh& navMesh() { return navMeshOwned_; }
    const NavMesh& navMesh() const { return navMeshOwned_; }

    u32 createBehaviorTree();
    BTTree* getBehaviorTree(u32 index);
    void assignBehaviorTree(u32 entityId, u32 treeIndex);
    Blackboard& getBlackboard(u32 entityId);

    SteeringOutput seek(const AIEntity& entity, const Vec3& target) const;
    SteeringOutput flee(const AIEntity& entity, const Vec3& threat, f32 panicRadius = 10.0f) const;
    SteeringOutput arrive(const AIEntity& entity, const Vec3& target, f32 slowRadius = 5.0f) const;
    SteeringOutput wander(const AIEntity& entity, f32 wanderRadius = 3.0f, f32 wanderDistance = 5.0f) const;
    SteeringOutput obstacleAvoidance(const AIEntity& entity, f32 lookAhead = 10.0f) const;
    SteeringOutput pathFollow(AIEntity& entity) const;
    SteeringOutput pursue(const AIEntity& entity, const Vec3& targetPos, const Vec3& targetVel) const;
    SteeringOutput evade(const AIEntity& entity, const Vec3& threatPos, const Vec3& threatVel) const;
    SteeringOutput separation(const AIEntity& entity, f32 separationRadius = 3.0f) const;
    SteeringOutput alignment(const AIEntity& entity, f32 neighborRadius = 5.0f) const;
    SteeringOutput cohesion(const AIEntity& entity, f32 neighborRadius = 5.0f) const;
    SteeringOutput flocking(const AIEntity& entity, f32 separationRadius = 3.0f,
                            f32 alignmentRadius = 5.0f, f32 cohesionRadius = 5.0f) const;

    void addObstacle(const Vec3& position, f32 radius);
    void removeObstacle(u32 index);
    void clearObstacles();

    struct Formation {
        Vec3 center{0, 0, 0};
        f32 spacing = 2.0f;
        u32 entityCount = 0;
        u32 entityIds[MAX_ENTITIES] = {};
    };
    SteeringOutput formation(const AIEntity& entity, const Formation& formation) const;

    SteeringOutput combineSteering(const AIEntity& entity, const SteeringOutput& steer) const;

    struct PerceptionEvent {
        enum class Type : u8 { Damage, Sound, Sight, Touch };
        Type type;
        Vec3 position{0, 0, 0};
        f32 intensity = 1.0f;
        u32 sourceId = 0;
        f32 time = 0.0f;
    };

    void emitPerceptionEvent(const PerceptionEvent& event);
    void clearPerceptionEvents();
    bool canSeeTarget(const AIEntity& observer, const Vec3& targetPos, f32 maxDistance = 50.0f, f32 fovAngle = 120.0f) const;
    bool canHearSound(const AIEntity& observer, const Vec3& soundPos, f32 soundRadius = 20.0f) const;
    bool hasLineOfSight(const Vec3& from, const Vec3& to) const;

    void requestPath(u32 entityId, const Vec3& start, const Vec3& end);
    void assignSquad(u32 entityId, u32 squadId);
    u32 getSquadLeader(u32 squadId) const;
    f32 assessThreat(const AIEntity& entity, const Vec3& threatPos) const;

    struct Stats {
        u32 activeEntities = 0;
        u32 pathRequests = 0;
        f32 avgSteerMs = 0.0f;
    };
    const Stats& stats() const { return stats_; }

private:
    void updateAIEntity(AIEntity& entity, f32 dt);
    void applySteering(AIEntity& entity, const SteeringOutput& steer, f32 dt);

    AIEntity entities_[MAX_ENTITIES];
    u32 entityCount_ = 0;
    u32 nextEntityId_ = 1;

    Vector<BTTree> trees_;
    Blackboard blackboards_[MAX_ENTITIES];
    u32 entityTreeMap_[MAX_ENTITIES] = {};
    u32 entityBlackboardMap_[MAX_ENTITIES] = {};

    const NavMesh* navMesh_ = nullptr;
    NavMesh navMeshOwned_;

    Vector<PerceptionEvent> perceptionEvents_;

    Vec3 obstacles_[MAX_OBSTACLES];
    f32 obstacleRadii_[MAX_OBSTACLES];
    u32 obstacleCount_ = 0;

    Stats stats_;
};

}
