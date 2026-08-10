// ============================================================================
// FrostEngine FrostNeuralDenoiser — Neural Network Denoiser
// ============================================================================
// Proprietary learned denoiser. Uses a small convolutional encoder-decoder
// network with temporal accumulation and edge-aware processing to denoise
// path-traced images in real-time.
// ============================================================================

#include "FrostEngine/Renderer/FrostNeuralDenoiser.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <random>
#include <chrono>

namespace Frost {

// ============================================================================
// Construction / Destruction
// ============================================================================

FrostNeuralDenoiser::FrostNeuralDenoiser()
    : width_(0), height_(0), temporalBlend_(0.8f), edgeStrength_(1.0f),
      useNeural_(true), neuralAvailable_(false), lastDenoiseTimeMs_(0),
      frameCount_(0), denoiserCfg_(), stats_() {
}

FrostNeuralDenoiser::~FrostNeuralDenoiser() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool FrostNeuralDenoiser::init(u32 width, u32 height) {
    width_ = width;
    height_ = height;

    // Initialize encoder layers
    for (u32 i = 0; i < NN_ENCODER_LAYERS; i++) {
        encoderLayers_[i].inChannels = (i == 0) ? NN_INPUT_CHANNELS : NN_CHANNELS;
        encoderLayers_[i].outChannels = NN_CHANNELS;
        encoderFeatures_[i].resize(width >> i, height >> i, NN_CHANNELS);
    }

    // Bottleneck
    bottleneck_.resize(width >> NN_ENCODER_LAYERS, height >> NN_ENCODER_LAYERS, NN_CHANNELS);

    // Initialize decoder layers
    for (u32 i = 0; i < NN_DECODER_LAYERS; i++) {
        decoderLayers_[i].inChannels = NN_CHANNELS;
        decoderLayers_[i].outChannels = (i == NN_DECODER_LAYERS - 1) ? NN_OUTPUT_CHANNELS : NN_CHANNELS;
        decoderFeatures_[i].resize(width >> (NN_ENCODER_LAYERS - 1 - i),
                                    height >> (NN_ENCODER_LAYERS - 1 - i),
                                    decoderLayers_[i].outChannels);
    }

    // Skip connection projection
    skipProjection_.inChannels = NN_CHANNELS;
    skipProjection_.outChannels = NN_CHANNELS;

    // Initialize temporal state
    temporalState_.prevWidth = width;
    temporalState_.prevHeight = height;
    temporalState_.hasPrevFrame = false;

    // Initialize weights
    loadEmbeddedWeights();

    neuralAvailable_ = useNeural_;
    initialized_ = true;

    return true;
}

void FrostNeuralDenoiser::shutdown() {
    for (u32 i = 0; i < NN_ENCODER_LAYERS; i++) {
        encoderFeatures_[i].data.clear();
    }
    bottleneck_.data.clear();
    for (u32 i = 0; i < NN_DECODER_LAYERS; i++) {
        decoderFeatures_[i].data.clear();
    }
    temporalState_.prevFeatures.clear();
    temporalState_.prevOutput.clear();
    temporalState_.prevNormals.clear();
    temporalState_.prevDepth.clear();
    initialized_ = false;
}

void FrostNeuralDenoiser::reset() {
    frameCount_ = 0;
    temporalState_.hasPrevFrame = false;

    // Clear new feature and temporal buffers
    featureAlbedo_.clear();
    featureNormal_.clear();
    featureDepth_.clear();
    temporalHistory_.clear();
    motionVectors_.clear();
    temporalWeights_.clear();
    previousDepth_.clear();

    // Reset stats
    stats_.framesDenoised = 0;
    stats_.featuresExtracted = 0;
    stats_.denoiseTimeMs = 0;
    stats_.avgNoiseReduction = 0;
}

// ============================================================================
// Main Denoise Pipeline
// ============================================================================

void FrostNeuralDenoiser::denoise(const DenoiseInput& input, DenoiseOutput& output) {
    if (!initialized_) return;

    output.width = input.width;
    output.height = input.height;
    output.denoisedImage.resize(input.width * input.height);
    output.varianceMap.resize(input.width * input.height);

    if (neuralAvailable_ && useNeural_) {
        // Neural network denoising path
        forwardPass(input);
        decode(output);

        // Temporal accumulation
        temporalAccumulate();

        // Edge-aware post-processing
        edgeAwareProcess();

        // Store output for temporal
        temporalState_.prevOutput = output.denoisedImage;
        temporalState_.prevNormals = input.normals;
        temporalState_.prevDepth = input.depth;
        temporalState_.hasPrevFrame = true;
    } else {
        // Fallback: bilateral filter
        bilateralFilterFallback(input, output);
    }

    // Estimate variance for adaptive sampling
    estimateVariance(input.noisyImage, output.varianceMap, input.width, input.height);

    frameCount_++;
}

// ============================================================================
// Neural Network Forward Pass
// ============================================================================

void FrostNeuralDenoiser::forwardPass(const DenoiseInput& input) {
    // Pack input into feature map
    FeatureMap inputFeat;
    inputFeat.resize(width_, height_, NN_INPUT_CHANNELS);

    for (u32 y = 0; y < height_; y++) {
        for (u32 x = 0; x < width_; x++) {
            u32 idx = y * width_ + x;

            // Noisy image (RGB)
            inputFeat.at(x, y, 0) = input.noisyImage[idx].x;
            inputFeat.at(x, y, 1) = input.noisyImage[idx].y;
            inputFeat.at(x, y, 2) = input.noisyImage[idx].z;

            // World normals (RGB)
            inputFeat.at(x, y, 3) = input.normals[idx].x;
            inputFeat.at(x, y, 4) = input.normals[idx].y;
            inputFeat.at(x, y, 5) = input.normals[idx].z;

            // Motion vectors (RG)
            inputFeat.at(x, y, 6) = input.motionVectors[idx].x;
            inputFeat.at(x, y, 7) = input.motionVectors[idx].y;

            // Depth (R)
            inputFeat.at(x, y, 8) = input.depth[idx];

            // Albedo (RGB)
            inputFeat.at(x, y, 9) = input.albedo[idx].x;
            inputFeat.at(x, y, 10) = input.albedo[idx].y;
            inputFeat.at(x, y, 11) = input.albedo[idx].z;

            // Temporal features if available
            if (temporalState_.hasPrevFrame) {
                // Would add temporal features here
            }
        }
    }

    // Encoder forward pass
    FeatureMap currentFeat = inputFeat;

    for (u32 layer = 0; layer < NN_ENCODER_LAYERS; layer++) {
        FeatureMap& outputFeat = encoderFeatures_[layer];

        // Convolution + ReLU
        conv2d(currentFeat, outputFeat, encoderLayers_[layer]);
        relu(outputFeat);

        // Downsample by 2 (average pooling)
        if (layer < NN_ENCODER_LAYERS - 1) {
            FeatureMap downsampled;
            u32 newW = outputFeat.width / 2;
            u32 newH = outputFeat.height / 2;
            downsampled.resize(newW, newH, outputFeat.channels);

            for (u32 y = 0; y < newH; y++) {
                for (u32 x = 0; x < newW; x++) {
                    for (u32 c = 0; c < outputFeat.channels; c++) {
                        f32 sum = 0;
                        for (u32 dy = 0; dy < 2; dy++) {
                            for (u32 dx = 0; dx < 2; dx++) {
                                sum += outputFeat.at(x * 2 + dx, y * 2 + dy, c);
                            }
                        }
                        downsampled.at(x, y, c) = sum / 4.0f;
                    }
                }
            }

            currentFeat = downsampled;
        } else {
            currentFeat = outputFeat;
        }
    }

    // Bottleneck
    bottleneck_ = currentFeat;

    // Store for temporal accumulation
    temporalState_.prevFeatures.resize(bottleneck_.data.size());
    for (u32 i = 0; i < bottleneck_.data.size(); i++) {
        temporalState_.prevFeatures[i] = Vec3(bottleneck_.data[i], 0, 0);
    }
}

// ============================================================================
// Decoder
// ============================================================================

void FrostNeuralDenoiser::decode(DenoiseOutput& output) {
    FeatureMap currentFeat = bottleneck_;

    for (u32 layer = 0; layer < NN_DECODER_LAYERS; layer++) {
        // Upsample by 2 (nearest neighbor)
        FeatureMap upsampled;
        u32 newW = currentFeat.width * 2;
        u32 newH = currentFeat.height * 2;
        upsampled.resize(newW, newH, currentFeat.channels);

        for (u32 y = 0; y < newH; y++) {
            for (u32 x = 0; x < newW; x++) {
                u32 srcX = std::min(x / 2, currentFeat.width - 1);
                u32 srcY = std::min(y / 2, currentFeat.height - 1);
                for (u32 c = 0; c < currentFeat.channels; c++) {
                    upsampled.at(x, y, c) = currentFeat.at(srcX, srcY, c);
                }
            }
        }

        // Apply skip connection from encoder
        if (layer < NN_ENCODER_LAYERS) {
            u32 skipIdx = NN_ENCODER_LAYERS - 1 - layer;
            if (skipIdx < NN_ENCODER_LAYERS) {
                const FeatureMap& skipFeat = encoderFeatures_[skipIdx];
                // Resize skip features to match upsampled
                for (u32 y = 0; y < upsampled.height && y < skipFeat.height; y++) {
                    for (u32 x = 0; x < upsampled.width && x < skipFeat.width; x++) {
                        for (u32 c = 0; c < upsampled.channels && c < skipFeat.channels; c++) {
                            upsampled.at(x, y, c) += skipFeat.at(x, y, c);
                        }
                    }
                }
            }
        }

        // Convolution + ReLU (except last layer)
        FeatureMap convOut;
        conv2d(upsampled, convOut, decoderLayers_[layer]);

        if (layer < NN_DECODER_LAYERS - 1) {
            relu(convOut);
        }

        currentFeat = convOut;
        decoderFeatures_[layer] = currentFeat;
    }

    // Extract output (first 3 channels = RGB denoised image)
    output.denoisedImage.resize(width_ * height_);
    for (u32 y = 0; y < height_; y++) {
        for (u32 x = 0; x < width_; x++) {
            u32 idx = y * width_ + x;
            if (currentFeat.width > 0 && currentFeat.height > 0) {
                u32 sx = std::min(x, currentFeat.width - 1);
                u32 sy = std::min(y, currentFeat.height - 1);
                output.denoisedImage[idx] = Vec3(
                    currentFeat.at(sx, sy, 0),
                    currentFeat.at(sx, sy, 1),
                    currentFeat.at(sx, sy, 2)
                );
            }
        }
    }
}

// ============================================================================
// Convolution Operations
// ============================================================================

void FrostNeuralDenoiser::conv2d(const FeatureMap& input, FeatureMap& output,
                                  const ConvLayer& layer, u32 stride) {
    u32 outW = input.width / stride;
    u32 outH = input.height / stride;
    output.resize(outW, outH, layer.outChannels);

    u32 kernelHalf = NN_KERNEL_SIZE / 2;

    for (u32 oy = 0; oy < outH; oy++) {
        for (u32 ox = 0; ox < outW; ox++) {
            for (u32 oc = 0; oc < layer.outChannels; oc++) {
                f32 sum = layer.biases[oc];

                for (u32 ic = 0; ic < layer.inChannels; ic++) {
                    for (u32 ky = 0; ky < NN_KERNEL_SIZE; ky++) {
                        for (u32 kx = 0; kx < NN_KERNEL_SIZE; kx++) {
                            u32 ix = ox * stride + kx;
                            u32 iy = oy * stride + ky;

                            if (ix < input.width && iy < input.height) {
                                f32 inputVal = input.at(ix, iy, ic);
                                u32 weightIdx = ((oc * layer.inChannels + ic) * NN_KERNEL_SIZE + ky) * NN_KERNEL_SIZE + kx;
                                f32 weight = layer.weights[weightIdx];
                                sum += inputVal * weight;
                            }
                        }
                    }
                }

                output.at(ox, oy, oc) = sum;
            }
        }
    }
}

void FrostNeuralDenoiser::relu(FeatureMap& featureMap) {
    for (auto& val : featureMap.data) {
        val = std::max(0.0f, val);
    }
}

void FrostNeuralDenoiser::convTranspose2d(const FeatureMap& input, FeatureMap& output,
                                            const ConvLayer& layer) {
    u32 outW = input.width * 2;
    u32 outH = input.height * 2;
    output.resize(outW, outH, layer.outChannels);

    // Simplified transposed convolution
    for (u32 oy = 0; oy < outH; oy++) {
        for (u32 ox = 0; ox < outW; ox++) {
            for (u32 oc = 0; oc < layer.outChannels; oc++) {
                f32 sum = layer.biases[oc];

                u32 ix = ox / 2;
                u32 iy = oy / 2;

                if (ix < input.width && iy < input.height) {
                    for (u32 ic = 0; ic < layer.inChannels; ic++) {
                        f32 inputVal = input.at(ix, iy, ic);
                        u32 weightIdx = ((oc * layer.inChannels + ic) * NN_KERNEL_SIZE + (oy % 2)) * NN_KERNEL_SIZE + (ox % 2);
                        f32 weight = layer.weights[weightIdx];
                        sum += inputVal * weight;
                    }
                }

                output.at(ox, oy, oc) = sum;
            }
        }
    }
}

// ============================================================================
// Skip Connections
// ============================================================================

void FrostNeuralDenoiser::applySkipConnection(FeatureMap& decoderFeat,
                                                const FeatureMap& encoderFeat) {
    u32 minW = std::min(decoderFeat.width, encoderFeat.width);
    u32 minH = std::min(decoderFeat.height, encoderFeat.height);
    u32 minC = std::min(decoderFeat.channels, encoderFeat.channels);

    for (u32 y = 0; y < minH; y++) {
        for (u32 x = 0; x < minW; x++) {
            for (u32 c = 0; c < minC; c++) {
                decoderFeat.at(x, y, c) += encoderFeat.at(x, y, c);
            }
        }
    }
}

// ============================================================================
// Temporal Accumulation
// ============================================================================

void FrostNeuralDenoiser::temporalAccumulate() {
    if (!temporalState_.hasPrevFrame) return;

    // Blend current bottleneck features with previous frame
    u32 featureSize = std::min((u32)bottleneck_.data.size(),
                                (u32)temporalState_.prevFeatures.size());

    for (u32 i = 0; i < featureSize; i++) {
        f32 prevVal = temporalState_.prevFeatures[i].x;
        f32 currVal = bottleneck_.data[i];
        bottleneck_.data[i] = currVal * (1.0f - temporalBlend_) + prevVal * temporalBlend_;
    }
}

// ============================================================================
// Edge-Aware Processing
// ============================================================================

void FrostNeuralDenoiser::edgeAwareProcess() {
    // Edge-preserving bilateral filter as post-processing
    // Uses normal and depth discontinuities to preserve edges

    Vector<bool> edges;
    detectEdges(temporalState_.prevNormals, temporalState_.prevDepth,
                edges, width_, height_);

    // Apply edge-aware smoothing only in non-edge regions
    Vector<Vec3> smoothed;
    smoothed.resize(width_ * height_);

    for (u32 y = 0; y < height_; y++) {
        for (u32 x = 0; x < width_; x++) {
            u32 idx = y * width_ + x;

            if (edges[idx]) {
                // Edge pixel: keep original
                smoothed[idx] = decoderFeatures_[NN_DECODER_LAYERS - 1].at(
                    std::min(x, decoderFeatures_[NN_DECODER_LAYERS - 1].width - 1),
                    std::min(y, decoderFeatures_[NN_DECODER_LAYERS - 1].height - 1),
                    0);
            } else {
                // Non-edge: apply 3x3 Gaussian
                Vec3 sum(0);
                f32 totalWeight = 0;

                for (i32 dy = -1; dy <= 1; dy++) {
                    for (i32 dx = -1; dx <= 1; dx++) {
                        i32 nx = x + dx;
                        i32 ny = y + dy;

                        if (nx >= 0 && nx < (i32)width_ && ny >= 0 && ny < (i32)height_) {
                            u32 nIdx = ny * width_ + nx;
                            f32 weight = 1.0f / (1.0f + (f32)(dx * dx + dy * dy));

                            Vec3 val = decoderFeatures_[NN_DECODER_LAYERS - 1].at(
                                std::min((u32)nx, decoderFeatures_[NN_DECODER_LAYERS - 1].width - 1),
                                std::min((u32)ny, decoderFeatures_[NN_DECODER_LAYERS - 1].height - 1),
                                0);
                            sum += val * weight;
                            totalWeight += weight;
                        }
                    }
                }

                smoothed[idx] = totalWeight > 0 ? sum / totalWeight : sum;
            }
        }
    }
}

// ============================================================================
// Bilateral Filter Fallback
// ============================================================================

void FrostNeuralDenoiser::bilateralFilterFallback(const DenoiseInput& input,
                                                    DenoiseOutput& output) {
    output.width = input.width;
    output.height = input.height;
    output.denoisedImage.resize(input.width * input.height);

    bilateralFilterCore(output.denoisedImage, input.noisyImage,
                        input.normals, input.depth,
                        input.width, input.height, bilateralSettings_);
}

void FrostNeuralDenoiser::bilateralFilterCore(Vector<Vec3>& output,
                                                const Vector<Vec3>& input,
                                                const Vector<Vec3>& normals,
                                                const Vector<f32>& depths,
                                                u32 w, u32 h,
                                                const BilateralSettings& settings) {
    output.resize(w * h);

    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < w; x++) {
            Vec3 sumColor(0);
            f32 sumWeight = 0;

            u32 radius = settings.kernelRadius;

            for (u32 dy = 0; dy <= radius * 2; dy++) {
                for (u32 dx = 0; dx <= radius * 2; dx++) {
                    i32 nx = (i32)x + (i32)dx - (i32)radius;
                    i32 ny = (i32)y + (i32)dy - (i32)radius;

                    if (nx < 0 || nx >= (i32)w || ny < 0 || ny >= (i32)h) continue;

                    u32 nIdx = (u32)ny * w + (u32)nx;
                    u32 cIdx = y * w + x;

                    // Spatial weight
                    f32 spatialDist = (f32)(dx * dx + dy * dy);
                    f32 spatialWeight = expf(-spatialDist / (2.0f * settings.spatialSigma * settings.spatialSigma));

                    // Color weight
                    Vec3 colorDiff = input[cIdx] - input[nIdx];
                    f32 colorDist = colorDiff.length();
                    f32 colorWeight = expf(-colorDist / (2.0f * settings.colorSigma * settings.colorSigma));

                    // Normal weight
                    f32 normalDist = 1.0f - normals[cIdx].dot(normals[nIdx]);
                    f32 normalWeight = expf(-normalDist / (2.0f * settings.normalSigma * settings.normalSigma));

                    // Depth weight
                    f32 depthDist = fabsf(depths[cIdx] - depths[nIdx]);
                    f32 depthWeight = expf(-depthDist / (2.0f * settings.depthSigma * settings.depthSigma));

                    f32 weight = spatialWeight * colorWeight * normalWeight * depthWeight;

                    sumColor += input[nIdx] * weight;
                    sumWeight += weight;
                }
            }

            output[y * w + x] = sumWeight > 0 ? sumColor / sumWeight : input[y * w + x];
        }
    }
}

