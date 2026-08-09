#pragma once

// ============================================================================
// FrostEngine FrostWaveletGI — Wavelet-Domain Global Illumination
// ============================================================================
// Proprietary wavelet-compressed GI system. Uses Haar wavelet transform for
// efficient compression and reconstruction of indirect lighting data with
// temporal difference encoding and multi-scale quality.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Math.h"
#include "Core/Vector.h"

namespace Frost {

// ============================================================================
// Wavelet structures
// ============================================================================

static constexpr u32 WAVELET_MAX_LEVELS = 8;
static constexpr u32 GI_BUFFER_BASE_SIZE = 256;
static constexpr u32 CLIPMAP_LEVELS = 6;
static constexpr f32 WAVELET_THRESHOLD_DEFAULT = 0.01f;

// Wavelet coefficient with position
struct WaveletCoefficient {
    Vec3 value;             // wavelet coefficient value (RGB)
    f32 position[4];        // quantized position in wavelet space
    u32 level;              // wavelet decomposition level
    u32 index;              // linear index in coefficient array
    f32 magnitude;          // |coefficient| for thresholding
    bool isSignificant;     // passes threshold test

    WaveletCoefficient() : value(0), level(0), index(0), magnitude(0),
                           isSignificant(false) {
        position[0] = position[1] = position[2] = position[3] = 0;
    }
};

// Compressed wavelet coefficient (quantized)
struct CompressedCoefficient {
    i16 valueR, valueG, valueB;  // quantized to 16-bit
    u16 quantizedPos[4];         // position
    u8 level;
    u8 flags;                    // bit 0: significant, bit 1: temporal diff
    Vec3 value;                  // decompressed coefficient value (RGB)
    f32 magnitude;               // |coefficient| for thresholding

    CompressedCoefficient() : valueR(0), valueG(0), valueB(0), level(0), flags(0),
                                value(0), magnitude(0) {
        quantizedPos[0] = quantizedPos[1] = quantizedPos[2] = quantizedPos[3] = 0;
    }
};

// Clipmap level for multi-scale GI storage
struct WaveletClipmapLevel {
    u32 resolution;          // resolution of this level
    f32 cellSize;            // world-space cell size
    Vec3 origin;             // world-space origin of clipmap

    Vector<CompressedCoefficient> coefficients;
    u32 coefficientCount;
    u32 significantCount;    // number of non-zero coefficients

    WaveletClipmapLevel() : resolution(0), cellSize(0), coefficientCount(0),
                            significantCount(0) {}
};

// Wavelet quality settings
enum class WaveletQuality : u8 {
    Low = 0,         // 64x64 GI buffer, 4 wavelet levels
    Medium,          // 128x128 GI buffer, 6 wavelet levels
    High,            // 256x256 GI buffer, 8 wavelet levels
    Epic,            // 512x512 GI buffer, 8 wavelet levels, temporal
    COUNT
};

// GI sample point for wavelet compression
struct GISamplePoint {
    Vec3 worldPos;
    Vec3 normal;
    Vec3 irradiance;        // accumulated irradiance
    Vec3 direction;         // dominant direction
    f32 confidence;         // quality of estimate [0,1]
    u32 frameAccumulated;   // number of frames accumulated
};

// ============================================================================
// Main FrostWaveletGI system
// ============================================================================

class FrostWaveletGI {
public:
    FrostWaveletGI();
    ~FrostWaveletGI();

    bool init(WaveletQuality quality = WaveletQuality::Medium);
    void shutdown();
    void reset();

    // Main update: compute GI, apply wavelet transform, compress
    void update(f32 deltaTime, const Mat4& viewProj, u32 screenW, u32 screenH);

    // Render GI to low-res buffer
    void renderGIBuffer(const Mat4& viewProj, u32 screenW, u32 screenH);

    // Apply Haar wavelet transform to GI buffer
    void applyWaveletTransform();

    // Apply inverse wavelet transform for reconstruction
    void applyInverseWaveletTransform();

    // Compress wavelet coefficients (threshold small ones)
    void compressCoefficients(f32 threshold);

    // Decompress: reconstruct from compressed wavelets
    void decompress();

    // Store compressed GI in clipmap
    void updateClipmap(Vec3 cameraPos);

