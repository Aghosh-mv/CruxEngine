// ============================================================================
// FrostEngine FrostWaveletGI — Wavelet-Domain Global Illumination
// ============================================================================
// Proprietary wavelet-compressed GI system. Uses Haar wavelet transform for
// efficient compression and reconstruction of indirect lighting data with
// temporal difference encoding and multi-scale quality.
// ============================================================================

#include "FrostEngine/Renderer/FrostWaveletGI.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace Frost {

// ============================================================================
// Construction / Destruction
// ============================================================================

FrostWaveletGI::FrostWaveletGI()
    : quality_(WaveletQuality::Medium), giBufferWidth_(128), giBufferHeight_(128),
      waveletLevels_(6), threshold_(WAVELET_THRESHOLD_DEFAULT), temporalBlend_(0.8f),
      coefficientScale_(255.0f), significantCoeffCount_(0), totalCoeffCount_(0),
      lastUpdateTimeMs_(0), frameNumber_(0), initialized_(false) {
}

FrostWaveletGI::~FrostWaveletGI() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool FrostWaveletGI::init(WaveletQuality quality) {
    quality_ = quality;
    setQuality(quality);

    initClipmaps();

    initialized_ = true;
    return true;
}

void FrostWaveletGI::shutdown() {
    giBuffer_.clear();
    prevGIBuffer_.clear();
    reconstructedGI_.clear();
    coefficients_.clear();
    compressedCoeffs_.clear();

    for (u32 i = 0; i < CLIPMAP_LEVELS; i++) {
        clipmapLevels_[i].coefficients.clear();
    }

    initialized_ = false;
}

void FrostWaveletGI::reset() {
    for (auto& p : giBuffer_) p = Vec3(0);
    for (auto& p : prevGIBuffer_) p = Vec3(0);
    for (auto& p : reconstructedGI_) p = Vec3(0);
    frameNumber_ = 0;
}

// ============================================================================
// Quality Settings
// ============================================================================

void FrostWaveletGI::setQuality(WaveletQuality quality) {
    quality_ = quality;

    switch (quality) {
        case WaveletQuality::Low:
            giBufferWidth_ = 64;
            giBufferHeight_ = 64;
            waveletLevels_ = 4;
            break;
        case WaveletQuality::Medium:
            giBufferWidth_ = 128;
            giBufferHeight_ = 128;
            waveletLevels_ = 6;
            break;
        case WaveletQuality::High:
            giBufferWidth_ = 256;
            giBufferHeight_ = 256;
            waveletLevels_ = 8;
            break;
        case WaveletQuality::Epic:
            giBufferWidth_ = 512;
            giBufferHeight_ = 512;
            waveletLevels_ = 8;
            break;
        default:
            giBufferWidth_ = 128;
            giBufferHeight_ = 128;
            waveletLevels_ = 6;
            break;
    }

    u32 size = giBufferWidth_ * giBufferHeight_;
    giBuffer_.resize(size);
    prevGIBuffer_.resize(size);
    reconstructedGI_.resize(size);

    coefficients_.resize(size);
    compressedCoeffs_.resize(size);

    clearGIBuffer();
}

// ============================================================================
// Main Update
// ============================================================================

void FrostWaveletGI::update(f32 deltaTime, const Mat4& viewProj, u32 screenW, u32 screenH) {
    if (!initialized_) return;

    // 1. Render GI to low-res buffer
    renderGIBuffer(viewProj, screenW, screenH);

    // 2. Apply Haar wavelet transform
    applyWaveletTransform();

    // 3. Compress wavelet coefficients
    compressCoefficients(threshold_);

    // 4. Temporal difference encoding
    if (frameNumber_ > 0) {
        temporalDifferenceEncode();
    }

    // 5. Store in clipmap
    Vec3 cameraPos(0);  // would get from camera in production
    updateClipmap(cameraPos);

    // 6. Reconstruct from compressed wavelets
    applyInverseWaveletTransform();

    // 7. Blend with previous frame
    blendTemporalFrames();

    frameNumber_++;
}

// ============================================================================
// GI Buffer Rendering
// ============================================================================

