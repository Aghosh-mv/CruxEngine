#include "FrostEngine/Editor/Viewport.h"

namespace Frost {

void EditorViewport::onMouseDown(Vec2 pos, int button, bool shift, bool ctrl, bool alt) {
    (void)shift; (void)ctrl; (void)alt;
    mouseDown_[button] = true;
    lastMousePos_ = pos;

    if (button == 1) {
        // Middle mouse: orbit
    } else if (button == 2) {
        // Right mouse: look around
    } else if (button == 0) {
        // Left mouse: select or gizmo interaction
        if (gizmo_.visible && selectedEntity_ != 0xFFFFFFFF) {
            gizmo_.activeAxis = gizmo_.hitTest(pos, camera_.projectionMatrix(size_.x / size_.y) * camera_.viewMatrix(), (u32)size_.x, (u32)size_.y);
            if (gizmo_.activeAxis != ViewportGizmo::None) {
                gizmo_.dragging = true;
                gizmo_.dragStart = Vec3(pos.x, pos.y, 0);
            }
        }
    }
}

void EditorViewport::onMouseUp(Vec2 pos, int button) {
    (void)pos;
    mouseDown_[button] = false;
    if (button == 0) {
        gizmo_.dragging = false;
        gizmo_.activeAxis = ViewportGizmo::None;
    }
}

void EditorViewport::onMouseMove(Vec2 pos, Vec2 delta, bool shift, bool ctrl, bool alt) {
    (void)alt;
    if (mouseDown_[1]) {
        // Middle mouse: orbit
        camera_.orbit(delta.x, delta.y);
    } else if (mouseDown_[2]) {
        // Right mouse: WASD-style look
        if (!shift && !ctrl) {
            camera_.yaw += delta.x * camera_.orbitSpeed;
            camera_.pitch += delta.y * camera_.orbitSpeed;
            camera_.pitch = Mathf::clamp(camera_.pitch, -1.5f, 1.5f);
        }
    } else if (mouseDown_[0]) {
        // Left mouse: pan (with shift) or gizmo drag
        if (shift) {
            camera_.pan(delta.x, delta.y);
        } else if (gizmo_.dragging && selectedEntity_ != 0xFFFFFFFF) {
            gizmo_.dragDelta = Vec3(delta.x, delta.y, 0);
        }
    }
    lastMousePos_ = pos;
}

void EditorViewport::onMouseWheel(f32 delta, bool ctrl) {
    if (ctrl) {
        camera_.zoomSpeed = 5.0f;
    } else {
        camera_.zoomSpeed = 2.0f;
    }
    camera_.zoom(delta);
}

void EditorViewport::onKeyDown(int key, bool shift, bool ctrl, bool alt) {
    (void)ctrl; (void)alt;
    f32 dt = 0.016f; // approximate frame time
    f32 speed = shift ? camera_.moveSpeed * camera_.sprintMultiplier : camera_.moveSpeed;

    switch (key) {
        case 'W': camera_.moveForward(dt * speed / camera_.moveSpeed); break;
        case 'S': camera_.moveBackward(dt * speed / camera_.moveSpeed); break;
        case 'A': camera_.moveLeft(dt * speed / camera_.moveSpeed); break;
        case 'D': camera_.moveRight(dt * speed / camera_.moveSpeed); break;
        case 'Q': camera_.moveDown(dt * speed / camera_.moveSpeed); break;
        case 'E': camera_.moveUp(dt * speed / camera_.moveSpeed); break;
        case 'G': cycleGizmoMode(); break;
    }
}

void EditorViewport::onKeyUp(int key) {
    (void)key;
}

void EditorViewport::render(const Mat4& viewProj) {
    renderGrid(viewProj);
    renderAxes(viewProj);
    renderSelectionOutline(viewProj);
    renderGizmo(viewProj);
    renderStats();
}

void EditorViewport::renderGrid(const Mat4& viewProj) {
    (void)viewProj;
    // Grid rendering would use the renderer to draw a ground grid
    // Lines at integer intervals from -50 to 50
}

void EditorViewport::renderAxes(const Mat4& viewProj) {
    (void)viewProj;
    // Render RGB axes at origin: X=red, Y=green, Z=blue
}

void EditorViewport::renderSelectionOutline(const Mat4& viewProj) {
    (void)viewProj;
    if (selectedEntity_ == 0xFFFFFFFF) return;
    // Render an outline highlight around the selected entity's bounding box
}

void EditorViewport::renderGizmo(const Mat4& viewProj) {
    if (!gizmo_.visible || selectedEntity_ == 0xFFFFFFFF) return;
    gizmo_.render(viewProj);
}

void EditorViewport::renderStats() {
    // Stats overlay would be rendered as UI text
    // triangles: stats_.triangles, drawCalls: stats_.drawCalls, etc.
}

// ---- ViewportGizmo ----

void ViewportGizmo::buildGeometry() {
    lines.clear();

    f32 axisLength = 2.0f;
    f32 arrowSize = 0.2f;

    // X axis (red)
    lines.pushBack({Vec3(0, 0, 0), Vec3(axisLength, 0, 0), Color(1, 0, 0, 1)});
    lines.pushBack({Vec3(axisLength, 0, 0), Vec3(axisLength - arrowSize, arrowSize, 0), Color(1, 0, 0, 1)});
    lines.pushBack({Vec3(axisLength, 0, 0), Vec3(axisLength - arrowSize, -arrowSize, 0), Color(1, 0, 0, 1)});

    // Y axis (green)
    lines.pushBack({Vec3(0, 0, 0), Vec3(0, axisLength, 0), Color(0, 1, 0, 1)});
    lines.pushBack({Vec3(0, axisLength, 0), Vec3(arrowSize, axisLength - arrowSize, 0), Color(0, 1, 0, 1)});
    lines.pushBack({Vec3(0, axisLength, 0), Vec3(-arrowSize, axisLength - arrowSize, 0), Color(0, 1, 0, 1)});

    // Z axis (blue)
    lines.pushBack({Vec3(0, 0, 0), Vec3(0, 0, axisLength), Color(0, 0, 1, 1)});
    lines.pushBack({Vec3(0, 0, axisLength), Vec3(0, arrowSize, axisLength - arrowSize), Color(0, 0, 1, 1)});
    lines.pushBack({Vec3(0, 0, axisLength), Vec3(0, -arrowSize, axisLength - arrowSize), Color(0, 0, 1, 1)});

    // Rotation rings
    if (mode == Rotate) {
        u32 segments = 32;
        for (u32 i = 0; i < segments; i++) {
            f32 a0 = (f32)i / (f32)segments * 6.2831853f;
            f32 a1 = (f32)(i + 1) / (f32)segments * 6.2831853f;
            // X ring (YZ plane)
            lines.pushBack({
                Vec3(0, cosf(a0) * axisLength, sinf(a0) * axisLength),
                Vec3(0, cosf(a1) * axisLength, sinf(a1) * axisLength),
                Color(1, 0, 0, 0.5f)
            });
            // Y ring (XZ plane)
            lines.pushBack({
                Vec3(cosf(a0) * axisLength, 0, sinf(a0) * axisLength),
                Vec3(cosf(a1) * axisLength, 0, sinf(a1) * axisLength),
                Color(0, 1, 0, 0.5f)
            });
            // Z ring (XY plane)
            lines.pushBack({
                Vec3(cosf(a0) * axisLength, sinf(a0) * axisLength, 0),
                Vec3(cosf(a1) * axisLength, sinf(a1) * axisLength, 0),
                Color(0, 0, 1, 0.5f)
            });
        }
    }

    // Scale handles (small cubes at ends)
    if (mode == Scale) {
        f32 cubeSize = 0.15f;
        // X
        lines.pushBack({Vec3(axisLength - cubeSize, -cubeSize, -cubeSize), Vec3(axisLength + cubeSize, -cubeSize, -cubeSize), Color(1, 0, 0, 1)});
        lines.pushBack({Vec3(axisLength - cubeSize, cubeSize, -cubeSize), Vec3(axisLength + cubeSize, cubeSize, -cubeSize), Color(1, 0, 0, 1)});
        lines.pushBack({Vec3(axisLength - cubeSize, -cubeSize, cubeSize), Vec3(axisLength + cubeSize, -cubeSize, cubeSize), Color(1, 0, 0, 1)});
        lines.pushBack({Vec3(axisLength - cubeSize, cubeSize, cubeSize), Vec3(axisLength + cubeSize, cubeSize, cubeSize), Color(1, 0, 0, 1)});
    }
}

void ViewportGizmo::render(const Mat4& viewProj) {
    (void)viewProj;
    // In a real implementation, upload lines to GPU and render
    // For now, geometry is built and would be drawn by the renderer
}

ViewportGizmo::Axis ViewportGizmo::hitTest(const Vec2& screenPos, const Mat4& viewProj, u32 width, u32 height) {
    (void)screenPos; (void)viewProj; (void)width; (void)height;
    // Simplified hit test - in reality would project axis endpoints to screen
    // and find closest axis to mouse position
    return None;
}

Vec3 ViewportGizmo::axisDirection(Axis axis) const {
    switch (axis) {
        case X: return Vec3(1, 0, 0);
        case Y: return Vec3(0, 1, 0);
        case Z: return Vec3(0, 0, 1);
        case XY: return Vec3(1, 1, 0).normalized();
        case XZ: return Vec3(1, 0, 1).normalized();
        case YZ: return Vec3(0, 1, 1).normalized();
        case All: return Vec3(1, 1, 1).normalized();
        default: return Vec3(0, 0, 0);
    }
}

}
