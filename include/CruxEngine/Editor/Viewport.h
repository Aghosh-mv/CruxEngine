#pragma once

// ============================================================================
// CruxEngine Editor Viewport — 3D scene view with gizmos
// ============================================================================
// Features:
//   - Multiple camera modes (perspective, ortho, free, orbit)
//   - Grid, axes, bounds visualization
//   - Selection outline/highlight
//   - Gizmo: translate, rotate, scale (world/local space)
//   - Frustum culling visualization
//   - Wireframe/shaded/unlit modes
//   - Post-process preview
//   - Render stats overlay
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Math.h"
#include "Editor/Editor.h"

namespace Crux {

struct ViewportGizmo {
    enum Mode { Translate, Rotate, Scale } mode = Translate;
    enum Space { World, Local } space = World;
    enum Axis { None, X, Y, Z, XY, XZ, YZ, All } activeAxis = None;
    bool visible = true;
    bool snapping = false;
    f32 snapTranslate = 1.0f;
    f32 snapRotate = 15.0f;
    f32 snapScale = 0.1f;

    Vec3 dragStart;
    Vec3 dragDelta;
    bool dragging = false;

    // Gizmo geometry (rendered as lines)
    struct Line { Vec3 a, b; Color color; };
    Vector<Line> lines;
    u32 vao = 0, vbo = 0;

    void buildGeometry();
    void render(const Mat4& viewProj);
    Axis hitTest(const Vec2& screenPos, const Mat4& viewProj, u32 width, u32 height);
    Vec3 axisDirection(Axis axis) const;
};

struct ViewportCamera {
    enum Mode { Perspective, Orthographic, Free, Orbit } mode = Perspective;

    Vec3 position{0, 10, -20};
    Vec3 target{0, 0, 0};
    Vec3 up{0, 1, 0};

    // Orbit
    f32 distance = 25.0f;
    f32 yaw = 0.0f;
    f32 pitch = -0.3f;

    // Projection
    f32 fov = 60.0f;
    f32 nearPlane = 0.1f;
    f32 farPlane = 10000.0f;
    f32 orthoSize = 20.0f;

    // Movement
    f32 moveSpeed = 20.0f;
    f32 sprintMultiplier = 3.0f;
    f32 orbitSpeed = 0.005f;
    f32 zoomSpeed = 2.0f;

    Mat4 viewMatrix() const {
        if (mode == Mode::Orbit) {
            Vec3 pos = position();
            return Mat4::lookAt(pos, target, up);
        }
        return Mat4::lookAt(position, position + forward(), up);
    }

    Mat4 projectionMatrix(f32 aspect) const {
        if (mode == Mode::Orthographic) {
            f32 h = orthoSize;
            f32 w = h * aspect;
            Mat4 r = Mat4::identity();
            r.m[0] = 1.0f / w;
            r.m[5] = 1.0f / h;
            r.m[10] = -2.0f / (farPlane - nearPlane);
            r.m[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
            return r;
        }
        return Mat4::perspective(fov * 0.017453f, aspect, nearPlane, farPlane);
    }

    Vec3 position() const {
        if (mode == Mode::Orbit) {
            return Vec3(
                target.x + distance * cosf(pitch) * sinf(yaw),
                target.y + distance * sinf(pitch),
                target.z + distance * cosf(pitch) * cosf(yaw)
            );
        }
        return position;
    }

    Vec3 forward() const {
        if (mode == Mode::Orbit) {
            return (target - position()).normalized();
        }
        return (target - position).normalized();
    }

    Vec3 right() const { return forward().cross(up).normalized(); }

    void orbit(f32 dx, f32 dy) {
        yaw += dx * orbitSpeed;
        pitch += dy * orbitSpeed;
        pitch = Math::clamp(pitch, -1.5f, 1.5f);
    }

    void zoom(f32 delta) {
        if (mode == Mode::Orbit) {
            distance -= delta * zoomSpeed;
            distance = Math::clamp(distance, 0.5f, 10000.0f);
        } else {
            position += forward() * delta * zoomSpeed * moveSpeed * 0.1f;
        }
    }

    void pan(f32 dx, f32 dy) {
        Vec3 r = right();
        Vec3 u = up;
        Vec3 offset = r * dx * moveSpeed * 0.01f + u * dy * moveSpeed * 0.01f;
        if (mode == Mode::Orbit) target += offset;
        else { position += offset; target += offset; }
    }

    void moveForward(f32 dt) { position += forward() * moveSpeed * dt; target += forward() * moveSpeed * dt; }
    void moveBackward(f32 dt) { position -= forward() * moveSpeed * dt; target -= forward() * moveSpeed * dt; }
    void moveLeft(f32 dt) { position -= right() * moveSpeed * dt; target -= right() * moveSpeed * dt; }
    void moveRight(f32 dt) { position += right() * moveSpeed * dt; target += right() * moveSpeed * dt; }
    void moveUp(f32 dt) { position += up * moveSpeed * dt; target += up * moveSpeed * dt; }
    void moveDown(f32 dt) { position -= up * moveSpeed * dt; target -= up * moveSpeed * dt; }

    void focusOn(Vec3 pos, f32 dist = 10.0f) {
        target = pos;
        distance = dist;
    }

    // Frustum planes for culling
    Vec4 frustumPlanes[6];
    void updateFrustum(const Mat4& viewProj) {
        // Left
        frustumPlanes[0] = Vec4(viewProj.m[3] + viewProj.m[0], viewProj.m[7] + viewProj.m[4], viewProj.m[11] + viewProj.m[8], viewProj.m[15] + viewProj.m[12]);
        // Right
        frustumPlanes[1] = Vec4(viewProj.m[3] - viewProj.m[0], viewProj.m[7] - viewProj.m[4], viewProj.m[11] - viewProj.m[8], viewProj.m[15] - viewProj.m[12]);
        // Bottom
        frustumPlanes[2] = Vec4(viewProj.m[3] + viewProj.m[1], viewProj.m[7] + viewProj.m[5], viewProj.m[11] + viewProj.m[9], viewProj.m[15] + viewProj.m[13]);
        // Top
        frustumPlanes[3] = Vec4(viewProj.m[3] - viewProj.m[1], viewProj.m[7] - viewProj.m[5], viewProj.m[11] - viewProj.m[9], viewProj.m[15] - viewProj.m[13]);
        // Near
        frustumPlanes[4] = Vec4(viewProj.m[3] + viewProj.m[2], viewProj.m[7] + viewProj.m[6], viewProj.m[11] + viewProj.m[10], viewProj.m[15] + viewProj.m[14]);
        // Far
        frustumPlanes[5] = Vec4(viewProj.m[3] - viewProj.m[2], viewProj.m[7] - viewProj.m[6], viewProj.m[11] - viewProj.m[10], viewProj.m[15] - viewProj.m[14]);
        for (int i = 0; i < 6; i++) {
            f32 len = sqrtf(frustumPlanes[i].x*frustumPlanes[i].x + frustumPlanes[i].y*frustumPlanes[i].y + frustumPlanes[i].z*frustumPlanes[i].z);
            if (len > 0) { frustumPlanes[i].x /= len; frustumPlanes[i].y /= len; frustumPlanes[i].z /= len; frustumPlanes[i].w /= len; }
        }
    }

    bool sphereInFrustum(Vec3 center, f32 radius) const {
        for (int i = 0; i < 6; i++) {
            f32 d = frustumPlanes[i].x * center.x + frustumPlanes[i].y * center.y + frustumPlanes[i].z * center.z + frustumPlanes[i].w;
            if (d < -radius) return false;
        }
        return true;
    }
};

struct ViewportStats {
    u32 triangles = 0;
    u32 drawCalls = 0;
    u32 shadows = 0;
    f32 gpuTimeMs = 0.0f;
    f32 cpuTimeMs = 0.0f;
    u32 visibleEntities = 0;
    u32 culledEntities = 0;
};

class EditorViewport {
public:
    bool init(u32 width, u32 height) {
        size_ = Vec2((f32)width, (f32)height);
        camera_.mode = ViewportCamera::Perspective;
        camera_.position = Vec3(0, 15, -30);
        camera_.target = Vec3(0, 5, 0);
        gizmo_.buildGeometry();
        return true;
    }

