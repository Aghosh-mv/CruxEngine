#include "Physics/DestructionSystem.h"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frost {

DestructionSystem::DestructionSystem()
    : fractureMode_(FractureMode::Voronoi), nextObjectId_(1) {
    memset(&stats_, 0, sizeof(stats_));
}

DestructionSystem::~DestructionSystem() { shutdown(); }

bool DestructionSystem::init(const DestructionConfig& config) {
    config_ = config;
    objects_.clear();
    debris_.clear();
    eventQueue_.clear();
    FROST_LOG_INFO("[DestructionSystem] Initialized (maxDamage=%.1f, threshold=%.1f, maxDebris=%u, cascading=%s)",
        config_.maxDamage, config_.fractureThreshold, config_.maxDebris, config_.enableCascading ? "true" : "false");
    return true;
}

void DestructionSystem::shutdown() {
    objects_.clear();
    debris_.clear();
    eventQueue_.clear();
    FROST_LOG_INFO("[DestructionSystem] Shutdown");
}

void DestructionSystem::update(f32 dt) {
    processDestructionEvents(dt);
    updateDebris(dt);
    cleanupDebris();
    updatePhysics(dt);

    for (auto& obj : objects_) {
        if (!obj.active || obj.destroyed) continue;
        if (config_.enableCascading) checkCascadingFailure(obj.objectId, dt);
    }

    stats_.totalObjects = objects_.size();
    stats_.destroyedObjects = 0;
    stats_.activeDebris = 0;
    for (const auto& obj : objects_) { if (obj.destroyed) stats_.destroyedObjects++; }
    for (const auto& d : debris_) { if (d.active) stats_.activeDebris++; }
}

u32 DestructionSystem::createObject(const Vec3& position, f32 health) {
    DestructionObject obj;
    obj.objectId = nextObjectId_++;
    obj.position = position;
    obj.transform = Mat4::translation(position);
    obj.totalDamage = 0;
    obj.maxHealth = health;
    obj.currentHealth = health;
    obj.destroyed = false;
    obj.active = true;
    obj.mass = 1.0f;
    obj.inverseMass = 1.0f;
    obj.centerOfMass = position;
    obj.boundingRadius = 1.0f;
    objects_.push_back(obj);
    return obj.objectId;
}

void DestructionSystem::destroyObject(u32 objectId) {
    for (auto& obj : objects_) {
        if (obj.objectId == objectId) {
            obj.destroyed = true;
            obj.active = false;
            if (config_.generateDebris) generateDebris(objectId, obj.position, obj.boundingRadius, 8);
            return;
        }
    }
}

void DestructionSystem::applyDamage(u32 objectId, const Vec3& point, f32 damage, f32 radius) {
    DestructionEvent evt;
    evt.objectId = objectId;
    evt.impactPoint = point;
    evt.impactNormal = Vec3(0, 1, 0);
    evt.damage = damage;
    evt.radius = radius;
    evt.time = 0;
    eventQueue_.push_back(evt);
}

void DestructionSystem::applyDamage(u32 objectId, const Vec3& point, const Vec3& normal, f32 damage, f32 radius) {
    DestructionEvent evt;
    evt.objectId = objectId;
    evt.impactPoint = point;
    evt.impactNormal = normal;
    evt.damage = damage;
    evt.radius = radius;
    evt.time = 0;
    eventQueue_.push_back(evt);
}

void DestructionSystem::fractureObject(u32 objectId, const FracturePoint& point) {
    for (auto& obj : objects_) {
        if (obj.objectId == objectId) {
            switch (fractureMode_) {
                case FractureMode::Voronoi:
                    fractureVoronoi(objectId, point.position, point.radius, config_.maxFracturePoints);
                    break;
                case FractureMode::Radial:
                    fractureRadial(objectId, point.position, Vec3(0, 1, 0), point.radius, 8);
                    break;
                case FractureMode::Planar:
                    fracturePlanar(objectId, point.position, Vec3(0, 1, 0));
                    break;
                case FractureMode::Procedural:
                    fractureProcedural(objectId, point.position, point.radius);
                    break;
            }
            return;
        }
    }
}