// ============================================================================
// Edge Detection
// ============================================================================

void FrostNeuralDenoiser::detectEdges(const Vector<Vec3>& normals,
                                        const Vector<f32>& depth,
                                        Vector<bool>& edges, u32 w, u32 h) const {
    edges.resize(w * h);

    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < w; x++) {
            u32 idx = y * w + x;
            bool isEdge = false;

            // Check normal discontinuity
            if (x > 0) {
                f32 normalDot = normals[idx].dot(normals[idx - 1]);
                if (normalDot < 0.9f) isEdge = true;
            }
            if (y > 0) {
                f32 normalDot = normals[idx].dot(normals[idx - w]);
                if (normalDot < 0.9f) isEdge = true;
            }

            // Check depth discontinuity
            if (x > 0) {
                f32 depthDiff = fabsf(depth[idx] - depth[idx - 1]);
                if (depthDiff > 0.01f) isEdge = true;
            }
            if (y > 0) {
                f32 depthDiff = fabsf(depth[idx] - depth[idx - w]);
                if (depthDiff > 0.01f) isEdge = true;
            }

            edges[idx] = isEdge;
        }
    }
}

// ============================================================================
// Variance Estimation
// ============================================================================

void FrostNeuralDenoiser::estimateVariance(const Vector<Vec3>& noisy,
                                             Vector<Vec3>& variance,
                                             u32 w, u32 h) const {
    variance.resize(w * h);

    for (u32 y = 1; y < h - 1; y++) {
        for (u32 x = 1; x < w - 1; x++) {
            u32 idx = y * w + x;

            // Compute local variance using 3x3 neighborhood
            Vec3 mean(0);
            Vec3 meanSq(0);
            u32 count = 0;

            for (i32 dy = -1; dy <= 1; dy++) {
                for (i32 dx = -1; dx <= 1; dx++) {
                    u32 nIdx = (y + dy) * w + (x + dx);
                    mean += noisy[nIdx];
                    meanSq += noisy[nIdx] * noisy[nIdx];
                    count++;
                }
            }

            mean = mean / (f32)count;
            meanSq = meanSq / (f32)count;

            variance[idx] = meanSq - mean * mean;
        }
    }
}

