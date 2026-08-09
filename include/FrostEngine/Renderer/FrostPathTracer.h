#pragma once

// ============================================================================
// FrostEngine FrostPathTracer — Hardware Path Tracer
// ============================================================================
// Proprietary real-time path tracer. Full offline-quality path tracing with
// Cook-Torrance BRDF, multiple importance sampling, next event estimation,
// Russian roulette, progressive refinement, and atrous wavelet denoising.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Math.h"
#include "Core/Vector.h"

namespace Frost {

// ============================================================================
// Path tracing structures
// ============================================================================

static constexpr u32 MAX_PATH_TRACER_SAMPLES = 4096;
static constexpr u32 MAX_PATH_LENGTH = 32;
static constexpr u32 DENOISE_ITERATIONS = 4;

// Path tracer pixel state
struct PathTracerPixel {
    Vec3 radiance;          // accumulated radiance
    Vec3 throughput;        // path throughput
    f32 alpha;              // coverage
    u32 sampleCount;        // number of samples taken
    f32 variance;           // estimated variance
    bool active;            // path still alive

    PathTracerPixel() : radiance(0), throughput(1), alpha(1),
                        sampleCount(0), variance(0), active(true) {}
};

// Ray for path tracing
struct Ray {
    Vec3 origin;
    Vec3 direction;
    f32 tMin;
    f32 tMax;

    Ray() : origin(0), direction(0, 0, -1), tMin(0.001f), tMax(1e30f) {}
    Ray(Vec3 o, Vec3 d, f32 tmin = 0.001f, f32 tmax = 1e30f)
        : origin(o), direction(d), tMin(tmin), tMax(tmax) {}
};

// Surface intersection result
struct Intersection {
    Vec3 position;
    Vec3 normal;
    Vec3 geometricNormal;
    Vec2 uv;
    f32 t;
    u32 materialID;
    u32 triangleID;
    u32 meshID;
    bool hit;

    Intersection() : position(0), normal(0), geometricNormal(0), uv(0),
                     t(1e30f), materialID(0), triangleID(0xFFFFFFFF),
                     meshID(0), hit(false) {}
};

// Cook-Torrance microfacet material
struct PathTracerMaterial {
    Vec3 albedo;
    Vec3 emission;
    Vec3 subsurfaceColor;
    f32 roughness;
    f32 metalness;
    f32 ior;                // index of refraction
    f32 specularTint;
    f32 sheenTint;
    f32 clearcoatGloss;
    f32 subsurface;
    f32 transmission;
    f32 anisotropy;
    f32 absorption;         // volumetric absorption
    bool isEmissive;
    bool isThinSurface;
    bool isDoubleSided;
    bool isSpecular;

    PathTracerMaterial() : albedo(0.8f), emission(0), subsurfaceColor(0),
                           roughness(0.5f), metalness(0.0f), ior(1.5f),
                           specularTint(0), sheenTint(0), clearcoatGloss(0),
                           subsurface(0), transmission(0), anisotropy(0),
                           absorption(0), isEmissive(false), isThinSurface(false),
                           isDoubleSided(false), isSpecular(false) {}
};

// Directional light for next event estimation
struct PTDirectionalLight {
    Vec3 direction;
    Vec3 color;
    f32 intensity;
    f32 angularRadius;      // for soft shadows

    PTDirectionalLight() : direction(0, -1, 0), color(1), intensity(3),
                           angularRadius(0.004f) {}
};

// Point/area light for NEE
struct PTAreaLight {
    Vec3 position;
    Vec3 color;
    f32 intensity;
    f32 radius;             // disk area light radius
    f32 emissionPower;      // total emission power for importance

    PTAreaLight() : position(0), color(1), intensity(100), radius(1), emissionPower(100) {}
};

// Denoiser state
struct DenoiseState {
    Vector<Vec3> colorBuffer;
    Vector<Vec3> normalBuffer;
    Vector<Vec3> depthBuffer;
    Vector<Vec3> albedoBuffer;
    Vector<Vec3> outputBuffer;
    u32 width;
    u32 height;

    void resize(u32 w, u32 h) {
        width = w; height = h;
        u32 size = w * h;
        colorBuffer.resize(size);
        normalBuffer.resize(size);
        depthBuffer.resize(size);
        albedoBuffer.resize(size);
        outputBuffer.resize(size);
    }
};

// Mesh data for path tracing
struct PTMeshData {
    Vector<Vec3> positions;
    Vector<Vec3> normals;
    Vector<Vec2> uvs;
    Vector<u32> indices;
    u32 materialID;
    u32 meshID;
};

// BVH node for acceleration structure
struct BVHNode {
    Vec3 boundsMin;
    Vec3 boundsMax;
    u32 leftChild;          // 0xFFFFFFFF = leaf
    u32 rightChild;         // 0xFFFFFFFF = leaf
    u32 firstPrimIndex;     // for leaves
    u32 primCount;          // for leaves

