#include "Renderer/Camera.h"

namespace Crux {

void Camera::setPerspective(f32 fovDeg, f32 aspect, f32 near, f32 far) {
    projection_ = Projection::Perspective;
    fov_ = fovDeg; aspect_ = aspect; near_ = near; far_ = far;
    dirty_ = true;
}

void Camera::setOrthographic(f32 l, f32 r, f32 b, f32 t, f32 near, f32 far) {
    projection_ = Projection::Orthographic;
    orthoLeft_ = l; orthoRight_ = r; orthoBottom_ = b; orthoTop_ = t;
    near_ = near; far_ = far;
    dirty_ = true;
}

void Camera::setAspect(f32 aspect) {
    if (Mathf::abs(aspect - aspect_) > 1e-6f) { aspect_ = aspect; dirty_ = true; }
}

void Camera::lookAt(const Vec3& target) {
    Vec3 dir = (target - position_);
    if (dir.lengthSquared() < 1e-6f) return;
    rotation_ = Quat::lookRotation(dir.normalized(), Vec3::up());
    dirty_ = true;
}

void Camera::ensure() const {
    if (!dirty_ && !frustumDirty_) return;
    view_ = Mat4::lookAt(position_, position_ + rotation_.forward(), Vec3::up());
    if (projection_ == Projection::Perspective) {
        proj_ = Mat4::perspective(Mathf::radians(fov_), aspect_, near_, far_);
    } else {
        // Orthographic projection (column-major)
        f32 x = orthoRight_ - orthoLeft_;
        f32 y = orthoTop_ - orthoBottom_;
        f32 z = far_ - near_;
        proj_ = Mat4();
        proj_.m[0] = 2.0f / x;
        proj_.m[5] = 2.0f / y;
        proj_.m[10] = -2.0f / z;
        proj_.m[12] = -(orthoRight_ + orthoLeft_) / x;
        proj_.m[13] = -(orthoTop_ + orthoBottom_) / y;
        proj_.m[14] = -(far_ + near_) / z;
        proj_.m[15] = 1.0f;
    }
    Mat4 r;
    for (i32 i = 0; i < 4; i++)
        for (i32 j = 0; j < 4; j++)
            r.m[i * 4 + j] = proj_.m[j * 4 + i]; // transpose to column-major
    for (i32 i = 0; i < 4; i++) {
        for (i32 j = 0; j < 4; j++) {
            f32 sum = 0;
            for (i32 k = 0; k < 4; k++) sum += r.m[i * 4 + k] * view_.m[k * 4 + j];
            viewProj_.m[i * 4 + j] = sum;
        }
    }
    updateFrustum();
    dirty_ = false;
    frustumDirty_ = false;
}

void Camera::updateFrustum() const {
    const f32* m = viewProj_.m;
    // Gribb-Hartmann plane extraction (row-based from column-major matrix)
    f32 rows[4][4];
    for (i32 i = 0; i < 4; i++)
        for (i32 j = 0; j < 4; j++)
            rows[i][j] = m[j * 4 + i];

    Vec4 planes[6];
    Vec3 ns[6]; f32 ds[6];
    for (i32 i = 0; i < 4; i++) {
        Vec3 n(rows[3][0] + rows[i][0],
               rows[3][1] + rows[i][1],
               rows[3][2] + rows[i][2]);
        f32 d = rows[3][3] + rows[i][3];
        f32 len = n.length();
        if (len > 1e-8f) { n = n / len; d = d / len; }
        Vec3 n2(rows[3][0] - rows[i][0],
                rows[3][1] - rows[i][1],
                rows[3][2] - rows[i][2]);
        f32 d2 = rows[3][3] - rows[i][3];
        f32 len2 = n2.length();
        if (len2 > 1e-8f) { n2 = n2 / len2; d2 = d2 / len2; }
        if (i < 2) { ns[i] = n; ds[i] = d; }        // left, right
        else if (i < 4) { ns[i] = n; ds[i] = d; }   // bottom, top
        else if (i == 4) { ns[i] = n; ds[i] = d; }  // near
        else { ns[i] = n2; ds[i] = d2; }            // far
    }
    for (i32 i = 0; i < 6; i++)
        planes_[i] = Vec4(ns[i], ds[i]);
    frustumDirty_ = false;
}

bool Camera::containsSphere(const Vec3& center, f32 radius) const {
    ensure();
    for (i32 i = 0; i < 6; i++) {
        f32 dist = planes_[i].x * center.x + planes_[i].y * center.y +
                    planes_[i].z * center.z + planes_[i].w;
        if (dist < -radius) return false;
    }
    return true;
}

void Camera::screenRay(f32 ndcX, f32 ndcY, Vec3& origin, Vec3& dir) const {
    ensure();
    Mat4 invVP;
    f32* mi = invVP.m;
    const f32* m = viewProj_.m;
    // manual 4x4 inverse of viewProj
    f32 inv[16];
    f32 det;
    {
        f32 a0 = m[0] * m[5] - m[1] * m[4];
        f32 a1 = m[0] * m[6] - m[2] * m[4];
        f32 a2 = m[0] * m[7] - m[3] * m[4];
        f32 a3 = m[1] * m[6] - m[2] * m[5];
        f32 a4 = m[1] * m[7] - m[3] * m[5];
        f32 a5 = m[2] * m[7] - m[3] * m[6];
        f32 b0 = m[8] * m[13] - m[9] * m[12];
        f32 b1 = m[8] * m[14] - m[10] * m[12];
        f32 b2 = m[8] * m[15] - m[11] * m[12];
        f32 b3 = m[9] * m[14] - m[10] * m[13];
        f32 b4 = m[9] * m[15] - m[11] * m[13];
        f32 b5 = m[10] * m[15] - m[11] * m[14];
        det = a0 * b5 - a1 * b4 + a2 * b3 + a3 * b2 - a4 * b1 + a5 * b0;
        if (std::abs(det) < 1e-9f) { origin = position_; dir = rotation_.forward(); return; }
        f32 id = 1.0f / det;
        inv[0] = (m[5] * b5 - m[6] * b4 + m[7] * b3) * id;
        inv[1] = (-m[1] * b5 + m[2] * b4 - m[3] * b3) * id;
        inv[2] = (m[13] * a5 - m[14] * a4 + m[15] * a3) * id;
        inv[3] = (-m[9] * a5 + m[10] * a4 - m[11] * a3) * id;
        inv[4] = (-m[4] * b5 + m[6] * b2 - m[7] * b1) * id;
        inv[5] = (m[0] * b5 - m[2] * b2 + m[3] * b1) * id;
        inv[6] = (-m[12] * a5 + m[14] * a2 - m[15] * a1) * id;
        inv[7] = (m[8] * a5 - m[10] * a2 + m[11] * a1) * id;
        inv[8] = (m[4] * b4 - m[5] * b2 + m[7] * b0) * id;
        inv[9] = (-m[0] * b4 + m[1] * b2 - m[3] * b0) * id;
        inv[10] = (m[12] * a4 - m[13] * a2 + m[15] * a0) * id;
        inv[11] = (-m[8] * a4 + m[9] * a2 - m[11] * a0) * id;
        inv[12] = (-m[4] * b3 + m[5] * b1 - m[6] * b0) * id;
        inv[13] = (m[0] * b3 - m[1] * b1 + m[2] * b0) * id;
        inv[14] = (-m[12] * a3 + m[13] * a1 - m[14] * a0) * id;
        inv[15] = (m[8] * a3 - m[9] * a1 + m[10] * a0) * id;
    }
    for (i32 i = 0; i < 16; i++) mi[i] = inv[i];

    Vec4 ndc(ndcX, ndcY, 1.0f, 1.0f);
    Vec4 world = invVP * ndc;
    Vec3 farPoint(world.x / world.w, world.y / world.w, world.z / world.w);
    origin = position_;
    Vec3 d = farPoint - origin;
    if (d.lengthSquared() < 1e-8f) d = rotation_.forward();
    dir = d.normalized();
}

}
