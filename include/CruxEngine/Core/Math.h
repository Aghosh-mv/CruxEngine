#pragma once

#include "Core/Types.h"
#include <cmath>
#include <algorithm>

namespace Crux {

namespace Mathf {
    constexpr f32 PI = 3.14159265358979323846f;
    constexpr f32 TWO_PI = 6.28318530717958647692f;
    constexpr f32 HALF_PI = 1.57079632679489661923f;
    constexpr f32 DEG2RAD = 0.01745329251994329576f;
    constexpr f32 RAD2DEG = 57.29577951308232087679f;
    constexpr f32 EPSILON = 1.192092896e-7f;

    inline f32 clamp(f32 v, f32 lo, f32 hi) { return std::fmin(std::fmax(v, lo), hi); }
    inline f32 saturate(f32 v) { return std::fmin(std::fmax(v, 0.0f), 1.0f); }
    inline f32 lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
    inline f32 min(f32 a, f32 b) { return a < b ? a : b; }
    inline f32 max(f32 a, f32 b) { return a > b ? a : b; }
    inline f32 abs(f32 v) { return std::fabs(v); }
    inline f32 sqrt(f32 v) { return std::sqrt(v); }
    inline f32 smoothstep(f32 edge0, f32 edge1, f32 x) {
        f32 t = saturate((x - edge0) / (edge1 - edge0));
        return t * t * (3.0f - 2.0f * t);
    }
    inline f32 smootherstep(f32 edge0, f32 edge1, f32 x) {
        f32 t = saturate((x - edge0) / (edge1 - edge0));
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }
    inline f32 repeat(f32 t, f32 length) { return t - std::floor(t / length) * length; }
    inline f32 pingPong(f32 t, f32 length) {
        t = repeat(t, length * 2.0f);
        return length - std::abs(t - length);
    }
    inline f32 degrees(f32 rad) { return rad * RAD2DEG; }
    inline f32 radians(f32 deg) { return deg * DEG2RAD; }
    inline f32 sign(f32 v) { return v < 0.0f ? -1.0f : (v > 0.0f ? 1.0f : 0.0f); }
    inline f32 maxv(f32 a, f32 b) { return a > b ? a : b; }
    inline f32 minv(f32 a, f32 b) { return a < b ? a : b; }
    inline bool approx(f32 a, f32 b, f32 eps = 1e-4f) { return std::abs(a - b) <= eps; }
    inline f32 approach(f32 current, f32 target, f32 delta) {
        if (current < target) return std::min(current + delta, target);
        return std::max(current - delta, target);
    }
    inline f32 damp(f32 current, f32 target, f32 lambda, f32 dt) {
        return lerp(current, target, 1.0f - std::exp(-lambda * dt));
    }
    inline f32 wrap(f32 value, f32 min, f32 max) {
        f32 range = max - min;
        if (range <= 0.0f) return min;
        f32 result = std::fmod(value - min, range);
        if (result < 0.0f) result += range;
        return result + min;
    }
    inline int iround(f32 v) { return (int)std::lround(v); }
}

struct Vec2i { i32 x = 0, y = 0; Vec2i() = default; Vec2i(i32 x, i32 y) : x(x), y(y) {} };
struct Vec3i { i32 x = 0, y = 0, z = 0; Vec3i() = default; Vec3i(i32 x, i32 y, i32 z) : x(x), y(y), z(z) {} };

struct Quat {
    f32 x = 0, y = 0, z = 0, w = 1;

    Quat() = default;
    Quat(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}
    Quat(const Vec3& axis, f32 angle) {
        f32 half = angle * 0.5f;
        f32 s = std::sin(half);
        Vec3 a = axis.normalized();
        x = a.x * s; y = a.y * s; z = a.z * s; w = std::cos(half);
    }

    static Quat identity() { return Quat(0, 0, 0, 1); }
    static Quat fromEuler(const Vec3& euler);
    static Quat lookRotation(const Vec3& forward, const Vec3& up);

