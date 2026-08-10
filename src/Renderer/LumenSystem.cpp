// ============================================================================
// FrostEngine Lumen-like Dynamic Global Illumination - Implementation
// ============================================================================

#include "FrostEngine/Renderer/LumenSystem.h"
#include "FrostEngine/Renderer/Camera.h"
#include "FrostEngine/Core/Math.h"
#include <cmath>
#include <cstring>
#include <chrono>

namespace Frost {

// ============================================================================
// Constructor / Destructor
// ============================================================================
LumenSystem::LumenSystem() = default;

LumenSystem::~LumenSystem() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================
bool LumenSystem::init(u32 screenWidth, u32 screenHeight) {
    if (initialized_) return true;

    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;

    // Initialize surface cache
    surfaceCards_.resize(surfaceCacheCfg_.maxCards);
    for (auto& card : surfaceCards_) {
        card.isValid = false;
        card.isEmissive = false;
    }

    // Initialize page-based surface cache
    pages_.reserve(pageCacheCfg_.maxPages);
    radianceAtlas_.resize(pageCacheCfg_.atlasSize * pageCacheCfg_.atlasSize * 3);
    for (auto& texel : radianceAtlas_) texel = 0.0f;

    // Initialize radiance cache probes
    u32 totalProbes = radianceCacheCfg_.probesPerAxis * radianceCacheCfg_.probesPerAxis *
                      radianceCacheCfg_.probesPerAxis;
    radianceProbes_.resize(totalProbes);
    activeProbeCount_ = 0;

    for (u32 z = 0; z < radianceCacheCfg_.probesPerAxis; z++) {
        for (u32 y = 0; y < radianceCacheCfg_.probesPerAxis; y++) {
            for (u32 x = 0; x < radianceCacheCfg_.probesPerAxis; x++) {
                u32 idx = z * radianceCacheCfg_.probesPerAxis * radianceCacheCfg_.probesPerAxis +
                          y * radianceCacheCfg_.probesPerAxis + x;
                RadianceProbe& probe = radianceProbes_[idx];
                probe.position = Vec3(
                    (f32)x * radianceCacheCfg_.probeSpacing,
                    (f32)y * radianceCacheCfg_.probeSpacing,
                    (f32)z * radianceCacheCfg_.probeSpacing
                );
                probe.weight = 0.0f;
                probe.historyWeight = 0.0f;
                probe.needsUpdate = true;
                probe.frameLastUpdated = 0;
                for (u32 f = 0; f < 6; f++) {
                    probe.radiance[f] = Vec3(0);
                    probe.historyRadiance[f] = Vec3(0);
                }
            }
        }
    }

    // Initialize screen-space buffers
    u32 pixelCount = screenWidth_ * screenHeight_;
    depthBuffer_.resize(pixelCount);
    normalBuffer_.resize(pixelCount);
    albedoBuffer_.resize(pixelCount);
    historyGI_.resize(pixelCount);
    currentGI_.resize(pixelCount);
    historyDepth_.resize(pixelCount);
    reflectionOutput_.resize(pixelCount);

    // Initialize bounce buffers
    bounceBuffer_[0].resize(pixelCount);
    bounceBuffer_[1].resize(pixelCount);

    // Initialize global distance field clipmaps
    clipmapLevels_.resize(globalDFCfg_.clipmapLevels);
    u32 clipmapPixelsPerLevel = globalDFCfg_.clipmapResolution * globalDFCfg_.clipmapResolution *
                                globalDFCfg_.clipmapResolution;
    clipmapData_.resize(globalDFCfg_.clipmapLevels * clipmapPixelsPerLevel);

    // Initialize defaults
    sunDirection_ = Vec3(0.5f, -0.8f, -0.3f).normalized();
    sunColor_ = Vec3(1.0f, 0.96f, 0.9f);
    sunIntensity_ = 3.0f;
    ambientColor_ = Vec3(0.3f, 0.4f, 0.6f);
    ambientIntensity_ = 0.45f;

    applyQualityPreset();

    initialized_ = true;
    return true;
}

void LumenSystem::shutdown() {
    if (!initialized_) return;

    surfaceCards_.clear();
    pages_.clear();
    patches_.clear();
    pageMap_.clear();
    radianceAtlas_.clear();
    freePageSlots_.clear();
    sdfMeshes_.clear();
    clipmapLevels_.clear();
    clipmapData_.clear();
    radianceProbes_.clear();
    depthBuffer_.clear();
    normalBuffer_.clear();
    albedoBuffer_.clear();
    historyGI_.clear();
    currentGI_.clear();
    historyDepth_.clear();
    reflectionOutput_.clear();
    bounceBuffer_[0].clear();
    bounceBuffer_[1].clear();

    initialized_ = false;
}

void LumenSystem::resize(u32 screenWidth, u32 screenHeight) {
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;

    u32 pixelCount = screenWidth_ * screenHeight_;
    depthBuffer_.resize(pixelCount);
    normalBuffer_.resize(pixelCount);
    albedoBuffer_.resize(pixelCount);
    historyGI_.resize(pixelCount);
    currentGI_.resize(pixelCount);
    historyDepth_.resize(pixelCount);
    reflectionOutput_.resize(pixelCount);
    bounceBuffer_[0].resize(pixelCount);
    bounceBuffer_[1].resize(pixelCount);
}

// ============================================================================
// Quality Preset
// ============================================================================
void LumenSystem::applyQualityPreset() {
    switch (quality_) {
    case LumenQuality::Low:
        screenTraceCfg_.maxSteps = 16;
        screenTraceCfg_.hiZEnabled = true;
        temporalCfg_.historyBlendWeight = 0.85f;
        bounceCfg_.maxBounces = 1;
        bounceCfg_.bounceResolutionScale = 4;
        radianceCacheCfg_.maxRaysPerProbe = 16;
        radianceCacheCfg_.probesPerAxis = 8;
        reflectionCfg_.maxRoughnessSamples = 4;
        break;

    case LumenQuality::Medium:
        screenTraceCfg_.maxSteps = 32;
        screenTraceCfg_.hiZEnabled = true;
        temporalCfg_.historyBlendWeight = 0.88f;
        bounceCfg_.maxBounces = 1;
        bounceCfg_.bounceResolutionScale = 2;
        radianceCacheCfg_.maxRaysPerProbe = 32;
        radianceCacheCfg_.probesPerAxis = 12;
        reflectionCfg_.maxRoughnessSamples = 8;
        break;

    case LumenQuality::High:
        screenTraceCfg_.maxSteps = 64;
        screenTraceCfg_.hiZEnabled = true;
        temporalCfg_.historyBlendWeight = 0.90f;
        bounceCfg_.maxBounces = 1;
        bounceCfg_.bounceResolutionScale = 1;
        radianceCacheCfg_.maxRaysPerProbe = 64;
        radianceCacheCfg_.probesPerAxis = 16;
        reflectionCfg_.maxRoughnessSamples = 16;
        break;

    case LumenQuality::Epic:
        screenTraceCfg_.maxSteps = 64;
        screenTraceCfg_.hiZEnabled = true;
        temporalCfg_.historyBlendWeight = 0.92f;
        bounceCfg_.maxBounces = 2;
        bounceCfg_.bounceResolutionScale = 1;
        radianceCacheCfg_.maxRaysPerProbe = 128;
        radianceCacheCfg_.probesPerAxis = 24;
        reflectionCfg_.maxRoughnessSamples = 32;
        break;
    }

    // Reallocate radiance cache if probe count changed
    u32 totalProbes = radianceCacheCfg_.probesPerAxis * radianceCacheCfg_.probesPerAxis *
                      radianceCacheCfg_.probesPerAxis;
    if (radianceProbes_.size() != totalProbes) {
        radianceProbes_.resize(totalProbes);
        for (auto& probe : radianceProbes_) {
            probe.needsUpdate = true;
            probe.weight = 0.0f;
            probe.historyWeight = 0.0f;
            for (u32 f = 0; f < 6; f++) {
                probe.radiance[f] = Vec3(0);
                probe.historyRadiance[f] = Vec3(0);
            }
        }
    }
}

void LumenSystem::setQuality(LumenQuality quality) {
    if (quality_ != quality) {
        quality_ = quality;
        applyQualityPreset();
    }
}

// ============================================================================
// Frame Lifecycle
// ============================================================================
void LumenSystem::beginFrame(const Camera& camera, f32 deltaTime) {
    deltaTime_ = deltaTime;
    time_ += deltaTime;
    frameIndex_++;

    // Cache camera data
    cameraPosition_ = camera.position();
    cameraDirection_ = camera.forward();
    viewMatrix_ = camera.view();
    projMatrix_ = camera.proj();
    viewProjMatrix_ = camera.viewProj();

    // Reset stats
    stats_.cardsRendered = 0;
    stats_.probesUpdated = 0;
    stats_.screenTraces = 0;
    stats_.sdfTraces = 0;
    stats_.bouncePasses = 0;
    stats_.giBuildTimeMs = 0.0f;
    stats_.reflectionTimeMs = 0.0f;
    stats_.residentPages = 0;
    stats_.patchesCached = 0;
    stats_.pagesEvicted = 0;
    stats_.raysTraced = 0;
    stats_.cacheUpdateMs = 0.0f;
}

void LumenSystem::render() {
    // 1. Build surface cache cards
    buildSurfaceCards();
    renderSurfaceCacheCards();

    // 2. Build global distance field
    buildGlobalDistanceField();

    // 3. Update radiance cache
    updateRadianceCache();

    // 4. Perform screen tracing for on-screen GI
    performScreenTracing();

    // 5. Perform SDF tracing for off-screen GI
    performSDFTracing();

    // 6. Compute emissive contributions
    computeEmissiveContribution();

    // 7. Perform bounce lighting
    performBounceLighting();

    // 8. Temporal accumulation for denoising
    temporalAccumulation();

    // 9. Perform reflections
    if (reflectionCfg_.enabled) {
        performReflections();
    }
}

void LumenSystem::endFrame() {
    // Swap history and current buffers
    historyGI_.swap(currentGI_);
    prevViewProjMatrix_ = viewProjMatrix_;
}

void LumenSystem::applyGI() {
    // The GI result is now in currentGI_ and can be applied to the scene
    giFactor_ = (quality_ == LumenQuality::Low) ? 0.75f :
                (quality_ == LumenQuality::Medium) ? 0.85f : 1.0f;
    reflectionFactor_ = reflectionCfg_.enabled ? 1.0f : 0.0f;
}

// ============================================================================
// Surface Cache - Build cards from visible surfaces
// ============================================================================
void LumenSystem::buildSurfaceCards() {
    generateScreenCards();
    updateEmissiveCards();
}

void LumenSystem::generateScreenCards() {
    // Generate screen-space cards for all valid surface entries
    activeCardCount_ = 0;

    for (u32 i = 0; i < surfaceCards_.size(); i++) {
        SurfaceCard& card = surfaceCards_[i];
        if (!card.isValid) continue;

        // Compute screen-space size
        f32 screenSize = computeCardScreenSize(card.position, card.worldRadius, viewProjMatrix_);

        // Skip cards too small to matter
        if (screenSize < 1.0f) continue;

        // Check if card faces the camera
        Vec3 toCamera = cameraPosition_ - card.position;
        f32 cameraDot = toCamera.normalized().dot(card.normal);

        if (cameraDot < surfaceCacheCfg_.normalThreshold) continue;

        // Update card UV bounds based on camera view
        f32 halfSize = card.worldRadius;
        Vec3 localRight = card.tangent * halfSize;
        Vec3 localUp = card.bitangent * halfSize;

        // Project corners to screen space
        Vec3 corners[4] = {
            card.position - localRight - localUp,
            card.position + localRight - localUp,
            card.position + localRight + localUp,
            card.position - localRight + localUp
        };

        f32 minU = 1e30f, maxU = -1e30f;
        f32 minV = 1e30f, maxV = -1e30f;

        for (u32 c = 0; c < 4; c++) {
            Vec4 clip = viewProjMatrix_ * Vec4(corners[c], 1.0f);
            if (clip.w <= 0.0f) continue;
            f32 ndcX = clip.x / clip.w;
            f32 ndcY = clip.y / clip.w;
            f32 u = ndcX * 0.5f + 0.5f;
            f32 v = ndcY * 0.5f + 0.5f;
            minU = Mathf::min(minU, u);
            maxU = Mathf::max(maxU, u);
            minV = Mathf::min(minV, v);
            maxV = Mathf::max(maxV, v);
        }

        card.uvMin = Vec2(Mathf::clamp(minU, 0.0f, 1.0f), Mathf::clamp(minV, 0.0f, 1.0f));
        card.uvMax = Vec2(Mathf::clamp(maxU, 0.0f, 1.0f), Mathf::clamp(maxV, 0.0f, 1.0f));

        activeCardCount_++;
        stats_.cardsRendered++;
    }
}

void LumenSystem::renderSurfaceCacheCards() {
    // Render surface cache cards to low-res buffer
    // This would normally render each card to a small texture atlas
    // In our CPU simulation, we update the albedo/emissive data
    for (u32 i = 0; i < surfaceCards_.size(); i++) {
        SurfaceCard& card = surfaceCards_[i];
        if (!card.isValid) continue;

        // Simulate card rendering by updating stored colors
        // In GPU path, this would render to a card atlas texture
    }
}

void LumenSystem::updateEmissiveCards() {
    emissiveCardCount_ = 0;
    for (u32 i = 0; i < surfaceCards_.size(); i++) {
        SurfaceCard& card = surfaceCards_[i];
        if (!card.isValid || !card.isEmissive) continue;
        if (card.emissiveIntensity < emissiveCfg_.minEmissiveIntensity) continue;

        emissiveCardCount_++;
    }
}

f32 LumenSystem::computeCardScreenSize(const Vec3& center, f32 radius, const Mat4& viewProj) const {
    // Project center to clip space
    Vec4 clip = viewProj * Vec4(center, 1.0f);
    if (clip.w <= 0.0f) return 0.0f;

    // Project offset point to get scale
    Vec3 offsetCenter = center + Vec3(radius, 0, 0);
    Vec4 clipOffset = viewProj * Vec4(offsetCenter, 1.0f);
    if (clipOffset.w <= 0.0f) return 0.0f;

    f32 ndcX0 = clip.x / clip.w;
    f32 ndcX1 = clipOffset.x / clipOffset.w;

    f32 screenRadius = std::abs(ndcX1 - ndcX0) * 0.5f * (f32)screenWidth_;
    return screenRadius;
}

// ============================================================================
// Screen Tracing - HiZ acceleration
// ============================================================================
void LumenSystem::performScreenTracing() {
    for (u32 y = 0; y < screenHeight_; y++) {
        for (u32 x = 0; x < screenWidth_; x++) {
            u32 idx = y * screenWidth_ + x;

            Vec3 worldNormal = normalBuffer_[idx];
            if (worldNormal.lengthSquared() < 0.01f) continue;

            // Compute indirect direction (cosine-weighted hemisphere)
            f32 u1 = Mathf::saturate((f32)(x + frameIndex_ * 17) / (f32)screenWidth_);
            f32 u2 = Mathf::saturate((f32)(y + frameIndex_ * 31) / (f32)screenHeight_);
            Vec3 worldPos = Vec3((f32)x, (f32)y, depthBuffer_[idx]);

            // Cosine-weighted hemisphere sampling
            f32 phi = u1 * Mathf::TWO_PI;
            f32 cosTheta = std::sqrt(1.0f - u2);
            f32 sinTheta = std::sqrt(u2);

            Vec3 tangent = worldNormal.cross(Vec3(0, 1, 0));
            if (tangent.lengthSquared() < 0.001f) tangent = Vec3(1, 0, 0);
            tangent = tangent.normalized();
            Vec3 bitangent = worldNormal.cross(tangent);

            Vec3 rayDir = tangent * (std::cos(phi) * sinTheta) +
                         bitangent * (std::sin(phi) * sinTheta) +
                         worldNormal * cosTheta;
            rayDir = rayDir.normalized();

            // Trace ray
            Vec3 hitPos;
            f32 hitT;
            traceScreenSpaceRay(worldPos, rayDir, hitPos, hitT);

            stats_.screenTraces++;
        }
    }
}

void LumenSystem::traceScreenSpaceRay(const Vec3& origin, const Vec3& dir, Vec3& hitPos, f32& hitT) {
    hitT = 0.0f;
    hitPos = origin;

    if (screenTraceCfg_.hiZEnabled) {
        // HiZ accelerated tracing
        hitPos = hiZTrace(origin, dir, screenTraceCfg_.maxDistance);
        hitT = (hitPos - origin).length();
    } else {
        // Simple linear stepping
        f32 stepSize = screenTraceCfg_.stepSize;
        Vec3 step = dir * stepSize;

        for (u32 i = 0; i < screenTraceCfg_.maxSteps; i++) {
            Vec3 samplePos = origin + dir * hitT;

            // Project to screen
            Vec4 clip = viewProjMatrix_ * Vec4(samplePos, 1.0f);
            if (clip.w <= 0.0f) break;

            f32 ndcX = clip.x / clip.w;
            f32 ndcY = clip.y / clip.w;
            f32 ndcZ = clip.z / clip.w;

            i32 px = (i32)((ndcX * 0.5f + 0.5f) * (f32)screenWidth_);
            i32 py = (i32)((ndcY * 0.5f + 0.5f) * (f32)screenHeight_);

            if (px < 0 || px >= (i32)screenWidth_ || py < 0 || py >= (i32)screenHeight_) break;

            f32 sceneDepth = depthBuffer_[py * screenWidth_ + px];

            if (ndcZ > sceneDepth + screenTraceCfg_.thickness) {
                hitPos = samplePos;
                return;
            }

            hitT += stepSize;
        }
    }
}

Vec3 LumenSystem::hiZTrace(const Vec3& origin, const Vec3& dir, f32 maxDist) {
    // Hierarchical Z-buffer ray marching
    f32 t = 0.01f;
    Vec3 currentPos = origin;
    i32 currentMip = 0;

    for (u32 step = 0; step < screenTraceCfg_.maxSteps; step++) {
        Vec4 clip = viewProjMatrix_ * Vec4(currentPos, 1.0f);
        if (clip.w <= 0.0f) break;

        f32 ndcX = clip.x / clip.w;
        f32 ndcY = clip.y / clip.w;
        f32 ndcZ = clip.z / clip.w;

        i32 px = (i32)((ndcX * 0.5f + 0.5f) * (f32)screenWidth_);
        i32 py = (i32)((ndcY * 0.5f + 0.5f) * (f32)screenHeight_);

        if (px < 0 || px >= (i32)screenWidth_ || py < 0 || py >= (i32)screenHeight_) break;

        f32 sceneDepth;
        if (sampleHiZ(currentMip, px >> currentMip, py >> currentMip, sceneDepth)) {
            if (ndcZ > sceneDepth && ndcZ < sceneDepth + screenTraceCfg_.thickness) {
                // Hit found
                return currentPos;
            }

            // Advance by the difference between ray depth and scene depth
            f32 depthDiff = sceneDepth - ndcZ;
            if (depthDiff > 0) {
                // Step forward
                f32 stepLen = Mathf::max(depthDiff * 5.0f, 0.01f);
                t += stepLen;
                currentMip = Mathf::max(currentMip - 1, 0);
            } else {
                // Behind geometry, go to higher mip for faster traversal
                currentMip = Mathf::min(currentMip + 1, (i32)screenTraceCfg_.hiZMaxMip);
            }
        } else {
            currentMip = Mathf::max(currentMip - 1, 0);
        }

        currentPos = origin + dir * t;
        if (t > maxDist) break;
    }

    return currentPos;
}

bool LumenSystem::sampleHiZ(i32 mip, i32 x, i32 y, f32& depth) {
    u32 mipWidth = screenWidth_ >> mip;
    u32 mipHeight = screenHeight_ >> mip;

    if (x < 0 || x >= (i32)mipWidth || y < 0 || y >= (i32)mipHeight) return false;

    // In a real implementation, this would sample from a HiZ mip chain texture
    // For CPU simulation, we sample from the depth buffer and downsample
    i32 srcX = x << mip;
    i32 srcY = y << mip;
    if (srcX >= (i32)screenWidth_ || srcY >= (i32)screenHeight_) return false;

    depth = depthBuffer_[srcY * screenWidth_ + srcX];

    // Sample max depth in the mip region
    for (i32 dy = 0; dy < (1 << mip); dy++) {
        for (i32 dx = 0; dx < (1 << mip); dx++) {
            i32 sx = srcX + dx;
            i32 sy = srcY + dy;
            if (sx < (i32)screenWidth_ && sy < (i32)screenHeight_) {
                depth = Mathf::max(depth, depthBuffer_[sy * screenWidth_ + sx]);
            }
        }
    }

    return true;
}

// ============================================================================
// SDF Tracing
// ============================================================================
void LumenSystem::performSDFTracing() {
    for (u32 i = 0; i < surfaceCards_.size(); i++) {
        SurfaceCard& card = surfaceCards_[i];
        if (!card.isValid) continue;

        // Cast rays from card positions through SDF
        Vec3 rayOrigin = card.position + card.normal * 0.1f;
        Vec3 rayDir = -card.normal; // cast inward for GI

        Vec3 hitPos, hitNormal;
        if (traceSDFRay(rayOrigin, rayDir, 1000.0f, hitPos, hitNormal)) {
            stats_.sdfTraces++;
        }
    }
}

f32 LumenSystem::sampleSDFGlobal(const Vec3& worldPos) {
    // Sample the global distance field clipmap
    if (clipmapLevels_.empty()) return 1e30f;

    // Find appropriate clipmap level
    for (i32 level = (i32)clipmapLevels_.size() - 1; level >= 0; level--) {
        const ClipmapLevel& clipmap = clipmapLevels_[level];
        Vec3 localPos = (worldPos - clipmap.center) / clipmap.spacing;

        // Check if within clipmap bounds
        f32 halfExtent = (f32)globalDFCfg_.clipmapResolution * 0.5f;
        if (std::abs(localPos.x) <= halfExtent &&
            std::abs(localPos.y) <= halfExtent &&
            std::abs(localPos.z) <= halfExtent) {
            return sampleClipmap(level, localPos);
        }
    }

    // Fallback: use coarsest level
    if (!clipmapLevels_.empty()) {
        Vec3 localPos = (worldPos - clipmapLevels_[0].center) / clipmapLevels_[0].spacing;
        return sampleClipmap(0, localPos);
    }

    return 1e30f;
}

f32 LumenSystem::sampleSDFMesh(const Vec3& worldPos, u32 meshIndex) {
    if (meshIndex >= sdfMeshes_.size()) return 1e30f;

    const SDFMeshData& sdf = sdfMeshes_[meshIndex];
    if (!sdf.valid) return 1e30f;

    // Convert world position to SDF local space
    Vec3 localPos = (worldPos - sdf.gridOrigin);
    localPos.x /= sdf.gridSpacing.x;
    localPos.y /= sdf.gridSpacing.y;
    localPos.z /= sdf.gridSpacing.z;

    // Trilinear interpolation
    i32 ix = (i32)std::floor(localPos.x);
    i32 iy = (i32)std::floor(localPos.y);
    i32 iz = (i32)std::floor(localPos.z);

    if (ix < 0 || ix >= sdf.gridSize.x - 1 ||
        iy < 0 || iy >= sdf.gridSize.y - 1 ||
        iz < 0 || iz >= sdf.gridSize.z - 1) {
        return 1e30f;
    }

    f32 fx = localPos.x - (f32)ix;
    f32 fy = localPos.y - (f32)iy;
    f32 fz = localPos.z - (f32)iz;

    auto sampleAt = [&](i32 x, i32 y, i32 z) -> f32 {
        u32 idx = (u32)(z * sdf.gridSize.y * sdf.gridSize.x + y * sdf.gridSize.x + x);
        if (idx < sdf.sdfSamples.size()) {
            return sdf.sdfSamples[idx].x; // distance stored in x
        }
        return 1e30f;
    };

    f32 d000 = sampleAt(ix, iy, iz);
    f32 d100 = sampleAt(ix + 1, iy, iz);
    f32 d010 = sampleAt(ix, iy + 1, iz);
    f32 d110 = sampleAt(ix + 1, iy + 1, iz);
    f32 d001 = sampleAt(ix, iy, iz + 1);
    f32 d101 = sampleAt(ix + 1, iy, iz + 1);
    f32 d011 = sampleAt(ix, iy + 1, iz + 1);
    f32 d111 = sampleAt(ix + 1, iy + 1, iz + 1);

    // Trilinear interpolation
    f32 d00 = Mathf::lerp(d000, d100, fx);
    f32 d10 = Mathf::lerp(d010, d110, fx);
    f32 d01 = Mathf::lerp(d001, d101, fx);
    f32 d11 = Mathf::lerp(d011, d111, fx);

    f32 d0 = Mathf::lerp(d00, d10, fy);
    f32 d1 = Mathf::lerp(d01, d11, fy);

    return Mathf::lerp(d0, d1, fz) * sdf.gridSpacing.x;
}

Vec3 LumenSystem::estimateSDFNormal(const Vec3& worldPos) {
    // Central differences for SDF gradient
    f32 eps = 0.1f;
    f32 dX = sampleSDFGlobal(worldPos + Vec3(eps, 0, 0)) -
             sampleSDFGlobal(worldPos - Vec3(eps, 0, 0));
    f32 dY = sampleSDFGlobal(worldPos + Vec3(0, eps, 0)) -
             sampleSDFGlobal(worldPos - Vec3(0, eps, 0));
    f32 dZ = sampleSDFGlobal(worldPos + Vec3(0, 0, eps)) -
             sampleSDFGlobal(worldPos - Vec3(0, 0, eps));

    Vec3 grad(dX, dY, dZ);
    f32 len = grad.length();
    return len > 0.0001f ? grad / len : Vec3(0, 1, 0);
}

bool LumenSystem::traceSDFRay(const Vec3& origin, const Vec3& dir, f32 maxDist,
                               Vec3& hitPos, Vec3& hitNormal) {
    f32 t = 0.0f;
    const f32 bias = globalDFCfg_.meshSDFMargin;
    u32 maxSteps = 128;

    for (u32 step = 0; step < maxSteps; step++) {
        Vec3 samplePos = origin + dir * t;
        f32 dist = sampleSDFGlobal(samplePos);

        if (dist < bias) {
            // Hit found
            hitPos = samplePos;
            hitNormal = estimateSDFNormal(samplePos);
            return true;
        }

        // Advance by distance (sphere marching)
        t += dist;

        if (t > maxDist) break;
    }

    return false;
}

// ============================================================================
// Global Distance Field
// ============================================================================
void LumenSystem::buildGlobalDistanceField() {
    mergeSDFClipmap();
}

void LumenSystem::mergeSDFClipmap() {
    // Build clipmap levels centered on camera
    f32 baseSpacing = globalDFCfg_.clipmapBaseResolution;

    for (u32 level = 0; level < globalDFCfg_.clipmapLevels; level++) {
        ClipmapLevel& clipmap = clipmapLevels_[level];
        clipmap.center = cameraPosition_;
        clipmap.spacing = baseSpacing * std::pow(globalDFCfg_.clipmapSpacingFactor, (f32)level);
        clipmap.resolution = globalDFCfg_.clipmapResolution;
        clipmap.dirty = true;

        // Merge SDF meshes into this clipmap level
        for (u32 meshIdx = 0; meshIdx < sdfMeshes_.size(); meshIdx++) {
            const SDFMeshData& sdf = sdfMeshes_[meshIdx];
            if (!sdf.valid) continue;

            // Check if mesh overlaps this clipmap level
            Vec3 localMin = (sdf.boundsMin - clipmap.center) / clipmap.spacing;
            Vec3 localMax = (sdf.boundsMax - clipmap.center) / clipmap.spacing;

            f32 halfRes = (f32)clipmap.resolution * 0.5f;
            if (localMax.x < -halfRes || localMin.x > halfRes ||
                localMax.y < -halfRes || localMin.y > halfRes ||
                localMax.z < -halfRes || localMin.z > halfRes) {
                continue;
            }

            // Rasterize mesh SDF into clipmap
            for (i32 z = 0; z < clipmap.resolution; z++) {
                for (i32 y = 0; y < clipmap.resolution; y++) {
                    for (i32 x = 0; x < clipmap.resolution; x++) {
                        Vec3 localPos(
                            ((f32)x - halfRes) * clipmap.spacing + clipmap.center.x,
                            ((f32)y - halfRes) * clipmap.spacing + clipmap.center.y,
                            ((f32)z - halfRes) * clipmap.spacing + clipmap.center.z
                        );

                        f32 meshDist = sampleSDFMesh(localPos, meshIdx);
                        u32 dataIndex = level * clipmap.resolution * clipmap.resolution * clipmap.resolution +
                                       z * clipmap.resolution * clipmap.resolution +
                                       y * clipmap.resolution + x;

                        if (dataIndex < clipmapData_.size()) {
                            // Union: take minimum distance
                            f32 existing = clipmapData_[dataIndex];
                            clipmapData_[dataIndex] = Mathf::min(existing, meshDist);
                        }
                    }
                }
            }
        }
    }
}

f32 LumenSystem::sampleClipmap(i32 level, const Vec3& localPos) {
    if (level < 0 || level >= (i32)clipmapLevels_.size()) return 1e30f;

    const ClipmapLevel& clipmap = clipmapLevels_[level];
    f32 halfRes = (f32)clipmap.resolution * 0.5f;

    // Check bounds
    if (localPos.x < -halfRes || localPos.x >= halfRes ||
        localPos.y < -halfRes || localPos.y >= halfRes ||
        localPos.z < -halfRes || localPos.z >= halfRes) {
        return 1e30f;
    }

    // Trilinear sample
    f32 fx = localPos.x + halfRes;
    f32 fy = localPos.y + halfRes;
    f32 fz = localPos.z + halfRes;

    i32 ix = (i32)std::floor(fx);
    i32 iy = (i32)std::floor(fy);
    i32 iz = (i32)std::floor(fz);

    ix = Mathf::clamp(ix, 0, clipmap.resolution - 2);
    iy = Mathf::clamp(iy, 0, clipmap.resolution - 2);
    iz = Mathf::clamp(iz, 0, clipmap.resolution - 2);

    f32 tX = fx - (f32)ix;
    f32 tY = fy - (f32)iy;
    f32 tZ = fz - (f32)iz;

    auto sampleVoxel = [&](i32 x, i32 y, i32 z) -> f32 {
        u32 idx = (u32)(z * clipmap.resolution * clipmap.resolution +
                        y * clipmap.resolution + x);
        if (idx < clipmapData_.size()) {
            return clipmapData_[level * clipmap.resolution * clipmap.resolution * clipmap.resolution + idx];
        }
        return 1e30f;
    };

    f32 d000 = sampleVoxel(ix, iy, iz);
    f32 d100 = sampleVoxel(ix + 1, iy, iz);
    f32 d010 = sampleVoxel(ix, iy + 1, iz);
    f32 d110 = sampleVoxel(ix + 1, iy + 1, iz);
    f32 d001 = sampleVoxel(ix, iy, iz + 1);
    f32 d101 = sampleVoxel(ix + 1, iy, iz + 1);
    f32 d011 = sampleVoxel(ix, iy + 1, iz + 1);
    f32 d111 = sampleVoxel(ix + 1, iy + 1, iz + 1);

    f32 d00 = Mathf::lerp(d000, d100, tX);
    f32 d10 = Mathf::lerp(d010, d110, tX);
    f32 d01 = Mathf::lerp(d001, d101, tX);
    f32 d11 = Mathf::lerp(d011, d111, tX);

    f32 d0 = Mathf::lerp(d00, d10, tY);
    f32 d1 = Mathf::lerp(d01, d11, tY);

    return Mathf::lerp(d0, d1, tZ);
}

// ============================================================================
// Radiance Cache
// ============================================================================
void LumenSystem::updateRadianceCache() {
    // Update probes that need refreshing
    u32 probesUpdatedThisFrame = 0;

    for (u32 i = 0; i < radianceProbes_.size(); i++) {
        RadianceProbe& probe = radianceProbes_[i];
        if (!probe.needsUpdate) continue;

        // Check if probe is within view range
        f32 distToCamera = (probe.position - cameraPosition_).length();
        if (distToCamera > radianceCacheCfg_.probeSpacing * radianceCacheCfg_.probesPerAxis) {
            continue;
        }

        updateProbe(i);
        probe.needsUpdate = false;
        probe.frameLastUpdated = frameIndex_;
        probesUpdatedThisFrame++;

        if (probesUpdatedThisFrame >= radianceCacheCfg_.updateProbesPerFrame) break;
    }

    // Mark nearby probes for update
    for (u32 i = 0; i < radianceProbes_.size(); i++) {
        RadianceProbe& probe = radianceProbes_[i];
        f32 dist = (probe.position - cameraPosition_).length();
        if (dist < radianceCacheCfg_.probeSpacing * 2.0f) {
            probe.needsUpdate = true;
        }
    }

    // Inject direct and indirect light
    injectDirectLight();
    injectIndirectLight();

    // Temporal filtering
    temporalFilterRadianceCache();

    stats_.probesUpdated = probesUpdatedThisFrame;
}

void LumenSystem::updateProbe(u32 probeIndex) {
    RadianceProbe& probe = radianceProbes_[probeIndex];

    // Cast rays in 6 directions (simplified cube map)
    Vec3 directions[6] = {
        Vec3(1, 0, 0),   Vec3(-1, 0, 0),
        Vec3(0, 1, 0),   Vec3(0, -1, 0),
        Vec3(0, 0, 1),   Vec3(0, 0, -1)
    };

    for (u32 face = 0; face < 6; face++) {
        Vec3 radiance = Vec3(0);
        Vec3 dir = directions[face];

        // Cast multiple rays per face
        u32 raysPerFace = radianceCacheCfg_.maxRaysPerProbe / 6;
        for (u32 r = 0; r < raysPerFace; r++) {
            // Jittered direction
            f32 u1 = (f32)((probeIndex * 7 + r * 13 + frameIndex_ * 3) % 1000) / 1000.0f;
            f32 u2 = (f32)((probeIndex * 11 + r * 17 + frameIndex_ * 5) % 1000) / 1000.0f;

            f32 phi = u1 * Mathf::TWO_PI;
            f32 cosTheta = 1.0f - 2.0f * u2;
            f32 sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);

            // Build orthonormal basis around face direction
            Vec3 tangent = dir.cross(Vec3(0, 1, 0));
            if (tangent.lengthSquared() < 0.001f) tangent = dir.cross(Vec3(1, 0, 0));
            tangent = tangent.normalized();
            Vec3 bitangent = dir.cross(tangent);

            Vec3 rayDir = tangent * (std::cos(phi) * sinTheta) +
                         bitangent * (std::sin(phi) * sinTheta) +
                         dir * cosTheta;
            rayDir = rayDir.normalized();

            // Trace ray through SDF
            Vec3 hitPos, hitNormal;
            f32 hitT = 0.0f;

            if (traceSDFRay(probe.position, rayDir,
                           radianceCacheCfg_.maxTraceDistance, hitPos, hitNormal)) {
                // Find surface card at hit position
                Vec3 hitRadiance = ambientColor_ * ambientIntensity_;

                // Check direct lighting at hit point
                f32 NdotL = Mathf::max(hitNormal.dot(-sunDirection_), 0.0f);
                hitRadiance = hitRadiance + sunColor_ * sunIntensity_ * NdotL;

                // Check emissive surfaces
                for (u32 c = 0; c < surfaceCards_.size(); c++) {
                    const SurfaceCard& card = surfaceCards_[c];
                    if (!card.isValid || !card.isEmissive) continue;

                    f32 dist = (hitPos - card.position).length();
                    if (dist < card.worldRadius) {
                        hitRadiance = hitRadiance + card.emissive * card.emissiveIntensity;
                    }
                }

                radiance = radiance + hitRadiance;
            } else {
                // Miss: sky color
                f32 skyFactor = Mathf::saturate(rayDir.y * 0.5f + 0.5f);
                Vec3 skyColor = Vec3(0.4f, 0.6f, 0.9f) * skyFactor +
                               Vec3(0.1f, 0.15f, 0.3f) * (1.0f - skyFactor);
                radiance = radiance + skyColor;
            }
        }

        probe.radiance[face] = radiance / (f32)raysPerFace;
    }
}

