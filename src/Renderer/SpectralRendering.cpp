#pragma once

#include "FrostEngine/Renderer/SpectralRendering.h"
#include <cmath>
#include <algorithm>

namespace Frost {

namespace {
// ============================================================================
// CIE 1931 2-degree standard observer, sampled at 10nm from 380-780nm
// ============================================================================
struct CIESample {
    f32 wavelength;
    f32 x;
    f32 y;
    f32 z;
};

constexpr CIESample kCIE1931[] = {
    {380.0f, 0.001368f, 0.000039f, 0.006450f},
    {390.0f, 0.004243f, 0.000120f, 0.020050f},
    {400.0f, 0.014310f, 0.000396f, 0.067850f},
    {410.0f, 0.043510f, 0.001210f, 0.207400f},
    {420.0f, 0.134380f, 0.004000f, 0.645600f},
    {430.0f, 0.283900f, 0.011600f, 1.385600f},
    {440.0f, 0.348280f, 0.023000f, 1.747060f},
    {450.0f, 0.336200f, 0.038000f, 1.772110f},
    {460.0f, 0.290800f, 0.060000f, 1.669200f},
    {470.0f, 0.195360f, 0.090980f, 1.287640f},
    {480.0f, 0.095640f, 0.139020f, 0.812950f},
    {490.0f, 0.032010f, 0.208020f, 0.465180f},
    {500.0f, 0.004900f, 0.323000f, 0.272000f},
    {510.0f, 0.009300f, 0.503000f, 0.158200f},
    {520.0f, 0.063270f, 0.710000f, 0.078250f},
    {530.0f, 0.165500f, 0.862000f, 0.042160f},
    {540.0f, 0.290400f, 0.954000f, 0.020300f},
    {550.0f, 0.433450f, 0.994950f, 0.008750f},
    {560.0f, 0.594500f, 0.995000f, 0.003900f},
    {570.0f, 0.762100f, 0.952000f, 0.002100f},
    {580.0f, 0.916300f, 0.870000f, 0.001650f},
    {590.0f, 1.026300f, 0.757000f, 0.001100f},
    {600.0f, 1.062200f, 0.631000f, 0.000800f},
    {610.0f, 1.002600f, 0.503000f, 0.000340f},
    {620.0f, 0.854450f, 0.381000f, 0.000190f},
    {630.0f, 0.642400f, 0.265000f, 0.000050f},
    {640.0f, 0.447900f, 0.175000f, 0.000020f},
    {650.0f, 0.283500f, 0.107000f, 0.000000f},
    {660.0f, 0.164900f, 0.061000f, 0.000000f},
    {670.0f, 0.087400f, 0.032000f, 0.000000f},
    {680.0f, 0.046770f, 0.017000f, 0.000000f},
    {690.0f, 0.022700f, 0.008210f, 0.000000f},
    {700.0f, 0.011359f, 0.004102f, 0.000000f},
    {710.0f, 0.005790f, 0.002091f, 0.000000f},
    {720.0f, 0.002899f, 0.001047f, 0.000000f},
    {730.0f, 0.001440f, 0.000520f, 0.000000f},
    {740.0f, 0.000690f, 0.000249f, 0.000000f},
    {750.0f, 0.000332f, 0.000120f, 0.000000f},
    {760.0f, 0.000166f, 0.000060f, 0.000000f},
    {770.0f, 0.000083f, 0.000030f, 0.000000f},
    {780.0f, 0.000042f, 0.000015f, 0.000000f}
};
constexpr u32 kCIESampleCount = sizeof(kCIE1931) / sizeof(kCIE1931[0]);

// Interpolate the CIE 1931 tables at an arbitrary wavelength
void cieAt(f32 wavelength, f32& x, f32& y, f32& z) {
    if (wavelength <= kCIE1931[0].wavelength) {
        x = kCIE1931[0].x;
        y = kCIE1931[0].y;
        z = kCIE1931[0].z;
        return;
    }
    if (wavelength >= kCIE1931[kCIESampleCount - 1].wavelength) {
        x = kCIE1931[kCIESampleCount - 1].x;
        y = kCIE1931[kCIESampleCount - 1].y;
        z = kCIE1931[kCIESampleCount - 1].z;
        return;
    }
    for (u32 i = 1; i < kCIESampleCount; i++) {
        if (wavelength <= kCIE1931[i].wavelength) {
            const CIESample& a = kCIE1931[i - 1];
            const CIESample& b = kCIE1931[i];
            f32 t = (wavelength - a.wavelength) / (b.wavelength - a.wavelength);
            x = Mathf::lerp(a.x, b.x, t);
            y = Mathf::lerp(a.y, b.y, t);
            z = Mathf::lerp(a.z, b.z, t);
            return;
        }
    }
    x = kCIE1931[kCIESampleCount - 1].x;
    y = kCIE1931[kCIESampleCount - 1].y;
    z = kCIE1931[kCIESampleCount - 1].z;
}

// Trapezoidal integration of power * weight over the spectrum's wavelength range
f32 trapezoidal(const Spectrum& spectrum, const Vector<f32>& weights) {
    usize n = spectrum.samples.size();
    if (n == 0) return 0.0f;
    if (n == 1) return spectrum.samples[0].power * weights[0];

    f32 minWavelength = spectrum.samples[0].wavelength;
    f32 maxWavelength = spectrum.samples[n - 1].wavelength;
    f32 step = (maxWavelength - minWavelength) / (f32)(n - 1);

    f32 sum = 0.0f;
    for (usize i = 1; i < n; i++) {
        f32 a = spectrum.samples[i - 1].power * weights[i - 1];
        f32 b = spectrum.samples[i].power * weights[i];
        sum += 0.5f * (a + b) * step;
    }
    return sum;
}

// True when two spectra share the same wavelength grid (count + endpoints)
bool sameGrid(const Spectrum& a, const Spectrum& b) {
    if (a.samples.size() != b.samples.size()) return false;
    if (a.samples.empty()) return false;
    return Mathf::abs(a.samples[0].wavelength - b.samples[0].wavelength) < 0.01f &&
           Mathf::abs(a.samples[a.samples.size() - 1].wavelength -
                      b.samples[b.samples.size() - 1].wavelength) < 0.01f;
}
} // namespace

// ============================================================================
// SpectralRendering implementation
// ============================================================================

Spectrum SpectralRendering::blackbodySpectrum(f32 temperatureKelvin) {
    Spectrum spectrum;
    f32 c1 = 3.7418e-16f;
    f32 c2 = 1.4388e-2f;

    spectrum.samples.clear();
    for (u32 i = 0; i < sampleCount_; i++) {
        f32 t = (sampleCount_ == 1) ? 0.0f : (f32)i / (f32)(sampleCount_ - 1);
        f32 wavelengthNm = Mathf::lerp(SPECTRAL_MIN_NM, SPECTRAL_MAX_NM, t);
        f32 wavelengthM = wavelengthNm * 1e-9f;
        f32 denominator = expf(c2 / (wavelengthM * temperatureKelvin)) - 1.0f;
        f32 power = c1 / (powf(wavelengthM, 5.0f) * denominator);
        spectrum.samples.push_back(SpectralSample{ wavelengthNm, power });
    }

    // Normalize peak power to 1 for a unit-intensity SPD
    f32 peak = 0.0f;
    for (usize i = 0; i < spectrum.samples.size(); i++)
        if (spectrum.samples[i].power > peak) peak = spectrum.samples[i].power;
    if (peak > 0.0f)
        for (usize i = 0; i < spectrum.samples.size(); i++)
            spectrum.samples[i].power /= peak;

    spectrum.luminance = spectralLuminance(spectrum);
    colorTemp_ = temperatureKelvin;
    return spectrum;
}

f32 SpectralRendering::sampleSpectrum(const Spectrum& spectrum, f32 wavelength) {
    usize n = spectrum.samples.size();
    if (n == 0) return 0.0f;
    if (n == 1) return spectrum.samples[0].power;
    if (wavelength <= spectrum.samples[0].wavelength)
        return spectrum.samples[0].power;
    if (wavelength >= spectrum.samples[n - 1].wavelength)
        return spectrum.samples[n - 1].power;

    for (usize i = 1; i < n; i++) {
        if (wavelength <= spectrum.samples[i].wavelength) {
            const SpectralSample& a = spectrum.samples[i - 1];
            const SpectralSample& b = spectrum.samples[i];
            f32 span = b.wavelength - a.wavelength;
            f32 t = (span > 0.0f) ? (wavelength - a.wavelength) / span : 0.0f;
            return Mathf::lerp(a.power, b.power, t);
        }
    }
    return spectrum.samples[n - 1].power;
}

Vec3 SpectralRendering::spectrumToXYZ(const Spectrum& spectrum) {
    if (spectrum.samples.empty()) return Vec3(0);
    if (cieX_.size() != spectrum.samples.size())
        buildCIEWeighting((u32)spectrum.samples.size());
    return Vec3(
        trapezoidal(spectrum, cieX_),
        trapezoidal(spectrum, cieY_),
        trapezoidal(spectrum, cieZ_));
}

Vec3 SpectralRendering::XYZToLinearRGB(const Vec3& xyz) {
    return Vec3(
         3.2406f * xyz.x - 1.5372f * xyz.y - 0.4986f * xyz.z,
        -0.9689f * xyz.x + 1.8758f * xyz.y + 0.0415f * xyz.z,
         0.0557f * xyz.x - 0.2040f * xyz.y + 1.0570f * xyz.z);
}

Vec3 SpectralRendering::linearRGBToSRGB(const Vec3& linear) {
    auto toSRGB = [](f32 c) {
        c = Mathf::max(c, 0.0f);
        if (c <= 0.0031308f) return 12.92f * c;
        return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
    };
    return Vec3(toSRGB(linear.x), toSRGB(linear.y), toSRGB(linear.z));
}

f32 SpectralRendering::spectralLuminance(const Spectrum& spectrum) {
    usize n = spectrum.samples.size();
    if (n == 0) return 0.0f;
    if (cieY_.size() != n) buildCIEWeighting((u32)n);

    f32 lum = trapezoidal(spectrum, cieY_);

    // Normalize so a spectrally flat SPD maps to luminance 1.0
    f32 minWavelength = spectrum.samples[0].wavelength;
    f32 maxWavelength = spectrum.samples[n - 1].wavelength;
    f32 step = (maxWavelength - minWavelength) / (f32)(n - 1);
    f32 norm = 0.0f;
    for (usize i = 1; i < n; i++)
        norm += 0.5f * (cieY_[i - 1] + cieY_[i]);
    norm *= step;
    if (norm > 0.0f) lum /= norm;
    return lum;
}

Spectrum SpectralRendering::addSpectra(const Spectrum& a, const Spectrum& b) {
    Spectrum result;
    if (sameGrid(a, b)) {
        for (usize i = 0; i < a.samples.size(); i++) {
            result.samples.push_back(SpectralSample{
                a.samples[i].wavelength,
                a.samples[i].power + b.samples[i].power});
        }
    } else {
        for (u32 i = 0; i < sampleCount_; i++) {
            f32 t = (sampleCount_ == 1) ? 0.0f : (f32)i / (f32)(sampleCount_ - 1);
            f32 wavelength = Mathf::lerp(SPECTRAL_MIN_NM, SPECTRAL_MAX_NM, t);
            result.samples.push_back(SpectralSample{
                wavelength,
                sampleSpectrum(a, wavelength) + sampleSpectrum(b, wavelength)});
        }
    }
    result.luminance = spectralLuminance(result);
    return result;
}

Spectrum SpectralRendering::multiplySpectra(const Spectrum& a, const Spectrum& b) {
    Spectrum result;
    if (sameGrid(a, b)) {
        for (usize i = 0; i < a.samples.size(); i++) {
            result.samples.push_back(SpectralSample{
                a.samples[i].wavelength,
                a.samples[i].power * b.samples[i].power});
        }
    } else {
        for (u32 i = 0; i < sampleCount_; i++) {
            f32 t = (sampleCount_ == 1) ? 0.0f : (f32)i / (f32)(sampleCount_ - 1);
            f32 wavelength = Mathf::lerp(SPECTRAL_MIN_NM, SPECTRAL_MAX_NM, t);
            result.samples.push_back(SpectralSample{
                wavelength,
                sampleSpectrum(a, wavelength) * sampleSpectrum(b, wavelength)});
        }
    }
    result.luminance = spectralLuminance(result);
    return result;
}

Spectrum SpectralRendering::normalizeSpectrum(const Spectrum& spectrum) {
    Spectrum result = spectrum;
    f32 peak = 0.0f;
    for (usize i = 0; i < result.samples.size(); i++)
        if (result.samples[i].power > peak) peak = result.samples[i].power;
    if (peak > 0.0f)
        for (usize i = 0; i < result.samples.size(); i++)
            result.samples[i].power /= peak;
    result.luminance = spectralLuminance(result);
    return result;
}

void SpectralRendering::setSampleCount(u32 count) {
    if (count < 2) count = 2;
    sampleCount_ = count;
    buildCIEWeighting(sampleCount_);
}

u32 SpectralRendering::getSampleCount() const {
    return sampleCount_;
}

void SpectralRendering::setEnableSpectral(bool enable) {
    enableSpectral_ = enable;
}

bool SpectralRendering::isSpectralEnabled() const {
    return enableSpectral_;
}

void SpectralRendering::buildCIEWeighting(u32 sampleCount) {
    if (sampleCount < 2) sampleCount = 2;
    cieX_.clear();
    cieY_.clear();
    cieZ_.clear();
    for (u32 i = 0; i < sampleCount; i++) {
        f32 t = (f32)i / (f32)(sampleCount - 1);
        f32 wavelength = Mathf::lerp(SPECTRAL_MIN_NM, SPECTRAL_MAX_NM, t);
        f32 x = 0, y = 0, z = 0;
        cieAt(wavelength, x, y, z);
        cieX_.push_back(x);
        cieY_.push_back(y);
        cieZ_.push_back(z);
    }
}

} // namespace Frost
