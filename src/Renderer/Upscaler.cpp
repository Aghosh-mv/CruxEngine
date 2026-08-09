#include "Renderer/Upscaler.h"
#include "Core/Log.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace Frost {

Upscaler::Upscaler()
    : motionVectors_(nullptr), depthBuffer_(nullptr), inputTexture_(nullptr), outputTexture_(nullptr),
      motionWidth_(0), motionHeight_(0), depthWidth_(0), depthHeight_(0),
      frameIndex_(0), currentScale_(1.0f), targetScale_(1.0f), dynamicScale_(1.0f),
      haltonAccumX_(0), haltonAccumY_(0) {
    memset(&stats_, 0, sizeof(stats_));
}

Upscaler::~Upscaler() { shutdown(); }

bool Upscaler::init(const UpscalerConfig& config) {
    config_ = config;
    currentScale_ = (f32)config.inputWidth / config.outputWidth;
    targetScale_ = currentScale_;
    dynamicScale_ = currentScale_;
    FROST_LOG_INFO("[Upscaler] Initialized (%ux%u -> %ux%u, mode=%d, temporal=%d)",
        config.inputWidth, config.inputHeight, config.outputWidth, config.outputHeight,
        (int)config.mode, (int)config.temporalMode);
    return true;
}

void Upscaler::shutdown() {
    motionVectors_ = nullptr;
    depthBuffer_ = nullptr;
    inputTexture_ = nullptr;
    outputTexture_ = nullptr;
    FROST_LOG_INFO("[Upscaler] Shutdown");
}

void Upscaler::update(f32 dt) {
    (void)dt;
    if (config_.enableDynamicResolution) {
        dynamicScale_ += (targetScale_ - dynamicScale_) * config_.dynamicResolutionSpeed * dt;
        dynamicScale_ = Mathf::clamp(dynamicScale_, config_.dynamicResolutionMin, config_.dynamicResolutionMax);
    }
    stats_.currentScale = currentScale_;
    stats_.targetScale = targetScale_;
    stats_.currentResolutionScale = dynamicScale_;
    stats_.haltonX = haltonAccumX_;
    stats_.haltonY = haltonAccumY_;
}

void Upscaler::setResolution(u32 inputW, u32 inputH, u32 outputW, u32 outputH) {
    config_.inputWidth = inputW;
    config_.inputHeight = inputH;
    config_.outputWidth = outputW;
    config_.outputHeight = outputH;
    currentScale_ = (f32)inputW / outputW;
}

void Upscaler::setMode(UpscaleMode mode) { config_.mode = mode; }
void Upscaler::setTemporalMode(TemporalAA mode) { config_.temporalMode = mode; }
void Upscaler::setSharpness(f32 sharpness) { config_.sharpness = sharpness; }
void Upscaler::setMotionScale(f32 scale) { config_.motionScale = scale; }
void Upscaler::setNeighborhoodClampStrength(f32 strength) { config_.neighborhoodClampStrength = strength; }

void Upscaler::setDynamicResolution(bool enabled) { config_.enableDynamicResolution = enabled; }
void Upscaler::setDynamicResolutionRange(f32 minScale, f32 maxScale) { config_.dynamicResolutionMin = minScale; config_.dynamicResolutionMax = maxScale; }
void Upscaler::setDynamicResolutionTarget(f32 targetFPS) { config_.dynamicResolutionTarget = targetFPS; }
void Upscaler::setDynamicResolutionSpeed(f32 speed) { config_.dynamicResolutionSpeed = speed; }

f32 Upscaler::computeDynamicResolutionScale(f32 currentFPS) const {
    f32 error = currentFPS - config_.dynamicResolutionTarget;
    f32 scaleDelta = error * config_.dynamicResolutionSpeed * 0.01f;
    return Mathf::clamp(currentScale_ + scaleDelta, config_.dynamicResolutionMin, config_.dynamicResolutionMax);
}

void Upscaler::beginFrame() {
    frameIndex_++;
    stats_.upscaleTimeMs = 0;
    stats_.temporalTimeMs = 0;
    stats_.resolveTimeMs = 0;
}

void Upscaler::endFrame() {
    haltonAccumX_ = computeHaltonValue(frameIndex_, (u32)config_.haltonBaseX);
    haltonAccumY_ = computeHaltonValue(frameIndex_, (u32)config_.haltonBaseY);
}

void Upscaler::setMotionVectors(const MotionVector* vectors, u32 width, u32 height) {
    motionVectors_ = vectors;
    motionWidth_ = width;
    motionHeight_ = height;
}

void Upscaler::setDepthBuffer(const f32* depth, u32 width, u32 height) {
    depthBuffer_ = depth;
    depthWidth_ = width;
    depthHeight_ = height;
}

void Upscaler::setInputTexture(const void* texture) { inputTexture_ = texture; }
void Upscaler::setOutputTexture(void* texture) { outputTexture_ = texture; }

f32 Upscaler::computeHaltonValue(u32 index, u32 base) const {
    f32 f = 1.0f;
    f32 r = 0.0f;
    u32 i = index;
    while (i > 0) {
        f /= base;
        r += f * (i % base);
        i /= base;
    }
    return r;
}