void LumenSystem::injectDirectLight() {
    for (u32 i = 0; i < radianceProbes_.size(); i++) {
        RadianceProbe& probe = radianceProbes_[i];

        // Add sun contribution to all faces
        for (u32 face = 0; face < 6; face++) {
            Vec3 faceNormal;
            switch (face) {
                case 0: faceNormal = Vec3(1, 0, 0); break;
                case 1: faceNormal = Vec3(-1, 0, 0); break;
                case 2: faceNormal = Vec3(0, 1, 0); break;
                case 3: faceNormal = Vec3(0, -1, 0); break;
                case 4: faceNormal = Vec3(0, 0, 1); break;
                default: faceNormal = Vec3(0, 0, -1); break;
            }

            f32 NdotL = Mathf::max(faceNormal.dot(-sunDirection_), 0.0f);
            probe.radiance[face] = probe.radiance[face] +
                                  sunColor_ * sunIntensity_ * NdotL * 0.5f;
        }
    }
}

void LumenSystem::injectIndirectLight() {
    // Gather radiance from neighboring probes
    for (u32 i = 0; i < radianceProbes_.size(); i++) {
        RadianceProbe& probe = radianceProbes_[i];
        Vec3 indirectLight = Vec3(0);
        f32 totalWeight = 0.0f;

        // Check 6 neighbors
        Vec3 offsets[6] = {
            Vec3(1, 0, 0), Vec3(-1, 0, 0),
            Vec3(0, 1, 0), Vec3(0, -1, 0),
            Vec3(0, 0, 1), Vec3(0, 0, -1)
        };

        for (u32 n = 0; n < 6; n++) {
            Vec3 neighborPos = probe.position + offsets[n] * radianceCacheCfg_.probeSpacing;
            i32 neighborIdx = findNearestProbe(neighborPos);

            if (neighborIdx >= 0 && neighborIdx != (i32)i) {
                const RadianceProbe& neighbor = radianceProbes_[neighborIdx];
                f32 dist = (probe.position - neighbor.position).length();
                f32 weight = 1.0f / (1.0f + dist * dist / (radianceCacheCfg_.probeRadius * radianceCacheCfg_.probeRadius));

                for (u32 f = 0; f < 6; f++) {
                    indirectLight = indirectLight + neighbor.radiance[f] * weight;
                }
                totalWeight += weight * 6.0f;
            }
        }

        if (totalWeight > 0.0f) {
            indirectLight = indirectLight / totalWeight;
        }

        // Blend with existing radiance
        for (u32 f = 0; f < 6; f++) {
            probe.radiance[f] = probe.radiance[f] * 0.7f + indirectLight * 0.3f;
        }
    }
}