void DestructionSystem::fractureVoronoi(u32 objectId, const Vec3& center, f32 radius, u32 pointCount) {
    for (auto& obj : objects_) {
        if (obj.objectId == objectId) {
            Vector<Vec3> sites;
            sites.resize(pointCount);
            generateVoronoiPoints(sites.data(), pointCount, center, radius, objectId);
            u32 baseTriCount = obj.triangles.size();
            for (u32 s = 0; s < pointCount; s++) {
                for (u32 t = 0; t < baseTriCount; t++) {
                    FractureTriangle tri = obj.triangles[t];
                    clipTriangleToCell(tri, sites[s], radius / pointCount);
                    if (tri.area > 0.001f) {
                        obj.triangles.push_back(tri);
                        stats_.totalTriangles++;
                    }
                }
            }
            stats_.totalFractures++;
            return;
        }
    }
}

void DestructionSystem::fractureRadial(u32 objectId, const Vec3& center, const Vec3& normal, f32 radius, u32 segments) {
    for (auto& obj : objects_) {
        if (obj.objectId == objectId) {
            Vec3 tangent, bitangent;
            if (std::abs(normal.y) < 0.99f) {
                tangent = normal.cross(Vec3(0, 1, 0)).normalized();
            } else {
                tangent = normal.cross(Vec3(1, 0, 0)).normalized();
            }
            bitangent = normal.cross(tangent).normalized();

            for (u32 s = 0; s < segments; s++) {
                f32 angle0 = (f32)s / segments * 2.0f * 3.14159265f;
                f32 angle1 = (f32)(s + 1) / segments * 2.0f * 3.14159265f;
                Vec3 dir0 = tangent * std::cos(angle0) + bitangent * std::sin(angle0);
                Vec3 dir1 = tangent * std::cos(angle1) + bitangent * std::sin(angle1);

                FractureTriangle tri;
                tri.indices[0] = obj.vertices.size();
                tri.indices[1] = obj.vertices.size() + 1;
                tri.indices[2] = obj.vertices.size() + 2;
                tri.normal = normal;
                tri.area = radius * radius * 0.5f;
                tri.materialIndex = 0;

                FractureVertex v0, v1, v2;
                v0.position = center;
                v1.position = center + dir0 * radius;
                v2.position = center + dir1 * radius;
                v0.normal = v1.normal = v2.normal = normal;
                obj.vertices.push_back(v0);
                obj.vertices.push_back(v1);
                obj.vertices.push_back(v2);
                obj.triangles.push_back(tri);
                stats_.totalTriangles++;
            }
            stats_.totalFractures++;
            return;
        }
    }
}

void DestructionSystem::fracturePlanar(u32 objectId, const Vec3& point, const Vec3& normal) {
    for (auto& obj : objects_) {
        if (obj.objectId == objectId) {
            Vector<FractureTriangle> newTris;
            for (auto& tri : obj.triangles) {
                Vec3 center = (obj.vertices[tri.indices[0]].position +
                               obj.vertices[tri.indices[1]].position +
                               obj.vertices[tri.indices[2]].position) / 3.0f;
                f32 dist = (center - point).dot(normal);
                if (dist > 0) {
                    newTris.push_back(tri);
                }
            }
            obj.triangles = newTris;
            stats_.totalFractures++;
            return;
        }
    }
}

void DestructionSystem::fractureProcedural(u32 objectId, const Vec3& center, f32 radius) {
    fractureVoronoi(objectId, center, radius, 8);
    fractureRadial(objectId, center, Vec3(0, 1, 0), radius, 6);
}

