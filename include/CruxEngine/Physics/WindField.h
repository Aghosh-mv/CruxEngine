#pragma once

#include "Core/Types.h"
#include "Core/Math.h"
#include "Core/Noise.h"
#include "Physics/RigidBody.h"

namespace Crux {

// The world's wind model: a slowly-varying flow field plus gusts, thermal
// updrafts and downdrafts. Any dynamic body can sample it.
class WindField {
public:
    WindField();

    void setBaseWind(const Vec3& dir, f32 strength, f32 turbulence = 0.6f) {
        baseDir_ = dir.normalized();
        baseStrength_ = strength;
        turbulence_ = turbulence;
    }

    // Samples the wind velocity at a world position at a given simulation time.
    Vec3 sample(const Vec3& worldPos, f32 time) const;

    // Adds a thermal column (rising warm air) at a position.
    void addThermal(const Vec3& center, f32 radius, f32 strength);

    // Adds a sink (downdraft), useful near ridges.
    void addSink(const Vec3& center, f32 radius, f32 strength);

    u32 thermalCount() const { return thermalCount_; }

private:
    struct Thermal {
        Vec3 center;
        f32 radius;
        f32 strength;
    };

    Vec3 baseDir_{ 1, 0, 0 };
    f32 baseStrength_ = 5.0f;
    f32 turbulence_ = 0.5f;
    Noise noise_;
    Thermal thermals_[16];
    u32 thermalCount_ = 0;
};

// A simple flat aero-surface: computes lift and drag from incident airflow.
// Used for the glider wing, sail and prop surfaces.
class AeroSurface {
public:
    AeroSurface() = default;

    // chord length, span, airfoil shape; area computed from span*chord.
    void set(f32 span, f32 chord, f32 maxLiftCoeff = 1.4f, f32 stallAngle = 15.0f) {
        span_ = span;
        chord_ = chord;
        area_ = span * chord;
        maxLiftCoeff_ = maxLiftCoeff;
        stallAngleDeg_ = stallAngle;
    }

    // Computes lift+drag forces given the relative airflow velocity.
    // surfaceNormal is the wing's up direction, forward its travel direction.
    void computeForce(const Vec3& airflow, const Vec3& surfaceNormal, const Vec3& forward,
                      f32 airDensity, Vec3& force, f32& angleOfAttack) const;

    f32 area() const { return area_; }
    f32 aspectRatio() const { return span_ * span_ / (area_ + 1e-6f); }

private:
    f32 span_ = 6.0f;
    f32 chord_ = 1.0f;
    f32 area_ = 6.0f;
    f32 maxLiftCoeff_ = 1.4f;
    f32 stallAngleDeg_ = 15.0f;
};

}