    BVHNode() : boundsMin(1e30f), boundsMax(-1e30f),
                leftChild(0xFFFFFFFF), rightChild(0xFFFFFFFF),
                firstPrimIndex(0), primCount(0) {}
};

// ============================================================================
// Main FrostPathTracer system
// ============================================================================

class FrostPathTracer {
public:
    FrostPathTracer();
    ~FrostPathTracer();

    bool init(u32 width, u32 height);
    void shutdown();
    void reset();

    // Set scene data
    void setSceneMeshes(const PTMeshData* meshes, u32 meshCount,
                        const PathTracerMaterial* materials, u32 materialCount);

    // Set lights
    void setDirectionalLight(const PTDirectionalLight& light);
    void setAreaLights(const PTAreaLight* lights, u32 count);

    // Set camera
    void setCamera(const Mat4& view, const Mat4& proj, Vec3 pos);

    // Main render: trace paths and denoise
    void render(f32 deltaTime);

    // Trace a single pixel
    Vec3 tracePixel(u32 x, u32 y, u32 sampleIndex);

    // Trace a path from camera
    Vec3 tracePath(Ray ray, u32 maxDepth);

    // Direct lighting (next event estimation)
    Vec3 computeDirectLighting(const Intersection& hit, const PathTracerMaterial& mat, const Ray& ray);

    // Indirect lighting
    Vec3 computeIndirectLighting(const Intersection& hit, const PathTracerMaterial& mat,
                                 Vec3& outDir, f32& pdf);

    // Volumetric rendering
    Vec3 traceVolumetric(Ray ray, f32 tMax, f32 absorption);
    Vec3 traceHeterogeneousVolume(Ray ray, f32 tMax, f32 absorption) const;
    Vec3 computePhaseFunction(Vec3 viewDir, Vec3 lightDir, f32 g) const;

    // Subsurface scattering
    Vec3 computeSubsurfaceScattering(const Intersection& hit, const PathTracerMaterial& mat, Vec3 lightDir) const;
    Vec3 computeSheenColor(const Intersection& hit, const PathTracerMaterial& mat, Vec3 viewDir, Vec3 lightDir) const;
    f32 computeClearcoat(Vec3 viewDir, Vec3 lightDir, f32 clearcoatRoughness) const;
    Vec3 evaluateAnisotropicBRDF(Vec3 V, Vec3 L, Vec3 N, const PathTracerMaterial& mat) const;
    Vec3 renderCaustics(const Intersection& hit, const PathTracerMaterial& mat) const;

    // Adaptive sampling
    void computeAdaptiveSamples(Vector<u32>& outSamples) const;
    f32 computePixelVariance(u32 x, u32 y) const;

    // Advanced denoising
    void featureAtrousDenoise(Vector<Vec3>& output, const Vector<Vec3>& colors,
                              const Vector<f32>& albedo, const Vector<Vec3>& normals,
                              u32 width, u32 height, u32 iterations);

    // Light sampling
    Vec3 sampleLight(const Intersection& hit, f32& pdf) const;
    f32 computeLightPdf(Vec3 point, Vec3 lightPos, u32 lightIdx) const;

    // Fresnel and BRDF helpers
    Vec3 computeFresnelSchlick(Vec3 F0, f32 cosTheta) const;
    f32 computeSmithG1(Vec3 N, Vec3 V, f32 roughness) const;
    f32 computeBeckmannDistribution(Vec3 H, Vec3 N, f32 roughness) const;

    // Denoise the accumulated image
    void denoise();

    // Atrous wavelet denoiser
    void atrousWaveletDenoise(Vector<Vec3>& image, const Vector<Vec3>& normals,
                              const Vector<Vec3>& depths, u32 width, u32 height,
                              u32 iterations);

    // Get output
    const Vector<Vec3>& outputImage() const { return denoise_.outputBuffer; }
    u32 sampleCount() const { return sampleCount_; }
    u32 width() const { return width_; }
    u32 height() const { return height_; }

    // Adaptive sampling
    void setAdaptiveSampling(bool enabled) { adaptiveSampling_ = enabled; }
    void setMaxSamples(u32 max) { maxSamples_ = max; }

private:
    // BRDF evaluation
    Vec3 evaluateCookTorrance(Vec3 V, Vec3 L, Vec3 N, const PathTracerMaterial& mat) const;
    f32 cookTorranceD(Vec3 N, Vec3 H, f32 roughness) const;
    f32 cookTorranceG(Vec3 N, Vec3 V, Vec3 L, f32 roughness) const;
    Vec3 cookTorranceF(Vec3 V, Vec3 H, f32 ior, Vec3 F0) const;
    Vec3 microfacetSample(Vec3 V, Vec3 N, f32 roughness, f32 u1, f32 u2,
                          Vec3& halfVec, f32& pdf) const;
    Vec3 importanceSampleGGX(Vec2 u, Vec3 N, f32 roughness) const;
    f32 ggxDistribution(Vec3 H, Vec3 N, f32 roughness) const;
    f32 geometrySchlickGGX(Vec3 N, Vec3 V, f32 roughness) const;
    f32 geometrySmith(Vec3 N, Vec3 V, Vec3 L, f32 roughness) const;