Vec3 LumenSystem::computeProbeRadiance(const RadianceProbe& probe, const Vec3& direction) {
    // Find the two closest faces and interpolate
    f32 faceWeights[6];
    Vec3 faceNormals[6] = {
        Vec3(1, 0, 0),  Vec3(-1, 0, 0),
        Vec3(0, 1, 0),  Vec3(0, -1, 0),
        Vec3(0, 0, 1),  Vec3(0, 0, -1)
    };

    f32 totalWeight = 0.0f;
    for (u32 f = 0; f < 6; f++) {
        faceWeights[f] = Mathf::max(direction.dot(faceNormals[f]), 0.0f);
        totalWeight += faceWeights[f];
    }

    if (totalWeight < 0.001f) return probe.radiance[0];

    Vec3 result = Vec3(0);
    for (u32 f = 0; f < 6; f++) {
        result = result + probe.radiance[f] * (faceWeights[f] / totalWeight);
    }
    return result;
}

void LumenSystem::temporalFilterRadianceCache() {
    for (u32 i = 0; i < radianceProbes_.size(); i++) {
        RadianceProbe& probe = radianceProbes_[i];

        for (u32 f = 0; f < 6; f++) {
            // Exponential moving average
            probe.radiance[f] = probe.historyRadiance[f] * radianceCacheCfg_.probeHysteresis +
                               probe.radiance[f] * (1.0f - radianceCacheCfg_.probeHysteresis);
            probe.historyRadiance[f] = probe.radiance[f];
        }

        probe.weight = Mathf::lerp(probe.weight, probe.needsUpdate ? 1.0f : 0.0f, 0.1f);
    }
}