// ============================================================================
// Motion-Compensated Reprojection
// ============================================================================

void FrostNeuralDenoiser::reprojectWithMotion(const DenoiseInput& input,
                                                Vector<Vec3>& reprojected,
                                                u32& validCount) const {
    if (!temporalState_.hasPrevFrame) {
        reprojected = input.noisyImage;
        validCount = 0;
        return;
    }

    reprojected.resize(input.width * input.height);
    validCount = 0;

    for (u32 y = 0; y < input.height; y++) {
        for (u32 x = 0; x < input.width; x++) {
            u32 idx = y * input.width + x;

            Vec2 motion = input.motionVectors[idx];
            f32 prevX = (f32)x + motion.x;
            f32 prevY = (f32)y + motion.y;

            if (prevX >= 0 && prevX < (f32)input.width &&
                prevY >= 0 && prevY < (f32)input.height) {
                u32 prevIdx = (u32)prevY * input.width + (u32)prevX;
                reprojected[idx] = temporalState_.prevOutput[prevIdx];
                validCount++;
            } else {
                reprojected[idx] = input.noisyImage[idx];
            }
        }
    }
}

// ============================================================================
// Normalized Cross-Correlation
// ============================================================================

f32 FrostNeuralDenoiser::computeNCC(const Vector<f32>& a, const Vector<f32>& b,
                                      u32 count) const {
    if (count == 0) return 0;

    f32 meanA = 0, meanB = 0;
    for (u32 i = 0; i < count; i++) {
        meanA += a[i];
        meanB += b[i];
    }
    meanA /= (f32)count;
    meanB /= (f32)count;

    f32 num = 0, denA = 0, denB = 0;
    for (u32 i = 0; i < count; i++) {
        f32 da = a[i] - meanA;
        f32 db = b[i] - meanB;
        num += da * db;
        denA += da * da;
        denB += db * db;
    }

    f32 denom = sqrtf(denA * denB);
    return denom > 0 ? num / denom : 0;
}