void DestructionSystem::generateDebris(u32 objectId, const Vec3& center, f32 radius, u32 count) {
    for (u32 i = 0; i < count && debris_.size() < config_.maxDebris; i++) {
        DebrisPiece piece;
        f32 r = config_.minDebrisSize + (f32)std::rand() / RAND_MAX * (config_.maxDebrisSize - config_.minDebrisSize);
        piece.position = center + Vec3(
            ((f32)std::rand() / RAND_MAX - 0.5f) * radius,
            ((f32)std::rand() / RAND_MAX - 0.5f) * radius,
            ((f32)std::rand() / RAND_MAX - 0.5f) * radius
        );
        piece.size = Vec3(r);
        piece.velocity = Vec3(
            ((f32)std::rand() / RAND_MAX - 0.5f) * config_.debrisImpulse,
            (f32)std::rand() / RAND_MAX * config_.debrisImpulse,
            ((f32)std::rand() / RAND_MAX - 0.5f) * config_.debrisImpulse
        );
        piece.angularVelocity = Vec3(
            ((f32)std::rand() / RAND_MAX - 0.5f) * config_.debrisSpin,
            ((f32)std::rand() / RAND_MAX - 0.5f) * config_.debrisSpin,
            ((f32)std::rand() / RAND_MAX - 0.5f) * config_.debrisSpin
        );
        piece.mass = r * r * r;
        piece.lifetime = config_.debrisLifetime;
        piece.age = 0;
        piece.parentObjectId = objectId;
        piece.type = DebrisType::Dynamic;
        piece.active = true;
        piece.drag = 0.98f;
        piece.angularDamping = 0.95f;
        piece.transform = Mat4::translation(piece.position);
        debris_.push_back(piece);
    }
}

void DestructionSystem::applyDebrisImpulse(u32 debrisIndex, const Vec3& impulse, const Vec3& angularImpulse) {
    if (debrisIndex < debris_.size()) {
        debris_[debrisIndex].velocity += impulse;
        debris_[debrisIndex].angularVelocity += angularImpulse;
    }
}

void DestructionSystem::updateDebris(f32 dt) {
    for (auto& piece : debris_) {
        if (!piece.active) continue;
        piece.age += dt;
        if (piece.age >= piece.lifetime) { piece.active = false; continue; }
        piece.velocity.y += config_.debrisGravity * dt;
        piece.velocity *= piece.drag;
        piece.angularVelocity *= piece.angularDamping;
        piece.position += piece.velocity * dt;
        piece.transform = Mat4::translation(piece.position);
    }
}

void DestructionSystem::cleanupDebris() {
    for (usize i = 0; i < debris_.size(); ) {
        if (!debris_[i].active) debris_.erase(i);
        else ++i;
    }
}

void DestructionSystem::computeStructuralIntegrity(u32 objectId) {
    for (auto& obj : objects_) {
        if (obj.objectId == objectId) {
            for (auto& node : obj.structuralNodes) {
                node.integrity = node.maxIntegrity - node.damage;
                if (node.integrity < 0) node.integrity = 0;
                if (node.integrity < config_.structuralIntegrityThreshold * node.maxIntegrity) {
                    node.fractured = true;
                }
            }
            return;
        }
    }
}

void DestructionSystem::propagateDamage(u32 objectId, const Vec3& point, f32 damage, f32 radius) {
    for (auto& obj : objects_) {
        if (obj.objectId == objectId) {
            for (auto& node : obj.structuralNodes) {
                f32 dist = (node.position - point).length();
                if (dist < radius) {
                    f32 falloff = 1.0f - (dist / radius);
                    node.damage += damage * falloff;
                }
            }
            return;
        }
    }
}

void DestructionSystem::checkCascadingFailure(u32 objectId, f32 dt) {
    (void)dt;
    for (auto& obj : objects_) {
        if (obj.objectId != objectId) continue;
        for (auto& node : obj.structuralNodes) {
            if (node.fractured) {
                for (u32 childId : node.children) {
                    if (childId < obj.structuralNodes.size()) {
                        StructuralNode& child = obj.structuralNodes[childId];
                        child.damage += config_.cascadeDamageRadius * 0.1f;
                    }
                }
            }
        }
    }
}

bool DestructionSystem::isStructurallySound(u32 objectId) const {
    for (const auto& obj : objects_) {
        if (obj.objectId == objectId) {
            for (const auto& node : obj.structuralNodes) {
                if (node.integrity < config_.structuralIntegrityThreshold * node.maxIntegrity) return false;
            }
            return true;
        }
    }
    return false;
}

f32 DestructionSystem::computeNodeIntegrity(const StructuralNode& node) const {
    return node.maxIntegrity - node.damage;
}

void DestructionSystem::applyForce(u32 objectId, const Vec3& force) {
    for (auto& obj : objects_) {
        if (obj.objectId == objectId) {
            Vec3 accel = force * obj.inverseMass;
            obj.position += accel * 0.016f;
            return;
        }
    }
}

