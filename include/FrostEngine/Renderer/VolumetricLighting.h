#pragma once
#include "Core/Types.h"
#include "Core/Math.h"
#include "Core/Vector.h"

namespace Frost {

enum class VolumetricQuality : u8 { Low = 0, Medium, High, Ultra };
enum class CloudQuality : u8 { Off = 0, Low, Medium, High, Ultra };

struct VolumetricLightingConfig {
    bool enabled = true;
    VolumetricQuality quality = VolumetricQuality::Medium;
    CloudQuality cloudQuality = CloudQuality::Medium;
    u32 froxelResolutionX = 160;
    u32 froxelResolutionY = 90;
    u32 froxelResolutionZ = 64;
    f32 froxelDepthScale = 1.0f;
    f32 maxDistance = 256.0f;
    f32 scatteringIntensity = 1.0f;
    f32 extinctionIntensity = 0.5f;
    f32 phaseG = 0.76f;
    f32 temporalBlendFactor = 0.1f;
    bool enableTemporalReprojection = true;
    bool enableRayleigh = true;
    bool enableMie = true;
    f32 rayleighScattering = 0.058f;
    f32 rayleighAbsorption = 0.0f;
    f32 mieScattering = 0.11f;
    f32 mieAbsorption = 0.001f;
    f32 mieG = 0.76f;
    f32 atmosphereHeight = 100000.0f;
    f32 densityFalloff = 4.0f;
    f32 cloudCoverage = 0.5f;
    f32 cloudSpeed = 0.01f;
    f32 cloudDensity = 0.3f;
    f32 cloudHeight = 2000.0f;
    f32 cloudThickness = 500.0f;
    f32 cloudWindX = 0.5f;
    f32 cloudWindZ = 0.2f;
    f32 cloudNoiseScale = 0.005f;
    f32 cloudNoiseHeight = 0.01f;
    f32 cloudBeerLambert = 1.0f;
    f32 cloudPowderEffect = 1.0f;
    f32 cloudHGPhase = 0.8f;
    bool enableCloudShadows = true;
    f32 cloudShadowIntensity = 0.5f;
};

struct VolumetricConfig {
    f32 fogDensity = 0.01f;
    Vec3 fogColor = Vec3(0.7f, 0.8f, 0.9f);
    f32 fogHeight = 0.0f;
    f32 fogHeightFalloff = 0.1f;
    u32 rayMarchSteps = 64;
    f32 maxDistance = 500.0f;
    f32 godRayIntensity = 0.5f;
    u32 godRaySamples = 16;
    bool enableFog = true;
    bool enableGodRays = true;
    bool enableVolumetricShadows = true;
};

struct VolumetricLightingStats {
    f32 computeTimeMs;
    f32 froxelTimeMs;
    f32 scatterTimeMs;
    f32 integrateTimeMs;
    f32 cloudTimeMs;
    u32 froxelCount;
    u32 cloudPixels;
    f32 averageScattering;
    f32 averageExtinction;
    u32 fogSamples;
    u32 godRaySamples;
    f32 volumetricTimeMs;
};

struct FroxelData {
    f32 scattering;
    f32 extinction;
    f32 phase;
    f32 ambient;
    f32 density;
    f32 emission;
};

struct CloudData {
    f32 density;
    f32 scattering;
    f32 extinction;
    f32 height;
    f32 coverage;
    f32 windSpeed;
};

struct AtmosphereParams {
    Vec3 sunDirection;
    Vec3 sunColor;
    f32 sunIntensity;
    f32 rayleighScaleHeight;
    f32 mieScaleHeight;
    Vec3 rayleighCoefficients;
    Vec3 mieCoefficients;
    f32 ozoneAbsorption;
};

class VolumetricLighting {
public:
    VolumetricLighting();
    ~VolumetricLighting();

    bool init(const VolumetricLightingConfig& config);
    void shutdown();
    void update(f32 dt);

    void setSunDirection(const Vec3& direction);
    void setSunColor(const Vec3& color);
    void setSunIntensity(f32 intensity);
    void setAtmosphereParams(const AtmosphereParams& params);

    void computeFroxels(const Mat4& viewMatrix, const Mat4& projection, const Vec3& cameraPos);
    void computeScattering(const Vec3& sunDir, f32 sunIntensity);
    void integrateVolume(const Mat4& viewMatrix, const Mat4& projection);
    void applyVolumetricLighting(void* renderTarget, u32 width, u32 height);

    void computeClouds(const Mat4& viewMatrix, const Mat4& projection, const Vec3& cameraPos);
    void applyClouds(void* renderTarget, u32 width, u32 height);
    void applyCloudShadows(void* shadowMap, u32 width, u32 height);

    f32 computeRayleighPhase(f32 cosTheta) const;
    f32 computeMiePhase(f32 cosTheta, f32 g) const;
    Vec3 computeRayleighScattering(f32 altitude) const;
    Vec3 computeMieScattering(f32 altitude) const;
    f32 computeDensityProfile(f32 height, f32 scaleHeight, f32 falloff) const;

    f32 computeCloudNoise(f32 x, f32 y, f32 z) const;
    f32 computeCloudDetail(f32 x, f32 y, f32 z) const;
    f32 computeCloudFBM(f32 x, f32 y, f32 z, u32 octaves) const;
    f32 computeCloudCoverage(f32 x, f32 z) const;
    f32 computeCloudShape(f32 height, f32 coverage) const;
    f32 computeBeerLambert(f32 density, f32 distance) const;
    f32 computePowderEffect(f32 density) const;
    f32 computeHenyeyGreenstein(f32 cosTheta, f32 g) const;

    void computeTemporalReprojection(void* current, const void* history, u32 width, u32 height, f32 blendFactor);
    void computeRayleighMieCoefficients(Vec3& rayleigh, Vec3& mie) const;

    VolumetricLightingStats getStats() const;
    void resetStats();
    void printStats() const;

    void setQuality(VolumetricQuality quality);
    void setCloudQuality(CloudQuality quality);
    void setScatteringIntensity(f32 intensity);
    void setExtinctionIntensity(f32 intensity);
    void setPhaseG(f32 g);
    void setCloudCoverage(f32 coverage);
    void setCloudSpeed(f32 speed);
    void setCloudDensity(f32 density);

    void setVolumetricConfig(const VolumetricConfig& cfg);
    const VolumetricConfig& getVolumetricConfig() const;

    void applyFog(const Vec3& rayOrigin, const Vec3& rayDir, f32 distance, Vec3& color) const;
    f32 computeGodRays(const Vec3& sunDir, const Vec3& viewPos, u32 width, u32 height) const;
    void buildFogVolume(const Vec3& cameraPos, u32 resolution);
    f32 sampleFogVolume(const Vec3& pos, const Vec3& cameraPos, u32 resolution) const;
    f32 rayMarchFog(const Vec3& origin, const Vec3& dir, f32 maxDist, const Vec3& cameraPos, u32 resolution) const;
    f32 applyVolumetricShadows(const Vec3& origin, const Vec3& dir, f32 maxDist, const Vector<Mat4>& shadowCascadeViewProjs) const;
    void clearVolume();

private:
    VolumetricLightingConfig config_;
    mutable VolumetricLightingStats stats_;
    AtmosphereParams atmosphere_;
    FroxelData* froxels_;
    CloudData* cloudData_;
    u32 froxelCount_;
    u32 cloudPixelCount_;
    f32 time_;
    f32 temporalBlend_;

    VolumetricConfig volCfg_;
    Vector<f32> fogDensityVolume_;
    u32 fogResolution_;
    Vector<Vec3> godRayAccum_;
};

}
