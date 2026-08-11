#pragma once

#include "Renderer/Types.h"

namespace Frost {
namespace Renderer {

struct AccelerationStructure;
struct ShaderModule;
struct PipelineState;
struct FrameBuffer;
struct DescriptorSet;
struct DescriptorSetLayout;
struct RenderPass;
struct CommandBuffer;
struct Buffer;
struct Texture;
struct Sampler;
struct Fence;
struct Semaphore;
struct SwapChain;
struct GraphicsDeviceDesc;
struct SwapChainDesc;
struct ShaderModuleDesc;
struct PipelineStateDesc;
struct RenderPassDesc;
struct CommandListDesc;
struct DescriptorSetLayoutDesc;
struct AccelerationStructureDesc;
struct AccelerationBuildDesc;
struct GPUProperties;
struct GPUmemInfo;
struct Viewport;
struct Scissor;
struct Color;
struct Transform;
enum class AccelerationStructureType : u8;

struct GraphicsDevice {
    virtual ~GraphicsDevice() = default;
    
    virtual bool init(const GraphicsDeviceDesc& desc) = 0;
    virtual void shutdown() = 0;
    
    virtual SwapChain* createSwapChain(const SwapChainDesc& desc) = 0;
    virtual void destroySwapChain(SwapChain* swapChain) = 0;
    
    virtual Buffer* createBuffer(const BufferDesc& desc) = 0;
    virtual void destroyBuffer(Buffer* buffer) = 0;
    
    virtual Texture* createTexture(const TextureDesc& desc) = 0;
    virtual void destroyTexture(Texture* texture) = 0;
    
    virtual ShaderModule* createShaderModule(const ShaderModuleDesc& desc) = 0;
    virtual void destroyShaderModule(ShaderModule* shader) = 0;
    
    virtual PipelineState* createPipelineState(const PipelineStateDesc& desc) = 0;
    virtual void destroyPipelineState(PipelineState* pipeline) = 0;
    
    virtual RenderPass* createRenderPass(const RenderPassDesc& desc) = 0;
    virtual void destroyRenderPass(RenderPass* pass) = 0;
    
    virtual CommandBuffer* createCommandList(const CommandListDesc& desc) = 0;
    virtual void destroyCommandList(CommandBuffer* cmd) = 0;
    
    virtual DescriptorSetLayout* createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) = 0;
    virtual void destroyDescriptorSetLayout(DescriptorSetLayout* layout) = 0;
    
    virtual DescriptorSet* createDescriptorSet(DescriptorSetLayout* layout) = 0;
    virtual void destroyDescriptorSet(DescriptorSet* set) = 0;
    
    virtual AccelerationStructure* createAccelerationStructure(const AccelerationStructureDesc& desc) = 0;
    virtual void destroyAccelerationStructure(AccelerationStructure* as) = 0;
    
    virtual bool isRayTracingSupported() const = 0;
    virtual bool isMeshShadingSupported() const = 0;
    virtual bool isVariableRateShadingSupported() const = 0;
    virtual bool isMeshletShadingSupported() const = 0;
    
    virtual const GPUProperties& getProperties() const = 0;
    virtual const GPUmemInfo& getMemoryInfo() const = 0;
    
    virtual void waitIdle() = 0;
    virtual void flush() = 0;
};

struct GraphicsDeviceDesc {
    BackendType backend = BackendType::Vulkan;
    void* window = nullptr;
    u32 width = 1280;
    u32 height = 720;
    u32 framesInFlight = 2;
    bool enableValidation = false;
    bool enableRayTracing = true;
    bool enableMeshShading = true;
    bool enableDebugUtils = true;
    Vector<const char*> extensions;
    Vector<const char*> layers;
    String appName = "FrostEngine";
    u32 appVersion = 1;
};

struct SwapChainDesc {
    void* window = nullptr;
    u32 width = 1280;
    u32 height = 720;
    Format colorFormat = Format::R8G8B8A8_SRGB;
    Format depthFormat = Format::D32_Float;
    bool vsync = true;
    bool hdr = false;
    bool allowDynamic = false;
};

struct ShaderModuleDesc {
    Vector<u32> bytecode;
    ShaderStage stage;
    String entryPoint;
    String name;
};

enum class AccelerationStructureType : u8 {
    BottomLevel,
    TopLevel,
    Generic,
};

struct AccelerationStructureDesc {
    Buffer* vertices = nullptr;
    Buffer* indices = nullptr;
    Vector<Transform> instances;
    u32 primitiveCount = 0;
    bool allowUpdate = true;
    bool fast_trace = true;
    bool compact = false;
    AccelerationStructureType type = AccelerationStructureType::TopLevel;
};

enum class QueryType : u8 {
    Occlusion,
    PipelineStatistics,
    Timestamp,
};

struct QueryPoolDesc {
    QueryType type = QueryType::Timestamp;
    u32 queryCount = 0;
};

struct GPUProperties {
    String name;
    String vendor;
    String driverVersion;
    u32 apiVersion;
    u32 shaderVersion;
    u32 maxComputeWorkGroupCount[3];
    u32 maxComputeWorkGroupSize[3];
    u32 maxRayGenRecursionDepth;
    u32 maxMissRecursionDepth;
    u32 maxHitGroupRecursionDepth;
    u32 maxMeshWorkGroupCount[3];
    u32 maxMeshWorkGroupSize[3];
    u32 maxMeshletCount;
    u64 dedicatedAllocSize;
    bool rtPipelineAtomicResourceAlignment;
    bool separateDepthStencilLayouts;
};

struct GPUmemInfo {
    u64 budget = 0;
    u64 used = 0;
    u64 available = 0;
};

struct SwapChain {
    virtual ~SwapChain() = default;
    