void FrostWaveletGI::renderGIBuffer(const Mat4& viewProj, u32 screenW, u32 screenH) {
    clearGIBuffer();

    // Simulate GI rendering: for each GI buffer pixel, compute indirect lighting
    // In production, this would render the scene from the GI buffer's perspective
    // and accumulate indirect light contributions

    f32 aspect = (f32)screenW / (f32)screenH;

    for (u32 y = 0; y < giBufferHeight_; y++) {
        for (u32 x = 0; x < giBufferWidth_; x++) {
            // Compute direction for this GI buffer pixel
            f32 u = ((f32)x + 0.5f) / (f32)giBufferWidth_;
            f32 v = ((f32)y + 0.5f) / (f32)giBufferHeight_;

            // Spherical direction from UV
            f32 theta = v * 3.14159265f;
            f32 phi = u * 6.28318530f;

            Vec3 dir(
                sinf(theta) * cosf(phi),
                cosf(theta),
                sinf(theta) * sinf(phi)
            );

            // Simplified GI: sky contribution + ground bounce
            f32 skyFactor = Mathf::max(dir.y, 0.0f);
            f32 groundFactor = Mathf::max(-dir.y, 0.0f);

            Vec3 skyGI = Vec3(0.1f, 0.15f, 0.25f) * skyFactor * 2.0f;
            Vec3 groundGI = Vec3(0.05f, 0.08f, 0.02f) * groundFactor * 0.5f;

            // Add some variation based on position
            f32 variation = sinf((f32)x * 0.1f) * cosf((f32)y * 0.1f) * 0.1f;
            Vec3 gi = skyGI + groundGI + Vec3(variation);

            accumulateGISample(x, y, gi);
        }
    }
}

void FrostWaveletGI::clearGIBuffer() {
    for (auto& p : giBuffer_) p = Vec3(0);
}

void FrostWaveletGI::accumulateGISample(u32 x, u32 y, Vec3 irradiance) {
    if (x >= giBufferWidth_ || y >= giBufferHeight_) return;
    u32 idx = y * giBufferWidth_ + x;
    giBuffer_[idx] = giBuffer_[idx] + irradiance;
}

Vec3 FrostWaveletGI::getGIPixel(u32 x, u32 y) const {
    if (x >= giBufferWidth_ || y >= giBufferHeight_) return Vec3(0);
    return giBuffer_[y * giBufferWidth_ + x];
}

// ============================================================================
// Haar Wavelet Transform — 2D Forward and Inverse
// ============================================================================

void FrostWaveletGI::applyWaveletTransform() {
    // Copy GI buffer to coefficients for in-place transform
    for (u32 i = 0; i < giBuffer_.size(); i++) {
        coefficients_[i].value = giBuffer_[i];
        coefficients_[i].level = 0;
        coefficients_[i].index = i;
        coefficients_[i].magnitude = computeCoefficientMagnitude(giBuffer_[i]);
    }

    // Apply 2D Haar wavelet transform
    haarForward2D(giBuffer_, giBufferWidth_, giBufferHeight_);

    // Update coefficients from transformed buffer
    totalCoeffCount_ = giBufferWidth_ * giBufferHeight_;
    for (u32 i = 0; i < totalCoeffCount_; i++) {
        coefficients_[i].value = giBuffer_[i];
        coefficients_[i].magnitude = computeCoefficientMagnitude(giBuffer_[i]);
        coefficients_[i].level = this->computeWaveletLevel(i);
    }
}

void FrostWaveletGI::applyInverseWaveletTransform() {
    // Copy compressed coefficients back to buffer
    for (u32 i = 0; i < coefficients_.size(); i++) {
        giBuffer_[i] = coefficients_[i].value;
    }

    // Apply inverse 2D Haar wavelet transform
    haarInverse2D(giBuffer_, giBufferWidth_, giBufferHeight_);

    // Store reconstructed result
    reconstructedGI_ = giBuffer_;
}

