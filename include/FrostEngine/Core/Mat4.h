#pragma once

#include "Core/Types.h"

namespace Frost { struct Quat; struct Mat3; }

namespace Frost {

struct Mat4 {
    f32 m[16];
    
    Mat4() { for(u32 i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.0f : 0.0f; }
    
    static Mat4 identity() { 
        Mat4 r; 
        for(u32 i = 0; i < 4; i++) r.m[i * 5] = 1.0f; 
        return r; 
    }
    
    static Mat4 translation(const Vec3& t) {
        Mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r;
    }
    
    static Mat4 scaling(const Vec3& s) {
        Mat4 r; r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; r.m[15] = 1.0f; return r;
    }
    
    static Mat4 rotation(const Vec3& axis, f32 angle) {
        f32 c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
        Vec3 a = axis.normalized();
        Mat4 r;
        r.m[0] = t * a.x * a.x + c;       r.m[1] = t * a.x * a.y + s * a.z; r.m[2] = t * a.x * a.z - s * a.y;
        r.m[4] = t * a.x * a.y - s * a.z; r.m[5] = t * a.y * a.y + c;       r.m[6] = t * a.y * a.z + s * a.x;
        r.m[8] = t * a.x * a.z + s * a.y; r.m[9] = t * a.y * a.z - s * a.x; r.m[10] = t * a.z * a.z + c;
        r.m[15] = 1.0f;
        return r;
    }

    static Mat4 rotation(const Quat& q);

    static Mat4 perspective(f32 fov, f32 aspect, f32 near, f32 far) {
        f32 f = 1.0f / std::tan(fov * 0.5f);
        f32 nf = 1.0f / (near - far);
        Mat4 r = {};
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (far + near) * nf;
        r.m[11] = -1.0f;
        r.m[14] = 2.0f * far * near * nf;
        return r;
    }
    
    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 z = (eye - target).normalized();
        Vec3 x = up.cross(z).normalized();
        Vec3 y = z.cross(x);
        Mat4 r = Mat4::identity();
        r.m[0] = x.x; r.m[4] = x.y; r.m[8] = x.z; r.m[12] = -x.dot(eye);
        r.m[1] = y.x; r.m[5] = y.y; r.m[9] = y.z; r.m[13] = -y.dot(eye);
        r.m[2] = z.x; r.m[6] = z.y; r.m[10] = z.z; r.m[14] = -z.dot(eye);
        return r;
    }
    
    Vec3 translation() const { return Vec3(m[12], m[13], m[14]); }
    Vec3 forward() const { return Vec3(m[8], m[9], m[10]).normalized(); }
    Vec3 right() const { return Vec3(m[0], m[1], m[2]).normalized(); }
    Vec3 up() const { return Vec3(m[4], m[5], m[6]).normalized(); }
    
    f32* data() { return m; }
    const f32* data() const { return m; }

    Vec4 operator*(const Vec4& v) const {
        return Vec4(
            m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
            m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w);
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (i32 col = 0; col < 4; col++) {
            for (i32 row = 0; row < 4; row++) {
                f32 sum = 0;
                for (i32 k = 0; k < 4; k++)
                    sum += m[k * 4 + row] * o.m[col * 4 + k];
                r.m[col * 4 + row] = sum;
            }
        }
        return r;
    }

