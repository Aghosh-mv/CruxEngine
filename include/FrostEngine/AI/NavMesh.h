#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Math.h"

namespace Frost {

struct NavTriangle {
    Vec3 vertices[3];
    Vec3 center;
    Vec3 normal;
    i32 neighbors[3] = {-1, -1, -1};
    i32 areaId = 0;
    f32 cost = 1.0f;
    bool walkable = true;
};

struct NavPath {
    Vector<Vec3> points;
    f32 totalLength = 0.0f;
    i32 currentWaypoint = 0;

    Vec3 getCurrentWaypoint() const {
        if (currentWaypoint >= 0 && currentWaypoint < (i32)points.size())
            return points[currentWaypoint];
        return Vec3(0);
    }

    void advanceWaypoint() {
        if (currentWaypoint < (i32)points.size() - 1) currentWaypoint++;
    }

    bool reachedEnd() const {
        return currentWaypoint >= (i32)points.size() - 1;
    }

    void clear() { points.clear(); totalLength = 0.0f; currentWaypoint = 0; }

    f32 distanceToEnd(const Vec3& position) const {
        if (points.empty()) return 0.0f;
        f32 total = 0.0f;
        Vec3 prev = position;
        for (u32 i = (u32)currentWaypoint; i < points.size(); i++) {
            total += (points[i] - prev).length();
            prev = points[i];
        }
        return total;
    }
};

struct NavMeshPathfindResult {
    Vector<i32> triangles;
    Vector<Vec3> waypoints;
    bool found = false;
};

class NavMesh {
public:
    static constexpr u32 MAX_TRIANGLES = 16384;
    static constexpr u32 MAX_VERTICES = 32768;

    NavMesh() = default;
    ~NavMesh() = default;

    bool buildFromTerrain(f32(*heightFn)(f32 x, f32 z, void*), void* userData,
                          f32 terrainMinX, f32 terrainMinZ,
                          f32 terrainMaxX, f32 terrainMaxZ,
                          f32 stepSize = 2.0f, f32 agentHeight = 2.0f,
                          f32 agentRadius = 0.5f, f32 agentMaxSlope = 45.0f);

    void addTriangle(const Vec3& v0, const Vec3& v1, const Vec3& v2, i32 areaId = 0);
    void buildConnectivity();
    void clear();

    NavMeshPathfindResult findPath(const Vec3& start, const Vec3& end) const;
    Vector<Vec3> smoothPath(const Vector<Vec3>& rawPath) const;
    Vec3 getClosestPoint(const Vec3& point) const;
    i32 getTriangleAt(const Vec3& point) const;
    Vec3 randomPoint() const;
    Vec3 randomPointInRadius(const Vec3& center, f32 radius) const;

    u32 triangleCount() const { return (u32)triangles_.size(); }
    const NavTriangle& triangle(u32 index) const { return triangles_[index]; }
    bool valid() const { return !triangles_.empty(); }

    struct RaycastResult {
        bool hit = false;
        Vec3 hitPoint{0, 0, 0};
        f32 distance = 0.0f;
        i32 triangleIndex = -1;
    };
    RaycastResult raycast(const Vec3& start, const Vec3& end) const;

    void markArea(const Vec3& center, f32 radius, i32 areaId);
    void setTriangleCost(u32 index, f32 cost);
    void setTriangleWalkable(u32 index, bool walkable);

    f32 getPathLength(const Vec3& start, const Vec3& end) const;

private:
    i32 findNearestTriangle(const Vec3& point) const;
    i32 findPathAStar(i32 startTri, i32 endTri, Vector<i32>& path) const;
    f32 heuristic(i32 a, i32 b) const;
    i32 findEdgeShared(i32 triA, i32 triB) const;
    Vec3 getPortalPoint(i32 triA, i32 triB) const;

    Vector<NavTriangle> triangles_;
};

}