void DestructionSystem::applyTorque(u32 objectId, const Vec3& torque) {
    (void)objectId; (void)torque;
}

void DestructionSystem::applyImpulse(u32 objectId, const Vec3& impulse, const Vec3& point) {
    for (auto& obj : objects_) {
        if (obj.objectId == objectId) {
            Vec3 velocityChange = impulse * obj.inverseMass;
            obj.position += velocityChange * 0.016f;
            Vec3 r = point - obj.centerOfMass;
            Vec3 angularImpulse = r.cross(impulse);
            obj.position += angularImpulse * 0.016f;
            return;
        }
    }
}

void DestructionSystem::updatePhysics(f32 dt) {
    for (auto& obj : objects_) {
        if (!obj.active || obj.destroyed) continue;
        obj.transform = Mat4::translation(obj.position);
    }
}

void DestructionSystem::setFractureMode(FractureMode mode) { fractureMode_ = mode; }
void DestructionSystem::setMaxDamage(f32 damage) { config_.maxDamage = damage; }
void DestructionSystem::setFractureThreshold(f32 threshold) { config_.fractureThreshold = threshold; }
void DestructionSystem::setStructuralIntegrityThreshold(f32 threshold) { config_.structuralIntegrityThreshold = threshold; }
void DestructionSystem::setEnableCascading(bool enable) { config_.enableCascading = enable; }
void DestructionSystem::setMaxDebris(u32 max) { config_.maxDebris = max; }

DestructionObject* DestructionSystem::getObject(u32 objectId) {
    for (auto& obj : objects_) { if (obj.objectId == objectId) return &obj; }
    return nullptr;
}

const DestructionObject* DestructionSystem::getObject(u32 objectId) const {
    for (const auto& obj : objects_) { if (obj.objectId == objectId) return &obj; }
    return nullptr;
}

DebrisPiece* DestructionSystem::getDebris(u32 index) { return (index < debris_.size()) ? &debris_[index] : nullptr; }
const DebrisPiece* DestructionSystem::getDebris(u32 index) const { return (index < debris_.size()) ? &debris_[index] : nullptr; }
u32 DestructionSystem::getDebrisCount() const { return debris_.size(); }
u32 DestructionSystem::getObjectCount() const { return objects_.size(); }

DestructionStats DestructionSystem::getStats() const { return stats_; }
void DestructionSystem::resetStats() { stats_ = {}; }

void DestructionSystem::printStats() const {
    FROST_LOG_INFO("[DestructionSystem] Objects: %u (destroyed=%u), Debris: %u, Fractures: %u",
        stats_.totalObjects, stats_.destroyedObjects, stats_.activeDebris, stats_.totalFractures);
}

void DestructionSystem::generateVoronoiPoints(Vec3* points, u32 count, const Vec3& center, f32 radius, u32 seed) const {
    for (u32 i = 0; i < count; i++) {
        f32 r = (f32)std::rand() / RAND_MAX;
        f32 theta = (f32)std::rand() / RAND_MAX * 2.0f * 3.14159265f;
        f32 phi = (f32)std::rand() / RAND_MAX * 3.14159265f;
        f32 x = r * std::sin(phi) * std::cos(theta);
        f32 y = r * std::sin(phi) * std::sin(theta);
        f32 z = r * std::cos(phi);
        points[i] = center + Vec3(x, y, z) * radius * config_.fractureRandomness;
    }
}

u32 DestructionSystem::computeVoronoiCell(const Vec3& point, const Vec3* sites, u32 siteCount) const {
    u32 closest = 0;
    f32 minDist = 1e10f;
    for (u32 i = 0; i < siteCount; i++) {
        f32 dist = (point - sites[i]).lengthSquared();
        if (dist < minDist) { minDist = dist; closest = i; }
    }
    return closest;
}