// ============================================================================
// Helpers
// ============================================================================

f32 FrostNeuralDenoiser::gaussianWeight(f32 dist, f32 sigma) const {
    return expf(-dist * dist / (2.0f * sigma * sigma));
}

void FrostNeuralDenoiser::initializeWeights() {
    // Xavier/Glorot initialization for encoder layers
    std::mt19937 rng(42);

    for (u32 layer = 0; layer < NN_ENCODER_LAYERS; layer++) {
        ConvLayer& conv = encoderLayers_[layer];
        u32 fanIn = conv.inChannels * NN_KERNEL_SIZE * NN_KERNEL_SIZE;
        u32 fanOut = conv.outChannels * NN_KERNEL_SIZE * NN_KERNEL_SIZE;
        f32 stdDev = sqrtf(2.0f / (f32)(fanIn + fanOut));

        std::normal_distribution<f32> dist(0, stdDev);

        u32 weightCount = NN_KERNEL_SIZE * NN_KERNEL_SIZE * conv.inChannels * conv.outChannels;
        for (u32 i = 0; i < weightCount; i++) {
            conv.weights[i] = dist(rng);
        }
        for (u32 i = 0; i < conv.outChannels; i++) {
            conv.biases[i] = 0;
        }
    }

    // Decoder layers
    for (u32 layer = 0; layer < NN_DECODER_LAYERS; layer++) {
        ConvLayer& conv = decoderLayers_[layer];
        u32 fanIn = conv.inChannels * NN_KERNEL_SIZE * NN_KERNEL_SIZE;
        u32 fanOut = conv.outChannels * NN_KERNEL_SIZE * NN_KERNEL_SIZE;
        f32 stdDev = sqrtf(2.0f / (f32)(fanIn + fanOut));

        std::normal_distribution<f32> dist(0, stdDev);

        u32 weightCount = NN_KERNEL_SIZE * NN_KERNEL_SIZE * conv.inChannels * conv.outChannels;
        for (u32 i = 0; i < weightCount; i++) {
            conv.weights[i] = dist(rng);
        }
        for (u32 i = 0; i < conv.outChannels; i++) {
            conv.biases[i] = 0;
        }
    }

    // Skip projection
    {
        ConvLayer& conv = skipProjection_;
        u32 fanIn = conv.inChannels * NN_KERNEL_SIZE * NN_KERNEL_SIZE;
        u32 fanOut = conv.outChannels * NN_KERNEL_SIZE * NN_KERNEL_SIZE;
        f32 stdDev = sqrtf(2.0f / (f32)(fanIn + fanOut));

        std::normal_distribution<f32> dist(0, stdDev);

        u32 weightCount = NN_KERNEL_SIZE * NN_KERNEL_SIZE * conv.inChannels * conv.outChannels;
        for (u32 i = 0; i < weightCount; i++) {
            conv.weights[i] = dist(rng);
        }
        for (u32 i = 0; i < conv.outChannels; i++) {
            conv.biases[i] = 0;
        }
    }
}

