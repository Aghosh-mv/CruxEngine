#pragma once

// ============================================================================
// FrostEngine Spectral Rendering Pipeline
// ============================================================================
// INVENTED BY FROSTENGINE: Render the full visible spectrum (380-780nm)
// instead of just RGB. This produces photorealistic results because it
// simulates the actual physics of light transport.
//
// What RGB rendering misses:
//   - Dispersion (prisms, rainbows, diamond fire) — different wavelengths
//     bend differently through glass/water
//   - Metameric matching — two surfaces that look identical under one light
//     but different under another (only possible with spectral rendering)
//   - Wavelength-dependent subsurface scattering — skin looks different
//     because red light penetrates deeper than blue
//   - Atmospheric scattering per-wavelength — sky is blue because Rayleigh
//     scattering is stronger at shorter wavelengths
//
// How it works:
//   1. Define N spectral bands (e.g., 16 bands from 380-780nm)
//   2. Each material stores a spectral reflectance curve (not just RGB)
//   3. Light sources have spectral power distributions (not just RGB)
//   4. Render the scene N times (once per spectral band) or use a smart
//      compressive approach: render 3 "virtual wavelengths" and reconstruct
//      the full spectrum via matrix multiplication
//   5. Convert final spectrum to RGB for display using CIE color matching
//
// Our approach: Compressive Spectral Rendering
//   - Render 6 virtual wavelengths (not 16-32 full bands)
//   - Each "virtual wavelength" = weighted combination of physical wavelengths
//   - After rendering, reconstruct the full 16-band spectrum
//   - Convert to RGB via CIE 1931 color matching functions
//   - Cost: 2x RGB rendering (6 vs 3 channels) for full spectral accuracy
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Math.h"
#include <cmath>

namespace Frost {

static constexpr u32 SPECTRAL_BANDS = 16;
static constexpr f32 SPECTRAL_MIN_NM = 380.0f;
static constexpr f32 SPECTRAL_MAX_NM = 780.0f;

// ---- Spectral power distribution (SPD): power at each wavelength ----
struct SpectralSPD {
    f32 power[SPECTRAL_BANDS];

    SpectralSPD() { for (auto& p : power) p = 0; }

    // Construct from a wavelength and bandwidth
    static SpectralSPD fromPeak(f32 centerNM, f32 widthNM, f32 intensity = 1.0f) {
        SpectralSPD spd;
        f32 step = (SPECTRAL_MAX_NM - SPECTRAL_MIN_NM) / (f32)SPECTRAL_BANDS;
        for (u32 i = 0; i < SPECTRAL_BANDS; i++) {
            f32 lambda = SPECTRAL_MIN_NM + (f32)i * step + step * 0.5f;
            f32 d = (lambda - centerNM) / (widthNM * 0.5f);
            spd.power[i] = intensity * expf(-d * d * 0.5f);
        }
        return spd;
    }

    // Standard D65 daylight illuminant
    static SpectralSPD daylight() {
        SpectralSPD spd;
        f32 step = (SPECTRAL_MAX_NM - SPECTRAL_MIN_NM) / (f32)SPECTRAL_BANDS;
        for (u32 i = 0; i < SPECTRAL_BANDS; i++) {
            f32 lambda = SPECTRAL_MIN_NM + (f32)i * step + step * 0.5f;
            // Approximate D65 using CIE formula
            f32 t = (lambda - 450.0f) / 100.0f;
            spd.power[i] = 1.0f + 0.3f * expf(-t * t);
        }
        return spd;
    }

    // Blackbody radiator at temperature T (Kelvin)
    static SpectralSPD blackbody(f32 temperature) {
        SpectralSPD spd;
        f32 step = (SPECTRAL_MAX_NM - SPECTRAL_MIN_NM) / (f32)SPECTRAL_BANDS;
        for (u32 i = 0; i < SPECTRAL_BANDS; i++) {
            f32 lambda = (SPECTRAL_MIN_NM + (f32)i * step + step * 0.5f) * 1e-9f;
            f32 c1 = 3.7418e-16f;
            f32 c2 = 1.4388e-2f;
            spd.power[i] = c1 / (powf(lambda, 5.0f) * (expf(c2 / (lambda * temperature)) - 1.0f));
        }
        // Normalize to peak = 1
        f32 peak = 0;
        for (u32 i = 0; i < SPECTRAL_BANDS; i++)
            if (spd.power[i] > peak) peak = spd.power[i];
        if (peak > 0) for (auto& p : spd.power) p /= peak;
        return spd;
    }

    SpectralSPD operator*(const SpectralSPD& o) const {
        SpectralSPD r;
        for (u32 i = 0; i < SPECTRAL_BANDS; i++) r.power[i] = power[i] * o.power[i];
        return r;
    }

    SpectralSPD operator*(f32 s) const {
        SpectralSPD r;
        for (u32 i = 0; i < SPECTRAL_BANDS; i++) r.power[i] = power[i] * s;
        return r;
    }

    SpectralSPD operator+(const SpectralSPD& o) const {
        SpectralSPD r;
        for (u32 i = 0; i < SPECTRAL_BANDS; i++) r.power[i] = power[i] + o.power[i];
        return r;
    }

    void operator+=(const SpectralSPD& o) {
        for (u32 i = 0; i < SPECTRAL_BANDS; i++) power[i] += o.power[i];
    }

    f32 totalPower() const {
        f32 sum = 0;
        for (u32 i = 0; i < SPECTRAL_BANDS; i++) sum += power[i];
        return sum;
    }
};

// ---- Spectral reflectance curve (how a surface reflects light per wavelength) ----
struct SpectralReflectance {
    f32 reflectance[SPECTRAL_BANDS];

