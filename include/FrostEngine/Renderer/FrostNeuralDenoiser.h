#pragma once

// ============================================================================
// FrostEngine FrostNeuralDenoiser — Neural Network Denoiser
// ============================================================================
// Proprietary learned denoiser. Uses a small convolutional encoder-decoder
// network with temporal accumulation and edge-aware processing to denoise
// path-traced images in real-time.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Math.h"
#include "Core/Vector.h"

namespace Frost {

// ============================================================================
// Neural network structures
// ============================================================================

static constexpr u32 NN_ENCODER_LAYERS = 4;
static constexpr u32 NN_DECODER_LAYERS = 3;
static constexpr u32 NN_CHANNELS = 64;
static constexpr u32 NN_KERNEL_SIZE = 3;
static constexpr u32 NN_INPUT_CHANNELS = 12;  // noisy(3) + normal(3) + motion(2) + depth(1) + albedo(3) = 12
static constexpr u32 NN_OUTPUT_CHANNELS = 3;   // denoised RGB

// Convolutional layer weights (embedded pre-trained weights)
struct ConvLayer {
    f32 weights[NN_KERNEL_SIZE * NN_KERNEL_SIZE * NN_CHANNELS * NN_CHANNELS];
    f32 biases[NN_CHANNELS];
    u32 inChannels;
    u32 outChannels;

    ConvLayer() : inChannels(0), outChannels(0) {
        memset(weights, 0, sizeof(weights));
        memset(biases, 0, sizeof(biases));
    }
};

// Feature map for intermediate network activations
struct FeatureMap {
    Vector<f32> data;
    u32 width;
    u32 height;
    u32 channels;

    FeatureMap() : width(0), height(0), channels(0) {}

    void resize(u32 w, u32 h, u32 c) {
        width = w; height = h; channels = c;
        data.resize(w * h * c);
    }

    f32& at(u32 x, u32 y, u32 c) {
        return data[(y * width + x) * channels + c];
    }

    const f32& at(u32 x, u32 y, u32 c) const {
        return data[(y * width + x) * channels + c];
    }
};

// Network input bundle
struct DenoiseInput {
    Vector<Vec3> noisyImage;        // noisy render (RGB)
    Vector<Vec3> normals;           // world normals (RGB)
    Vector<Vec2> motionVectors;     // motion vectors (RG)
    Vector<f32> depth;              // linear depth (R)
    Vector<Vec3> albedo;            // surface albedo (RGB)
    u32 width;
    u32 height;

    DenoiseInput() : width(0), height(0) {}
};

// Network output
struct DenoiseOutput {
    Vector<Vec3> denoisedImage;
    Vector<Vec3> varianceMap;       // per-pixel variance estimate
    u32 width;
    u32 height;

    DenoiseOutput() : width(0), height(0) {}
};

// Edge-aware bilateral filter settings
struct BilateralSettings {
    f32 spatialSigma;       // spatial Gaussian sigma (pixels)
    f32 colorSigma;         // color Gaussian sigma
    f32 normalSigma;        // normal edge-stopping sigma
    f32 depthSigma;         // depth edge-stopping sigma
    u32 kernelRadius;       // filter kernel radius

    BilateralSettings() : spatialSigma(3.0f), colorSigma(0.1f),
                          normalSigma(0.1f), depthSigma(0.01f),
                          kernelRadius(4) {}
};

// Temporal accumulation state
struct TemporalState {
    Vector<Vec3> prevFeatures;      // previous frame features
    Vector<Vec3> prevOutput;        // previous frame output
    Vector<Vec3> prevNormals;       // previous frame normals
    Vector<f32> prevDepth;          // previous frame depth
    u32 prevWidth;
    u32 prevHeight;
    bool hasPrevFrame;

    TemporalState() : prevWidth(0), prevHeight(0), hasPrevFrame(false) {}
};

// ============================================================================
// Main FrostNeuralDenoiser system
// ============================================================================

class FrostNeuralDenoiser {
public:
    FrostNeuralDenoiser();
    ~FrostNeuralDenoiser();

    bool init(u32 width, u32 height);
    void shutdown();
    void reset();

    // Main denoise: take noisy input, produce clean output
    void denoise(const DenoiseInput& input, DenoiseOutput& output);

    // Neural network forward pass
    void forwardPass(const DenoiseInput& input);

    // Temporal accumulation: blend with previous frame
    void temporalAccumulate();

