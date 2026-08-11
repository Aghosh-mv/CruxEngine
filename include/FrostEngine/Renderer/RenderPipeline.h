#pragma once

#include "Renderer/Types.h"
#include "Renderer/Device.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Vector.h"
#include "Core/UniquePtr.h"
#include "Scene/Entity.h"
#include "Scene/SceneManager.h"

namespace Frost {

class Shader;
struct PipelineState;

enum class PassType : u8 {
    ShadowPass,
    DepthPass,
    GBufferPass,
    ForwardPass,
    LightPass,
    AmbientOcclusionPass,
    GlobalIlluminationPass,
    ReflectionPass,
    RefractionPass,
    PostProcessPass,
    CompositePass,
    UI,
};

struct RenderPass {
    PassType type;
    String name;
    bool enabled = true;
    int priority = 0;
    u32 viewportWidth = 0;
    u32 viewportHeight = 0;
    
    virtual void prepare(const ViewData& view) = 0;
    virtual void execute(Renderer::CommandBuffer* cmd, const ViewData& view) = 0;
    virtual void resolve(Renderer::CommandBuffer* cmd) = 0;
    
    virtual ~RenderPass() = default;
};

struct RenderPassNode {
    u32 passId = 0;
    u32 nameHash = 0;
    Vector<u32> inputTextures;
    Vector<u32> outputTextures;
    u32 dependencies = 0;
    bool isCompute = false;
};

struct ResourceTransition {
    u32 resourceId = 0;
    u32 fromState = 0;
    u32 toState = 0;
    u32 passIndex = 0;
};

class RenderPipeline {
public:
    static RenderPipeline& instance();
    
    void init(Renderer::GraphicsDevice* device);
    void shutdown();
    
    void addPass(RenderPass* pass);
    void removePass(PassType type);
    void setPassEnabled(PassType type, bool enabled);
    
    void prepare(const ViewData& view);
    void execute(Renderer::CommandBuffer* cmd, const ViewData& view);
    void resolve(Renderer::CommandBuffer* cmd);
    void executeAll(Renderer::CommandBuffer* cmd, const ViewData& view);
    
    u32 addRenderPass(u32 nameHash, const Vector<u32>& inputs, const Vector<u32>& outputs, bool isCompute);
    void addResourceTransition(u32 resourceId, u32 fromState, u32 toState, u32 passIndex);
    void buildGraph();
    u32 executePassGraph();
    void beginFrame(f32 dt);
    void endFrame();
    void setTargetFrameTime(f32 ms);
    f32 getTargetFrameTime() const;
    f32 getFrameTimeMs() const;
    f32 getSmoothedFrameTimeMs() const;
    f32 getFPS() const;
    u32 getTotalPasses() const;
    u32 getPassExecutions() const;
    u32 getGraphDepth() const;
    void resetStats();
    bool validateGraph() const;
    
    void createGBuffer(u32 width, u32 height);
    void destroyGBuffer();
    
    Renderer::Texture* getGBufferTarget(u32 index) const;
    Renderer::Texture* getDepthTarget() const;
    Renderer::Texture* getLightingTarget() const;
    Renderer::Texture* getNormalTarget() const;
    
private:
    RenderPipeline() = default;
    ~RenderPipeline() = default;
    
    RenderPipeline(const RenderPipeline&) = delete;
    RenderPipeline& operator=(const RenderPipeline&) = delete;
    
    Renderer::GraphicsDevice* device_ = nullptr;
    
    Vector<RenderPass*> passes_;
    Vector<RenderPass*> sortedPasses_;
    
    Renderer::Texture* gbufferTargets_[4] = {};
    Renderer::Texture* depthTarget_ = nullptr;
    Renderer::Texture* lightingTarget_ = nullptr;
    Renderer::Texture* normalTarget_ = nullptr;
    Renderer::Texture* velocityTarget_ = nullptr;
    Renderer::Texture* aoTarget_ = nullptr;
    Renderer::Texture* ssrTarget_ = nullptr;
    Renderer::Texture* bloomTargets_[2] = {};
    
    Renderer::FrameBuffer* gbufferFbo_ = nullptr;
    Renderer::RenderPass* gbufferPass_ = nullptr;
    
    u32 width_ = 0;
    u32 height_ = 0;
    
    Vector<RenderPassNode> passGraph_;
    Vector<ResourceTransition> transitions_;
    Vector<f32> frameTimes_;
    u32 frameIndex_ = 0;
    f32 targetFrameTimeMs_ = 16.67f;
    f32 frameTimeMs_ = 0.0f;
    f32 smoothedFrameTimeMs_ = 0.0f;
    u32 totalPasses_ = 0;
    u32 passExecutions_ = 0;
};

struct BoundingFrustum {
    Mat4 viewProj = Mat4::identity();

    bool contains(const Vec3& point) const {
        Vec4 p = viewProj * Vec4(point, 1.0f);
        if (p.w <= 0.0f) return false;
        f32 x = p.x / p.w, y = p.y / p.w, z = p.z / p.w;
        return x >= -1.0f && x <= 1.0f &&
               y >= -1.0f && y <= 1.0f &&
               z >= 0.0f && z <= 1.0f;
    }
};

class ShadowPass : public RenderPass {
public:
    ShadowPass();
    ~ShadowPass() override;
    