void FrostWaveletGI::haarForward2D(Vector<Vec3>& data, u32 width, u32 height) {
    // In-place 2D Haar wavelet transform
    // Process rows first, then columns

    Vector<Vec3> temp;
    temp.resize(width * height);

    // Forward transform on rows
    for (u32 y = 0; y < height; y++) {
        Vector<Vec3> row;
        row.resize(width);
        for (u32 x = 0; x < width; x++) {
            row[x] = data[y * width + x];
        }
        haarForward1DRow(row.data(), width);
        for (u32 x = 0; x < width; x++) {
            temp[y * width + x] = row[x];
        }
    }

    // Forward transform on columns
    for (u32 x = 0; x < width; x++) {
        Vector<Vec3> col;
        col.resize(height);
        for (u32 y = 0; y < height; y++) {
            col[y] = temp[y * width + x];
        }
        haarForward1DCol(col.data(), height, 1);
        for (u32 y = 0; y < height; y++) {
            data[y * width + x] = col[y];
        }
    }
}

void FrostWaveletGI::haarInverse2D(Vector<Vec3>& data, u32 width, u32 height) {
    Vector<Vec3> temp;
    temp.resize(width * height);

    // Inverse transform on columns
    for (u32 x = 0; x < width; x++) {
        Vector<Vec3> col;
        col.resize(height);
        for (u32 y = 0; y < height; y++) {
            col[y] = data[y * width + x];
        }
        haarInverse1DCol(col.data(), height, 1);
        for (u32 y = 0; y < height; y++) {
            temp[y * width + x] = col[y];
        }
    }

    // Inverse transform on rows
    for (u32 y = 0; y < height; y++) {
        Vector<Vec3> row;
        row.resize(width);
        for (u32 x = 0; x < width; x++) {
            row[x] = temp[y * width + x];
        }
        haarInverse1DRow(row.data(), width);
        for (u32 x = 0; x < width; x++) {
            data[y * width + x] = row[x];
        }
    }
}

void FrostWaveletGI::haarForward1DRow(Vec3* row, u32 length) {
    // In-place 1D Haar forward transform on a row
    Vector<Vec3> temp;
    temp.resize(length);

    u32 n = length;
    while (n >= 2) {
        u32 half = n / 2;
        for (u32 i = 0; i < half; i++) {
            Vec3 a = row[i * 2];
            Vec3 b = row[i * 2 + 1];
            temp[i] = (a + b) * 0.5f;            // average (approximation)
            temp[half + i] = (a - b) * 0.5f;     // difference (detail)
        }
        for (u32 i = 0; i < n; i++) {
            row[i] = temp[i];
        }
        n = half;
    }
}

void FrostWaveletGI::haarForward1DCol(Vec3* col, u32 length, u32 stride) {
    // 1D Haar forward transform along a column
    Vector<Vec3> temp;
    temp.resize(length);

    u32 n = length;
    while (n >= 2) {
        u32 half = n / 2;
        for (u32 i = 0; i < half; i++) {
            Vec3 a = col[i * 2 * stride];
            Vec3 b = col[(i * 2 + 1) * stride];
            temp[i] = (a + b) * 0.5f;
            temp[half + i] = (a - b) * 0.5f;
        }
        for (u32 i = 0; i < n; i++) {
            col[i * stride] = temp[i];
        }
        n = half;
    }
}

void FrostWaveletGI::haarInverse1DRow(Vec3* row, u32 length) {
    Vector<Vec3> temp;
    temp.resize(length);

    u32 n = 2;
    while (n <= length) {
        u32 half = n / 2;
        for (u32 i = 0; i < half; i++) {
            Vec3 avg = row[i];
            Vec3 diff = row[half + i];
            temp[i * 2] = avg + diff;
            temp[i * 2 + 1] = avg - diff;
        }
        for (u32 i = 0; i < n; i++) {
            row[i] = temp[i];
        }
        n *= 2;
    }
}