    Quat operator*(const Quat& q) const {
        return Quat(
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w,
            w * q.w - x * q.x - y * q.y - z * q.z);
    }
    Vec3 operator*(const Vec3& v) const {
        Vec3 u(x, y, z);
        Vec3 t = u.cross(v) * 2.0f;
        return v + t * w + u.cross(t);
    }
    Quat operator*(f32 s) const { return Quat(x * s, y * s, z * s, w * s); }
    Quat operator+(const Quat& q) const { return Quat(x + q.x, y + q.y, z + q.z, w + q.w); }
    Quat operator-(const Quat& q) const { return Quat(x - q.x, y - q.y, z - q.z, w - q.w); }
    Quat operator-() const { return Quat(-x, -y, -z, -w); }

    f32 lengthSquared() const { return x * x + y * y + z * z + w * w; }
    f32 length() const { return std::sqrt(lengthSquared()); }
    Quat normalized() const {
        f32 l = length();
        if (l < Mathf::EPSILON) return identity();
        return Quat(x / l, y / l, z / l, w / l);
    }
    Quat conjugate() const { return Quat(-x, -y, -z, w); }
    Quat inverse() const { return conjugate().normalized(); }

    Vec3 euler() const;
    Vec3 forward() const { return *this * Vec3(0, 0, -1); }
    Vec3 up() const { return *this * Vec3(0, 1, 0); }
    Vec3 right() const { return *this * Vec3(1, 0, 0); }

    static Quat slerp(const Quat& a, const Quat& b, f32 t);
    static Quat fromMat3(const Mat3& m);
};

struct Mat3 {
    f32 m[9];
    Mat3() { for (i32 i = 0; i < 9; i++) m[i] = (i % 4 == 0) ? 1.0f : 0.0f; }
    static Mat3 identity() { Mat3 r; for (i32 i = 0; i < 3; i++) r.m[i * 4] = 1.0f; return r; }
    static Mat3 fromQuat(const Quat& q) {
        Mat3 r;
        f32 x = q.x, y = q.y, z = q.z, w = q.w;
        r.m[0] = 1 - 2 * (y * y + z * z); r.m[1] = 2 * (x * y + z * w);     r.m[2] = 2 * (x * z - y * w);
        r.m[3] = 2 * (x * y - z * w);     r.m[4] = 1 - 2 * (x * x + z * z); r.m[5] = 2 * (y * z + x * w);
        r.m[6] = 2 * (x * z + y * w);     r.m[7] = 2 * (y * z - x * w);     r.m[8] = 1 - 2 * (x * x + y * y);
        return r;
    }
    Vec3 operator*(const Vec3& v) const {
        return Vec3(
            m[0] * v.x + m[1] * v.y + m[2] * v.z,
            m[3] * v.x + m[4] * v.y + m[5] * v.z,
            m[6] * v.x + m[7] * v.y + m[8] * v.z);
    }
    Mat3 transpose() const {
        Mat3 r;
        for (i32 i = 0; i < 3; i++)
            for (i32 j = 0; j < 3; j++)
                r.m[j * 3 + i] = m[i * 3 + j];
        return r;
    }
};

struct Color {
    f32 r = 1, g = 1, b = 1, a = 1;
    Color() = default;
    Color(f32 r, f32 g, f32 b, f32 a = 1) : r(r), g(g), b(b), a(a) {}
    Vec3 rgb() const { return Vec3(r, g, b); }
    Vec4 rgba() const { return Vec4(r, g, b, a); }
    static Color fromHex(u32 hex) {
        return Color(((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f,
                     (hex & 0xFF) / 255.0f, 1.0f);
    }
    static Color lerp(const Color& a, const Color& b, f32 t) {
        return Color(Mathf::lerp(a.r, b.r, t), Mathf::lerp(a.g, b.g, t),
                     Mathf::lerp(a.b, b.b, t), Mathf::lerp(a.a, b.a, t));
    }
};

}
