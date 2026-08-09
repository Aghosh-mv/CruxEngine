#include "Core/Math.h"
#include "Core/Mat4.h"

namespace Frost {

Quat Quat::fromEuler(const Vec3& e) {
    f32 cx = std::cos(e.x * 0.5f), sx = std::sin(e.x * 0.5f);
    f32 cy = std::cos(e.y * 0.5f), sy = std::sin(e.y * 0.5f);
    f32 cz = std::cos(e.z * 0.5f), sz = std::sin(e.z * 0.5f);
    return Quat(
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz).normalized();
}

Quat Quat::lookRotation(const Vec3& fwd, const Vec3& up) {
    Vec3 f = fwd.normalized();
    Vec3 r = up.cross(f).normalized();
    Vec3 u = f.cross(r);
    Mat4 m;
    m.m[0] = r.x; m.m[4] = r.y; m.m[8] = r.z;
    m.m[1] = u.x; m.m[5] = u.y; m.m[9] = u.z;
    m.m[2] = f.x; m.m[6] = f.y; m.m[10] = f.z;
    f32 trace = m.m[0] + m.m[5] + m.m[10];
    Quat q;
    if (trace > 0.0f) {
        f32 s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m.m[6] - m.m[9]) / s;
        q.y = (m.m[8] - m.m[2]) / s;
        q.z = (m.m[1] - m.m[4]) / s;
    } else if (m.m[0] > m.m[5] && m.m[0] > m.m[10]) {
        f32 s = std::sqrt(1.0f + m.m[0] - m.m[5] - m.m[10]) * 2.0f;
        q.w = (m.m[6] - m.m[9]) / s;
        q.x = 0.25f * s;
        q.y = (m.m[1] + m.m[4]) / s;
        q.z = (m.m[8] + m.m[2]) / s;
    } else if (m.m[5] > m.m[10]) {
        f32 s = std::sqrt(1.0f + m.m[5] - m.m[0] - m.m[10]) * 2.0f;
        q.w = (m.m[8] - m.m[2]) / s;
        q.x = (m.m[1] + m.m[4]) / s;
        q.y = 0.25f * s;
        q.z = (m.m[6] + m.m[9]) / s;
    } else {
        f32 s = std::sqrt(1.0f + m.m[10] - m.m[0] - m.m[5]) * 2.0f;
        q.w = (m.m[1] - m.m[4]) / s;
        q.x = (m.m[8] + m.m[2]) / s;
        q.y = (m.m[6] + m.m[9]) / s;
        q.z = 0.25f * s;
    }
    return q.normalized();
}

Quat Quat::fromMat3(const Mat3& m) {
    f32 trace = m.m[0] + m.m[4] + m.m[8];
    Quat q;
    if (trace > 0) {
        f32 s = 0.5f / sqrtf(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (m.m[7] - m.m[5]) * s;
        q.y = (m.m[2] - m.m[6]) * s;
        q.z = (m.m[3] - m.m[1]) * s;
    } else if (m.m[0] > m.m[4] && m.m[0] > m.m[8]) {
        f32 s = 2.0f * sqrtf(1.0f + m.m[0] - m.m[4] - m.m[8]);
        q.w = (m.m[7] - m.m[5]) / s;
        q.x = 0.25f * s;
        q.y = (m.m[1] + m.m[3]) / s;
        q.z = (m.m[2] + m.m[6]) / s;
    } else if (m.m[4] > m.m[8]) {
        f32 s = 2.0f * sqrtf(1.0f + m.m[4] - m.m[0] - m.m[8]);
        q.w = (m.m[2] - m.m[6]) / s;
        q.x = (m.m[1] + m.m[3]) / s;
        q.y = 0.25f * s;
        q.z = (m.m[5] + m.m[7]) / s;
    } else {
        f32 s = 2.0f * sqrtf(1.0f + m.m[8] - m.m[0] - m.m[4]);
        q.w = (m.m[3] - m.m[1]) / s;
        q.x = (m.m[2] + m.m[6]) / s;
        q.y = (m.m[5] + m.m[7]) / s;
        q.z = 0.25f * s;
    }
    return q.normalized();
}

Vec3 Quat::euler() const {
    Vec3 e;
    f32 sp = 2.0f * (w * y - x * z);
    if (std::abs(sp) >= 1.0f) {
        e.x = sp >= 0 ? Mathf::HALF_PI : -Mathf::HALF_PI;
        e.y = std::atan2(2.0f * (x * y + w * z), 1.0f - 2.0f * (y * y + z * z));
        e.z = 0.0f;
    } else {
        e.x = std::asin(sp);
        e.y = std::atan2(2.0f * (x * w + y * z), 1.0f - 2.0f * (z * z + w * w));
        e.z = std::atan2(2.0f * (x * y + z * w), 1.0f - 2.0f * (y * y + w * w));
    }
    return e;
}

Quat Quat::slerp(const Quat& a, const Quat& b, f32 t) {
    f32 dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quat b2 = b;
    if (dot < 0.0f) { dot = -dot; b2 = -b; }
    if (dot > 0.9995f) {
        Quat r = a + (b2 - a) * t;
        return r.normalized();
    }
    f32 theta0 = std::acos(dot);
    f32 theta = theta0 * t;
    f32 sinTheta = std::sin(theta);
    f32 sinTheta0 = std::sin(theta0);
    f32 s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
    f32 s1 = sinTheta / sinTheta0;
    return (a * s0) + (b2 * s1);
}

Mat4 Mat4::rotation(const Quat& q) {
    f32 xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    f32 xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    f32 wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat4 r = identity();
    r.m[0]  = 1.0f - 2.0f * (yy + zz);
    r.m[1]  = 2.0f * (xy + wz);
    r.m[2]  = 2.0f * (xz - wy);
    r.m[4]  = 2.0f * (xy - wz);
    r.m[5]  = 1.0f - 2.0f * (xx + zz);
    r.m[6]  = 2.0f * (yz + wx);
    r.m[8]  = 2.0f * (xz + wy);
    r.m[9]  = 2.0f * (yz - wx);
    r.m[10] = 1.0f - 2.0f * (xx + yy);
    return r;
}

}