void FrostWaveletGI::haarInverse1DCol(Vec3* col, u32 length, u32 stride) {
    Vector<Vec3> temp;
    temp.resize(length);

    u32 n = 2;
    while (n <= length) {
        u32 half = n / 2;
        for (u32 i = 0; i < half; i++) {
            Vec3 avg = col[i * stride];
            Vec3 diff = col[(half + i) * stride];
            temp[i * 2] = avg + diff;
            temp[(i * 2 + 1)] = avg - diff;
        }
        for (u32 i = 0; i < n; i++) {
            col[i * stride] = temp[i];
        }
        n *= 2;
    }
}

// ============================================================================
// Coefficient Compression
// ============================================================================

void FrostWaveletGI::compressCoefficients(f32 threshold) {
    significantCoeffCount_ = 0;

    for (u32 i = 0; i < coefficients_.size(); i++) {
        WaveletCoefficient& coeff = coefficients_[i];

        // Threshold: mark small coefficients as insignificant
        if (coeff.magnitude < threshold) {
            coeff.isSignificant = false;
            coeff.value = Vec3(0);  // zero out insignificant
        } else {
            coeff.isSignificant = true;
            significantCoeffCount_++;
        }
    }

    // Quantize significant coefficients for storage
    quantizeCoefficients(coefficientScale_);
}

void FrostWaveletGI::thresholdCoefficients(f32 threshold) {
    for (auto& coeff : coefficients_) {
        if (coeff.magnitude < threshold) {
            coeff.isSignificant = false;
            coeff.value = Vec3(0);
        }
    }
}

void FrostWaveletGI::quantizeCoefficients(f32 scale) {
    compressedCoeffs_.resize(coefficients_.size());

    for (u32 i = 0; i < coefficients_.size(); i++) {
        const WaveletCoefficient& coeff = coefficients_[i];
        CompressedCoefficient& comp = compressedCoeffs_[i];

        // Quantize value to 16-bit
        comp.valueR = (i16)Mathf::clamp(coeff.value.x * scale, -32768.0f, 32767.0f);
        comp.valueG = (i16)Mathf::clamp(coeff.value.y * scale, -32768.0f, 32767.0f);
        comp.valueB = (i16)Mathf::clamp(coeff.value.z * scale, -32768.0f, 32767.0f);

        // Quantize position
        comp.quantizedPos[0] = (u16)(coeff.position[0] * 65535.0f);
        comp.quantizedPos[1] = (u16)(coeff.position[1] * 65535.0f);
        comp.quantizedPos[2] = (u16)(coeff.position[2] * 65535.0f);
        comp.quantizedPos[3] = (u16)(coeff.position[3] * 65535.0f);

        comp.level = (u8)coeff.level;
        comp.flags = coeff.isSignificant ? 1 : 0;
    }
}

void FrostWaveletGI::dequantizeCoefficients(f32 scale) {
    for (u32 i = 0; i < compressedCoeffs_.size(); i++) {
        const CompressedCoefficient& comp = compressedCoeffs_[i];
        WaveletCoefficient& coeff = coefficients_[i];

        coeff.value.x = (f32)comp.valueR / scale;
        coeff.value.y = (f32)comp.valueG / scale;
        coeff.value.z = (f32)comp.valueB / scale;

        coeff.position[0] = (f32)comp.quantizedPos[0] / 65535.0f;
        coeff.position[1] = (f32)comp.quantizedPos[1] / 65535.0f;
        coeff.position[2] = (f32)comp.quantizedPos[2] / 65535.0f;
        coeff.position[3] = (f32)comp.quantizedPos[3] / 65535.0f;

        coeff.level = comp.level;
        coeff.isSignificant = (comp.flags & 1) != 0;
    }
}

// ============================================================================
// Temporal Wavelet — Difference Encoding Between Frames
// ============================================================================

void FrostWaveletGI::temporalDifferenceEncode() {
    // Compute difference between current and previous GI buffer
    if (prevGIBuffer_.size() != giBuffer_.size()) {
        prevGIBuffer_.resize(giBuffer_.size());
        for (auto& p : prevGIBuffer_) p = Vec3(0);
    }

    for (u32 i = 0; i < giBuffer_.size(); i++) {
        Vec3 diff = giBuffer_[i] - prevGIBuffer_[i];

        // If difference is small, keep previous value (compression)
        f32 diffMag = computeCoefficientMagnitude(diff);
        if (diffMag < threshold_ * 0.5f) {
            giBuffer_[i] = prevGIBuffer_[i];  // reuse previous
        }
    }

    // Store current frame for next frame's difference
    prevGIBuffer_ = giBuffer_;
}

