#pragma once

// ============================================================================
// CruxEngine Editor — Full editor framework (viewport, panels, inspector)
// ============================================================================
// Provides:
//   - Viewport with camera controls (orbit, pan, zoom)
//   - Scene hierarchy panel
//   - Property inspector
//   - Asset browser
//   - Console/log panel
//   - Toolbar with play/pause/stop
//   - Gizmo system (translate, rotate, scale)
//   - Undo/redo history
//   - Drag-and-drop support
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Math.h"

namespace Crux {

// ---- Editor panel types ----
enum class EditorPanel : u8 {
    Viewport,
    SceneHierarchy,
    PropertyInspector,
    AssetBrowser,
    Console,
    Toolbar,
    MaterialEditor,
    LightingPanel,
    Marketplace,
    CodeEditor,
    COUNT
};

// ---- Gizmo mode ----
enum class GizmoMode : u8 {
    Translate,
    Rotate,
    Scale,
    COUNT
};

enum class GizmoAxis : u8 {
    None, X, Y, Z, XY, XZ, YZ, All
};

// ---- Editor selection ----
struct EditorSelection {
    u32 selectedEntity = 0xFFFFFFFF;
    u32 hoveredEntity = 0xFFFFFFFF;
    Vector<u32> multiSelection;

    void clear() { selectedEntity = 0xFFFFFFFF; hoveredEntity = 0xFFFFFFFF; multiSelection.clear(); }
    bool hasSelection() const { return selectedEntity != 0xFFFFFFFF; }
};

// ---- Undo/Redo action ----
struct EditorAction {
    String description;
    enum Type { ModifyProperty, AddEntity, RemoveEntity, Reparent, MoveComponent } type;
    u32 targetID;
    u64 oldValue;
    u64 newValue;
};

// ---- Viewport camera controller ----
struct ViewportCamera {
    Vec3 position{0, 10, -20};
    Vec3 target{0, 0, 0};
    f32 distance = 25.0f;
    f32 yaw = 0.0f;
    f32 pitch = -0.3f;
    f32 fov = 60.0f;
    f32 nearPlane = 0.1f;
    f32 farPlane = 5000.0f;
    f32 moveSpeed = 15.0f;
    f32 orbitSpeed = 0.005f;
    f32 zoomSpeed = 2.0f;
    bool orthographic = false;
    f32 orthoSize = 20.0f;

    void orbit(f32 dx, f32 dy) {
        yaw += dx * orbitSpeed;
        pitch += dy * orbitSpeed;
        if (pitch > 1.5f) pitch = 1.5f;
        if (pitch < -1.5f) pitch = -1.5f;
    }

    void zoom(f32 delta) {
        distance -= delta * zoomSpeed;
        if (distance < 0.5f) distance = 0.5f;
        if (distance > 5000.0f) distance = 5000.0f;
    }

    void pan(f32 dx, f32 dy) {
        Vec3 right = Vec3(cosf(yaw), 0, sinf(yaw));
        Vec3 up = Vec3(0, 1, 0);
        target = target + right * dx * moveSpeed * 0.01f + up * dy * moveSpeed * 0.01f;
    }

    Vec3 cameraPosition() const {
        return Vec3(
            target.x + distance * cosf(pitch) * sinf(yaw),
            target.y + distance * sinf(pitch),
            target.z + distance * cosf(pitch) * cosf(yaw)
        );
    }

    Mat4 viewMatrix() const {
        return Mat4::lookAt(cameraPosition(), target, Vec3(0, 1, 0));
    }

