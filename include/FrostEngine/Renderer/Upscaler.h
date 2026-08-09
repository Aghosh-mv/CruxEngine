#pragma once
#include "Core/Types.h"
#include "Core/Math.h"
#include "Core/Vector.h"

namespace Frost {

enum class UpscaleMode : u8 { Bilinear, Bicubic, Lanczos, NearestNeighbor, FSR1, FSR2, DLSS, XeSS };
enum class TemporalAA : u8 { None, TAA, TSAA };

struct UpscalerConfig {
    UpscaleMode mode = UpscaleMode::Bicubic;
    TemporalAA temporalMode = TemporalAA::TAA;
    u32 inputWidth = 1280;
    u32 inputHeight = 720;
    u32 outputWidth = 1920;
    u32 outputHeight = 1080;
    f32 sharpness = 0.5f;
    f32 temporalBlendFactor = 0.05f;
    f32 haltonBaseX = 2.0f;
    f32 haltonBaseY = 3.0f;
    f32 motionScale = 1.0f;
    f32 neighborhoodClampStrength = 0.25f;
    f32 dynamicResolutionMin = 0.5f;
    f32 dynamicResolutionMax = 1.0f;
    f32 dynamicResolutionTarget = 16.67f;
    f32 dynamicResolutionSpeed = 0.1f;
    f32 rcasSharpness = 0.2f;
    bool enableCAS = true;
    bool enableRCAS = true;
    bool enableDynamicResolution = false;
};

struct UpscalerStats {
    f32 upscaleTimeMs;
    f32 temporalTimeMs;
    f32 resolveTimeMs;
    f32 currentScale;
    f32 targetScale;
    f32 currentResolutionScale;
    f32 haltonX;
    f32 haltonY;
    f32 currentFPS;
    f32 targetFPS;
};

struct MotionVector {
    f32 x;
    f32 y;
    f32 z;
    f32 w;
};

struct NeighborhoodData {
    f32 minR, maxR;
    f32 minG, maxG;
    f32 minB, maxB;
    f32 avgR, avgG, avgB;
};

class Upscaler {
public:
    Upscaler();
    ~Upscaler();

    bool init(const UpscalerConfig& config);
    void shutdown();
    void update(f32 dt);

    void setResolution(u32 inputW, u32 inputH, u32 outputW, u32 outputH);
    void setMode(UpscaleMode mode);
    void setTemporalMode(TemporalAA mode);
    void setSharpness(f32 sharpness);
    void setMotionScale(f32 scale);
    void setNeighborhoodClampStrength(f32 strength);

    void setDynamicResolution(bool enabled);
    void setDynamicResolutionRange(f32 minScale, f32 maxScale);
    void setDynamicResolutionTarget(f32 targetFPS);
    void setDynamicResolutionSpeed(f32 speed);
    f32 computeDynamicResolutionScale(f32 currentFPS) const;

    void beginFrame();
    void endFrame();
    void setMotionVectors(const MotionVector* vectors, u32 width, u32 height);
    void setDepthBuffer(const f32* depth, u32 width, u32 height);
    void setInputTexture(const void* texture);
    void setOutputTexture(void* texture);

    f32 computeHaltonValue(u32 index, u32 base) const;
    Vec2 getHaltonJitter(u32 frameIndex) const;
    void applyJitter(Mat4& projection) const;

    f32 computeRCASWeight(f32 luminance, f32 contrast) const;
    f32 computeCASContrast(f32* rgb) const;
    void applyRCAS(void* texture, u32 width, u32 height, f32 sharpness);
    void applyCAS(void* texture, u32 width, u32 height, f32 sharpness);

    void resolveTemporal(void* output, const void* current, const void* history,
                         const MotionVector* motion, u32 width, u32 height, f32 blendFactor);
    NeighborhoodData computeNeighborhood(const void* texture, u32 x, u32 y, u32 width, u32 height) const;
    f32 computeLuminance(f32 r, f32 g, f32 b) const;
    void clampToNeighborhood(f32& r, f32& g, f32& b, const NeighborhoodData& neighbor, f32 strength) const;

    void upscaleBilinear(void* output, const void* input, u32 inW, u32 inH, u32 outW, u32 outH);
    void upscaleBicubic(void* output, const void* input, u32 inW, u32 inH, u32 outW, u32 outH);
    void upscaleLanczos(void* output, const void* input, u32 inW, u32 inH, u32 outW, u32 outH);
    void upscaleNearest(void* output, const void* input, u32 inW, u32 inH, u32 outW, u32 outH);

    UpscalerStats getStats() const;
    void resetStats();
    void printStats() const;

    f32 getCurrentScale() const;
    f32 getTargetScale() const;
    Vec2 getResolutionScale() const;

private:
    UpscalerConfig config_;
    UpscalerStats stats_;
    const MotionVector* motionVectors_;
    const f32* depthBuffer_;
    const void* inputTexture_;
    void* outputTexture_;
    u32 motionWidth_;
    u32 motionHeight_;
    u32 depthWidth_;
    u32 depthHeight_;
    u32 frameIndex_;
    f32 currentScale_;
    f32 targetScale_;
    f32 dynamicScale_;
    f32 haltonAccumX_;
    f32 haltonAccumY_;
};

}