    Mat4 inverse() const {
        Mat4 inv;
        inv.m[0] = m[5]  * m[10] * m[15] - m[5]  * m[11] * m[14] - m[9]  * m[6]  * m[15] + m[9]  * m[7]  * m[14] + m[13] * m[6]  * m[11] - m[13] * m[7]  * m[10];
        inv.m[4] = -m[4] * m[10] * m[15] + m[4]  * m[11] * m[14] + m[8]  * m[6]  * m[15] - m[8]  * m[7]  * m[14] - m[12] * m[6]  * m[11] + m[12] * m[7]  * m[10];
        inv.m[8] = m[4]  * m[9]  * m[15] - m[4]  * m[11] * m[13] - m[8]  * m[5]  * m[15] + m[8]  * m[7]  * m[13] + m[12] * m[5]  * m[11] - m[12] * m[7]  * m[9];
        inv.m[12] = -m[4] * m[9]  * m[14] + m[4]  * m[10] * m[13] + m[8]  * m[5]  * m[14] - m[8]  * m[6]  * m[13] - m[12] * m[5]  * m[10] + m[12] * m[6]  * m[9];
        inv.m[1] = -m[1] * m[10] * m[15] + m[1]  * m[11] * m[14] + m[9]  * m[2]  * m[15] - m[9]  * m[3]  * m[14] - m[13] * m[2]  * m[11] + m[13] * m[3]  * m[10];
        inv.m[5] = m[0]  * m[10] * m[15] - m[0]  * m[11] * m[14] - m[8]  * m[2]  * m[15] + m[8]  * m[3]  * m[14] + m[12] * m[2]  * m[11] - m[12] * m[3]  * m[10];
        inv.m[9] = -m[0] * m[9]  * m[15] + m[0]  * m[11] * m[13] + m[8]  * m[1]  * m[15] - m[8]  * m[3]  * m[13] - m[12] * m[1]  * m[11] + m[12] * m[3]  * m[9];
        inv.m[13] = m[0]  * m[9]  * m[14] - m[0]  * m[10] * m[13] - m[8]  * m[1]  * m[14] + m[8]  * m[2]  * m[13] + m[12] * m[1]  * m[10] - m[12] * m[2]  * m[9];
        inv.m[2] = m[1]  * m[6]  * m[15] - m[1]  * m[7]  * m[14] - m[5]  * m[2]  * m[15] + m[5]  * m[3]  * m[14] + m[13] * m[2]  * m[7]  - m[13] * m[3]  * m[6];
        inv.m[6] = -m[0] * m[6]  * m[15] + m[0]  * m[7]  * m[14] + m[4]  * m[2]  * m[15] - m[4]  * m[3]  * m[14] - m[12] * m[2]  * m[7]  + m[12] * m[3]  * m[6];
        inv.m[10] = m[0]  * m[5]  * m[15] - m[0]  * m[7]  * m[13] - m[4]  * m[1]  * m[15] + m[4]  * m[3]  * m[13] + m[12] * m[1]  * m[7]  - m[12] * m[3]  * m[5];
        inv.m[14] = -m[0] * m[5]  * m[14] + m[0]  * m[6]  * m[13] + m[4]  * m[1]  * m[14] - m[4]  * m[2]  * m[13] - m[12] * m[1]  * m[6]  + m[12] * m[2]  * m[5];
        inv.m[3] = -m[1] * m[6]  * m[11] + m[1]  * m[7]  * m[10] + m[5]  * m[2]  * m[11] - m[5]  * m[3]  * m[10] - m[9]  * m[2]  * m[7]  + m[9]  * m[3]  * m[6];
        inv.m[7] = m[0]  * m[6]  * m[11] - m[0]  * m[7]  * m[10] - m[4]  * m[2]  * m[11] + m[4]  * m[3]  * m[10] + m[8]  * m[2]  * m[7]  - m[8]  * m[3]  * m[6];
        inv.m[11] = -m[0] * m[5]  * m[11] + m[0]  * m[7]  * m[9]  + m[4]  * m[1]  * m[11] - m[4]  * m[3]  * m[9]  - m[8]  * m[1]  * m[7]  + m[8]  * m[3]  * m[5];
        inv.m[15] = m[0]  * m[5]  * m[10] - m[0]  * m[6]  * m[9]  - m[4]  * m[1]  * m[10] + m[4]  * m[2]  * m[9]  + m[8]  * m[1]  * m[6]  - m[8]  * m[2]  * m[5];
        f32 det = m[0] * inv.m[0] + m[1] * inv.m[4] + m[2] * inv.m[8] + m[3] * inv.m[12];
        if (std::abs(det) < 1e-10f) return Mat4::identity();
        f32 invDet = 1.0f / det;
        for (u32 i = 0; i < 16; i++) inv.m[i] *= invDet;
        return inv;
    }
};

}