    void prepare(const ViewData& view) override;
    void execute(Renderer::CommandBuffer* cmd, const ViewData& view) override;
    void resolve(Renderer::CommandBuffer* cmd) override;
    
    void setLight(LightComponent* light);
    void setQuality(ShadowQuality quality);
    
private:
    LightComponent* light_ = nullptr;
    ShadowQuality quality_ = ShadowQuality::High;
    
    Renderer::Texture* shadowMap_ = nullptr;
    Mat4 viewMatrix_;
    Mat4 projMatrix_;
    BoundingFrustum frustum_;
};

class GBufferPass : public RenderPass {
public:
    GBufferPass();
    ~GBufferPass() override;
    
    void prepare(const ViewData& view) override;
    void execute(Renderer::CommandBuffer* cmd, const ViewData& view) override;
    void resolve(Renderer::CommandBuffer* cmd) override;
    
    void setRenderPass(Renderer::RenderPass* pass) { gbufferPass_ = pass; }
    void setFrameBuffer(Renderer::FrameBuffer* fbo) { gbufferFbo_ = fbo; }
    
private:
    Renderer::RenderPass* gbufferPass_ = nullptr;
    Renderer::FrameBuffer* gbufferFbo_ = nullptr;
};

class LightPass : public RenderPass {
public:
    LightPass();
    ~LightPass() override;
    
    void prepare(const ViewData& view) override;
    void execute(Renderer::CommandBuffer* cmd, const ViewData& view) override;
    void resolve(Renderer::CommandBuffer* cmd) override;
    
    void enableScreenSpaceReflections(bool enable) { enableSSR_ = enable; }
    void enableScreenSpaceAO(bool enable) { enableSSAO_ = enable; }
    void enableContactShadows(bool enable) { enableContactShadows_ = enable; }
    
private:
    bool enableSSR_ = true;
    bool enableSSAO_ = true;
    bool enableContactShadows_ = true;
    bool enableVolumetric_ = true;
    
    Shader* deferredShader_ = nullptr;
    PipelineState* lightPipeline_ = nullptr;
    
    Vector<LightComponent*> visibleLights_;
    Vector<Mat4> lightMatrices_;
    Vector<Vec4> lightData_;
};

class GlobalIlluminationPass : public RenderPass {
public:
    enum class GIQuality : u8 {
        Off,
        Low,
        Medium,
        High,
        Ultra,
    };
    
    GlobalIlluminationPass();
    ~GlobalIlluminationPass() override;
    
    void prepare(const ViewData& view) override;
    void execute(Renderer::CommandBuffer* cmd, const ViewData& view) override;
    void resolve(Renderer::CommandBuffer* cmd) override;
    
    void setQuality(GIQuality quality) { quality_ = quality; }
    void setDenoise(bool enable) { enableDenoise_ = enable; }
    void setTemporalAccumulation(bool enable) { enableTemporal_ = enable; }
    
private:
    GIQuality quality_ = GIQuality::High;
    bool enableDenoise_ = true;
    bool enableTemporal_ = true;
    
    Renderer::AccelerationStructure* topLevelAS_ = nullptr;
    Renderer::Texture* voxelRadiance_ = nullptr;
    Renderer::Texture* indirectTexture_ = nullptr;
    Renderer::Buffer* raygenSbt_ = nullptr;
    Renderer::Buffer* missSbt_ = nullptr;
    
    bool initialized_ = false;
};

class PostProcessPass : public RenderPass {
public:
    PostProcessPass();
    ~PostProcessPass() override;
    
    void prepare(const ViewData& view) override;
    void execute(Renderer::CommandBuffer* cmd, const ViewData& view) override;
    void resolve(Renderer::CommandBuffer* cmd) override;
    
    void enableBloom(bool enable) { enableBloom_ = enable; }
    void enableTonemapping(bool enable) { enableTonemap_ = enable; }
    void enableChromaticAberration(bool enable) { enableCA_ = enable; }
    void enableVignette(bool enable) { enableVignette_ = enable; }
    void enableFilmGrain(bool enable) { enableGrain_ = enable; }
    void enableTemporalAA(bool enable) { enableTAA_ = enable; }
    void enableDLSS(bool enable);
    void enableFSR(bool enable);
    
    void setBloomThreshold(float threshold) { bloomThreshold_ = threshold; }
    void setBloomIntensity(float intensity) { bloomIntensity_ = intensity; }
    void setExposure(float exposure) { exposure_ = exposure; }
    void setVignetteIntensity(float intensity) { vignetteIntensity_ = intensity; }
    void setChromaticAberrationAmount(float amount) { caAmount_ = amount; }
    
private:
    bool enableBloom_ = true;
    bool enableTonemap_ = true;
    bool enableCA_ = false;
    bool enableVignette_ = false;
    bool enableGrain_ = false;
    bool enableTAA_ = true;
    bool enableDLSS_ = false;
    bool enableFSR_ = false;
    
    float bloomThreshold_ = 1.0f;
    float bloomIntensity_ = 0.5f;
    float exposure_ = 1.0f;
    float vignetteIntensity_ = 0.3f;
    float caAmount_ = 0.002f;
    float grainAmount_ = 0.003f;
    
    Renderer::Texture* bloomExtract_ = nullptr;
    Renderer::Texture* bloomBlur_ = nullptr;
    Renderer::Texture* tempTargets_[2] = {};
};

}