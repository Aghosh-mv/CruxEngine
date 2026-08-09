#include "Physics/WindField.h"

namespace Frost {

WindField::WindField() : noise_(4242u) {}

Vec3 WindField::sample(const Vec3& p, f32 time) const {
    Vec3 wind = baseDir_ * baseStrength_;

    // Low-frequency turbulent component (spatial + temporal variation)
    f32 tx = noise_.perlin(p.x * 0.05f, p.y * 0.03f, time * 0.15f);
    f32 ty = noise_.perlin(p.x * 0.03f + 40.0f, p.z * 0.04f, time * 0.2f);
    f32 tz = noise_.perlin(p.y * 0.04f, p.z * 0.05f, time * 0.18f);
    wind.x += tx * turbulence_ * baseStrength_ * 0.4f;
    wind.y += ty * turbulence_ * baseStrength_ * 0.25f;
    wind.z += tz * turbulence_ * baseStrength_ * 0.4f;

    // Gusts: travelling wave of stronger wind
    f32 gust = 0.5f + 0.5f * std::sin(p.x * 0.08f - time * 1.2f);
    gust *= std::sin(p.z * 0.05f + time * 0.9f) * 0.5f + 0.5f;
    f32 gustAmp = 0.35f * std::sin(time * 0.7f) + 0.35f;
    wind += baseDir_ * baseStrength_ * gust * gustAmp;

    // Thermals & sinks
    for (u32 i = 0; i < thermalCount_; i++) {
        const Thermal& t = thermals_[i];
        Vec3 d = p - t.center;
        d.y = 0;
        f32 dist = d.length();
        if (dist < t.radius) {
            f32 falloff = 1.0f - dist / t.radius;
            falloff = falloff * falloff;
            wind.y += t.strength * falloff;
        }
    }

    return wind;
}

void WindField::addThermal(const Vec3& center, f32 radius, f32 strength) {
    if (thermalCount_ < 16) {
        thermals_[thermalCount_].center = center;
        thermals_[thermalCount_].radius = radius;
        thermals_[thermalCount_].strength = strength;
        thermalCount_++;
    }
}

void WindField::addSink(const Vec3& center, f32 radius, f32 strength) {
    if (thermalCount_ < 16) {
        thermals_[thermalCount_].center = center;
        thermals_[thermalCount_].radius = radius;
        thermals_[thermalCount_].strength = -strength;
        thermalCount_++;
    }
}

void AeroSurface::computeForce(const Vec3& airflow, const Vec3& surfaceNormal,
                               const Vec3& forward, f32 airDensity,
                               Vec3& force, f32& angleOfAttack) const {
    force = Vec3(0);
    angleOfAttack = 0.0f;
    f32 speed = airflow.length();
    if (speed < 0.01f) return;

    Vec3 flowDir = airflow / speed;
    Vec3 n = surfaceNormal.normalized();
    Vec3 f = forward.normalized();

    // Angle of attack: angle between the airflow and the wing plane.
    Vec3 wingPlaneNormal = n;
    f32 sinAoA = flowDir.dot(wingPlaneNormal);
    angleOfAttack = std::asin(Mathf::clamp(sinAoA, -1.0f, 1.0f));

    // Lift coefficient: linear ramp then stall
    f32 aoaDeg = Mathf::degrees(angleOfAttack);
    f32 cl;
    f32 stallDeg = stallAngleDeg_;
    if (std::abs(aoaDeg) < stallDeg) {
        cl = maxLiftCoeff_ * (aoaDeg / stallDeg);
    } else {
        f32 beyond = (std::abs(aoaDeg) - stallDeg) / 45.0f;
        f32 sign = aoaDeg >= 0 ? 1.0f : -1.0f;
        cl = sign * maxLiftCoeff_ * (1.0f - Mathf::clamp(beyond, 0.0f, 0.9f));
    }

    // Drag coefficient: parasitic + induced
    f32 cd0 = 0.03f;
    f32 ar = aspectRatio();
    f32 induced = cl * cl / (Mathf::PI * ar + 0.5f);
    f32 cd = cd0 + induced;

    // Lift acts perpendicular to airflow, in the plane of the wing
    Vec3 liftDir = flowDir.cross(n.cross(flowDir)).normalized();
    Vec3 dragDir = -flowDir;

    f32 q = 0.5f * airDensity * speed * speed * area_;
    force = liftDir * (q * cl) + dragDir * (q * cd);
}

}