    virtual bool acquireNextImage() = 0;
    virtual bool present() = 0;
    virtual bool resize(u32 w, u32 h) = 0;
    
    virtual Texture* getColorTexture() = 0;
    virtual Texture* getDepthTexture() = 0;
    virtual u32 getCurrentImageIndex() = 0;
    virtual u32 getImageCount() = 0;
    virtual Format getColorFormat() = 0;
    virtual Format getDepthFormat() = 0;
    virtual bool isHdr() = 0;
    virtual bool isVSync() = 0;
};

struct Buffer {
    virtual ~Buffer() = default;
    
    virtual void* map() = 0;
    virtual void unmap() = 0;
    virtual void flush() = 0;
    virtual void invalidate() = 0;
    
    virtual u64 getSize() const = 0;
    virtual const BufferDesc& getDesc() const = 0;
};

struct Texture {
    virtual ~Texture() = default;
    
    virtual void genMips() = 0;
    virtual void setLayout(ImageLayout layout) = 0;
    
    virtual u32 getWidth() const = 0;
    virtual u32 getHeight() const = 0;
    virtual u32 getDepth() const = 0;
    virtual u32 getMipLevels() const = 0;
    virtual Format getFormat() const = 0;
    virtual const TextureDesc& getDesc() const = 0;
};

struct ShaderModule {
    virtual ~ShaderModule() = default;
    virtual const ShaderModuleDesc& getDesc() const = 0;
};

struct PipelineState {
    virtual ~PipelineState() = default;
    virtual const PipelineStateDesc& getDesc() const = 0;
};

struct RenderPass {
    virtual ~RenderPass() = default;
};

struct CommandBuffer {
    virtual ~CommandBuffer() = default;
    
    virtual void begin() = 0;
    virtual void end() = 0;
    virtual void reset() = 0;
    
    virtual void beginRenderPass(RenderPass* pass, FrameBuffer* fb) = 0;
    virtual void endRenderPass() = 0;
    
    virtual void bindPipeline(PipelineState* pipeline) = 0;
    virtual void bindDescriptorSet(DescriptorSet* set, u32 slot) = 0;
    virtual void bindVertexBuffer(Buffer* buffer, u32 slot, u64 offset = 0) = 0;
    virtual void bindIndexBuffer(Buffer* buffer, u64 offset = 0) = 0;
    
    virtual void setViewport(const Viewport& vp) = 0;
    virtual void setScissor(const Scissor& sc) = 0;
    
    virtual void draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0) = 0;
    virtual void drawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0) = 0;
    virtual void drawMesh(u32 groupCount) = 0;
    virtual void drawMeshIndirect(Buffer* args, u64 offset) = 0;
    
    virtual void setPushConstant(u32 offset, u32 size, const void* data) = 0;
    virtual void insertMemoryBarrier(u32 srcAccess, u32 dstAccess) = 0;
    
    virtual void buildAccelerationStructure(AccelerationStructure* as, const AccelerationBuildDesc& desc) = 0;
    virtual void traceRays(const Buffer* raygen, u32 width, u32 height, u32 depth) = 0;
    virtual void copyBuffer(Buffer* src, Buffer* dst, u64 size, u64 srcOffset = 0, u64 dstOffset = 0) = 0;
    virtual void copyTexture(Texture* src, Texture* dst) = 0;
    virtual void blit(Texture* src, Texture* dst) = 0;
    
    virtual void beginDebugMarker(const char* name, Color color) = 0;
    virtual void endDebugMarker() = 0;
};

struct AccelerationBuildDesc {
    AccelerationStructure* dst = nullptr;
    Buffer* src = nullptr;
    Buffer* scratch = nullptr;
    bool update = false;
};

struct AccelerationStructure {
    virtual ~AccelerationStructure() = default;
    virtual u64 getHandle() const = 0;
    virtual u64 getSize() const = 0;
    virtual u64 getBuildScratchSize() const = 0;
};

struct FrameBuffer {
    virtual ~FrameBuffer() = default;
    virtual u32 getWidth() const = 0;
    virtual u32 getHeight() const = 0;
    virtual const RenderPass* getRenderPass() const = 0;
};

struct DescriptorSet {
    virtual ~DescriptorSet() = default;
    virtual void setBuffer(u32 binding, Buffer* buffer, u64 offset = 0, u64 size = u64(-1)) = 0;
    virtual void setTexture(u32 binding, Texture* texture, ImageLayout layout = ImageLayout::ShaderRead) = 0;
    virtual void setTexture(u32 binding, Texture* texture, Sampler* sampler) = 0;
    virtual void setAccelerationStructure(u32 binding, AccelerationStructure* as) = 0;
    virtual void update() = 0;
};

struct DescriptorSetLayout {
    virtual ~DescriptorSetLayout() = default;
};

struct Fence {
    virtual ~Fence() = default;
    virtual void signal() = 0;
    virtual void wait(u64 timeout = u64(-1)) = 0;
    virtual void reset() = 0;
    virtual bool isSignaled() = 0;
};

struct Semaphore {
    virtual ~Semaphore() = default;
    virtual void signal(u64 value) = 0;
    virtual void wait(u64 value) = 0;
    virtual void reset() = 0;
};

struct Sampler {
    virtual ~Sampler() = default;
};

struct Transform {
    Mat4 worldToLocal;
    Mat4 localToWorld;
    u32 customData;
};

GraphicsDevice* createGraphicsDevice();
void destroyGraphicsDevice(GraphicsDevice* device);

}
}