void FrostWaveletGI::blendTemporalFrames() {
    // Blend reconstructed GI with previous frame for temporal stability
    if (prevGIBuffer_.size() != reconstructedGI_.size()) return;

    for (u32 i = 0; i < reconstructedGI_.size(); i++) {
        reconstructedGI_[i] = reconstructedGI_[i] * (1.0f - temporalBlend_) +
                              prevGIBuffer_[i] * temporalBlend_;
    }
}

// ============================================================================
// Clipmap Management
// ============================================================================

void FrostWaveletGI::initClipmaps() {
    for (u32 i = 0; i < CLIPMAP_LEVELS; i++) {
        WaveletClipmapLevel& level = clipmapLevels_[i];
        level.resolution = GI_BUFFER_BASE_SIZE >> i;
        if (level.resolution < 16) level.resolution = 16;
        level.cellSize = (f32)(1 << i) * 2.0f;
        level.coefficientCount = level.resolution * level.resolution;
        level.coefficients.resize(level.coefficientCount);
        level.significantCount = 0;
    }
}

void FrostWaveletGI::updateClipmap(Vec3 cameraPos) {
    for (u32 i = 0; i < CLIPMAP_LEVELS; i++) {
        updateClipmapLevel(i, cameraPos);
    }
}

void FrostWaveletGI::updateClipmapLevel(u32 level, Vec3 cameraPos) {
    WaveletClipmapLevel& clipmap = clipmapLevels_[level];

    // Compute origin based on camera position and level cell size
    f32 cellSize = clipmap.cellSize;
    clipmap.origin = Vec3(
        floorf(cameraPos.x / cellSize) * cellSize,
        0,
        floorf(cameraPos.z / cellSize) * cellSize
    );

    // Downsample GI buffer to this clipmap level
    u32 srcRes = giBufferWidth_;
    u32 dstRes = clipmap.resolution;

    if (level == 0) {
        // Level 0: directly copy from GI buffer
        for (u32 i = 0; i < clipmap.coefficientCount && i < giBuffer_.size(); i++) {
            clipmap.coefficients[i].value = giBuffer_[i];
        }
    } else {
        // Higher levels: downsample from previous level
        u32 prevRes = clipmapLevels_[level - 1].resolution;
        for (u32 y = 0; y < dstRes; y++) {
            for (u32 x = 0; x < dstRes; x++) {
                u32 dstIdx = y * dstRes + x;

                // Average 2x2 block from previous level
                u32 srcX = x * 2;
                u32 srcY = y * 2;

                Vec3 avg(0);
                u32 count = 0;

                for (u32 dy = 0; dy < 2; dy++) {
                    for (u32 dx = 0; dx < 2; dx++) {
                        u32 sx = srcX + dx;
                        u32 sy = srcY + dy;
                        if (sx < prevRes && sy < prevRes) {
                            u32 srcIdx = sy * prevRes + sx;
                            avg += clipmapLevels_[level - 1].coefficients[srcIdx].value;
                            count++;
                        }
                    }
                }

                if (count > 0) avg = avg / (f32)count;

                clipmap.coefficients[dstIdx].value = avg;
                clipmap.coefficients[dstIdx].magnitude = computeCoefficientMagnitude(avg);
            }
        }
    }

    // Count significant coefficients
    clipmap.significantCount = 0;
    for (u32 i = 0; i < clipmap.coefficientCount; i++) {
        if (clipmap.coefficients[i].magnitude >= threshold_) {
            clipmap.significantCount++;
        }
    }
}

// ============================================================================
// GI Sampling
// ============================================================================