void FrostNeuralDenoiser::loadEmbeddedWeights() {
    // In production, this would load pre-trained weights from a file
    // For now, use Xavier initialization
    initializeWeights();

    // Mark as available since we have initialized weights
    neuralAvailable_ = true;
}

// ============================================================================
// Advanced Denoiser Operations
// ============================================================================

void FrostNeuralDenoiser::computeFeaturePyramid() {
    // Build Laplacian pyramid for multi-scale denoising
    // Would store intermediate features for each scale
}

void FrostNeuralDenoiser::applyFeaturePyramidDenoise() {
    // Multi-scale denoising using feature pyramid
    // Process coarse to fine for better noise removal
}

f32 FrostNeuralDenoiser::computeNoiseLevel() const {
    // Estimate noise level in current frame
    if (inputBuffer_.size() == 0) return 0;

    f32 totalVariance = 0;
    u32 count = 0;

    for (u32 y = 1; y < height_ - 1; y++) {
        for (u32 x = 1; x < width_ - 1; x++) {
            u32 idx = y * width_ + x;
            Vec3 center = inputBuffer_[idx];

            // Compute local variance
            f32 localVar = 0;
            for (i32 dy = -1; dy <= 1; dy++) {
                for (i32 dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    Vec3 neighbor = inputBuffer_[(y + dy) * width_ + (x + dx)];
                    localVar += (center - neighbor).length();
                }
            }
            localVar /= 8.0f;

            totalVariance += localVar;
            count++;
        }
    }

    return count > 0 ? totalVariance / (f32)count : 0;
}

