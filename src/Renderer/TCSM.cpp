// ============================================================================
// FrostEngine TCSM — Temporal Cascade Shadow Maps
// ============================================================================
// Cascade cache + temporal reprojection support:
//   - Per-cascade light-space cache entries let the renderer skip shadow map
//     re-renders when the light origin/extent barely change between frames.
//   - Jittered light offsets hide texel-alias shimmer across frames; the
//     accumulated history converges to a clean, stable shadow.
//   - temporalBlend() exponentially blends a reprojected history sample with
//     the freshly rendered value.
// ============================================================================

#include "FrostEngine/Renderer/TCSM.h"
#include <cmath>

namespace Frost {

// ---- File-local helpers ----

static f32 halton(u32 index, u32 base) {
    f32 f = 1.0f;
    f32 r = 0.0f;
    u32 i = index;
    while (i > 0) {
        f /= (f32)base;
        r += f * (f32)(i % base);
        i /= base;
    }
    return r;
}

// ============================================================================
// Construction
// ============================================================================

const CascadeCacheEntry TCSMSystem::kEmptyCacheEntry = {
    0, Vec3(0.0f), 0.0f, false
};

bool TCSMSystem::init(u32 cascadeCount, u32 shadowMapSize) {
    if (cascades_[0].resolution != 0) {
        for (u32 i = 0; i < MAX_CASCADES; i++) {
            cascades_[i].destroy();
        }
    }

    cascadeCount_ = (cascadeCount > MAX_CASCADES) ? MAX_CASCADES : cascadeCount;
    if (cascadeCount_ == 0) cascadeCount_ = 1;

    cascadeCache_.clear();
    jitterOffsets_.clear();
    for (u32 i = 0; i < cascadeCount_; i++) {
        cascades_[i].allocate(shadowMapSize);

        CascadeCacheEntry entry;
        entry.frameLastRendered = 0;
        entry.cachedOrigin = Vec3(0.0f);
        entry.cachedExtent = 0.0f;
        entry.valid = false;
        cascadeCache_.pushBack(entry);

        jitterOffsets_.pushBack(Vec3(0.0f));
    }

    cacheHits_ = 0;
    cacheMisses_ = 0;
    renderedCascades_ = 0;
    frameCounter_ = 0;
    initialized_ = true;
    return true;
}

// ============================================================================
// Cascade cache
// ============================================================================

bool TCSMSystem::updateCascadeOrigin(u32 cascadeIndex, const Vec3& lightOrigin, f32 extent) {
    frameCounter_++;

    if (cascadeIndex >= cascadeCache_.size()) {
        cacheMisses_++;
        renderedCascades_++;
        return true;
    }

    CascadeCacheEntry& entry = cascadeCache_[cascadeIndex];

    if (entry.valid) {
        f32 extentDelta = Mathf::abs(extent - entry.cachedExtent);
        f32 originDelta = (lightOrigin - entry.cachedOrigin).length();
        f32 threshold = entry.cachedExtent * 0.05f + 1e-4f;

        // Light stayed (nearly) put: reproject last frame's shadow map.
        if (extentDelta <= threshold && originDelta <= threshold) {
            cacheHits_++;
            entry.frameLastRendered = frameCounter_;
            return false;
        }
    }

    // Cache miss: record the new origin and request a re-render.
    cacheMisses_++;
    renderedCascades_++;
    entry.cachedOrigin = lightOrigin;
    entry.cachedExtent = extent;
    entry.frameLastRendered = frameCounter_;
    entry.valid = true;
    return true;
}

const CascadeCacheEntry& TCSMSystem::getCascadeCacheEntry(u32 cascadeIndex) const {
    if (cascadeIndex < cascadeCache_.size()) {
        return cascadeCache_[cascadeIndex];
    }
    return kEmptyCacheEntry;
}

u32 TCSMSystem::getCachedCascades() const {
    u32 count = 0;
    for (usize i = 0; i < cascadeCache_.size(); i++) {
        if (cascadeCache_[i].valid) count++;
    }
    return count;
}

// ============================================================================
// Jitter
// ============================================================================

Vec3 TCSMSystem::applyJitter(u32 cascadeIndex, f32 pixelSize) {
    if (cascadeIndex >= jitterOffsets_.size()) {
        return Vec3(0.0f);
    }

    f32 scale = pixelSize * jitterScale_;
    if (scale <= 0.0f) {
        jitterOffsets_[cascadeIndex] = Vec3(0.0f);
        return Vec3(0.0f);
    }

    // Deterministic, temporally-varying Halton sample (differs per cascade).
    u32 seed = frameCounter_ + cascadeIndex * 7u + 1u;
    f32 u = halton(seed, 2u);
    f32 v = halton(seed, 3u);
    f32 w = halton(seed, 5u);

    Vec3 offset((u - 0.5f) * 2.0f, (v - 0.5f) * 2.0f, (w - 0.5f) * 2.0f);
    offset *= scale;

    jitterOffsets_[cascadeIndex] = offset;
    return offset;
}

// ============================================================================
// Temporal blending
// ============================================================================

f32 TCSMSystem::temporalBlend(f32 prev, f32 cur, f32 historyWeight) const {
    f32 w = Mathf::saturate(historyWeight * reprojectionFactor_);
    return Mathf::lerp(cur, prev, w);
}

// ============================================================================
// Cache statistics
// ============================================================================

u32 TCSMSystem::getCacheHits() const {
    return cacheHits_;
}

u32 TCSMSystem::getCacheMisses() const {
    return cacheMisses_;
}

u32 TCSMSystem::getRenderedCascades() const {
    return renderedCascades_;
}

f32 TCSMSystem::getCacheHitRatio() const {
    u32 total = cacheHits_ + cacheMisses_;
    return total > 0 ? (f32)cacheHits_ / (f32)total : 0.0f;
}

void TCSMSystem::resetStats() {
    cacheHits_ = 0;
    cacheMisses_ = 0;
    renderedCascades_ = 0;
}

} // namespace Frost