Vec3 FrostWaveletGI::sampleGI(Vec3 worldPos, Vec3 normal) const {
    // Sample from reconstructed GI buffer
    // Convert world position to GI buffer UV
    f32 u = (atan2f(worldPos.z, worldPos.x) / 6.28318530f) + 0.5f;
    f32 v = acosf(Mathf::clamp(-worldPos.y / (worldPos.length() + 0.001f), -1.0f, 1.0f)) / 3.14159265f;

    u32 x = (u32)(u * giBufferWidth_) % giBufferWidth_;
    u32 y = (u32)(v * giBufferHeight_) % giBufferHeight_;

    return reconstructedGI_[y * giBufferWidth_ + x];
}

Vec3 FrostWaveletGI::sampleClipmap(Vec3 worldPos) const {
    // Find appropriate clipmap level based on distance from camera
    // Simplified: use level 0
    if (clipmapLevels_[0].coefficientCount > 0) {
        return clipmapLevels_[0].coefficients[0].value;
    }
    return Vec3(0);
}

// ============================================================================
// Statistics
// ============================================================================

f32 FrostWaveletGI::compressionRatio() const {
    if (totalCoeffCount_ == 0) return 0;
    return (f32)significantCoeffCount_ / (f32)totalCoeffCount_;
}

// ============================================================================
// Helpers
// ============================================================================

f32 FrostWaveletGI::computeCoefficientMagnitude(Vec3 coeff) const {
    return sqrtf(coeff.x * coeff.x + coeff.y * coeff.y + coeff.z * coeff.z);
}

u32 FrostWaveletGI::computeWaveletLevel(u32 index) const {
    // Determine wavelet level from coefficient index
    u32 level = 0;
    u32 n = totalCoeffCount_;
    while (n > 1 && index >= n / 2) {
        level++;
        n /= 2;
    }
    return std::min(level, waveletLevels_);
}

Vec3 FrostWaveletGI::quantizeTo16Bit(Vec3 value, f32 scale) const {
    return Vec3(
        Mathf::clamp(value.x * scale, -32768.0f, 32767.0f) / scale,
        Mathf::clamp(value.y * scale, -32768.0f, 32767.0f) / scale,
        Mathf::clamp(value.z * scale, -32768.0f, 32767.0f) / scale
    );
}

Vec3 FrostWaveletGI::dequantizeFrom16Bit(i16 r, i16 g, i16 b, f32 scale) const {
    return Vec3((f32)r / scale, (f32)g / scale, (f32)b / scale);
}

// ============================================================================
// Advanced Wavelet Operations
// ============================================================================

void FrostWaveletGI::applyAdaptiveThreshold(f32 baseThreshold) {
    // Apply level-dependent thresholding
    // Higher levels (coarser) get lower thresholds
    for (u32 i = 0; i < coefficients_.size(); i++) {
        WaveletCoefficient& coeff = coefficients_[i];

        f32 levelScale = 1.0f / (1.0f + (f32)coeff.level * 0.5f);
        f32 adaptiveThreshold = baseThreshold * levelScale;

        if (coeff.magnitude < adaptiveThreshold) {
            coeff.isSignificant = false;
            coeff.value = Vec3(0);
        } else {
            coeff.isSignificant = true;
        }
    }
}

void FrostWaveletGI::computeCoefficientEntropy() const {
    // Measure information content of wavelet coefficients
    u32 significantCount = 0;
    u32 totalCount = (u32)coefficients_.size();

    for (u32 i = 0; i < totalCount; i++) {
        if (coefficients_[i].isSignificant) significantCount++;
    }

    // Shannon entropy approximation
    f32 p = (f32)significantCount / (f32)totalCount;
    if (p > 0 && p < 1) {
        f32 entropy = -p * log2f(p) - (1.0f - p) * log2f(1.0f - p);
        // entropy would be stored for quality assessment
    }
}

void FrostWaveletGI::sortCoefficientsByMagnitude() {
    // Sort coefficients by magnitude for efficient compression
    for (u32 i = 0; i < coefficients_.size() - 1; i++) {
        for (u32 j = i + 1; j < coefficients_.size(); j++) {
            if (coefficients_[j].magnitude > coefficients_[i].magnitude) {
                WaveletCoefficient temp = coefficients_[i];
                coefficients_[i] = coefficients_[j];
                coefficients_[j] = temp;
            }
        }
    }
}

