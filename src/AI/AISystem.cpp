#include "AI/AISystem.h"
#include "Core/Log.h"
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace Frost {

void Blackboard::setInt(const char* key, i64 value) {
    i32 idx = findEntry(key);
    if (idx < 0) { if (entryCount >= MAX_ENTRIES) return; idx = entryCount++; names[idx] = key; }
    entries[idx].type = Entry::Type::Int;
    entries[idx].intValue = value;
}

void Blackboard::setFloat(const char* key, f32 value) {
    i32 idx = findEntry(key);
    if (idx < 0) { if (entryCount >= MAX_ENTRIES) return; idx = entryCount++; names[idx] = key; }
    entries[idx].type = Entry::Type::Float;
    entries[idx].floatValue = value;
}

void Blackboard::setBool(const char* key, bool value) {
    i32 idx = findEntry(key);
    if (idx < 0) { if (entryCount >= MAX_ENTRIES) return; idx = entryCount++; names[idx] = key; }
    entries[idx].type = Entry::Type::Bool;
    entries[idx].boolValue = value;
}

void Blackboard::setVec3(const char* key, const Vec3& value) {
    i32 idx = findEntry(key);
    if (idx < 0) { if (entryCount >= MAX_ENTRIES) return; idx = entryCount++; names[idx] = key; }
    entries[idx].type = Entry::Type::Vec3;
    entries[idx].vecValue = value;
}

void Blackboard::setString(const char* key, const char* value) {
    i32 idx = findEntry(key);
    if (idx < 0) { if (entryCount >= MAX_ENTRIES) return; idx = entryCount++; names[idx] = key; }
    entries[idx].type = Entry::Type::String;
    entries[idx].strValue = String(value);
}

void Blackboard::setPointer(const char* key, void* value) {
    i32 idx = findEntry(key);
    if (idx < 0) { if (entryCount >= MAX_ENTRIES) return; idx = entryCount++; names[idx] = key; }
    entries[idx].type = Entry::Type::Pointer;
    entries[idx].ptrValue = value;
}

i64 Blackboard::getInt(const char* key) const { i32 idx = findEntry(key); return (idx >= 0) ? entries[idx].intValue : 0; }
f32 Blackboard::getFloat(const char* key) const { i32 idx = findEntry(key); return (idx >= 0) ? entries[idx].floatValue : 0.0f; }
bool Blackboard::getBool(const char* key) const { i32 idx = findEntry(key); return (idx >= 0) ? entries[idx].boolValue : false; }
Vec3 Blackboard::getVec3(const char* key) const { i32 idx = findEntry(key); return (idx >= 0) ? entries[idx].vecValue : Vec3(0); }
const char* Blackboard::getString(const char* key) const { i32 idx = findEntry(key); return (idx >= 0) ? entries[idx].strValue.c_str() : ""; }
void* Blackboard::getPointer(const char* key) const { i32 idx = findEntry(key); return (idx >= 0) ? entries[idx].ptrValue : nullptr; }

i32 Blackboard::findEntry(const char* key) const {
    for (u32 i = 0; i < entryCount; i++) { if (names[i] == key) return (i32)i; }
    return -1;
}

bool Blackboard::hasKey(const char* key) const { return findEntry(key) >= 0; }

void Blackboard::removeKey(const char* key) {
    i32 idx = findEntry(key);
    if (idx >= 0) {
        for (u32 i = (u32)idx; i < entryCount - 1; i++) {
            entries[i] = entries[i + 1];
            names[i] = names[i + 1];
        }
        entryCount--;
    }
}

void Blackboard::clear() { entryCount = 0; }

BTStatus BTSelector::tick(Blackboard& bb, f32 dt) {
    if (children.empty()) return BTStatus::Failure;
    for (i32 i = (runningIndex >= 0 ? runningIndex : 0); i < (i32)children.size(); i++) {
        BTStatus status = children[i]->tick(bb, dt);
        if (status == BTStatus::Running) { runningIndex = i; return BTStatus::Running; }
        if (status == BTStatus::Success) { runningIndex = -1; return BTStatus::Success; }
    }
    runningIndex = -1;
    return BTStatus::Failure;
}

BTStatus BTSequence::tick(Blackboard& bb, f32 dt) {
    if (children.empty()) return BTStatus::Failure;
    for (i32 i = (runningIndex >= 0 ? runningIndex : 0); i < (i32)children.size(); i++) {
        BTStatus status = children[i]->tick(bb, dt);
        if (status == BTStatus::Running) { runningIndex = i; return BTStatus::Running; }
        if (status == BTStatus::Failure) { runningIndex = -1; return BTStatus::Failure; }
    }
    runningIndex = -1;
    return BTStatus::Success;
}

BTStatus BTParallel::tick(Blackboard& bb, f32 dt) {
    i32 successes = 0, failures = 0;
    i32 threshold = (successThreshold >= 0) ? successThreshold : (i32)children.size();
    for (auto* child : children) {
        if (!child) continue;
        BTStatus status = child->tick(bb, dt);
        if (status == BTStatus::Success) successes++;
        else if (status == BTStatus::Failure) failures++;
    }
    if (successes >= threshold) return BTStatus::Success;
    if (failures > (i32)children.size() - threshold) return BTStatus::Failure;
    return BTStatus::Running;
}

bool NavMesh::buildFromTerrain(f32(*heightFn)(f32, f32, void*), void* userData,
                                f32 minX, f32 minZ, f32 maxX, f32 maxZ,
                                f32 stepSize, f32 agentHeight, f32 agentRadius, f32 maxSlope) {
    clear();
    if (!heightFn) return false;
    f32 maxSlopeRad = maxSlope * Mathf::DEG2RAD;
    i32 gridW = (i32)((maxX - minX) / stepSize);
    i32 gridH = (i32)((maxZ - minZ) / stepSize);
    if (gridW < 2 || gridH < 2) return false;

    Vector<Vec3> verts;
    for (i32 z = 0; z <= gridH; z++) {
        for (i32 x = 0; x <= gridW; x++) {
            f32 wx = minX + x * stepSize;
            f32 wz = minZ + z * stepSize;
            f32 wy = heightFn(wx, wz, userData);
            verts.pushBack(Vec3(wx, wy, wz));
        }
    }
    for (i32 z = 0; z < gridH; z++) {
        for (i32 x = 0; x < gridW; x++) {
            i32 i00 = z * (gridW + 1) + x;
            Vec3 v00 = verts[i00], v10 = verts[i00 + 1], v01 = verts[i00 + gridW + 1], v11 = verts[i00 + gridW + 2];
            Vec3 n1 = (v10 - v00).cross(v01 - v00).normalized();
            Vec3 n2 = (v11 - v10).cross(v01 - v10).normalized();
            if (n1.y > 0 && std::acos(n1.y) < maxSlopeRad) addTriangle(v00, v10, v01);
            if (n2.y > 0 && std::acos(n2.y) < maxSlopeRad) addTriangle(v10, v11, v01);
        }
    }
    buildConnectivity();
    return true;
}

void NavMesh::addTriangle(const Vec3& v0, const Vec3& v1, const Vec3& v2, i32 areaId) {
    if (triangles_.size() >= MAX_TRIANGLES) return;
    NavTriangle tri;
    tri.vertices[0] = v0; tri.vertices[1] = v1; tri.vertices[2] = v2;
    tri.center = (v0 + v1 + v2) / 3.0f;
    tri.normal = (v1 - v0).cross(v2 - v0).normalized();
    tri.areaId = areaId;
    triangles_.pushBack(tri);
}

void NavMesh::buildConnectivity() {
    for (u32 i = 0; i < triangles_.size(); i++) {
        for (u32 j = i + 1; j < triangles_.size(); j++) {
            i32 sharedVerts = 0;
            for (int vi = 0; vi < 3; vi++) {
                for (int vj = 0; vj < 3; vj++) {
                    if ((triangles_[i].vertices[vi] - triangles_[j].vertices[vj]).lengthSquared() < 0.001f) sharedVerts++;
                }
            }
            if (sharedVerts >= 2) {
                for (int e = 0; e < 3; e++) { if (triangles_[i].neighbors[e] < 0) { triangles_[i].neighbors[e] = (i32)j; break; } }
                for (int e = 0; e < 3; e++) { if (triangles_[j].neighbors[e] < 0) { triangles_[j].neighbors[e] = (i32)i; break; } }
            }
        }
    }
}

void NavMesh::clear() { triangles_.clear(); }

i32 NavMesh::findNearestTriangle(const Vec3& point) const {
    i32 best = -1; f32 bestDist = 1e30f;
    for (u32 i = 0; i < triangles_.size(); i++) {
        f32 dist = (triangles_[i].center - point).lengthSquared();
        if (dist < bestDist) { bestDist = dist; best = (i32)i; }
    }
    return best;
}

f32 NavMesh::heuristic(i32 a, i32 b) const {
    if (a < 0 || b < 0 || a >= (i32)triangles_.size() || b >= (i32)triangles_.size()) return 1e30f;
    return (triangles_[a].center - triangles_[b].center).length() * (triangles_[a].cost + triangles_[b].cost) * 0.5f;
}

i32 NavMesh::findEdgeShared(i32 triA, i32 triB) const {
    if (triA < 0 || triB < 0) return -1;
    for (int e = 0; e < 3; e++) { if (triangles_[triA].neighbors[e] == triB) return e; }
    return -1;
}

Vec3 NavMesh::getPortalPoint(i32 triA, i32 triB) const {
    if (triA < 0 || triB < 0 || triA >= (i32)triangles_.size() || triB >= (i32)triangles_.size()) return Vec3(0);
    for (int va = 0; va < 3; va++) {
        for (int vb = 0; vb < 3; vb++) {
            if ((triangles_[triA].vertices[va] - triangles_[triB].vertices[vb]).lengthSquared() < 0.001f) {
                for (int va2 = 0; va2 < 3; va2++) {
                    if (va2 == va) continue;
                    for (int vb2 = 0; vb2 < 3; vb2++) {
                        if (vb2 == vb) continue;
                        if ((triangles_[triA].vertices[va2] - triangles_[triB].vertices[vb2]).lengthSquared() < 0.001f) {
                            return (triangles_[triA].vertices[va] + triangles_[triA].vertices[va2]) * 0.5f;
                        }
                    }
                }
            }
        }
    }
    return triangles_[triA].center;
}

i32 NavMesh::findPathAStar(i32 startTri, i32 endTri, Vector<i32>& path) const {
    if (startTri < 0 || endTri < 0) return -1;
    static constexpr i32 MAX_OPEN = 256;
    i32 openList[MAX_OPEN]; i32 openCount = 0;
    i32 parent[MAX_TRIANGLES];
    f32 gScore[MAX_TRIANGLES]; f32 fScore[MAX_TRIANGLES];
    bool closed[MAX_TRIANGLES] = {};
    for (u32 i = 0; i < triangles_.size(); i++) { gScore[i] = 1e30f; fScore[i] = 1e30f; parent[i] = -1; }
    gScore[startTri] = 0; fScore[startTri] = heuristic(startTri, endTri);
    openList[openCount++] = startTri;
    while (openCount > 0) {
        i32 current = -1; f32 bestF = 1e30f; i32 bestIdx = -1;
        for (i32 i = 0; i < openCount; i++) {
            if (fScore[openList[i]] < bestF) { bestF = fScore[openList[i]]; current = openList[i]; bestIdx = i; }
        }
        if (current == endTri) {
            path.clear(); i32 at = endTri;
            while (at >= 0) { path.pushBack(at); at = parent[at]; }
            return (i32)path.size();
        }
        openList[bestIdx] = openList[--openCount];
        closed[current] = true;
        for (int e = 0; e < 3; e++) {
            i32 neighbor = triangles_[current].neighbors[e];
            if (neighbor < 0 || closed[neighbor]) continue;
            f32 tentG = gScore[current] + heuristic(current, neighbor);
            if (tentG < gScore[neighbor]) {
                parent[neighbor] = current; gScore[neighbor] = tentG;
                fScore[neighbor] = tentG + heuristic(neighbor, endTri);
                bool found = false;
                for (i32 i = 0; i < openCount; i++) { if (openList[i] == neighbor) { found = true; break; } }
                if (!found && openCount < MAX_OPEN) openList[openCount++] = neighbor;
            }
        }
    }
    return -1;
}

NavMeshPathfindResult NavMesh::findPath(const Vec3& start, const Vec3& end) const {
    NavMeshPathfindResult result;
    i32 startTri = findNearestTriangle(start);
    i32 endTri = findNearestTriangle(end);
    if (startTri < 0 || endTri < 0) return result;
    Vector<i32> triPath;
    if (findPathAStar(startTri, endTri, triPath) < 0) return result;
    result.triangles = triPath;
    result.waypoints.pushBack(start);
    for (i32 i = (i32)triPath.size() - 2; i >= 0; i--) {
        i32 next = triPath[i];
        i32 prev = (i < (i32)triPath.size() - 1) ? triPath[i + 1] : -1;
        if (prev >= 0) result.waypoints.pushBack(getPortalPoint(next, prev));
    }
    result.waypoints.pushBack(end);
    result.found = true;
    return result;
}

Vector<Vec3> NavMesh::smoothPath(const Vector<Vec3>& rawPath) const {
    if (rawPath.size() <= 2) return rawPath;
    Vector<Vec3> smoothed;
    smoothed.pushBack(rawPath[0]);
    for (u32 i = 1; i < rawPath.size() - 1; i++) {
        Vec3 prev = rawPath[i - 1];
        Vec3 curr = rawPath[i];
        Vec3 next = rawPath[i + 1];
        Vec3 smoothedPos = curr * 0.5f + prev * 0.25f + next * 0.25f;
        NavMesh::RaycastResult rr = raycast(smoothedPos + Vec3(0, 1, 0), smoothedPos - Vec3(0, 5, 0));
        if (rr.hit) smoothedPos.y = rr.hitPoint.y + 0.1f;
        smoothed.pushBack(smoothedPos);
    }
    smoothed.pushBack(rawPath.back());
    return smoothed;
}

Vec3 NavMesh::getClosestPoint(const Vec3& point) const {
    i32 tri = findNearestTriangle(point);
    if (tri < 0) return point;
    return triangles_[tri].center;
}

i32 NavMesh::getTriangleAt(const Vec3& point) const { return findNearestTriangle(point); }

Vec3 NavMesh::randomPoint() const {
    if (triangles_.empty()) return Vec3(0);
    u32 idx = (u32)(std::rand() % triangles_.size());
    return triangles_[idx].center;
}

Vec3 NavMesh::randomPointInRadius(const Vec3& center, f32 radius) const {
    f32 r1 = (f32)std::rand() / (f32)RAND_MAX;
    f32 r2 = (f32)std::rand() / (f32)RAND_MAX;
    f32 angle = r1 * Mathf::TWO_PI;
    f32 dist = Mathf::sqrt(r2) * radius;
    Vec3 pt = center + Vec3(std::cos(angle) * dist, 0, std::sin(angle) * dist);
    i32 tri = findNearestTriangle(pt);
    if (tri >= 0) pt.y = triangles_[tri].center.y;
    return pt;
}

NavMesh::RaycastResult NavMesh::raycast(const Vec3& start, const Vec3& end) const {
    RaycastResult result;
    Vec3 dir = end - start;
    f32 maxDist = dir.length();
    if (maxDist < 0.001f) return result;
    dir = dir / maxDist;
    for (u32 i = 0; i < triangles_.size(); i++) {
        const NavTriangle& tri = triangles_[i];
        Vec3 edge1 = tri.vertices[1] - tri.vertices[0];
        Vec3 edge2 = tri.vertices[2] - tri.vertices[0];
        Vec3 h = dir.cross(edge2);
        f32 a = edge1.dot(h);
        if (a > -Mathf::EPSILON && a < Mathf::EPSILON) continue;
        f32 f = 1.0f / a;
        Vec3 s = start - tri.vertices[0];
        f32 u = f * s.dot(h);
        if (u < 0.0f || u > 1.0f) continue;
        Vec3 q = s.cross(edge1);
        f32 v = f * dir.dot(q);
        if (v < 0.0f || u + v > 1.0f) continue;
        f32 t = f * edge2.dot(q);
        if (t > Mathf::EPSILON && t < maxDist) {
            result.hit = true; result.hitPoint = start + dir * t;
            result.distance = t; result.triangleIndex = (i32)i;
            return result;
        }
    }
    return result;
}

void NavMesh::markArea(const Vec3& center, f32 radius, i32 areaId) {
    for (u32 i = 0; i < triangles_.size(); i++) {
        if ((triangles_[i].center - center).length() < radius) triangles_[i].areaId = areaId;
    }
}

void NavMesh::setTriangleCost(u32 index, f32 cost) { if (index < triangles_.size()) triangles_[index].cost = cost; }
void NavMesh::setTriangleWalkable(u32 index, bool walkable) { if (index < triangles_.size()) triangles_[index].walkable = walkable; }

f32 NavMesh::getPathLength(const Vec3& start, const Vec3& end) const {
    NavMeshPathfindResult result = findPath(start, end);
    if (!result.found) return (end - start).length();
    f32 len = 0;
    for (u32 i = 1; i < result.waypoints.size(); i++) {
        len += (result.waypoints[i] - result.waypoints[i - 1]).length();
    }
    return len;
}

AISystem::AISystem() {
    memset(entities_, 0, sizeof(entities_));
    memset(entityTreeMap_, 0xFF, sizeof(entityTreeMap_));
    memset(entityBlackboardMap_, 0xFF, sizeof(entityBlackboardMap_));
    memset(obstacles_, 0, sizeof(obstacles_));
    memset(obstacleRadii_, 0, sizeof(obstacleRadii_));
}

AISystem::~AISystem() { shutdown(); }

bool AISystem::init() { FROST_LOG_INFO("[AISystem] Initialized"); return true; }

void AISystem::shutdown() {
    entityCount_ = 0;
    trees_.clear();
    perceptionEvents_.clear();
    obstacleCount_ = 0;
    FROST_LOG_INFO("[AISystem] Shutdown");
}

void AISystem::update(f32 dt) {
    for (u32 i = 0; i < entityCount_; i++) {
        if (!entities_[i].active) continue;
        updateAIEntity(entities_[i], dt);
    }
}

u32 AISystem::createEntity() {
    if (entityCount_ >= MAX_ENTITIES) return 0;
    u32 id = nextEntityId_++;
    entities_[entityCount_].id = id;
    entities_[entityCount_].active = true;
    entityTreeMap_[entityCount_] = 0xFFFFFFFF;
    entityCount_++;
    return id;
}

void AISystem::destroyEntity(u32 entityId) {
    for (u32 i = 0; i < entityCount_; i++) {
        if (entities_[i].id == entityId) {
            entities_[i].active = false;
            if (i != entityCount_ - 1) {
                entities_[i] = entities_[entityCount_ - 1];
                entityTreeMap_[i] = entityTreeMap_[entityCount_ - 1];
            }
            entityCount_--;
            return;
        }
    }
}

AIEntity* AISystem::getEntity(u32 entityId) {
    for (u32 i = 0; i < entityCount_; i++) {
        if (entities_[i].id == entityId) return &entities_[i];
    }
    return nullptr;
}

u32 AISystem::createBehaviorTree() { trees_.emplace_back(); return (u32)trees_.size() - 1; }
BTTree* AISystem::getBehaviorTree(u32 index) { return (index < trees_.size()) ? &trees_[index] : nullptr; }

void AISystem::assignBehaviorTree(u32 entityId, u32 treeIndex) {
    for (u32 i = 0; i < entityCount_; i++) {
        if (entities_[i].id == entityId) { entityTreeMap_[i] = treeIndex; return; }
    }
}

Blackboard& AISystem::getBlackboard(u32 entityId) {
    for (u32 i = 0; i < entityCount_; i++) {
        if (entities_[i].id == entityId) { entityBlackboardMap_[i] = i; return blackboards_[i]; }
    }
    static Blackboard empty;
    return empty;
}

SteeringOutput AISystem::seek(const AIEntity& entity, const Vec3& target) const {
    SteeringOutput steer;
    Vec3 desired = target - entity.position;
    f32 dist = desired.length();
    if (dist < 0.01f) return steer;
    desired = desired / dist * entity.maxSpeed;
    steer.linear = desired - entity.velocity;
    if (steer.linear.length() > entity.maxForce) steer.linear = steer.linear.normalized() * entity.maxForce;
    return steer;
}

SteeringOutput AISystem::flee(const AIEntity& entity, const Vec3& threat, f32 panicRadius) const {
    SteeringOutput steer;
    Vec3 away = entity.position - threat;
    f32 dist = away.length();
    if (dist > panicRadius || dist < 0.01f) return steer;
    away = away / dist * entity.maxSpeed;
    steer.linear = away - entity.velocity;
    if (steer.linear.length() > entity.maxForce) steer.linear = steer.linear.normalized() * entity.maxForce;
    return steer;
}

SteeringOutput AISystem::arrive(const AIEntity& entity, const Vec3& target, f32 slowRadius) const {
    SteeringOutput steer;
    Vec3 toTarget = target - entity.position;
    f32 dist = toTarget.length();
    if (dist < 0.01f) return steer;
    f32 targetSpeed = entity.maxSpeed;
    if (dist < slowRadius) targetSpeed = entity.maxSpeed * (dist / slowRadius);
    Vec3 desired = toTarget / dist * targetSpeed;
    steer.linear = desired - entity.velocity;
    if (steer.linear.length() > entity.maxForce) steer.linear = steer.linear.normalized() * entity.maxForce;
    return steer;
}

SteeringOutput AISystem::wander(const AIEntity& entity, f32 wanderRadius, f32 wanderDistance) const {
    SteeringOutput steer;
    Vec3 forward = entity.velocity.lengthSquared() > 0.001f ? entity.velocity.normalized() : entity.heading;
    Vec3 circleCenter = entity.position + forward * wanderDistance;
    f32 angle = entity.wanderAngle + ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * 0.5f;
    Vec3 offset(std::cos(angle) * wanderRadius, 0, std::sin(angle) * wanderRadius);
    steer.linear = seek(entity, circleCenter + offset).linear;
    return steer;
}

SteeringOutput AISystem::obstacleAvoidance(const AIEntity& entity, f32 lookAhead) const {
    SteeringOutput steer;
    Vec3 ahead = entity.position + entity.heading * lookAhead;
    f32 closestDist = 1e30f;
    const Vec3* closestObs = nullptr;
    for (u32 i = 0; i < obstacleCount_; i++) {
        f32 d = (ahead - obstacles_[i]).length() - obstacleRadii_[i];
        if (d < closestDist) { closestDist = d; closestObs = &obstacles_[i]; }
    }
    if (closestObs && closestDist < entity.radius * 3.0f) {
        steer.linear = (ahead - *closestObs).normalized() * entity.maxForce;
    }
    return steer;
}

SteeringOutput AISystem::pathFollow(AIEntity& entity) const {
    SteeringOutput steer;
    if (entity.navPath.reachedEnd()) return steer;
    Vec3 waypoint = entity.navPath.getCurrentWaypoint();
    steer = arrive(entity, waypoint, 3.0f);
    if ((entity.position - waypoint).length() < 2.0f) entity.navPath.advanceWaypoint();
    return steer;
}

SteeringOutput AISystem::pursue(const AIEntity& entity, const Vec3& targetPos, const Vec3& targetVel) const {
    return seek(entity, targetPos + targetVel * 1.0f);
}

SteeringOutput AISystem::evade(const AIEntity& entity, const Vec3& threatPos, const Vec3& threatVel) const {
    return flee(entity, threatPos + threatVel * 1.0f, 20.0f);
}

SteeringOutput AISystem::separation(const AIEntity& entity, f32 separationRadius) const {
    SteeringOutput steer;
    Vec3 avgForce(0);
    u32 count = 0;
    for (u32 i = 0; i < entityCount_; i++) {
        if (entities_[i].id == entity.id || !entities_[i].active) continue;
        f32 dist = (entities_[i].position - entity.position).length();
        if (dist < separationRadius && dist > 0.01f) {
            avgForce += (entity.position - entities_[i].position) / dist;
            count++;
        }
    }
    if (count > 0) {
        avgForce = avgForce / (f32)count * entity.maxSpeed;
        steer.linear = avgForce - entity.velocity;
        if (steer.linear.length() > entity.maxForce) steer.linear = steer.linear.normalized() * entity.maxForce;
    }
    return steer;
}

SteeringOutput AISystem::alignment(const AIEntity& entity, f32 neighborRadius) const {
    SteeringOutput steer;
    Vec3 avgVel(0);
    u32 count = 0;
    for (u32 i = 0; i < entityCount_; i++) {
        if (entities_[i].id == entity.id || !entities_[i].active) continue;
        f32 dist = (entities_[i].position - entity.position).length();
        if (dist < neighborRadius) { avgVel += entities_[i].velocity; count++; }
    }
    if (count > 0) {
        avgVel = avgVel / (f32)count;
        steer.linear = avgVel - entity.velocity;
        if (steer.linear.length() > entity.maxForce) steer.linear = steer.linear.normalized() * entity.maxForce;
    }
    return steer;
}

SteeringOutput AISystem::cohesion(const AIEntity& entity, f32 neighborRadius) const {
    SteeringOutput steer;
    Vec3 center(0);
    u32 count = 0;
    for (u32 i = 0; i < entityCount_; i++) {
        if (entities_[i].id == entity.id || !entities_[i].active) continue;
        f32 dist = (entities_[i].position - entity.position).length();
        if (dist < neighborRadius) { center += entities_[i].position; count++; }
    }
    if (count > 0) {
        center = center / (f32)count;
        steer = seek(entity, center);
    }
    return steer;
}

SteeringOutput AISystem::flocking(const AIEntity& entity, f32 separationRadius, f32 alignmentRadius, f32 cohesionRadius) const {
    SteeringOutput sep = separation(entity, separationRadius);
    SteeringOutput ali = alignment(entity, alignmentRadius);
    SteeringOutput coh = cohesion(entity, cohesionRadius);
    SteeringOutput combined;
    combined.linear = sep.linear * 1.5f + ali.linear * 1.0f + coh.linear * 1.0f;
    if (combined.linear.length() > entity.maxForce) combined.linear = combined.linear.normalized() * entity.maxForce;
    return combined;
}

void AISystem::addObstacle(const Vec3& position, f32 radius) {
    if (obstacleCount_ < MAX_OBSTACLES) { obstacles_[obstacleCount_] = position; obstacleRadii_[obstacleCount_] = radius; obstacleCount_++; }
}

void AISystem::removeObstacle(u32 index) {
    if (index < obstacleCount_) {
        obstacles_[index] = obstacles_[--obstacleCount_];
        obstacleRadii_[index] = obstacleRadii_[obstacleCount_];
    }
}

void AISystem::clearObstacles() { obstacleCount_ = 0; }

SteeringOutput AISystem::formation(const AIEntity& entity, const Formation& formation) const {
    SteeringOutput steer;
    if (entity.id == 0) return steer;
    Vec3 desiredPos = formation.center;
    for (u32 i = 0; i < formation.entityCount; i++) {
        if (formation.entityIds[i] == entity.id) {
            i32 row = (i32)(i / 4);
            i32 col = (i32)(i % 4);
            desiredPos = formation.center + Vec3((col - 1.5f) * formation.spacing, 0, -row * formation.spacing);
            break;
        }
    }
    steer = arrive(entity, desiredPos, 2.0f);
    return steer;
}

SteeringOutput AISystem::combineSteering(const AIEntity& entity, const SteeringOutput& steer) const {
    SteeringOutput combined = steer;
    if (combined.linear.length() > entity.maxForce) combined.linear = combined.linear.normalized() * entity.maxForce;
    return combined;
}

void AISystem::emitPerceptionEvent(const PerceptionEvent& event) { perceptionEvents_.pushBack(event); }
void AISystem::clearPerceptionEvents() { perceptionEvents_.clear(); }

bool AISystem::canSeeTarget(const AIEntity& observer, const Vec3& targetPos, f32 maxDistance, f32 fovAngle) const {
    Vec3 toTarget = targetPos - observer.position;
    f32 dist = toTarget.length();
    if (dist > maxDistance) return false;
    Vec3 dir = toTarget / dist;
    f32 dot = observer.heading.dot(dir);
    if (dot < std::cos(fovAngle * Mathf::DEG2RAD * 0.5f)) return false;
    return hasLineOfSight(observer.position + Vec3(0, 1.5f, 0), targetPos + Vec3(0, 1.0f, 0));
}

bool AISystem::canHearSound(const AIEntity& observer, const Vec3& soundPos, f32 soundRadius) const {
    return (observer.position - soundPos).length() <= soundRadius;
}

bool AISystem::hasLineOfSight(const Vec3& from, const Vec3& to) const {
    const NavMesh* nav = navMesh_ ? navMesh_ : &navMeshOwned_;
    NavMesh::RaycastResult rr = nav->raycast(from, to);
    return !rr.hit || rr.distance > (to - from).length();
}

void AISystem::requestPath(u32 entityId, const Vec3& start, const Vec3& end) {
    AIEntity* ent = getEntity(entityId);
    if (!ent) return;
    stats_.pathRequests++;
    const NavMesh* nav = navMesh_ ? navMesh_ : &navMeshOwned_;
    if (!nav->valid()) return;
    NavMeshPathfindResult result = nav->findPath(start, end);
    if (result.found) {
        ent->navPath.points = nav->smoothPath(result.waypoints);
        ent->navPath.currentWaypoint = 0;
        ent->navPath.totalLength = 0;
        for (u32 i = 1; i < ent->navPath.points.size(); i++) {
            ent->navPath.totalLength += (ent->navPath.points[i] - ent->navPath.points[i - 1]).length();
        }
    }
}

void AISystem::assignSquad(u32 entityId, u32 squadId) {
    AIEntity* ent = getEntity(entityId);
    if (ent) ent->squadId = squadId;
}

u32 AISystem::getSquadLeader(u32 squadId) const {
    u32 leaderId = 0;
    for (u32 i = 0; i < entityCount_; i++) {
        if (entities_[i].active && entities_[i].squadId == squadId && entities_[i].isLeader) {
            leaderId = entities_[i].id;
            break;
        }
    }
    return leaderId;
}

f32 AISystem::assessThreat(const AIEntity& entity, const Vec3& threatPos) const {
    f32 dist = (entity.position - threatPos).length();
    f32 threatLevel = 1.0f / (1.0f + dist * 0.1f);
    if (canSeeTarget(entity, threatPos, 30.0f, 90.0f)) threatLevel *= 2.0f;
    return threatLevel;
}

void AISystem::updateAIEntity(AIEntity& entity, f32 dt) {
    if (entity.behaviorTreeIndex != 0xFFFFFFFF && entity.behaviorTreeIndex < trees_.size()) {
        for (u32 i = 0; i < entityCount_; i++) {
            if (entities_[i].id == entity.id) {
                trees_[entity.behaviorTreeIndex].tick(blackboards_[i], dt);
                break;
            }
        }
    }

    for (auto& evt : perceptionEvents_) {
        if (canHearSound(entity, evt.position, evt.intensity * 30.0f)) {
            entity.lastKnownTargetPos = evt.position;
            entity.alertLevel = Mathf::min(entity.alertLevel + evt.intensity * 0.5f, 1.0f);
        }
    }

    entity.alertLevel = Mathf::max(entity.alertLevel - dt * 0.1f, 0.0f);

    const NavMesh* nav = navMesh_ ? navMesh_ : &navMeshOwned_;
    if (nav->valid()) {
        entity.currentTriangle = nav->getTriangleAt(entity.position);
    }

    SteeringOutput combined{entity.heading * entity.speed * 0.5f, 0};
    applySteering(entity, combined, dt);
}

void AISystem::applySteering(AIEntity& entity, const SteeringOutput& steer, f32 dt) {
    entity.velocity += steer.linear * dt;
    f32 speed = entity.velocity.length();
    if (speed > entity.maxSpeed) entity.velocity = entity.velocity / speed * entity.maxSpeed;
    entity.position += entity.velocity * dt;
    if (entity.velocity.lengthSquared() > 0.001f) entity.heading = entity.velocity.normalized();
}

}