    // Temporal wavelet: difference encoding between frames
    void temporalDifferenceEncode();

    // Sample GI at a world position
    Vec3 sampleGI(Vec3 worldPos, Vec3 normal) const;

    // Sample from clipmap at a world position
    Vec3 sampleClipmap(Vec3 worldPos) const;

    // Get the reconstructed GI buffer
    const Vector<Vec3>& reconstructedBuffer() const { return reconstructedGI_; }

    // Quality control
    void setQuality(WaveletQuality quality);
    void setThreshold(f32 threshold) { threshold_ = threshold; }
    void setTemporalBlending(f32 blend) { temporalBlend_ = blend; }
    WaveletQuality quality() const { return quality_; }

    // Statistics
    u32 significantCoefficients() const { return significantCoeffCount_; }
    u32 totalCoefficients() const { return totalCoeffCount_; }
    f32 compressionRatio() const;
    f32 lastUpdateTimeMs() const { return lastUpdateTimeMs_; }

private:
    // Haar wavelet transform (2D)
    void haarForward2D(Vector<Vec3>& data, u32 width, u32 height);
    void haarInverse2D(Vector<Vec3>& data, u32 width, u32 height);
    void haarForward1DRow(Vec3* row, u32 length);
    void haarForward1DCol(Vec3* col, u32 length, u32 stride);
    void haarInverse1DRow(Vec3* row, u32 length);
    void haarInverse1DCol(Vec3* col, u32 length, u32 stride);

    // Coefficient management
    void thresholdCoefficients(f32 threshold);
    void quantizeCoefficients(f32 scale);
    void dequantizeCoefficients(f32 scale);
    u32 computeWaveletLevel(u32 index) const;
    void applyAdaptiveThreshold(f32 baseThreshold);
    void computeCoefficientEntropy() const;
    void sortCoefficientsByMagnitude();
    void applyLevelDependentThreshold(u32 maxLevel);

    // Clipmap management
    void initClipmaps();
    void updateClipmapLevel(u32 level, Vec3 cameraPos);
    void updateClipmapBlending();
    void initClipmapBorders();
    f32 computeClipmapCoverage(Vec3 cameraPos) const;

    // Temporal processing
    void computeTemporalDifference();
    void blendTemporalFrames();
    void applyTemporalFilter(f32 blendFactor);
    f32 computeTemporalStability() const;

    // GI buffer helpers
    void clearGIBuffer();
    void accumulateGISample(u32 x, u32 y, Vec3 irradiance);
    Vec3 getGIPixel(u32 x, u32 y) const;

    // Statistics and quality analysis
    f32 computeCompressionEfficiency() const;
    f32 computeReconstructionError() const;
    u32 computeWaveletEnergy() const;
    f32 computeLevelDistribution() const;
    void getWaveletStats(u32& totalCoeffs, u32& significant,
                         u32& levels, f32& compression) const;
    void getClipmapStats(Vector<u32>& levelResolutions,
                         Vector<u32>& levelSignificant) const;
    Vec3 getAverageGI() const;
    f32 computeGIRange() const;

    // Math helpers
    f32 computeCoefficientMagnitude(Vec3 coeff) const;
    Vec3 quantizeTo16Bit(Vec3 value, f32 scale) const;
    Vec3 dequantizeFrom16Bit(i16 r, i16 g, i16 b, f32 scale) const;

    // Configuration
    WaveletQuality quality_;
    u32 giBufferWidth_;
    u32 giBufferHeight_;
    u32 waveletLevels_;
    f32 threshold_;
    f32 temporalBlend_;
    f32 coefficientScale_;

    // GI buffers
    Vector<Vec3> giBuffer_;             // current frame GI
    Vector<Vec3> prevGIBuffer_;         // previous frame GI
    Vector<Vec3> reconstructedGI_;      // reconstructed after wavelet
    Vector<WaveletCoefficient> coefficients_;
    Vector<CompressedCoefficient> compressedCoeffs_;

    // Clipmap
    WaveletClipmapLevel clipmapLevels_[CLIPMAP_LEVELS];

    // Statistics
    u32 significantCoeffCount_;
    u32 totalCoeffCount_;
    f32 lastUpdateTimeMs_;
    u32 frameNumber_;

    bool initialized_;
};

} // namespace Frost
