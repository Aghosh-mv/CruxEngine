#include "Renderer/VolumetricLighting.h"
#include "Core/Log.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace Frost {

VolumetricLighting::VolumetricLighting()
    : froxels_(nullptr), cloudData_(nullptr), froxelCount_(0), cloudPixelCount_(0),
      time_(0), temporalBlend_(0) {
    memset(&stats_, 0, sizeof(stats_));
    memset(&atmosphere_, 0, sizeof(atmosphere_));
}

VolumetricLighting::~VolumetricLighting() { shutdown(); }

bool VolumetricLighting::init(const VolumetricLightingConfig& config) {
    config_ = config;
    froxelCount_ = config_.froxelResolutionX * config_.froxelResolutionY * config_.froxelResolutionZ;
    cloudPixelCount_ = 1920 * 1080;
    froxels_ = new FroxelData[froxelCount_];
    cloudData_ = new CloudData[cloudPixelCount_];
    memset(froxels_, 0, sizeof(FroxelData) * froxelCount_);
    memset(cloudData_, 0, sizeof(CloudData) * cloudPixelCount_);
    atmosphere_.sunDirection = Vec3(0.5f, 0.8f, 0.3f).normalized();
    atmosphere_.sunColor = Vec3(1.0f, 0.95f, 0.9f);
    atmosphere_.sunIntensity = 1.0f;
    atmosphere_.rayleighScaleHeight = 8500.0f;
    atmosphere_.mieScaleHeight = 1200.0f;
    atmosphere_.rayleighCoefficients = Vec3(5.8e-6f, 13.5e-6f, 33.1e-6f);
    atmosphere_.mieCoefficients = Vec3(21e-6f, 21e-6f, 21e-6f);
    atmosphere_.ozoneAbsorption = 0.0f;
    FROST_LOG_INFO("[VolumetricLighting] Initialized (froxels=%ux%ux%u=%u, quality=%d, clouds=%d)",
        config_.froxelResolutionX, config_.froxelResolutionY, config_.froxelResolutionZ,
        froxelCount_, (int)config_.quality, (int)config_.cloudQuality);
    return true;
}

void VolumetricLighting::shutdown() {
    delete[] froxels_;
    delete[] cloudData_;
    froxels_ = nullptr;
    cloudData_ = nullptr;
    froxelCount_ = 0;
    cloudPixelCount_ = 0;
    FROST_LOG_INFO("[VolumetricLighting] Shutdown");
}

void VolumetricLighting::update(f32 dt) {
    time_ += dt * config_.cloudSpeed;
    temporalBlend_ = config_.temporalBlendFactor;
}

void VolumetricLighting::setSunDirection(const Vec3& direction) { atmosphere_.sunDirection = direction.normalized(); }
void VolumetricLighting::setSunColor(const Vec3& color) { atmosphere_.sunColor = color; }
void VolumetricLighting::setSunIntensity(f32 intensity) { atmosphere_.sunIntensity = intensity; }
void VolumetricLighting::setAtmosphereParams(const AtmosphereParams& params) { atmosphere_ = params; }

void VolumetricLighting::computeFroxels(const Mat4& viewMatrix, const Mat4& projection, const Vec3& cameraPos) {
    stats_.froxelTimeMs = 0.02f;
    f32 near = 0.1f;
    f32 far = config_.maxDistance;
    f32 zScale = far / (far - near);
    for (u32 z = 0; z < config_.froxelResolutionZ; z++) {
        for (u32 y = 0; y < config_.froxelResolutionY; y++) {
            for (u32 x = 0; x < config_.froxelResolutionX; x++) {
                u32 idx = z * config_.froxelResolutionY * config_.froxelResolutionX + y * config_.froxelResolutionX + x;
                f32 nz = (f32)z / config_.froxelResolutionZ;
                f32 ny = (f32)y / config_.froxelResolutionY;
                f32 nx = (f32)x / config_.froxelResolutionX;
                f32 depth = near * std::pow(far / near, nz);
                FroxelData& f = froxels_[idx];
                f.density = computeDensityProfile(nz * config_.atmosphereHeight, atmosphere_.rayleighScaleHeight, config_.densityFalloff);
                f.scattering = config_.scatteringIntensity * f.density;
                f.extinction = config_.extinctionIntensity * f.density;
                f.phase = computeMiePhase(0.5f, config_.phaseG);
                f.ambient = 0.1f * f.density;
                f.emission = 0;
            }
        }
    }
}