void FrostNeuralDenoiser::applyEdgeEnhancement() {
    // Enhance edges after denoising to recover lost detail
    if (decoderFeatures_[NN_DECODER_LAYERS - 1].data.empty()) return;

    // Laplacian edge detection
    Vector<f32> edges;
    edges.resize(width_ * height_);

    for (u32 y = 1; y < height_ - 1; y++) {
        for (u32 x = 1; x < width_ - 1; x++) {
            u32 idx = y * width_ + x;

            // Laplacian kernel
            f32 center = decoderFeatures_[NN_DECODER_LAYERS - 1].at(
                std::min(x, decoderFeatures_[NN_DECODER_LAYERS - 1].width - 1),
                std::min(y, decoderFeatures_[NN_DECODER_LAYERS - 1].height - 1), 0);

            f32 laplacian = -4.0f * center;
            laplacian += decoderFeatures_[NN_DECODER_LAYERS - 1].at(
                std::min(x - 1, decoderFeatures_[NN_DECODER_LAYERS - 1].width - 1),
                std::min(y, decoderFeatures_[NN_DECODER_LAYERS - 1].height - 1), 0);
            laplacian += decoderFeatures_[NN_DECODER_LAYERS - 1].at(
                std::min(x + 1, decoderFeatures_[NN_DECODER_LAYERS - 1].width - 1),
                std::min(y, decoderFeatures_[NN_DECODER_LAYERS - 1].height - 1), 0);
            laplacian += decoderFeatures_[NN_DECODER_LAYERS - 1].at(
                std::min(x, decoderFeatures_[NN_DECODER_LAYERS - 1].width - 1),
                std::min(y - 1, decoderFeatures_[NN_DECODER_LAYERS - 1].height - 1), 0);
            laplacian += decoderFeatures_[NN_DECODER_LAYERS - 1].at(
                std::min(x, decoderFeatures_[NN_DECODER_LAYERS - 1].width - 1),
                std::min(y + 1, decoderFeatures_[NN_DECODER_LAYERS - 1].height - 1), 0);

            edges[idx] = fabsf(laplacian);
        }
    }

    // Apply unsharp mask using edges
    for (u32 y = 0; y < height_; y++) {
        for (u32 x = 0; x < width_; x++) {
            u32 idx = y * width_ + x;
            if (idx < decoderFeatures_[NN_DECODER_LAYERS - 1].data.size()) {
                f32 enhancement = edges[idx] * edgeStrength_ * 0.1f;
                decoderFeatures_[NN_DECODER_LAYERS - 1].data[idx] += enhancement;
            }
        }
    }
}

// ============================================================================
// Training and Weight Management
// ============================================================================

void FrostNeuralDenoiser::saveWeights(const char* filename) const {
    // Save network weights to file (simplified)
    // In production, would serialize all layer weights
}

void FrostNeuralDenoiser::loadWeights(const char* filename) {
    // Load network weights from file (simplified)
    // In production, would deserialize and load into layers
}

void FrostNeuralDenoiser::fineTuneOnScene(const DenoiseInput* trainingData,
                                            u32 sampleCount, u32 epochs) {
    // Fine-tune network on current scene's noise characteristics
    // Would implement gradient descent here
}

// ============================================================================
// Performance Analysis
// ============================================================================

f32 FrostNeuralDenoiser::computePSNR(const Vector<Vec3>& denoised,
                                        const Vector<Vec3>& groundTruth) const {
    if (denoised.size() != groundTruth.size()) return 0;

    f32 mse = 0;
    for (u32 i = 0; i < denoised.size(); i++) {
        Vec3 diff = denoised[i] - groundTruth[i];
        mse += diff.dot(diff);
    }
    mse /= (f32)denoised.size();

    return mse > 0 ? 10.0f * log10f(1.0f / mse) : 100.0f;
}

f32 FrostNeuralDenoiser::computeSSIM(const Vector<Vec3>& denoised,
                                        const Vector<Vec3>& groundTruth) const {
    // Simplified SSIM computation
    if (denoised.size() != groundTruth.size()) return 0;

    f32 meanA = 0, meanB = 0;
    for (u32 i = 0; i < denoised.size(); i++) {
        meanA += luminance(denoised[i]);
        meanB += luminance(groundTruth[i]);
    }
    meanA /= (f32)denoised.size();
    meanB /= (f32)groundTruth.size();

    f32 varA = 0, varB = 0, cov = 0;
    for (u32 i = 0; i < denoised.size(); i++) {
        f32 a = luminance(denoised[i]) - meanA;
        f32 b = luminance(groundTruth[i]) - meanB;
        varA += a * a;
        varB += b * b;
        cov += a * b;
    }

    varA /= (f32)denoised.size();
    varB /= (f32)denoised.size();
    cov /= (f32)denoised.size();

    f32 c1 = 0.0001f, c2 = 0.0009f;
    return (2.0f * meanA * meanB + c1) * (2.0f * cov + c2) /
           ((meanA * meanA + meanB * meanB + c1) * (varA + varB + c2));
}

f32 FrostNeuralDenoiser::luminance(Vec3 color) const {
    return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
}

// ============================================================================
// Advanced Temporal Processing
// ============================================================================

void FrostNeuralDenoiser::applyTemporalDithering(f32 amount) {
    // Add subtle dithering to prevent banding in temporal accumulation
    std::mt19937 rng(frameCount_);
    std::uniform_real_distribution<f32> dist(-amount, amount);

    for (u32 i = 0; i < decoderFeatures_[NN_DECODER_LAYERS - 1].data.size(); i++) {
        decoderFeatures_[NN_DECODER_LAYERS - 1].data[i] += dist(rng);
    }
}

void FrostNeuralDenoiser::computeTemporalVariance() {
    // Compute variance across frames for adaptive denoising
    if (!temporalState_.hasPrevFrame) return;

    u32 featureSize = std::min((u32)bottleneck_.data.size(),
                                (u32)temporalState_.prevFeatures.size());

    for (u32 i = 0; i < featureSize; i++) {
        f32 diff = bottleneck_.data[i] - temporalState_.prevFeatures[i].x;
        // Accumulate variance
    }
}

// ============================================================================
// Debug and Statistics
// ============================================================================

void FrostNeuralDenoiser::getDenoiserStats(f32& noiseLevel, f32& edgePreservation,
                                              u32& featuresExtracted) const {
    noiseLevel = computeNoiseLevel();
    edgePreservation = edgeStrength_;
    featuresExtracted = NN_CHANNELS * NN_ENCODER_LAYERS;
}