    // Edge-aware processing (post-network)
    void edgeAwareProcess();

    // Bilateral filter fallback (when NN unavailable)
    void bilateralFilterFallback(const DenoiseInput& input, DenoiseOutput& output);

    // Pre-trained weights (embedded)
    void loadEmbeddedWeights();

    // Set quality
    void setTemporalBlending(f32 blend) { temporalBlend_ = blend; }
    void setEdgeStoppingStrength(f32 strength) { edgeStrength_ = strength; }
    void setUseNeural(bool use) { useNeural_ = use; }

    // Statistics
    f32 lastDenoiseTimeMs() const { return lastDenoiseTimeMs_; }
    u32 frameCount() const { return frameCount_; }
    bool neuralAvailable() const { return neuralAvailable_; }

private:
    // Convolution operations
    void conv2d(const FeatureMap& input, FeatureMap& output,
                const ConvLayer& layer, u32 stride = 1);
    void relu(FeatureMap& featureMap);
    void convTranspose2d(const FeatureMap& input, FeatureMap& output,
                         const ConvLayer& layer);

    // Encoder: extract features from input
    void encode(const DenoiseInput& input);

    // Decode: reconstruct clean image from features
    void decode(DenoiseOutput& output);

    // Skip connections between encoder and decoder
    void applySkipConnection(FeatureMap& decoderFeat, const FeatureMap& encoderFeat);

    // Normalized cross-correlation for feature matching
    f32 computeNCC(const Vector<f32>& a, const Vector<f32>& b, u32 count) const;

    // Bilateral filter core
    void bilateralFilterCore(Vector<Vec3>& output, const Vector<Vec3>& input,
                             const Vector<Vec3>& normals, const Vector<f32>& depths,
                             u32 w, u32 h, const BilateralSettings& settings);

    // Edge detection
    void detectEdges(const Vector<Vec3>& normals, const Vector<f32>& depth,
                     Vector<bool>& edges, u32 w, u32 h) const;

    // Variance estimation
    void estimateVariance(const Vector<Vec3>& noisy, Vector<Vec3>& variance,
                          u32 w, u32 h) const;

    // Motion-compensated reprojection
    void reprojectWithMotion(const DenoiseInput& input, Vector<Vec3>& reprojected,
                             u32& validCount) const;

    // Gaussian weight helper
    f32 gaussianWeight(f32 dist, f32 sigma) const;

    // Initialize embedded weights (simplified random init for demo)
    void initializeWeights();

    // Configuration
    u32 width_;
    u32 height_;
    f32 temporalBlend_;
    f32 edgeStrength_;
    bool useNeural_;
    bool neuralAvailable_;

    // Network layers (encoder)
    ConvLayer encoderLayers_[NN_ENCODER_LAYERS];
    // Network layers (decoder)
    ConvLayer decoderLayers_[NN_DECODER_LAYERS];
    // Skip connection projection
    ConvLayer skipProjection_;

    // Feature maps
    FeatureMap encoderFeatures_[NN_ENCODER_LAYERS];
    FeatureMap decoderFeatures_[NN_DECODER_LAYERS];
    FeatureMap bottleneck_;

    // Temporal state
    TemporalState temporalState_;

    // Bilateral settings
    BilateralSettings bilateralSettings_;

    // Statistics
    f32 lastDenoiseTimeMs_;
    u32 frameCount_;
    bool initialized_;
    Vector<Vec3> inputBuffer_;

    // Advanced operations
    void computeFeaturePyramid();
    void applyFeaturePyramidDenoise();
    f32 computeNoiseLevel() const;
    void applyEdgeEnhancement();
    void saveWeights(const char* filename) const;
    void loadWeights(const char* filename);
    void fineTuneOnScene(const DenoiseInput* trainingData, u32 sampleCount, u32 epochs);
    f32 computePSNR(const Vector<Vec3>& denoised, const Vector<Vec3>& groundTruth) const;
    f32 computeSSIM(const Vector<Vec3>& denoised, const Vector<Vec3>& groundTruth) const;
    f32 luminance(Vec3 color) const;
    void applyTemporalDithering(f32 amount);
    void computeTemporalVariance();
    void getDenoiserStats(f32& noiseLevel, f32& edgePreservation, u32& featuresExtracted) const;
    Vector<Vec3> getFeatureMaps(u32 layer) const;
    f32 computeNetworkComplexity() const;
};

} // namespace Frost
