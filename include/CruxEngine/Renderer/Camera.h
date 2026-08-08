#pragma once

#include "Core/Types.h"
#include "Core/Math.h"

namespace Crux {

// View/projection camera with cached matrices and view-frustum planes for
// culling. Supports perspective and orthographic (shadow / minimap) modes.
class Camera {
public:
    enum class Projection { Perspective, Orthographic };

    void setPerspective(f32 fovDeg, f32 aspect, f32 near, f32 far);
    void setOrthographic(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far);
    void setAspect(f32 aspect);

    void setPosition(const Vec3& pos) { position_ = pos; dirty_ = true; }
    void setRotation(const Quat& rot) { rotation_ = rot; dirty_ = true; }
    void lookAt(const Vec3& target);

    Vec3 position() const { return position_; }
    Quat rotation() const { return rotation_; }
    Vec3 forward() const { return rotation_.forward(); }
    Vec3 right() const { return rotation_.right(); }
    Vec3 up() const { return rotation_.up(); }

    const Mat4& view() const { ensure(); return view_; }
    const Mat4& proj() const { ensure(); return proj_; }
    const Mat4& viewProj() const { ensure(); return viewProj_; }

    f32 nearPlane() const { return near_; }
    f32 farPlane() const { return far_; }
    f32 fov() const { return fov_; }
    f32 aspect() const { return aspect_; }

    // World-space frustum planes (normalized): [0..5] = left,right,bottom,top,near,far
    const Vec4* frustumPlanes() const { ensure(); return planes_; }

    // Quick test against the frustum.
    bool containsSphere(const Vec3& center, f32 radius) const;

    // Casts a ray from the camera through NDC coordinates (-1..1).
    void screenRay(f32 ndcX, f32 ndcY, Vec3& origin, Vec3& dir) const;

private:
    void ensure() const;
    void updateFrustum() const;

    Vec3 position_{ 0, 0, 0 };
    Quat rotation_{ Quat::identity() };
    Projection projection_ = Projection::Perspective;
    f32 fov_ = 60.0f, aspect_ = 16.0f / 9.0f, near_ = 0.1f, far_ = 1000.0f;
    f32 orthoLeft_ = -10, orthoRight_ = 10, orthoBottom_ = -10, orthoTop_ = 10;

    mutable Mat4 view_, proj_, viewProj_;
    mutable Vec4 planes_[6];
    mutable bool dirty_ = true;
    mutable bool frustumDirty_ = true;
};

}