    // MIS (Multiple Importance Sampling)
    f32 powerHeuristic(f32 pdf1, f32 pdf2, f32 beta = 2.0f) const;

    // Russian roulette
    bool russianRoulette(Vec3 throughput, u32 depth, f32& survivalProb) const;

    // Scene intersection
    bool intersectScene(const Ray& ray, Intersection& hit) const;
    bool intersectBVH(const Ray& ray, u32 nodeIdx, Intersection& hit) const;
    bool intersectTriangle(const Ray& ray, Vec3 a, Vec3 b, Vec3 c,
                           f32& t, f32& u, f32& v) const;

    // BVH building
    void buildBVH();
    void buildBVHRecursive(u32 nodeIdx, Vector<u32>& primIndices,
                           u32 start, u32 end);
    void computeNodeBounds(u32 nodeIdx, const Vector<u32>& primIndices,
                           u32 start, u32 end);

    // Sampling utilities
    Vec2 hammersley(u32 i, u32 N) const;
    Vec3 cosineWeightedHemisphere(Vec2 u, Vec3 N) const;
    Vec3 uniformHemisphere(Vec2 u, Vec3 N) const;
    Vec2 uniformDisk(u32 sampleIndex, u32 totalSamples) const;
    f32 luminance(Vec3 color) const;

// Denoiser helpers
    void bilateralFilter(Vector<Vec3>& output, const Vector<Vec3>& input,
                         const Vector<Vec3>& normals, const Vector<Vec3>& depths,
                         u32 w, u32 h, f32 spatialSigma, f32 colorSigma);
    f32 edgeStoppingWeight(Vec3 n1, Vec3 n2, f32 depth1, f32 depth2,
                          f32 normalSigma, f32 depthSigma) const;

    // Additional methods
    Vec3 traceVolumetric(Ray ray, f32 tMax, f32 absorption) const;
    Vec3 computeDisneyBRDF(Vec3 V, Vec3 L, Vec3 N, const PathTracerMaterial& mat) const;
    bool isShadowed(Vec3 from, Vec3 to) const;
    f32 computeVisibility(Vec3 from, Vec3 to) const;
    Vec3 getSceneBoundsMin() const;
    Vec3 getSceneBoundsMax() const;
    u32 getTriangleCount() const;
    f32 computeConvergenceRate() const;
    Vec3 getAverageRadiance() const;
    f32 computeNoiseLevel() const;
    void getPathTracerStats(u32& totalSamples, u32& activePixels, u32& totalBounces, f32& avgTimeMs) const;
    f32 computeBVHEfficiency() const;
    u32 computeBVHDepth() const;
    f32 computeBRDFEnergy(const PathTracerMaterial& mat, Vec3 V, Vec3 L) const;
    Vec3 computeAlbedo(const PathTracerMaterial& mat, Vec3 V, Vec3 L) const;
    f32 computeSceneComplexity() const;
    Vec3 computeSceneCenter() const;
    f32 computeSceneRadius() const;
    f32 computeTotalLightPower() const;
    Vec3 computeSceneAverageAlbedo() const;
    f32 computeSignalToNoiseRatio() const;
    f32 computePSNR() const;
    void getDenoiserStats(f32& rmse, f32& ssim, f32& psnr) const;

    // Data
    u32 width_;
    u32 height_;
    u32 sampleCount_;
    u32 maxSamples_;
    bool adaptiveSampling_;

    // Scene
    Vector<PTMeshData> sceneMeshes_;
    Vector<PathTracerMaterial> materials_;
    Vector<PTDirectionalLight> dirLight_;
    Vector<PTAreaLight> areaLights_;
    u32 meshCount_;
    u32 materialCount_;
    u32 areaLightCount_;

    // Camera
    Mat4 viewMatrix_;
    Mat4 projMatrix_;
    Vec3 cameraPos_;

    // Output
    Vector<PathTracerPixel> pixels_;
    Vector<Vec3> accumulatedRadiance_;
    Vector<Vec3> sampleVariance_;
    DenoiseState denoise_;

    // BVH
    Vector<BVHNode> bvhNodes_;
    u32 bvhNodeCount_;
    Vector<Vec3> primCentroids_;
    Vector<Vec3> primBoundsMin_;
    Vector<Vec3> primBoundsMax_;

    bool initialized_;
};

} // namespace Frost