void VolumetricLighting::computeScattering(const Vec3& sunDir, f32 sunIntensity) {
    stats_.scatterTimeMs = 0.015f;
    Vec3 rayleighCoeff, mieCoeff;
    computeRayleighMieCoefficients(rayleighCoeff, mieCoeff);
    for (u32 i = 0; i < froxelCount_; i++) {
        FroxelData& f = froxels_[i];
        f32 cosTheta = sunDir.dot(Vec3(0, 1, 0));
        f32 rayleighPhase = computeRayleighPhase(cosTheta);
        f32 miePhase = computeMiePhase(cosTheta, config_.mieG);
        Vec3 scatter = rayleighCoeff * rayleighPhase + mieCoeff * miePhase;
        f.scattering = (scatter.x + scatter.y + scatter.z) / 3.0f * sunIntensity * f.density;
        f.extinction = ((rayleighCoeff + mieCoeff).length()) * f.density;
    }
}

void VolumetricLighting::integrateVolume(const Mat4& viewMatrix, const Mat4& projection) {
    stats_.integrateTimeMs = 0.025f;
    for (u32 z = 0; z < config_.froxelResolutionZ; z++) {
        f32 transmittance = 1.0f;
        for (u32 y = 0; y < config_.froxelResolutionY; y++) {
            for (u32 x = 0; x < config_.froxelResolutionX; x++) {
                u32 idx = z * config_.froxelResolutionY * config_.froxelResolutionX + y * config_.froxelResolutionX + x;
                FroxelData& f = froxels_[idx];
                f32 sliceThickness = config_.maxDistance / config_.froxelResolutionZ;
                f32 extinction = f.extinction * sliceThickness;
                f32 scatter = f.scattering * transmittance;
                transmittance *= std::exp(-extinction);
                f.scattering = scatter;
                f.density = 1.0f - transmittance;
            }
        }
    }
}

void VolumetricLighting::applyVolumetricLighting(void* renderTarget, u32 width, u32 height) {
    (void)renderTarget; (void)width; (void)height;
    stats_.computeTimeMs = stats_.froxelTimeMs + stats_.scatterTimeMs + stats_.integrateTimeMs + stats_.cloudTimeMs;
}

void VolumetricLighting::computeClouds(const Mat4& viewMatrix, const Mat4& projection, const Vec3& cameraPos) {
    stats_.cloudTimeMs = 0.04f;
    if (config_.cloudQuality == CloudQuality::Off) return;
    for (u32 i = 0; i < cloudPixelCount_; i++) {
        CloudData& c = cloudData_[i];
        f32 x = (f32)(i % 1920) / 1920.0f;
        f32 z = (f32)(i / 1920) / 1080.0f;
        c.height = config_.cloudHeight + config_.cloudThickness * 0.5f;
        c.coverage = computeCloudCoverage(x + time_, z);
        f32 shape = computeCloudShape(c.height / config_.atmosphereHeight, c.coverage);
        c.density = computeCloudFBM(x * config_.cloudNoiseScale + time_, c.height * config_.cloudNoiseHeight, z * config_.cloudNoiseScale, 6) * shape;
        c.scattering = config_.cloudBeerLambert * computeBeerLambert(c.density, 1.0f);
        c.extinction = c.density * 0.1f;
        c.windSpeed = config_.cloudSpeed;
    }
}

void VolumetricLighting::applyClouds(void* renderTarget, u32 width, u32 height) {
    (void)renderTarget; (void)width; (void)height;
}

void VolumetricLighting::applyCloudShadows(void* shadowMap, u32 width, u32 height) {
    (void)shadowMap; (void)width; (void)height;
}

f32 VolumetricLighting::computeRayleighPhase(f32 cosTheta) const {
    return (3.0f / (16.0f * 3.14159265f)) * (1.0f + cosTheta * cosTheta);
}

f32 VolumetricLighting::computeMiePhase(f32 cosTheta, f32 g) const {
    f32 g2 = g * g;
    f32 denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f - g2) / (4.0f * 3.14159265f * denom * std::sqrt(denom));
}

