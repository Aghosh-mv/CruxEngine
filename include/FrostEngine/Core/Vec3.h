#pragma once

#include "Core/Types.h"
#include <cmath>

namespace Frost {

struct Vec3 {
    f32 x = 0, y = 0, z = 0;
    
    Vec3() = default;
    Vec3(f32 x, f32 y, f32 z) : x(x), y(y), z(z) {}
    Vec3(f32 v) : x(v), y(v), z(v) {}
    
    Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
    Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }
    Vec3 operator*(f32 s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator*(const Vec3& v) const { return Vec3(x * v.x, y * v.y, z * v.z); }
    Vec3 operator/(f32 s) const { return Vec3(x / s, y / s, z / s); }
    Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vec3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    Vec3& operator*=(f32 s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(f32 s) { x /= s; y /= s; z /= s; return *this; }
    
    f32 dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }
    Vec3 cross(const Vec3& v) const { 
        return Vec3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x); 
    }
    f32 length() const { return std::sqrt(dot(*this)); }
    f32 lengthSquared() const { return dot(*this); }
    Vec3 normalized() const { f32 l = length(); return l > 0 ? *this / l : Vec3(0); }
    Vec3 abs() const { return Vec3(std::abs(x), std::abs(y), std::abs(z)); }
    Vec3 min(const Vec3& v) const { return Vec3(std::fmin(x, v.x), std::fmin(y, v.y), std::fmin(z, v.z)); }
    Vec3 max(const Vec3& v) const { return Vec3(std::fmax(x, v.x), std::fmax(y, v.y), std::fmax(z, v.z)); }
    
    static Vec3 zero() { return Vec3(0); }
    static Vec3 one() { return Vec3(1); }
    static Vec3 up() { return Vec3(0, 1, 0); }
    static Vec3 right() { return Vec3(1, 0, 0); }
    static Vec3 forward() { return Vec3(0, 0, -1); }
    
    f32& operator[](usize i) { return (&x)[i]; }
    f32 operator[](usize i) const { return (&x)[i]; }
};

struct Vec4 {
    f32 x = 0, y = 0, z = 0, w = 1;
    Vec4() = default;
    Vec4(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}
    Vec4(const Vec3& v, f32 w = 1) : x(v.x), y(v.y), z(v.z), w(w) {}
    Vec3 xyz() const { return Vec3(x, y, z); }
    Vec4 operator/(f32 s) const { return Vec4(x / s, y / s, z / s, w / s); }
    f32& operator[](usize i) { return (&x)[i]; }
    f32 operator[](usize i) const { return (&x)[i]; }
};

struct Vec2 {
    f32 x = 0, y = 0;
    Vec2() = default;
    Vec2(f32 x, f32 y) : x(x), y(y) {}
    Vec2(f32 v) : x(v), y(v) {}
    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator*(f32 s) const { return Vec2(x * s, y * s); }
    Vec2 operator*(const Vec2& o) const { return Vec2(x * o.x, y * o.y); }
    f32 dot(const Vec2& v) const { return x * v.x + y * v.y; }
    f32 length() const { return sqrtf(x*x + y*y); }
    Vec2 normalized() const { f32 l = length(); return l > 0 ? Vec2(x/l, y/l) : Vec2(0,0); }
};

}