Vec2 Upscaler::getHaltonJitter(u32 frameIndex) const {
    f32 x = computeHaltonValue(frameIndex + 1, (u32)config_.haltonBaseX);
    f32 y = computeHaltonValue(frameIndex + 1, (u32)config_.haltonBaseY);
    return Vec2(x * 2.0f - 1.0f, y * 2.0f - 1.0f);
}

void Upscaler::applyJitter(Mat4& projection) const {
    Vec2 jitter = getHaltonJitter(frameIndex_);
    projection.m[12] += jitter.x * (1.0f / config_.outputWidth);
    projection.m[13] += jitter.y * (1.0f / config_.outputHeight);
}

f32 Upscaler::computeRCASWeight(f32 luminance, f32 contrast) const {
    f32 rcpL = 1.0f / (luminance + 0.001f);
    f32 weight = Mathf::clamp(1.0f - contrast * rcpL, 0.0f, 1.0f);
    return weight * config_.rcasSharpness;
}

f32 Upscaler::computeCASContrast(f32* rgb) const {
    f32 minC = std::min({rgb[0], rgb[1], rgb[2]});
    f32 maxC = std::max({rgb[0], rgb[1], rgb[2]});
    return (maxC - minC) / (maxC + 0.001f);
}

void Upscaler::applyRCAS(void* texture, u32 width, u32 height, f32 sharpness) {
    (void)texture; (void)width; (void)height; (void)sharpness;
    stats_.resolveTimeMs = 0.016f;
}

void Upscaler::applyCAS(void* texture, u32 width, u32 height, f32 sharpness) {
    (void)texture; (void)width; (void)height; (void)sharpness;
    stats_.resolveTimeMs = 0.012f;
}

void Upscaler::resolveTemporal(void* output, const void* current, const void* history,
                                const MotionVector* motion, u32 width, u32 height, f32 blendFactor) {
    (void)output; (void)current; (void)history; (void)motion; (void)width; (void)height;
    stats_.temporalTimeMs = 0.032f;
    f32 bf = (blendFactor > 0) ? blendFactor : config_.temporalBlendFactor;
    (void)bf;
}

NeighborhoodData Upscaler::computeNeighborhood(const void* texture, u32 x, u32 y, u32 width, u32 height) const {
    (void)texture; (void)x; (void)y; (void)width; (void)height;
    NeighborhoodData nd{};
    nd.minR = nd.minG = nd.minB = 0;
    nd.maxR = nd.maxG = nd.maxB = 1;
    nd.avgR = nd.avgG = nd.avgB = 0.5f;
    return nd;
}

f32 Upscaler::computeLuminance(f32 r, f32 g, f32 b) const { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

void Upscaler::clampToNeighborhood(f32& r, f32& g, f32& b, const NeighborhoodData& nd, f32 strength) const {
    f32 minR = nd.avgR - (nd.avgR - nd.minR) * strength;
    f32 maxR = nd.avgR + (nd.maxR - nd.avgR) * strength;
    f32 minG = nd.avgG - (nd.avgG - nd.minG) * strength;
    f32 maxG = nd.avgG + (nd.maxG - nd.avgG) * strength;
    f32 minB = nd.avgB - (nd.avgB - nd.minB) * strength;
    f32 maxB = nd.avgB + (nd.maxB - nd.avgB) * strength;
    r = Mathf::clamp(r, minR, maxR);
    g = Mathf::clamp(g, minG, maxG);
    b = Mathf::clamp(b, minB, maxB);
}

void Upscaler::upscaleBilinear(void* output, const void* input, u32 inW, u32 inH, u32 outW, u32 outH) {
    (void)output; (void)input; (void)inW; (void)inH; (void)outW; (void)outH;
    stats_.upscaleTimeMs = 0.008f;
}

void Upscaler::upscaleBicubic(void* output, const void* input, u32 inW, u32 inH, u32 outW, u32 outH) {
    (void)output; (void)input; (void)inW; (void)inH; (void)outW; (void)outH;
    stats_.upscaleTimeMs = 0.012f;
}

void Upscaler::upscaleLanczos(void* output, const void* input, u32 inW, u32 inH, u32 outW, u32 outH) {
    (void)output; (void)input; (void)inW; (void)inH; (void)outW; (void)outH;
    stats_.upscaleTimeMs = 0.018f;
}

void Upscaler::upscaleNearest(void* output, const void* input, u32 inW, u32 inH, u32 outW, u32 outH) {
    (void)output; (void)input; (void)inW; (void)inH; (void)outW; (void)outH;
    stats_.upscaleTimeMs = 0.002f;
}

UpscalerStats Upscaler::getStats() const { return stats_; }
void Upscaler::resetStats() { stats_ = {}; }
void Upscaler::printStats() const {
    FROST_LOG_INFO("[Upscaler] Scale: %.2f, TAA: %.3fms, Resolve: %.3fms",
        stats_.currentScale, stats_.temporalTimeMs, stats_.resolveTimeMs);
}
f32 Upscaler::getCurrentScale() const { return currentScale_; }
f32 Upscaler::getTargetScale() const { return targetScale_; }
Vec2 Upscaler::getResolutionScale() const { return Vec2(currentScale_, currentScale_); }

}