i32 LumenSystem::findNearestProbe(const Vec3& worldPos) const {
    i32 bestIdx = -1;
    f32 bestDist = 1e30f;

    for (u32 i = 0; i < radianceProbes_.size(); i++) {
        f32 dist = (radianceProbes_[i].position - worldPos).length();
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = (i32)i;
        }
    }

    return bestIdx;
}

// ============================================================================
// Temporal Accumulation
// ============================================================================
void LumenSystem::temporalAccumulation() {
    for (u32 i = 0; i < screenWidth_ * screenHeight_; i++) {
        Vec3 current = currentGI_[i];
        Vec3 history = historyGI_[i];

        if (temporalCfg_.neighborhoodClipping) {
            current = neighborhoodClip(current, history);
        }

        // Exponential moving average
        currentGI_[i] = history * temporalCfg_.historyBlendWeight +
                       current * (1.0f - temporalCfg_.historyBlendWeight);
    }
}

Vec3 LumenSystem::neighborhoodClip(const Vec3& current, const Vec3& history) {
    // Clip history to neighborhood of current (3x3 box)
    Vec3 minVal = current;
    Vec3 maxVal = current;

    // In a real implementation, we'd sample the 3x3 neighborhood
    // For CPU simulation, we use a simple clamp
    f32 clipRadius = temporalCfg_.clipRadius;

    Vec3 diff = history - current;
    f32 dist = diff.length();

    if (dist > clipRadius) {
        return current + diff * (clipRadius / dist);
    }

    return history;
}