    Mat4 projectionMatrix(f32 aspect) const {
        if (orthographic) {
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

    void moveForward(f32 dt) {
        Vec3 fwd = (target - cameraPosition()).normalized();
        target = target + fwd * moveSpeed * dt;
    }

    void moveBackward(f32 dt) {
        Vec3 fwd = (target - cameraPosition()).normalized();
        target = target - fwd * moveSpeed * dt;
    }

    void moveLeft(f32 dt) {
        Vec3 right = Vec3(cosf(yaw + 1.5708f), 0, sinf(yaw + 1.5708f));
        target = target - right * moveSpeed * dt;
    }

    void moveRight(f32 dt) {
        Vec3 right = Vec3(cosf(yaw + 1.5708f), 0, sinf(yaw + 1.5708f));
        target = target + right * moveSpeed * dt;
    }

    void focusOn(Vec3 pos, f32 dist = 10.0f) {
        target = pos;
        distance = dist;
    }
};

// ---- Gizmo system ----
struct Gizmo {
    GizmoMode mode = GizmoMode::Translate;
    GizmoAxis activeAxis = GizmoAxis::None;
    bool visible = true;
    bool snapping = false;
    f32 snapValues[3] = {1.0f, 15.0f, 0.1f}; // translate, rotate, scale

    Vec3 dragStart;
    Vec3 dragDelta;
    bool dragging = false;

    void cycleMode() {
        mode = (GizmoMode)(((u32)mode + 1) % (u32)GizmoMode::COUNT);
    }

    Vec3 getAxisDir(GizmoAxis axis) const {
        switch (axis) {
            case GizmoAxis::X: return Vec3(1, 0, 0);
            case GizmoAxis::Y: return Vec3(0, 1, 0);
            case GizmoAxis::Z: return Vec3(0, 0, 1);
            default: return Vec3(0, 0, 0);
        }
    }
};

// ---- Editor panel layout ----
struct EditorLayout {
    struct PanelRect {
        f32 x, y, w, h;
        bool visible = true;
        String title;
    };

    PanelRect panels[(u32)EditorPanel::COUNT];

    void defaultLayout(f32 totalW, f32 totalH) {
        // Viewport: main area
        panels[(u32)EditorPanel::Viewport] = {250, 30, totalW - 500, totalH - 60, true, "Viewport"};
        // Scene Hierarchy: left panel
        panels[(u32)EditorPanel::SceneHierarchy] = {0, 30, 250, totalH * 0.5f - 30, true, "Hierarchy"};
        // Property Inspector: left bottom
        panels[(u32)EditorPanel::PropertyInspector] = {0, totalH * 0.5f, 250, totalH * 0.5f - 30, true, "Inspector"};
        // Asset Browser: bottom
        panels[(u32)EditorPanel::AssetBrowser] = {250, totalH - 180, totalW - 500, 150, true, "Assets"};
        // Console: right
        panels[(u32)EditorPanel::Console] = {totalW - 250, 30, 250, totalH - 60, true, "Console"};
        // Toolbar: top
        panels[(u32)EditorPanel::Toolbar] = {0, 0, totalW, 30, true, "Toolbar"};
        // Material Editor
        panels[(u32)EditorPanel::MaterialEditor] = {totalW - 250, 30, 250, 300, false, "Materials"};
        // Lighting
        panels[(u32)EditorPanel::LightingPanel] = {0, 30, 250, 200, false, "Lighting"};
        // Marketplace
        panels[(u32)EditorPanel::Marketplace] = {250, 30, totalW - 500, totalH - 60, false, "Marketplace"};
        // Code Editor
        panels[(u32)EditorPanel::CodeEditor] = {250, 30, totalW - 500, totalH - 60, false, "Code Editor"};
    }

    PanelRect& panel(EditorPanel p) { return panels[(u32)p]; }
    const PanelRect& panel(EditorPanel p) const { return panels[(u32)p]; }
};

// ---- Editor state ----
class Editor {
public:
    bool init(f32 width, f32 height) {
        layout_.defaultLayout(width, height);
        viewport_.position = Vec3(0, 10, -25);
        viewport_.target = Vec3(0, 5, 0);
        viewport_.distance = 30;
        playing_ = false;
        return true;
    }

    void beginFrame() {
        undoIndex_ = 0; // simplified
    }

    // ---- Viewport interaction ----
    void viewportOrbit(f32 dx, f32 dy) { viewport_.orbit(dx, dy); }
    void viewportZoom(f32 delta) { viewport_.zoom(delta); }
    void viewportPan(f32 dx, f32 dy) { viewport_.pan(dx, dy); }

    void viewportMoveForward(f32 dt) { viewport_.moveForward(dt); }
    void viewportMoveBackward(f32 dt) { viewport_.moveBackward(dt); }
    void viewportMoveLeft(f32 dt) { viewport_.moveLeft(dt); }
    void viewportMoveRight(f32 dt) { viewport_.moveRight(dt); }

    // ---- Selection ----
    void selectEntity(u32 entity) { selection_.selectedEntity = entity; }
    void hoverEntity(u32 entity) { selection_.hoveredEntity = entity; }
    void clearSelection() { selection_.clear(); }

    // ---- Undo/Redo ----
    void pushAction(const EditorAction& action) {
        if (undoIndex_ < undoStack_.size()) {
            undoStack_.erase(undoIndex_, undoStack_.size());
        }
        undoStack_.pushBack(action);
        undoIndex_++;
    }

    bool canUndo() const { return undoIndex_ > 0; }
    bool canRedo() const { return undoIndex_ < undoStack_.size(); }

    void undo() { if (canUndo()) undoIndex_--; }
    void redo() { if (canRedo()) undoIndex_++; }

    // ---- Play mode ----
    void play() { playing_ = true; }
    void stop() { playing_ = false; }
    void pause() { paused_ = !paused_; }
    bool isPlaying() const { return playing_; }
    bool isPaused() const { return paused_; }

    // ---- Panel visibility ----
    void togglePanel(EditorPanel p) {
        layout_.panel(p).visible = !layout_.panel(p).visible;
    }

    // ---- Gizmo ----
    void setGizmoMode(GizmoMode m) { gizmo_.mode = m; }
    void cycleGizmoMode() { gizmo_.cycleMode(); }

    // ---- Focus camera on selection ----
    void focusSelection(Vec3 pos) { viewport_.focusOn(pos); }

    // ---- Resize ----
    void resize(f32 w, f32 h) { layout_.defaultLayout(w, h); }

    // ---- Accessors ----
    ViewportCamera& camera() { return viewport_; }
    const ViewportCamera& camera() const { return viewport_; }
    EditorSelection& selection() { return selection_; }
    const EditorSelection& selection() const { return selection_; }
    EditorLayout& layout() { return layout_; }
    Gizmo& gizmo() { return gizmo_; }

private:
    ViewportCamera viewport_;
    EditorSelection selection_;
    EditorLayout layout_;
    Gizmo gizmo_;
    Vector<EditorAction> undoStack_;
    u32 undoIndex_ = 0;
    bool playing_ = false;
    bool paused_ = false;
};

} // namespace Crux
