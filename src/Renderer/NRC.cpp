// ============================================================================
// FrostEngine NRC — Neural Radiance Caching
// ============================================================================
// Radiance cache storage, MLP training dataset, and cache query implementations
// for the NRCSystem. Complements the inline network in NRC.h.
// ============================================================================

#include "FrostEngine/Renderer/NRC.h"
#include <cmath>

namespace Frost {

static constexpr f32 kCacheUpdateRadiusSq = 0.25f;      // 0.5m update radius
static constexpr f32 kCachePositionWeightScale = 100.0f;

// ---- Query: nearest cache entry within confidence threshold ----
Vec3 NRCSystem::query(const Vec3& pos, const Vec3& normal, const Vec3& dir) const {
    if (cacheEntries_.empty()) {
        cacheMisses_++;
        return Vec3(0.0f);
    }

    usize best = nearestEntry(pos);
    f32 confidence = entryConfidence(cacheEntries_[best], pos, normal, dir);

    if (confidence >= confidenceThreshold_) {
        cacheHits_++;
        return cacheEntries_[best].radiance;
    }

    cacheMisses_++;
    return Vec3(0.0f);
}

// ---- Insert: add a cache entry, evicting the oldest when full ----
void NRCSystem::insert(const Vec3& pos, const Vec3& normal, const Vec3& dir, const Vec3& radiance) {
    if (cacheEntries_.size() >= (usize)maxCacheSize_) {
        usize oldest = 0;
        for (usize i = 1; i < cacheEntries_.size(); i++) {
            if (cacheEntries_[i].lastUpdateFrame < cacheEntries_[oldest].lastUpdateFrame) {
                oldest = i;
            }
        }
        cacheEntries_.erase(oldest);
    }

    CacheEntry entry;
    entry.position = pos;
    entry.normal = normal.normalized();
    entry.direction = dir.normalized();
    entry.radiance = radiance;
    entry.confidence = 1.0f;
    entry.lastUpdateFrame = updateCounter_++;
    cacheEntries_.push_back(entry);
}

// ---- Train: one pass over the accumulated training samples, returns average loss ----
f32 NRCSystem::train(f32 learningRate) {
    if (trainingSamples_.empty() || !network_) {
        return avgLoss_;
    }

    f32 totalLoss = 0.0f;
    for (usize i = 0; i < trainingSamples_.size(); i++) {
        const MLPTrainingSample& sample = trainingSamples_[i];

        f32 input[TinyMLP::INPUT_SIZE] = {};
        for (usize j = 0; j < sample.inputs.size() && j < TinyMLP::INPUT_SIZE; j++) {
            input[j] = sample.inputs[j];
        }
        f32 target[TinyMLP::OUTPUT_SIZE] = {
            sample.targetRadiance.x, sample.targetRadiance.y, sample.targetRadiance.z
        };

        f32 prediction[TinyMLP::OUTPUT_SIZE];
        network_->forward(input, prediction);

        f32 sampleLoss = 0.0f;
        for (u32 j = 0; j < TinyMLP::OUTPUT_SIZE; j++) {
            f32 diff = prediction[j] - target[j];
            sampleLoss += diff * diff;
        }
        totalLoss += sampleLoss * sample.weight;
        network_->trainStep(input, target, learningRate * sample.weight);
    }

    f32 passLoss = totalLoss / (f32)trainingSamples_.size();
    avgLoss_ = Mathf::lerp(avgLoss_, passLoss, 0.2f);
    trainingEpochs_++;
    return passLoss;
}

// ---- Add a training sample to the MLP dataset ----
void NRCSystem::addTrainingSample(const MLPTrainingSample& sample) {
    trainingSamples_.push_back(sample);
}

// ---- Update: blend into the nearest entry, or insert when none is close ----
void NRCSystem::updateCache(const Vec3& pos, const Vec3& normal, const Vec3& dir, const Vec3& radiance) {
    if (!cacheEntries_.empty()) {
        usize best = nearestEntry(pos);
        if ((cacheEntries_[best].position - pos).lengthSquared() <= kCacheUpdateRadiusSq) {
            CacheEntry& entry = cacheEntries_[best];
            entry.radiance.x = Mathf::lerp(entry.radiance.x, radiance.x, 0.5f);
            entry.radiance.y = Mathf::lerp(entry.radiance.y, radiance.y, 0.5f);
            entry.radiance.z = Mathf::lerp(entry.radiance.z, radiance.z, 0.5f);
            entry.normal = (entry.normal + normal.normalized()).normalized();
            entry.direction = (entry.direction + dir.normalized()).normalized();
            entry.confidence = Mathf::saturate(entry.confidence * 0.5f + 0.5f);
            entry.lastUpdateFrame = updateCounter_++;
            return;
        }
    }
    insert(pos, normal, dir, radiance);
}

// ---- Diagnostics ----
u32 NRCSystem::getCacheSize() const { return (u32)cacheEntries_.size(); }
u32 NRCSystem::getCacheHits() const { return cacheHits_; }
u32 NRCSystem::getCacheMisses() const { return cacheMisses_; }
f32 NRCSystem::getCacheHitRatio() const {
    u32 total = cacheHits_ + cacheMisses_;
    return total > 0 ? (f32)cacheHits_ / (f32)total : 0.0f;
}
f32 NRCSystem::getAvgLoss() const { return avgLoss_; }
u32 NRCSystem::getTrainingEpochs() const { return trainingEpochs_; }

// ---- Reset the radiance cache ----
void NRCSystem::clearCache() {
    cacheEntries_.clear();
    trainingSamples_.clear();
    cacheHits_ = 0;
    cacheMisses_ = 0;
    updateCounter_ = 0;
}

// ---- Private helpers ----
usize NRCSystem::nearestEntry(const Vec3& pos) const {
    usize best = 0;
    f32 bestDistSq = (cacheEntries_[best].position - pos).lengthSquared();
    for (usize i = 1; i < cacheEntries_.size(); i++) {
        f32 distSq = (cacheEntries_[i].position - pos).lengthSquared();
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            best = i;
        }
    }
    return best;
}

f32 NRCSystem::entryConfidence(const CacheEntry& entry, const Vec3& pos, const Vec3& normal, const Vec3& dir) const {
    f32 distSq = (entry.position - pos).lengthSquared();
    f32 positionWeight = 1.0f / (1.0f + distSq * kCachePositionWeightScale);
    f32 normalWeight = Mathf::saturate(0.5f + 0.5f * entry.normal.dot(normal.normalized()));
    f32 directionWeight = Mathf::saturate(0.5f + 0.5f * entry.direction.dot(dir.normalized()));
    return positionWeight * normalWeight * directionWeight * entry.confidence;
}

} // namespace Frost