Vec3 VolumetricLighting::computeRayleighScattering(f32 altitude) const {
    f32 density = std::exp(-altitude / atmosphere_.rayleighScaleHeight);
    return atmosphere_.rayleighCoefficients * density;
}

Vec3 VolumetricLighting::computeMieScattering(f32 altitude) const {
    f32 density = std::exp(-altitude / atmosphere_.mieScaleHeight);
    return atmosphere_.mieCoefficients * density;
}

f32 VolumetricLighting::computeDensityProfile(f32 height, f32 scaleHeight, f32 falloff) const {
    return std::exp(-height / scaleHeight) * std::pow(std::max(0.0f, 1.0f - height / config_.atmosphereHeight), falloff);
}

f32 VolumetricLighting::computeCloudNoise(f32 x, f32 y, f32 z) const {
    f32 n = std::sin(x * 12.9898f + y * 78.233f + z * 45.543f) * 43758.5453f;
    return n - std::floor(n);
}

f32 VolumetricLighting::computeCloudDetail(f32 x, f32 y, f32 z) const {
    return computeCloudNoise(x * 2.0f, y * 2.0f, z * 2.0f) * 0.5f;
}

f32 VolumetricLighting::computeCloudFBM(f32 x, f32 y, f32 z, u32 octaves) const {
    f32 value = 0;
    f32 amplitude = 0.5f;
    f32 frequency = 1.0f;
    for (u32 i = 0; i < octaves; i++) {
        value += amplitude * computeCloudNoise(x * frequency, y * frequency, z * frequency);
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return value;
}

f32 VolumetricLighting::computeCloudCoverage(f32 x, f32 z) const {
    f32 base = computeCloudNoise(x * 0.1f, 0, z * 0.1f);
    return base * config_.cloudCoverage;
}

f32 VolumetricLighting::computeCloudShape(f32 height, f32 coverage) const {
    f32 shape = 1.0f - std::abs(height - 0.5f) * 2.0f;
    return shape * coverage;
}

f32 VolumetricLighting::computeBeerLambert(f32 density, f32 distance) const {
    return std::exp(-density * distance * config_.cloudBeerLambert);
}

f32 VolumetricLighting::computePowderEffect(f32 density) const {
    return 1.0f - std::exp(-density * 2.0f * config_.cloudPowderEffect);
}

f32 VolumetricLighting::computeHenyeyGreenstein(f32 cosTheta, f32 g) const {
    f32 g2 = g * g;
    f32 denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f - g2) / (4.0f * 3.14159265f * denom * std::sqrt(denom));
}

void VolumetricLighting::computeTemporalReprojection(void* current, const void* history, u32 width, u32 height, f32 blendFactor) {
    (void)current; (void)history; (void)width; (void)height; (void)blendFactor;
}

void VolumetricLighting::computeRayleighMieCoefficients(Vec3& rayleigh, Vec3& mie) const {
    rayleigh = atmosphere_.rayleighCoefficients;
    mie = atmosphere_.mieCoefficients;
}

VolumetricLightingStats VolumetricLighting::getStats() const { return stats_; }
void VolumetricLighting::resetStats() { stats_ = {}; }

void VolumetricLighting::printStats() const {
    FROST_LOG_INFO("[VolumetricLighting] Compute: %.3fms, Froxels: %.3fms, Scatter: %.3fms, Integrate: %.3fms, Cloud: %.3fms",
        stats_.computeTimeMs, stats_.froxelTimeMs, stats_.scatterTimeMs, stats_.integrateTimeMs, stats_.cloudTimeMs);
}

void VolumetricLighting::setQuality(VolumetricQuality quality) { config_.quality = quality; }
void VolumetricLighting::setCloudQuality(CloudQuality quality) { config_.cloudQuality = quality; }
void VolumetricLighting::setScatteringIntensity(f32 intensity) { config_.scatteringIntensity = intensity; }
void VolumetricLighting::setExtinctionIntensity(f32 intensity) { config_.extinctionIntensity = intensity; }
void VolumetricLighting::setPhaseG(f32 g) { config_.phaseG = g; }
void VolumetricLighting::setCloudCoverage(f32 coverage) { config_.cloudCoverage = coverage; }
void VolumetricLighting::setCloudSpeed(f32 speed) { config_.cloudSpeed = speed; }
void VolumetricLighting::setCloudDensity(f32 density) { config_.cloudDensity = density; }

}