    void resize(u32 w, u32 h) { size_ = Vec2((f32)w, (f32)h); }

    void beginFrame() {
        Mat4 vp = camera_.projectionMatrix(size_.x / size_.y) * camera_.viewMatrix();
        camera_.updateFrustum(vp);
    }

    // Input handling
    void onMouseDown(Vec2 pos, int button, bool shift, bool ctrl, bool alt);
    void onMouseUp(Vec2 pos, int button);
    void onMouseMove(Vec2 pos, Vec2 delta, bool shift, bool ctrl, bool alt);
    void onMouseWheel(f32 delta, bool ctrl);
    void onKeyDown(int key, bool shift, bool ctrl, bool alt);
    void onKeyUp(int key);

    // Rendering
    void render(const Mat4& viewProj);
    void renderGrid(const Mat4& viewProj);
    void renderAxes(const Mat4& viewProj);
    void renderSelectionOutline(const Mat4& viewProj);
    void renderGizmo(const Mat4& viewProj);
    void renderStats();

    // Camera control
    void setCameraMode(ViewportCamera::Mode m) { camera_.mode = m; }
    void focusSelection(Vec3 center, f32 radius) { camera_.focusOn(center, radius * 2.0f); }

    // Selection
    void setSelectedEntity(u32 id) { selectedEntity_ = id; }
    u32 selectedEntity() const { return selectedEntity_; }
    void setHoveredEntity(u32 id) { hoveredEntity_ = id; }
    u32 hoveredEntity() const { return hoveredEntity_; }

    // Gizmo
    void setGizmoMode(ViewportGizmo::Mode m) { gizmo_.mode = m; }
    void setGizmoSpace(ViewportGizmo::Space s) { gizmo_.space = s; }
    void cycleGizmoMode() {
        gizmo_.mode = (ViewportGizmo::Mode)(((u32)gizmo_.mode + 1) % 3);
    }

    // Render mode
    enum RenderMode { Shaded, Wireframe, Unlit, Normals, UV, Depth } renderMode_ = Shaded;
    void setRenderMode(RenderMode m) { renderMode_ = m; }

    // Stats
    void updateStats(const ViewportStats& s) { stats_ = s; }
    const ViewportStats& stats() const { return stats_; }

    // Camera access
    ViewportCamera& camera() { return camera_; }
    const ViewportCamera& camera() const { return camera_; }
    ViewportGizmo& gizmo() { return gizmo_; }
    Vec2 size() const { return size_; }

private:
    Vec2 size_;
    ViewportCamera camera_;
    ViewportGizmo gizmo_;
    ViewportStats stats_;
    u32 selectedEntity_ = 0xFFFFFFFF;
    u32 hoveredEntity_ = 0xFFFFFFFF;
    Vec2 lastMousePos_;
    bool mouseDown_[3] = {false, false, false};
};

}