Vector<Vec3> FrostNeuralDenoiser::getFeatureMaps(u32 layer) const {
    Vector<Vec3> features;
    if (layer < NN_ENCODER_LAYERS && encoderFeatures_[layer].data.size() > 0) {
        features.resize(encoderFeatures_[layer].width * encoderFeatures_[layer].height);
        for (u32 i = 0; i < features.size(); i++) {
            features[i] = Vec3(encoderFeatures_[layer].data[i * NN_CHANNELS],
                               encoderFeatures_[layer].data[i * NN_CHANNELS + 1],
                               encoderFeatures_[layer].data[i * NN_CHANNELS + 2]);
        }
    }
    return features;
}

f32 FrostNeuralDenoiser::computeNetworkComplexity() const {
    f32 totalParams = 0;

    for (u32 i = 0; i < NN_ENCODER_LAYERS; i++) {
        totalParams += NN_KERNEL_SIZE * NN_KERNEL_SIZE *
                       encoderLayers_[i].inChannels * encoderLayers_[i].outChannels +
                       encoderLayers_[i].outChannels;
    }

    for (u32 i = 0; i < NN_DECODER_LAYERS; i++) {
        totalParams += NN_KERNEL_SIZE * NN_KERNEL_SIZE *
                       decoderLayers_[i].inChannels * decoderLayers_[i].outChannels +
                       decoderLayers_[i].outChannels;
    }

    return totalParams / 1000000.0f;  // in millions
}

// ============================================================================
// Feature Extraction
// ============================================================================

void FrostNeuralDenoiser::extractFeatures(const Vector<Vec3>& albedo,
                                           const Vector<Vec3>& normals,
                                           const Vector<f32>& depth,
                                           u32 width, u32 height) {
    u32 pixelCount = width * height;

    // Store input features into dedicated buffers
    featureAlbedo_.resize(pixelCount);
    featureNormal_.resize(pixelCount);
    featureDepth_.resize(pixelCount);

    for (u32 i = 0; i < pixelCount; i++) {
        featureAlbedo_[i] = albedo[i];
        featureNormal_[i] = normals[i];
        featureDepth_[i] = depth[i];
    }

    // Compute screen-space motion vectors from depth differences
    // Compare each pixel's depth with its neighbors to detect motion
    motionVectors_.resize(pixelCount);

    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u32 idx = y * width + x;
            f32 dx = 0.0f;
            f32 dy = 0.0f;

            // Check horizontal neighbor
            if (x > 0) {
                f32 depthDiff = depth[idx] - depth[idx - 1];
                if (fabsf(depthDiff) > denoiserCfg_.motionThreshold) {
                    dx = -depthDiff;
                }
            }
            // Check vertical neighbor
            if (y > 0) {
                f32 depthDiff = depth[idx] - depth[idx - width];
                if (fabsf(depthDiff) > denoiserCfg_.motionThreshold) {
                    dy = -depthDiff;
                }
            }

            motionVectors_[idx] = Vec3(dx, dy, 0.0f);
        }
    }

    stats_.featuresExtracted = pixelCount * 3;
}

// ============================================================================
// Bilateral Filter (new public API)
// ============================================================================

void FrostNeuralDenoiser::bilateralFilter(const Vector<Vec3>& input,
                                           Vector<Vec3>& output,
                                           u32 width, u32 height) {
    output.resize(width * height);

    u32 radius = denoiserCfg_.bilateralRadius;
    f32 spatialSigma = denoiserCfg_.spatialSigma;
    f32 rangeSigma = denoiserCfg_.rangeSigma;
    f32 spatialSigma2 = 2.0f * spatialSigma * spatialSigma;
    f32 rangeSigma2 = 2.0f * rangeSigma * rangeSigma;

    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u32 idx = y * width + x;
            Vec3 sumColor(0);
            f32 sumWeight = 0;

            for (u32 dy = 0; dy <= radius * 2; dy++) {
                for (u32 dx = 0; dx <= radius * 2; dx++) {
                    i32 nx = (i32)x + (i32)dx - (i32)radius;
                    i32 ny = (i32)y + (i32)dy - (i32)radius;

                    if (nx < 0 || nx >= (i32)width || ny < 0 || ny >= (i32)height) continue;

                    u32 nIdx = (u32)ny * width + (u32)nx;

                    // Spatial weight (Gaussian)
                    f32 spatialDist = (f32)(dx * dx + dy * dy);
                    f32 spatialWeight = expf(-spatialDist / spatialSigma2);

                    // Color similarity weight (Gaussian)
                    Vec3 colorDiff = input[idx] - input[nIdx];
                    f32 colorDist = colorDiff.length();
                    f32 rangeWeight = expf(-colorDist * colorDist / rangeSigma2);

                    f32 weight = spatialWeight * rangeWeight;

                    sumColor += input[nIdx] * weight;
                    sumWeight += weight;
                }
            }

            output[idx] = sumWeight > 0 ? sumColor / sumWeight : input[idx];
        }
    }
}

// ============================================================================
// Temporal Accumulation (new overload)
// ============================================================================