bool LumenSystem::isDisoccluded(const Vec2& currentUV, const Vec2& historyUV,
                                 f32 currentDepth, f32 historyDepth) {
    f32 uvDist = (currentUV - historyUV).length();
    f32 depthRatio = currentDepth / (historyDepth + 0.001f);

    return uvDist > temporalCfg_.motionThreshold ||
           depthRatio > 2.0f || depthRatio < 0.5f;
}

// ============================================================================
// Reflections
// ============================================================================
void LumenSystem::performReflections() {
    for (u32 y = 0; y < screenHeight_; y++) {
        for (u32 x = 0; x < screenWidth_; x++) {
            u32 idx = y * screenWidth_ + x;

            Vec3 worldNormal = normalBuffer_[idx];
            if (worldNormal.lengthSquared() < 0.01f) continue;

            Vec3 albedo = albedoBuffer_[idx];
            f32 roughness = 0.5f; // Would come from material

            if (roughness > reflectionCfg_.maxRoughness) continue;

            Vec3 worldPos = Vec3((f32)x, (f32)y, depthBuffer_[idx]);
            Vec3 viewDir = (cameraPosition_ - worldPos).normalized();

            // GGX importance sampling for rough reflections
            u32 numSamples = (roughness < reflectionCfg_.mirrorThreshold) ?
                            1 : reflectionCfg_.maxRoughnessSamples;

            Vec3 totalReflection = Vec3(0);

            for (u32 s = 0; s < numSamples; s++) {
                f32 u1 = (f32)((x * 7 + y * 13 + s * 17 + frameIndex_ * 3) % 1000) / 1000.0f;
                f32 u2 = (f32)((x * 11 + y * 19 + s * 23 + frameIndex_ * 7) % 1000) / 1000.0f;
                Vec2 Xi(u1, u2);

                Vec3 halfVec = importanceSampleGGX(Xi, roughness, worldNormal);
                Vec3 reflectDir = halfVec * (2.0f * viewDir.dot(halfVec)) - viewDir;

                // Trace reflection ray
                Vec3 hitPos, hitNormal;
                traceReflectionRay(worldPos, reflectDir, hitPos, hitNormal);

                // Evaluate lighting at hit point
                Vec3 hitRadiance = Vec3(0.3f, 0.4f, 0.6f) * ambientIntensity_;

                // Direct light at hit point
                f32 NdotL = Mathf::max(hitNormal.dot(-sunDirection_), 0.0f);
                hitRadiance = hitRadiance + sunColor_ * sunIntensity_ * NdotL;

                // Fresnel
                f32 HdotV = Mathf::max(halfVec.dot(viewDir), 0.0f);
                Vec3 F0 = Vec3(0.04f); // non-metallic
                Vec3 F = fresnelSchlick(HdotV, F0);

                totalReflection = totalReflection + hitRadiance * F;
            }

            reflectionOutput_[idx] = totalReflection / (f32)numSamples;
        }
    }

    stats_.reflectionTimeMs = 0.0f; // Would be measured with GPU timer
}

void LumenSystem::traceReflectionRay(const Vec3& origin, const Vec3& dir,
                                      Vec3& hitPos, Vec3& hitNormal) {
    // First try screen-space tracing
    f32 hitT;
    traceScreenSpaceRay(origin, dir, hitPos, hitT);

    // If no screen-space hit, try SDF
    if ((hitPos - origin).length() < 0.1f) {
        traceSDFRay(origin, dir, 1000.0f, hitPos, hitNormal);
    } else {
        hitNormal = estimateSDFNormal(hitPos);
    }
}

Vec3 LumenSystem::importanceSampleGGX(const Vec2& Xi, f32 roughness, const Vec3& N) {
    f32 a = roughness * roughness;
    f32 a2 = a * a;
    f32 phi = Xi.x * Mathf::TWO_PI;
    f32 cosTheta = std::sqrt((1.0f - Xi.y) / (1.0f + (a2 - 1.0f) * Xi.y));
    f32 sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);

    // Build tangent-space basis
    Vec3 up = std::abs(N.z) < 0.999f ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
    Vec3 tangent = up.cross(N).normalized();
    Vec3 bitangent = N.cross(tangent);

    Vec3 sampleVec = tangent * (std::cos(phi) * sinTheta) +
                    bitangent * (std::sin(phi) * sinTheta) +
                    N * cosTheta;

    return sampleVec.normalized();
}

