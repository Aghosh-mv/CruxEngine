#pragma once

// ============================================================================
// FrostEngine Editor Viewport — 3D scene view with gizmos
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

namespace Frost {

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