void FrostWaveletGI::applyLevelDependentThreshold(u32 maxLevel) {
    // Apply different thresholds per wavelet level
    for (u32 i = 0; i < coefficients_.size(); i++) {
        WaveletCoefficient& coeff = coefficients_[i];

        // Coarser levels get higher threshold (more compression)
        f32 levelFactor = (f32)(coeff.level + 1) / (f32)(maxLevel + 1);
        f32 levelThreshold = threshold_ * (1.0f + levelFactor * 2.0f);

        if (coeff.magnitude < levelThreshold) {
            coeff.isSignificant = false;
            coeff.value = Vec3(0);
        }
    }
}

// ============================================================================
// Clipmap Advanced Operations
// ============================================================================

void FrostWaveletGI::updateClipmapBlending() {
    // Blend between clipmap levels based on camera distance
    for (u32 i = 0; i < CLIPMAP_LEVELS - 1; i++) {
        WaveletClipmapLevel& current = clipmapLevels_[i];
        WaveletClipmapLevel& next = clipmapLevels_[i + 1];

        // At level boundaries, blend between levels
        f32 blendFactor = 0.5f;  // would be based on camera distance
        u32 blendCount = std::min(current.coefficientCount, next.coefficientCount);

        for (u32 j = 0; j < blendCount; j++) {
            current.coefficients[j].value = current.coefficients[j].value * (1.0f - blendFactor) +
                                            next.coefficients[j].value * blendFactor;
        }
    }
}

void FrostWaveletGI::initClipmapBorders() {
    // Initialize clipmap border regions for seamless transitions
    for (u32 i = 0; i < CLIPMAP_LEVELS; i++) {
        WaveletClipmapLevel& level = clipmapLevels_[i];

        // Fill border with extrapolated values
        u32 res = level.resolution;
        for (u32 y = 0; y < res; y++) {
            for (u32 x = 0; x < res; x++) {
                bool isBorder = (x == 0 || x == res - 1 || y == 0 || y == res - 1);
                if (isBorder) {
                    // Sample from next coarser level
                    u32 srcX = std::min(x / 2, clipmapLevels_[std::min(i + 1, CLIPMAP_LEVELS - 1)].resolution - 1);
                    u32 srcY = std::min(y / 2, clipmapLevels_[std::min(i + 1, CLIPMAP_LEVELS - 1)].resolution - 1);
                    u32 srcIdx = srcY * clipmapLevels_[std::min(i + 1, CLIPMAP_LEVELS - 1)].resolution + srcX;

                    level.coefficients[y * res + x] = clipmapLevels_[std::min(i + 1, CLIPMAP_LEVELS - 1)].coefficients[srcIdx];
                }
            }
        }
    }
}

f32 FrostWaveletGI::computeClipmapCoverage(Vec3 cameraPos) const {
    // Compute how well clipmaps cover the scene
    f32 totalCoverage = 0;

    for (u32 i = 0; i < CLIPMAP_LEVELS; i++) {
        const WaveletClipmapLevel& level = clipmapLevels_[i];
        f32 levelArea = level.cellSize * level.resolution;
        totalCoverage += levelArea;
    }

    return totalCoverage;
}

// ============================================================================
// Temporal Wavelet Operations
// ============================================================================

void FrostWaveletGI::computeTemporalDifference() {
    if (prevGIBuffer_.size() != giBuffer_.size()) {
        prevGIBuffer_.resize(giBuffer_.size());
        for (auto& p : prevGIBuffer_) p = Vec3(0);
    }

    for (u32 i = 0; i < giBuffer_.size(); i++) {
        Vec3 diff = giBuffer_[i] - prevGIBuffer_[i];

        f32 diffMag = computeCoefficientMagnitude(diff);
        if (diffMag < threshold_ * 0.5f) {
            giBuffer_[i] = prevGIBuffer_[i];
        }
    }

    prevGIBuffer_ = giBuffer_;
}