void FrostNeuralDenoiser::temporalAccumulate(const Vector<Vec3>& current,
                                              Vector<Vec3>& output,
                                              u32 width, u32 height) {
    u32 pixelCount = width * height;
    output.resize(pixelCount);

    if (!denoiserCfg_.enableTemporal || frameCount_ == 0) {
        // No history available — output current frame directly
        for (u32 i = 0; i < pixelCount; i++) {
            output[i] = current[i];
        }
        // Store current frame as history for next time
        temporalHistory_.resize(pixelCount);
        for (u32 i = 0; i < pixelCount; i++) {
            temporalHistory_[i] = current[i];
        }
        return;
    }

    // Ensure temporal history matches current resolution
    if (temporalHistory_.size() != pixelCount) {
        temporalHistory_ = current;
        for (u32 i = 0; i < pixelCount; i++) {
            output[i] = current[i];
        }
        return;
    }

    f32 blend = denoiserCfg_.temporalBlend;

    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u32 idx = y * width + x;

            // Use motion vectors to determine trust in history
            f32 motionLen = motionVectors_[idx].length();
            f32 motionFactor = 1.0f;
            if (motionLen > denoiserCfg_.motionThreshold) {
                // High motion — reduce history weight (less trustworthy)
                motionFactor = denoiserCfg_.motionThreshold / motionLen;
                if (motionFactor < 0.0f) motionFactor = 0.0f;
                if (motionFactor > 1.0f) motionFactor = 1.0f;
            }

            // Effective blend: full blend for static, reduced for moving
            f32 effectiveBlend = blend * motionFactor;

            output[idx] = current[idx] * (1.0f - effectiveBlend) +
                          temporalHistory_[idx] * effectiveBlend;
        }
    }

    // Update history for next frame
    for (u32 i = 0; i < pixelCount; i++) {
        temporalHistory_[i] = output[i];
    }
}

// ============================================================================
// Compute Motion Vectors
// ============================================================================

void FrostNeuralDenoiser::computeMotionVectors(const Vector<f32>& currentDepth,
                                                const Vector<f32>& previousDepth,
                                                u32 width, u32 height) {
    u32 pixelCount = width * height;
    motionVectors_.resize(pixelCount);

    if (previousDepth.size() != pixelCount) {
        for (u32 i = 0; i < pixelCount; i++) {
            motionVectors_[i] = Vec3(0);
        }
        return;
    }

    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u32 idx = y * width + x;
            f32 dx = 0.0f;
            f32 dy = 0.0f;

            // Compute depth difference — large changes indicate motion
            f32 depthDelta = currentDepth[idx] - previousDepth[idx];

            if (fabsf(depthDelta) > denoiserCfg_.motionThreshold) {
                // Try to find where this pixel moved by checking neighbors
                // in the previous frame's depth
                f32 bestDelta = depthDelta;
                u32 bestX = x;
                u32 bestY = y;

                // Search a small neighborhood in the previous depth
                for (i32 sy = -2; sy <= 2; sy++) {
                    for (i32 sx = -2; sx <= 2; sx++) {
                        i32 px = (i32)x + sx;
                        i32 py = (i32)y + sy;
                        if (px < 0 || px >= (i32)width || py < 0 || py >= (i32)height) continue;
                        u32 pIdx = (u32)py * width + (u32)px;
                        f32 localDelta = fabsf(currentDepth[idx] - previousDepth[pIdx]);
                        if (localDelta < fabsf(bestDelta)) {
                            bestDelta = currentDepth[idx] - previousDepth[pIdx];
                            bestX = (u32)px;
                            bestY = (u32)py;
                        }
                    }
                }

                dx = (f32)bestX - (f32)x;
                dy = (f32)bestY - (f32)y;
            }

            motionVectors_[idx] = Vec3(dx, dy, 0.0f);
        }
    }

    // Store current depth as previous for next frame
    previousDepth_ = currentDepth;
}

// ============================================================================
// Full Denoise Pipeline
// ============================================================================

void FrostNeuralDenoiser::denoiseImage(const Vector<Vec3>& noisyImage,
                                        Vector<Vec3>& denoisedImage,
                                        u32 width, u32 height) {
    auto startTime = std::chrono::high_resolution_clock::now();

    u32 pixelCount = width * height;
    denoisedImage.resize(pixelCount);

    // Step 1: Extract features (albedo, normal, depth already in feature buffers)
    // Features should be set via extractFeatures before calling this,
    // or we use the noisy image directly as a proxy
    stats_.featuresExtracted = pixelCount;

    // Step 2: Spatial bilateral filter
    Vector<Vec3> spatialResult;
    if (denoiserCfg_.enableSpatial) {
        bilateralFilter(noisyImage, spatialResult, width, height);
    } else {
        spatialResult = noisyImage;
    }

    // Step 3: Temporal accumulation
    Vector<Vec3> temporalResult;
    temporalAccumulate(spatialResult, temporalResult, width, height);

    // Step 4: Output
    for (u32 i = 0; i < pixelCount; i++) {
        denoisedImage[i] = temporalResult[i];
    }

    // Update stats
    auto endTime = std::chrono::high_resolution_clock::now();
    f32 elapsed = std::chrono::duration<f32, std::milli>(endTime - startTime).count();
    lastDenoiseTimeMs_ = elapsed;
    stats_.denoiseTimeMs = elapsed;
    stats_.framesDenoised++;

    // Compute average noise reduction (compare input vs output)
    f32 totalDiff = 0.0f;
    for (u32 i = 0; i < pixelCount; i++) {
        totalDiff += (noisyImage[i] - denoisedImage[i]).length();
    }
    f32 avgDiff = pixelCount > 0 ? totalDiff / (f32)pixelCount : 0.0f;
    if (stats_.framesDenoised <= 1) {
        stats_.avgNoiseReduction = avgDiff;
    } else {
        stats_.avgNoiseReduction = stats_.avgNoiseReduction * 0.95f + avgDiff * 0.05f;
    }

    frameCount_++;
}

// ============================================================================
// Configuration
// ============================================================================

void FrostNeuralDenoiser::setDenoiserConfig(const DenoiserConfig& config) {
    denoiserCfg_ = config;

    // Sync temporal blend with existing member if it differs
    if (denoiserCfg_.temporalBlend != temporalBlend_) {
        temporalBlend_ = denoiserCfg_.temporalBlend;
    }
}

} // namespace Frost