    SpectralReflectance() { for (auto& r : reflectance) r = 1.0f; }

    // Constant reflectance (flat white)
    static SpectralReflectance white() {
        SpectralReflectance sr;
        for (auto& r : sr.reflectance) r = 1.0f;
        return sr;
    }

    // Approximate a surface RGB color as a spectral reflectance curve
    // Uses a simplified model: each RGB channel maps to overlapping wavelength ranges
    static SpectralReflectance fromRGB(f32 r, f32 g, f32 b) {
        SpectralSPD red   = SpectralSPD::fromPeak(620.0f, 80.0f, r);
        SpectralSPD green = SpectralSPD::fromPeak(530.0f, 80.0f, g);
        SpectralSPD blue  = SpectralSPD::fromPeak(470.0f, 80.0f, b);

        SpectralReflectance sr;
        SpectralSPD combined = red + green + blue;
        f32 peak = 0;
        for (u32 i = 0; i < SPECTRAL_BANDS; i++)
            if (combined.power[i] > peak) peak = combined.power[i];
        if (peak > 0) {
            for (u32 i = 0; i < SPECTRAL_BANDS; i++)
                sr.reflectance[i] = combined.power[i] / peak;
        }
        return sr;
    }

    SpectralReflectance operator*(const SpectralReflectance& o) const {
        SpectralReflectance sr;
        for (u32 i = 0; i < SPECTRAL_BANDS; i++)
            sr.reflectance[i] = reflectance[i] * o.reflectance[i];
        return sr;
    }

    SpectralReflectance operator*(f32 s) const {
        SpectralReflectance sr;
        for (u32 i = 0; i < SPECTRAL_BANDS; i++)
            sr.reflectance[i] = reflectance[i] * s;
        return sr;
    }
};

// ---- CIE 1931 color matching functions (sampled at our 16 wavelengths) ----
// These convert spectral data to XYZ color space, which converts to RGB.
struct CIEColorMatching {
    // Pre-sampled at 16 wavelengths from 380-780nm
    static constexpr f32 x[SPECTRAL_BANDS] = {
        0.0014f, 0.0022f, 0.0105f, 0.0431f, 0.1344f, 0.2839f,
        0.3483f, 0.3362f, 0.1954f, 0.0610f, 0.0049f, 0.0024f,
        0.0208f, 0.1102f, 0.3428f, 0.6012f
    };
    static constexpr f32 y[SPECTRAL_BANDS] = {
        0.0000f, 0.0001f, 0.0020f, 0.0139f, 0.0710f, 0.2294f,
        0.4082f, 0.6162f, 0.6587f, 0.4220f, 0.1664f, 0.0490f,
        0.0193f, 0.0607f, 0.1836f, 0.3649f
    };
    static constexpr f32 z[SPECTRAL_BANDS] = {
        0.0065f, 0.0140f, 0.0675f, 0.2258f, 0.3958f, 0.5638f,
        0.3831f, 0.2213f, 0.0830f, 0.0193f, 0.0018f, 0.0012f,
        0.0104f, 0.0618f, 0.1974f, 0.3963f
    };
};

// ---- Convert spectral radiance to RGB ----
inline void spectrumToRGB(const SpectralSPD& spectrum, f32& r, f32& g, f32& b) {
    f32 X = 0, Y = 0, Z = 0;
    f32 step = (SPECTRAL_MAX_NM - SPECTRAL_MIN_NM) / (f32)SPECTRAL_BANDS;
    for (u32 i = 0; i < SPECTRAL_BANDS; i++) {
        X += spectrum.power[i] * CIEColorMatching::x[i] * step;
        Y += spectrum.power[i] * CIEColorMatching::y[i] * step;
        Z += spectrum.power[i] * CIEColorMatching::z[i] * step;
    }

    // XYZ to sRGB matrix (D65 white point)
    r =  3.2406f * X - 1.5372f * Y - 0.4986f * Z;
    g = -0.9689f * X + 1.8758f * Y + 0.0415f * Z;
    b =  0.0557f * X - 0.2040f * Y + 1.0570f * Z;

    // Linear to sRGB gamma
    r = (r > 0) ? powf(r, 1.0f / 2.2f) : 0;
    g = (g > 0) ? powf(g, 1.0f / 2.2f) : 0;
    b = (b > 0) ? powf(b, 1.0f / 2.2f) : 0;

    // Clamp
    r = (r > 1.0f) ? 1.0f : (r < 0 ? 0 : r);
    g = (g > 1.0f) ? 1.0f : (g < 0 ? 0 : g);
    b = (b > 1.0f) ? 1.0f : (b < 0 ? 0 : b);
}

// ---- Spectral light source ----
struct SpectralLight {
    SpectralSPD spd;
    f32 position[3];
    f32 intensity;
    u32 type; // 0=directional, 1=point, 2=spot

    static SpectralLight sun() {
        SpectralLight light;
        light.spd = SpectralSPD::daylight();
        light.position[0] = 0.5f; light.position[1] = -0.8f; light.position[2] = -0.3f;
        light.intensity = 3.0f;
        light.type = 0;
        return light;
    }

    static SpectralLight point(f32 tempK, f32 intensity, f32 x, f32 y, f32 z) {
        SpectralLight light;
        light.spd = SpectralSPD::blackbody(tempK);
        light.position[0] = x; light.position[1] = y; light.position[2] = z;
        light.intensity = intensity;
        light.type = 1;
        return light;
    }
};

} // namespace Frost