void FrostWaveletGI::applyTemporalFilter(f32 blendFactor) {
    if (prevGIBuffer_.size() != giBuffer_.size()) return;

    for (u32 i = 0; i < giBuffer_.size(); i++) {
        giBuffer_[i] = giBuffer_[i] * (1.0f - blendFactor) +
                        prevGIBuffer_[i] * blendFactor;
    }
}

f32 FrostWaveletGI::computeTemporalStability() const {
    if (prevGIBuffer_.size() != reconstructedGI_.size()) return 1.0f;

    f32 totalChange = 0;
    for (u32 i = 0; i < reconstructedGI_.size(); i++) {
        totalChange += (reconstructedGI_[i] - prevGIBuffer_[i]).length();
    }

    return 1.0f / (1.0f + totalChange / (f32)reconstructedGI_.size());
}

// ============================================================================
// Quality Analysis
// ============================================================================

f32 FrostWaveletGI::computeCompressionEfficiency() const {
    if (totalCoeffCount_ == 0) return 0;
    return (f32)significantCoeffCount_ / (f32)totalCoeffCount_;
}

f32 FrostWaveletGI::computeReconstructionError() const {
    if (giBuffer_.size() != reconstructedGI_.size()) return 0;

    f32 totalError = 0;
    for (u32 i = 0; i < giBuffer_.size(); i++) {
        Vec3 diff = giBuffer_[i] - reconstructedGI_[i];
        totalError += diff.length();
    }

    return totalError / (f32)giBuffer_.size();
}

u32 FrostWaveletGI::computeWaveletEnergy() const {
    u32 energy = 0;
    for (u32 i = 0; i < coefficients_.size(); i++) {
        if (coefficients_[i].isSignificant) {
            energy += (u32)(coefficients_[i].magnitude * 1000.0f);
        }
    }
    return energy;
}

f32 FrostWaveletGI::computeLevelDistribution() const {
    // Measure how energy is distributed across wavelet levels
    Vector<u32> levelCounts;
    levelCounts.resize(waveletLevels_ + 1, 0);

    for (u32 i = 0; i < coefficients_.size(); i++) {
        if (coefficients_[i].isSignificant) {
            u32 level = std::min(coefficients_[i].level, waveletLevels_);
            levelCounts[level]++;
        }
    }

    // Return entropy of level distribution
    f32 entropy = 0;
    u32 total = significantCoeffCount_;
    if (total == 0) return 0;

    for (u32 i = 0; i < levelCounts.size(); i++) {
        if (levelCounts[i] > 0) {
            f32 p = (f32)levelCounts[i] / (f32)total;
            entropy -= p * log2f(p + 0.0001f);
        }
    }

    return entropy;
}

// ============================================================================
// Debug and Statistics
// ============================================================================

void FrostWaveletGI::getWaveletStats(u32& totalCoeffs, u32& significant,
                                       u32& levels, f32& compression) const {
    totalCoeffs = totalCoeffCount_;
    significant = significantCoeffCount_;
    levels = waveletLevels_;
    compression = compressionRatio();
}

void FrostWaveletGI::getClipmapStats(Vector<u32>& levelResolutions,
                                       Vector<u32>& levelSignificant) const {
    levelResolutions.clear();
    levelSignificant.clear();

    for (u32 i = 0; i < CLIPMAP_LEVELS; i++) {
        levelResolutions.push_back(clipmapLevels_[i].resolution);
        levelSignificant.push_back(clipmapLevels_[i].significantCount);
    }
}

Vec3 FrostWaveletGI::getAverageGI() const {
    Vec3 total(0);
    for (u32 i = 0; i < reconstructedGI_.size(); i++) {
        total += reconstructedGI_[i];
    }
    return reconstructedGI_.size() > 0 ? total / (f32)reconstructedGI_.size() : Vec3(0);
}

f32 FrostWaveletGI::computeGIRange() const {
    Vec3 min(1e30f), max(-1e30f);
    for (u32 i = 0; i < reconstructedGI_.size(); i++) {
        min = min.min(reconstructedGI_[i]);
        max = max.max(reconstructedGI_[i]);
    }
    return (max - min).length();
}

} // namespace Frost