void DestructionSystem::clipTriangleToCell(FractureTriangle& tri, const Vec3& cellCenter, f32 cellRadius) const {
    Vec3 center;
    center.x = (tri.indices[0] + tri.indices[1] + tri.indices[2]) / 3.0f;
    center.y = (tri.indices[0] + tri.indices[1] + tri.indices[2]) / 3.0f;
    center.z = (tri.indices[0] + tri.indices[1] + tri.indices[2]) / 3.0f;
    f32 dist = (center - cellCenter).length();
    tri.area *= Mathf::max(0.0f, 1.0f - dist / cellRadius);
}

void DestructionSystem::computeRadialSegments(Vec3* vertices, u32 segments, const Vec3& center, const Vec3& normal, f32 radius) const {
    Vec3 tangent, bitangent;
    if (std::abs(normal.y) < 0.99f) {
        tangent = normal.cross(Vec3(0, 1, 0)).normalized();
    } else {
        tangent = normal.cross(Vec3(1, 0, 0)).normalized();
    }
    bitangent = normal.cross(tangent).normalized();
    for (u32 i = 0; i < segments; i++) {
        f32 angle = (f32)i / segments * 2.0f * 3.14159265f;
        vertices[i] = center + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
    }
}

void DestructionSystem::computePlanarCut(Vec3* outVertices, u32& outCount, const Vec3* inVertices, u32 inCount, const Vec3& point, const Vec3& normal) const {
    outCount = 0;
    for (u32 i = 0; i < inCount; i++) {
        f32 d = (inVertices[i] - point).dot(normal);
        if (d > 0) outVertices[outCount++] = inVertices[i];
    }
}

void DestructionSystem::addStructuralNode(u32 objectId, const Vec3& position, f32 integrity) {
    for (auto& obj : objects_) {
        if (obj.objectId == objectId) {
            StructuralNode node;
            node.nodeId = obj.structuralNodes.size();
            node.position = position;
            node.integrity = integrity;
            node.maxIntegrity = integrity;
            node.damage = 0;
            node.parentNodeId = 0xFFFFFFFF;
            node.fractured = false;
            obj.structuralNodes.push_back(node);
            return;
        }
    }
}

void DestructionSystem::connectStructuralNodes(u32 objectId, u32 nodeA, u32 nodeB) {
    for (auto& obj : objects_) {
        if (obj.objectId == objectId) {
            if (nodeA < obj.structuralNodes.size() && nodeB < obj.structuralNodes.size()) {
                obj.structuralNodes[nodeA].connections.push_back(nodeB);
                obj.structuralNodes[nodeB].connections.push_back(nodeA);
            }
            return;
        }
    }
}

void DestructionSystem::removeStructuralNode(u32 objectId, u32 nodeId) {
    for (auto& obj : objects_) {
        if (obj.objectId == objectId && nodeId < obj.structuralNodes.size()) {
            obj.structuralNodes[nodeId].fractured = true;
            for (u32 conn : obj.structuralNodes[nodeId].connections) {
                if (conn < obj.structuralNodes.size()) {
                    auto& connections = obj.structuralNodes[conn].connections;
                    for (usize j = 0; j < connections.size(); j++) {
                        if (connections[j] == nodeId) {
                            connections.erase(j);
                            break;
                        }
                    }
                }
            }
            return;
        }
    }
}

void DestructionSystem::queueDestructionEvent(const DestructionEvent& event) { eventQueue_.push_back(event); }

void DestructionSystem::processDestructionEvents(f32 dt) {
    (void)dt;
    for (const auto& evt : eventQueue_) {
        for (auto& obj : objects_) {
            if (obj.objectId == evt.objectId) {
                obj.totalDamage += evt.damage;
                obj.currentHealth -= evt.damage;
                if (obj.currentHealth <= 0) {
                    obj.destroyed = true;
                    obj.active = false;
                    if (config_.generateDebris) generateDebris(evt.objectId, evt.impactPoint, evt.radius, 8);
                } else {
                    FracturePoint fp;
                    fp.position = evt.impactPoint;
                    fp.radius = evt.radius;
                    fp.damage = evt.damage;
                    fp.randomness = config_.fractureRandomness;
                    fp.seed = obj.objectId;
                    fractureObject(evt.objectId, fp);
                    propagateDamage(evt.objectId, evt.impactPoint, evt.damage, evt.radius);
                }
                break;
            }
        }
    }
    eventQueue_.clear();
}

}