Vec3 LumenSystem::fresnelSchlick(f32 cosTheta, const Vec3& F0) {
    return F0 + (Vec3(1.0f) - F0) * std::pow(Mathf::clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

f32 LumenSystem::computeReflectionPDF(const Vec3& H, const Vec3& N, f32 roughness) {
    f32 NdotH = Mathf::max(N.dot(H), 0.0f);
    f32 a = roughness * roughness;
    f32 a2 = a * a;
    f32 denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (Mathf::PI * denom * denom + 0.0001f);
}

// ============================================================================
// Bounce Lighting
// ============================================================================
void LumenSystem::performBounceLighting() {
    for (u32 bounce = 0; bounce < bounceCfg_.maxBounces; bounce++) {
        if (bounceCfg_.screenSpaceBounce) {
            computeBounceFromSurfaceCache(bounce);
        }
        if (bounceCfg_.worldSpaceBounce) {
            computeBounceFromRadianceCache(bounce);
        }
        stats_.bouncePasses++;
    }
}

void LumenSystem::computeBounceFromSurfaceCache(u32 bounceIndex) {
    Vec3* inputBuffer = (bounceIndex == 0) ? currentGI_.data() : bounceBuffer_[bounceIndex - 1].data();
    Vec3* outputBuffer = bounceBuffer_[bounceIndex].data();

    for (u32 i = 0; i < screenWidth_ * screenHeight_; i++) {
        Vec3 incomingLight = inputBuffer[i];
        Vec3 albedo = albedoBuffer_[i];
        Vec3 normal = normalBuffer_[i];

        // Simple diffuse bounce: incoming * albedo / PI
        Vec3 bounce = incomingLight * albedo * (1.0f / Mathf::PI);

        // Attenuate by bounce energy
        bounce = bounce * bounceCfg_.bounceEnergyFalloff;

        outputBuffer[i] = bounce;
    }

    // Blend bounce into current GI
    for (u32 i = 0; i < screenWidth_ * screenHeight_; i++) {
        currentGI_[i] = currentGI_[i] + outputBuffer[i];
    }
}

void LumenSystem::computeBounceFromRadianceCache(u32 bounceIndex) {
    // Use radiance cache to fill in missing GI data
    for (u32 y = 0; y < screenHeight_; y++) {
        for (u32 x = 0; x < screenWidth_; x++) {
            u32 idx = y * screenWidth_ + x;

            Vec3 worldNormal = normalBuffer_[idx];
            if (worldNormal.lengthSquared() < 0.01f) continue;

            Vec3 worldPos = Vec3((f32)x, (f32)y, depthBuffer_[idx]);

            // Find nearest radiance probe
            i32 probeIdx = findNearestProbe(worldPos);
            if (probeIdx < 0) continue;

            // Gather radiance from probe
            Vec3 probeRadiance = computeProbeRadiance(radianceProbes_[probeIdx], worldNormal);

            // Scale by albedo
            Vec3 bounce = probeRadiance * albedoBuffer_[idx] * bounceCfg_.bounceEnergyFalloff;

            currentGI_[idx] = currentGI_[idx] + bounce;
        }
    }
}

// ============================================================================
// Emissive Surfaces
// ============================================================================
void LumenSystem::computeEmissiveContribution() {
    if (!emissiveCfg_.enabled) return;

    for (u32 i = 0; i < surfaceCards_.size(); i++) {
        const SurfaceCard& card = surfaceCards_[i];
        if (!card.isValid || !card.isEmissive) continue;
        if (card.emissiveIntensity < emissiveCfg_.minEmissiveIntensity) continue;

        // Inject emissive light into nearby pixels
        for (u32 y = 0; y < screenHeight_; y++) {
            for (u32 x = 0; x < screenWidth_; x++) {
                u32 idx = y * screenWidth_ + x;
                Vec3 worldPos = Vec3((f32)x, (f32)y, depthBuffer_[idx]);

                f32 dist = (worldPos - card.position).length();
                if (dist > card.worldRadius * 3.0f) continue;

                Vec3 hitNormal = normalBuffer_[idx];
                if (hitNormal.lengthSquared() < 0.01f) continue;

                Vec3 emission = evaluateEmissiveAreaLight(card, worldPos, hitNormal);
                currentGI_[idx] = currentGI_[idx] + emission;
            }
        }
    }
}

Vec3 LumenSystem::evaluateEmissiveAreaLight(const SurfaceCard& card, const Vec3& hitPos,
                                             const Vec3& hitNormal) {
    Vec3 toLight = card.position - hitPos;
    f32 dist = toLight.length();
    if (dist < 0.001f) return card.emissive * card.emissiveIntensity;

    Vec3 lightDir = toLight / dist;
    f32 NdotL = Mathf::max(hitNormal.dot(lightDir), 0.0f);

    // Area light solid angle approximation
    f32 lightArea = card.worldRadius * card.worldRadius * 4.0f;
    f32 solidAngle = lightArea / (dist * dist + 0.001f);
    solidAngle = Mathf::min(solidAngle, Mathf::PI);

    // Distance attenuation
    f32 attenuation = 1.0f / (dist * dist + 1.0f);

    // Sample light at random point on surface
    f32 sampleU = (f32)((u32)(hitPos.x * 100) % 1000) / 1000.0f;
    f32 sampleV = (f32)((u32)(hitPos.z * 100) % 1000) / 1000.0f;
    Vec3 pointOnLight = card.position +
                       card.tangent * (sampleU - 0.5f) * card.worldRadius * 2.0f +
                       card.bitangent * (sampleV - 0.5f) * card.worldRadius * 2.0f;

    Vec3 toSample = pointOnLight - hitPos;
    f32 sampleDist = toSample.length();
    Vec3 sampleDir = toSample / sampleDist;

    f32 NdotSample = Mathf::max(hitNormal.dot(sampleDir), 0.0f);
    f32 lightNdotSample = Mathf::max(card.normal.dot(-sampleDir), 0.0f);

    f32 pdf = computeAreaLightPDF(pointOnLight, hitPos, card.normal, lightArea);

    Vec3 emission = card.emissive * card.emissiveIntensity * emissiveCfg_.emissiveMultiplier;
    return emission * NdotL * solidAngle * attenuation * (1.0f / (pdf + 0.001f)) *
           emissiveCfg_.samplesPerEmissive;
}

f32 LumenSystem::computeAreaLightPDF(const Vec3& pointOnLight, const Vec3& hitPos,
                                      const Vec3& lightNormal, f32 lightArea) {
    Vec3 toHit = hitPos - pointOnLight;
    f32 dist = toHit.length();
    if (dist < 0.001f) return 0.0f;

    f32 cosTheta = Mathf::max(lightNormal.dot(toHit / dist), 0.0f);
    return dist * dist / (lightArea * cosTheta + 0.001f);
}

// ============================================================================
// Scene Submission
// ============================================================================
void LumenSystem::addMeshToSurfaceCache(u32 objectId, const Vec3& boundsMin, const Vec3& boundsMax,
                                         const Vec3& albedo, const Vec3& emissive, f32 emissiveIntensity) {
    for (auto& card : surfaceCards_) {
        if (!card.isValid) {
            card.objectId = objectId;
            card.position = (boundsMin + boundsMax) * 0.5f;
            card.boundsMin = boundsMin;
            card.boundsMax = boundsMax;
            card.worldRadius = (boundsMax - boundsMin).length() * 0.5f;
            card.normal = Vec3(0, 1, 0);
            card.tangent = Vec3(1, 0, 0);
            card.bitangent = Vec3(0, 0, 1);
            card.albedo = albedo;
            card.emissive = emissive;
            card.emissiveIntensity = emissiveIntensity;
            card.isEmissive = emissiveIntensity > emissiveCfg_.minEmissiveIntensity;
            card.isValid = true;
            return;
        }
    }
}

void LumenSystem::removeMeshFromSurfaceCache(u32 objectId) {
    for (auto& card : surfaceCards_) {
        if (card.isValid && card.objectId == objectId) {
            card.isValid = false;
        }
    }
}

void LumenSystem::updateMeshTransform(u32 objectId, const Mat4& transform) {
    for (auto& card : surfaceCards_) {
        if (card.isValid && card.objectId == objectId) {
            card.position = Vec3(
                transform.m[12], transform.m[13], transform.m[14]
            );
        }
    }
}

void LumenSystem::addSDFMesh(u32 objectId, const SDFMeshData& sdfData) {
    if (sdfMeshes_.size() <= objectId) {
        sdfMeshes_.resize(objectId + 1);
    }
    sdfMeshes_[objectId] = sdfData;
    sdfMeshes_[objectId].valid = true;
    sdfMeshCount_++;
}

void LumenSystem::removeSDFMesh(u32 objectId) {
    if (objectId < sdfMeshes_.size()) {
        sdfMeshes_[objectId].valid = false;
        sdfMeshCount_--;
    }
}

// ============================================================================
// Light Injection
// ============================================================================
void LumenSystem::setSunDirection(const Vec3& dir) {
    sunDirection_ = dir.normalized();
}

void LumenSystem::setSunColor(const Vec3& color, f32 intensity) {
    sunColor_ = color;
    sunIntensity_ = intensity;
}

void LumenSystem::setAmbientColor(const Vec3& color, f32 intensity) {
    ambientColor_ = color;
    ambientIntensity_ = intensity;
}

// ============================================================================
// Advanced Surface Cache: Hierarchical card updates
// ============================================================================
void LumenSystem::updateSurfaceCacheHierarchy() {
    // Update card LODs based on distance to camera
    for (u32 i = 0; i < surfaceCards_.size(); i++) {
        SurfaceCard& card = surfaceCards_[i];
        if (!card.isValid) continue;

        f32 dist = (card.position - cameraPosition_).length();
        f32 screenSize = computeCardScreenSize(card.position, card.worldRadius, viewProjMatrix_);

        // Determine card update priority
        if (screenSize > 64.0f) {
            // High priority: close to camera, update every frame
            card.emissiveIntensity = card.emissiveIntensity; // Keep current
        } else if (screenSize > 16.0f) {
            // Medium priority: update every 2 frames
            if (frameIndex_ % 2 != i % 2) continue;
        } else {
            // Low priority: update every 4 frames
            if (frameIndex_ % 4 != i % 4) continue;
        }
    }
}

// ============================================================================
// Advanced SDF Tracing with temporal coherence
// ============================================================================
void LumenSystem::traceSDFsWithTemporalCoherence() {
    // Use previous frame's results to accelerate current frame tracing
    for (u32 i = 0; i < surfaceCards_.size(); i++) {
        SurfaceCard& card = surfaceCards_[i];
        if (!card.isValid) continue;

        // Check if card moved significantly
        f32 movementThreshold = 0.5f;
        Vec3 prevPosition = card.position; // Would store from previous frame

        // Adaptive step size based on distance
        f32 dist = (card.position - cameraPosition_).length();
        f32 adaptiveStepSize = Mathf::max(0.1f, dist * 0.01f);

        // Trace with adaptive step
        Vec3 rayOrigin = card.position + card.normal * 0.1f;
        Vec3 rayDir = -card.normal;

        Vec3 hitPos, hitNormal;
        f32 maxDist = Mathf::min(dist * 2.0f, 1000.0f);

        if (traceSDFRay(rayOrigin, rayDir, maxDist, hitPos, hitNormal)) {
            // Compute irradiance at hit point
            f32 NdotL = Mathf::max(hitNormal.dot(-sunDirection_), 0.0f);
            Vec3 irradiance = sunColor_ * sunIntensity_ * NdotL;

            // Add ambient
            irradiance = irradiance + ambientColor_ * ambientIntensity_;

            // Store for this card
            card.albedo = irradiance;
        }
    }
}

// ============================================================================
// Multi-bounce light transport
// ============================================================================
void LumenSystem::computeMultiBounceTransport() {
    if (bounceCfg_.maxBounces < 2) return;

    // Second bounce: use first bounce output as input
    for (u32 y = 0; y < screenHeight_; y++) {
        for (u32 x = 0; x < screenWidth_; x++) {
            u32 idx = y * screenWidth_ + x;

            Vec3 firstBounce = currentGI_[idx];
            Vec3 normal = normalBuffer_[idx];
            Vec3 albedo = albedoBuffer_[idx];

            if (normal.lengthSquared() < 0.01f) continue;

            // Sample surrounding pixels for second bounce
            Vec3 secondBounce = Vec3(0);
            f32 sampleWeight = 0.0f;

            i32 kernelSize = 3;
            for (i32 ky = -kernelSize; ky <= kernelSize; ky++) {
                for (i32 kx = -kernelSize; kx <= kernelSize; kx++) {
                    i32 sx = Mathf::max(0, Mathf::min(x + kx, (i32)screenWidth_ - 1));
                    i32 sy = Mathf::max(0, Mathf::min(y + ky, (i32)screenHeight_ - 1));
                    u32 sIdx = sy * screenWidth_ + sx;

                    Vec3 sampleNormal = normalBuffer_[sIdx];
                    f32 normalDot = normal.dot(sampleNormal);
                    if (normalDot < 0.5f) continue;

                    f32 dist = std::sqrt((f32)(kx * kx + ky * ky));
                    f32 weight = 1.0f / (1.0f + dist * dist);

                    secondBounce = secondBounce + bounceBuffer_[0][sIdx] * weight;
                    sampleWeight += weight;
                }
            }

            if (sampleWeight > 0) {
                secondBounce = secondBounce / sampleWeight;
                currentGI_[idx] = currentGI_[idx] + secondBounce * albedo * 0.3f;
            }
        }
    }
}

// ============================================================================
// Emissive light importance sampling
// ============================================================================
Vec3 LumenSystem::sampleEmissiveLights(const Vec3& hitPos, const Vec3& hitNormal, u32 sampleCount) {
    Vec3 totalEmission = Vec3(0);

    for (u32 s = 0; s < sampleCount; s++) {
        // Find nearest emissive card
        f32 bestDist = 1e30f;
        i32 bestCard = -1;

        for (u32 i = 0; i < surfaceCards_.size(); i++) {
            const SurfaceCard& card = surfaceCards_[i];
            if (!card.isValid || !card.isEmissive) continue;
            if (card.emissiveIntensity < emissiveCfg_.minEmissiveIntensity) continue;

            f32 dist = (hitPos - card.position).length();
            if (dist < bestDist && dist < card.worldRadius * 5.0f) {
                bestDist = dist;
                bestCard = (i32)i;
            }
        }

        if (bestCard >= 0) {
            const SurfaceCard& card = surfaceCards_[bestCard];

            // Sample random point on emissive surface
            f32 u1 = (f32)((s * 7919 + frameIndex_ * 104729) % 10000) / 10000.0f;
            f32 u2 = (f32)((s * 104729 + frameIndex_ * 7919) % 10000) / 10000.0f;

            Vec3 pointOnLight = card.position +
                               card.tangent * (u1 - 0.5f) * card.worldRadius * 2.0f +
                               card.bitangent * (u2 - 0.5f) * card.worldRadius * 2.0f;

            Vec3 toLight = pointOnLight - hitPos;
            f32 lightDist = toLight.length();
            if (lightDist < 0.001f) continue;

            Vec3 lightDir = toLight / lightDist;
            f32 NdotL = Mathf::max(hitNormal.dot(lightDir), 0.0f);

            // Light PDF
            f32 lightArea = card.worldRadius * card.worldRadius * 4.0f;
            f32 pdf = lightDist * lightDist / (lightArea * Mathf::max(card.normal.dot(-lightDir), 0.0f) + 0.001f);

            totalEmission = totalEmission + card.emissive * card.emissiveIntensity *
                           NdotL / (pdf + 0.001f);
        }
    }

    return totalEmission / (f32)sampleCount;
}

// ============================================================================
// Screen-space GI compositing
// ============================================================================
void LumenSystem::compositeScreenSpaceGI(Vec3* outputBuffer) {
    if (!outputBuffer) return;

    for (u32 y = 0; y < screenHeight_; y++) {
        for (u32 x = 0; x < screenWidth_; x++) {
            u32 idx = y * screenWidth_ + x;

            Vec3 giContribution = currentGI_[idx] * giFactor_;
            Vec3 reflectionContribution = reflectionOutput_[idx] * reflectionFactor_;

            // Combine GI and reflections
            Vec3 combined = giContribution + reflectionContribution;

            // Apply to output
            outputBuffer[idx] = outputBuffer[idx] + combined;
        }
    }
}

// ============================================================================
// Adaptive quality based on performance
// ============================================================================
void LumenSystem::adaptQuality(f32 frameTimeMs) {
    // Adapt quality based on frame time
    static f32 avgFrameTime = 16.67f; // Target 60fps
    avgFrameTime = avgFrameTime * 0.95f + frameTimeMs * 0.05f;

    if (avgFrameTime > 20.0f && quality_ != LumenQuality::Low) {
        // Performance is bad, reduce quality
        switch (quality_) {
        case LumenQuality::Epic: quality_ = LumenQuality::High; break;
        case LumenQuality::High: quality_ = LumenQuality::Medium; break;
        case LumenQuality::Medium: quality_ = LumenQuality::Low; break;
        default: break;
        }
        applyQualityPreset();
    } else if (avgFrameTime < 12.0f && quality_ != LumenQuality::Epic) {
        // Performance is good, increase quality
        switch (quality_) {
        case LumenQuality::Low: quality_ = LumenQuality::Medium; break;
        case LumenQuality::Medium: quality_ = LumenQuality::High; break;
        case LumenQuality::High: quality_ = LumenQuality::Epic; break;
        default: break;
        }
        applyQualityPreset();
    }
}

// ============================================================================
// Debug visualization
// ============================================================================
void LumenSystem::visualizeSurfaceCards(Vec3* debugOutput) {
    if (!debugOutput) return;

    for (u32 i = 0; i < surfaceCards_.size(); i++) {
        const SurfaceCard& card = surfaceCards_[i];
        if (!card.isValid) continue;

        // Project card center to screen
        Vec4 clip = viewProjMatrix_ * Vec4(card.position, 1.0f);
        if (clip.w <= 0.0f) continue;

        f32 ndcX = clip.x / clip.w;
        f32 ndcY = clip.y / clip.w;

        i32 px = (i32)((ndcX * 0.5f + 0.5f) * (f32)screenWidth_);
        i32 py = (i32)((1.0f - ndcY * 0.5f - 0.5f) * (f32)screenHeight_);

        // Draw card indicator
        i32 cardSize = (i32)(card.worldRadius * 10.0f / (clip.w + 1.0f));
        cardSize = Mathf::max(cardSize, 2);

        for (i32 dy = -cardSize; dy <= cardSize; dy++) {
            for (i32 dx = -cardSize; dx <= cardSize; dx++) {
                i32 sx = px + dx;
                i32 sy = py + dy;
                if (sx >= 0 && sx < (i32)screenWidth_ && sy >= 0 && sy < (i32)screenHeight_) {
                    u32 idx = sy * screenWidth_ + sx;
                    if (card.isEmissive) {
                        debugOutput[idx] = Vec3(1, 0, 0); // Red for emissive
                    } else {
                        debugOutput[idx] = Vec3(0, 1, 0); // Green for regular
                    }
                }
            }
        }
    }
}

void LumenSystem::visualizeRadianceCache(Vec3* debugOutput) {
    if (!debugOutput) return;

    for (u32 i = 0; i < radianceProbes_.size(); i++) {
        const RadianceProbe& probe = radianceProbes_[i];

        // Project probe to screen
        Vec4 clip = viewProjMatrix_ * Vec4(probe.position, 1.0f);
        if (clip.w <= 0.0f) continue;

        f32 ndcX = clip.x / clip.w;
        f32 ndcY = clip.y / clip.w;

        i32 px = (i32)((ndcX * 0.5f + 0.5f) * (f32)screenWidth_);
        i32 py = (i32)((1.0f - ndcY * 0.5f - 0.5f) * (f32)screenHeight_);

        // Draw probe indicator
        i32 probeSize = 4;
        Vec3 probeColor = probe.radiance[0]; // Use face 0 radiance

        for (i32 dy = -probeSize; dy <= probeSize; dy++) {
            for (i32 dx = -probeSize; dx <= probeSize; dx++) {
                i32 sx = px + dx;
                i32 sy = py + dy;
                if (sx >= 0 && sx < (i32)screenWidth_ && sy >= 0 && sy < (i32)screenHeight_) {
                    u32 idx = sy * screenWidth_ + sx;
                    debugOutput[idx] = probeColor;
                }
            }
        }
    }
}

// ============================================================================
// Surface Cache pages: residency, patches, radiance atlas
// ============================================================================
static u32 lumenPagesPerRow(const SurfaceCachePageConfig& cfg) {
    u32 pagesPerRow = cfg.atlasSize / cfg.pageSize;
    return pagesPerRow > 1u ? pagesPerRow : 1u;
}

u32 LumenSystem::requestPage(const Vec3& worldPos) {
    i32 cx = (i32)std::floor(worldPos.x / (f32)pageCacheCfg_.pageSize);
    i32 cy = (i32)std::floor(worldPos.y / (f32)pageCacheCfg_.pageSize);
    i32 cz = (i32)std::floor(worldPos.z / (f32)pageCacheCfg_.pageSize);
    u64 key = ((u64)(u32)(cx & 0x1FFFFF) << 42) |
              ((u64)(u32)(cy & 0x1FFFFF) << 21) |
              (u64)(u32)(cz & 0x1FFFFF);

    auto it = pageMap_.find(key);
    if (it != pageMap_.end()) {
        u32 idx = it.value();
        if (idx < pages_.size()) pages_[idx].requested = true;
        return idx;
    }

    u32 pageIndex;
    if (!freePageSlots_.empty()) {
        pageIndex = freePageSlots_.back();
        freePageSlots_.pop_back();
    } else if (pages_.size() < pageCacheCfg_.maxPages) {
        pageIndex = (u32)pages_.size();
        pages_.push_back(SurfaceCachePage());
    } else {
        return (u32)-1;
    }

    SurfaceCachePage& page = pages_[pageIndex];
    page.pageX = (u32)cx;
    page.pageY = (u32)cy;
    page.level = 0;
    u32 pagesPerRow = lumenPagesPerRow(pageCacheCfg_);
    page.x = (pageIndex % pagesPerRow) * pageCacheCfg_.pageSize;
    page.y = (pageIndex / pagesPerRow) * pageCacheCfg_.pageSize;
    page.requested = true;
    page.resident = false;
    page.patchCount = 0;
    pageMap_[key] = pageIndex;
    return pageIndex;
}

void LumenSystem::evictPage(u32 pageIndex) {
    if (pageIndex >= pages_.size()) return;
    SurfaceCachePage& page = pages_[pageIndex];
    u64 key = ((u64)(u64)(page.pageX & 0x1FFFFF) << 42) |
              ((u64)(u64)(page.pageY & 0x1FFFFF) << 21) |
              (u64)(page.level & 0x1FFFFF);
    pageMap_.erase(key);
    page.resident = false;
    page.requested = false;
    page.patchCount = 0;
    freePageSlots_.push_back(pageIndex);
    stats_.pagesEvicted++;
}

void LumenSystem::ensureResidentPages(u32 cameraProxyCount, const Vec3* cameraProxies, f32 radius) {
    if (cameraProxies == nullptr) return;
    for (u32 i = 0; i < cameraProxyCount; i++) {
        requestPage(cameraProxies[i]);
    }

    u32 residentCount = 0;
    for (const auto& page : pages_) {
        if (page.resident) residentCount++;
    }
    while (residentCount > pageCacheCfg_.maxPages) {
        u32 oldestIndex = (u32)-1;
        f32 oldestTime = 1e30f;
        for (u32 i = 0; i < pages_.size(); i++) {
            if (pages_[i].resident && pages_[i].lastUpdateTime < oldestTime) {
                oldestTime = pages_[i].lastUpdateTime;
                oldestIndex = i;
            }
        }
        if (oldestIndex == (u32)-1) break;
        evictPage(oldestIndex);
        residentCount--;
    }
}

u32 LumenSystem::addPatch(const Vec3& pos, const Vec3& normal) {
    u32 pageIndex = requestPage(pos);
    if (pageIndex == (u32)-1) return (u32)-1;

    CachedPatch patch;
    patch.position = pos;
    patch.normal = normal;
    patch.radiance = Vec3(0);
    patch.pageIndex = pageIndex;
    patch.dirty = true;
    patches_.push_back(patch);
    pages_[pageIndex].patchCount++;
    stats_.patchesCached++;
    return (u32)(patches_.size() - 1);
}

void LumenSystem::updatePageRadiance(u32 pageIndex, u32 rayCount, const Vec3* rayDirections,
                                     const Vec3* rayHits, const Vec3* rayRadiance) {
    if (pageIndex >= pages_.size() || rayCount == 0 || rayRadiance == nullptr) return;

    Vec3 avg = Vec3(0);
    for (u32 r = 0; r < rayCount; r++) avg = avg + rayRadiance[r];
    avg = avg / (f32)rayCount;

    for (auto& patch : patches_) {
        if (patch.pageIndex == pageIndex) {
            patch.radiance = avg;
            patch.dirty = false;
        }
    }

    SurfaceCachePage& page = pages_[pageIndex];
    page.resident = true;
    page.requested = false;

    u32 pagesPerRow = lumenPagesPerRow(pageCacheCfg_);
    u32 atlasX = (pageIndex % pagesPerRow) * pageCacheCfg_.pageSize;
    u32 atlasY = (pageIndex / pagesPerRow) * pageCacheCfg_.pageSize;
    for (u32 py = 0; py < pageCacheCfg_.pageSize; py++) {
        for (u32 px = 0; px < pageCacheCfg_.pageSize; px++) {
            u32 tx = atlasX + px;
            u32 ty = atlasY + py;
            if (tx >= pageCacheCfg_.atlasSize || ty >= pageCacheCfg_.atlasSize) continue;
            u32 idx = (ty * pageCacheCfg_.atlasSize + tx) * 3;
            radianceAtlas_[idx + 0] = avg.x;
            radianceAtlas_[idx + 1] = avg.y;
            radianceAtlas_[idx + 2] = avg.z;
        }
    }
}

Vector<u32> LumenSystem::dirtyPages() const {
    Vector<u32> result;
    for (const auto& patch : patches_) {
        if (!patch.dirty) continue;
        bool found = false;
        for (u32 i = 0; i < result.size(); i++) {
            if (result[i] == patch.pageIndex) { found = true; break; }
        }
        if (!found) result.push_back(patch.pageIndex);
    }
    return result;
}

Vec3 LumenSystem::sampleAtlas(const Vec3& worldPos, u32 pageIndex) const {
    if (pageIndex >= pages_.size()) return Vec3(0);
    const SurfaceCachePage& page = pages_[pageIndex];
    if (!page.resident || radianceAtlas_.empty()) return Vec3(0);

    f32 cellX = worldPos.x / (f32)pageCacheCfg_.pageSize;
    f32 cellZ = worldPos.z / (f32)pageCacheCfg_.pageSize;
    f32 localU = cellX - std::floor(cellX);
    f32 localV = cellZ - std::floor(cellZ);

    Vec3 sum = Vec3(0);
    f32 weightTotal = 0.0f;
    for (i32 dy = -1; dy <= 1; dy++) {
        for (i32 dx = -1; dx <= 1; dx++) {
            f32 nx = localU * (f32)(pageCacheCfg_.pageSize - 1) + (f32)dx;
            f32 ny = localV * (f32)(pageCacheCfg_.pageSize - 1) + (f32)dy;
            if (nx < 0.0f || ny < 0.0f) continue;
            i32 tx = (i32)page.x + (i32)nx;
            i32 ty = (i32)page.y + (i32)ny;
            if (tx < 0 || tx >= (i32)pageCacheCfg_.atlasSize) continue;
            if (ty < 0 || ty >= (i32)pageCacheCfg_.atlasSize) continue;
            u32 idx = ((u32)ty * pageCacheCfg_.atlasSize + (u32)tx) * 3;
            sum = sum + Vec3(radianceAtlas_[idx + 0], radianceAtlas_[idx + 1], radianceAtlas_[idx + 2]);
            weightTotal += 1.0f;
        }
    }
    return weightTotal > 0.0f ? sum / weightTotal : Vec3(0);
}

Vec3 LumenSystem::traceRadiance(const Vec3& origin, const Vec3& dir, f32 maxDist, const f32* sceneDepth) {
    Vec3 accumulated = Vec3(0);
    f32 step = (f32)pageCacheCfg_.pageSize * 0.5f;
    f32 t = step;
    while (t < maxDist) {
        Vec3 samplePos = origin + dir * t;
        u32 pageIndex = requestPage(samplePos);
        if (pageIndex != (u32)-1 && pages_[pageIndex].resident) {
            accumulated = accumulated + sampleAtlas(samplePos, pageIndex);
            break;
        }
        t += step;
    }
    stats_.raysTraced++;
    return accumulated;
}

void LumenSystem::updateSurfaceCache(f32 dt, u32 frameIndex, u32 maxRaysPerFrame) {
    auto tStart = std::chrono::high_resolution_clock::now();

    for (auto& page : pages_) {
        if (page.resident) page.lastUpdateTime += dt;
    }

    u32 processed = 0;
    for (auto& patch : patches_) {
        if (!patch.dirty || processed >= maxRaysPerFrame) continue;

        f32 NdotL = Mathf::max(patch.normal.dot(-sunDirection_), 0.0f);
        Vec3 radiance = sunColor_ * sunIntensity_ * NdotL + ambientColor_ * ambientIntensity_;
        Vec3 rays[1] = {radiance};
        updatePageRadiance(patch.pageIndex, 1, nullptr, nullptr, rays);
        processed++;
    }

    for (auto& patch : patches_) {
        patch.dirty = false;
    }

    stats_.residentPages = 0;
    for (const auto& page : pages_) {
        if (page.resident) stats_.residentPages++;
    }
    stats_.patchesCached = (u32)patches_.size();
    stats_.raysTraced += (u64)processed * (u64)pageCacheCfg_.raysPerPatch;

    auto tEnd = std::chrono::high_resolution_clock::now();
    stats_.cacheUpdateMs = std::chrono::duration<f32, std::milli>(tEnd - tStart).count();
}

void LumenSystem::resetSurfaceCache() {
    pages_.clear();
    patches_.clear();
    pageMap_.clear();
    freePageSlots_.clear();
    for (auto& texel : radianceAtlas_) texel = 0.0f;
}

u32 LumenSystem::getDirtyPageCount() const {
    Vector<u32> pages = dirtyPages();
    return (u32)pages.size();
}

} // namespace Frost
