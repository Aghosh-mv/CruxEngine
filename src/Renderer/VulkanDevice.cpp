#include "Renderer/Device.h"
#include "Renderer/Types.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <cstring>
#include <vector>
#include <set>
#include <algorithm>

namespace Crux {
namespace Renderer {

struct VulkanInstance {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    VkQueue transferQueue = VK_NULL_HANDLE;
    VkCommandPool graphicsCommandPool = VK_NULL_HANDLE;
    VkCommandPool computeCommandPool = VK_NULL_HANDLE;
    PFN_vkCreateRayTracingKHR vkCreateRayTracingKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkBuildAccelerationStructureKHR vkBuildAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructureKHR vkCmdBuildAccelerationStructureKHR = nullptr;
};

struct VulkanSwapChain {
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    VkImage depthImage = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_SRGB;
    VkFormat depthFormat = VK_FORMAT_D32_FLOAT;
    u32 imageCount = 0;
    u32 currentImage = 0;
    bool vsync = true;
    bool hdr = false;
};

class VulkanGraphicsDevice : public GraphicsDevice {
public:
    VulkanGraphicsDevice();
    ~VulkanGraphicsDevice() override;
    
    bool init(const GraphicsDeviceDesc& desc) override;
    void shutdown() override;
    
    SwapChain* createSwapChain(const SwapChainDesc& desc) override;
    void destroySwapChain(SwapChain* swapChain) override;
    
    Buffer* createBuffer(const BufferDesc& desc) override;
    void destroyBuffer(Buffer* buffer) override;
    
    Texture* createTexture(const TextureDesc& desc) override;
    void destroyTexture(Texture* texture) override;
    
    ShaderModule* createShaderModule(const ShaderModuleDesc& desc) override;
    void destroyShaderModule(ShaderModule* shader) override;
    
    PipelineState* createPipelineState(const PipelineStateDesc& desc) override;
    void destroyPipelineState(PipelineState* pipeline) override;
    
    RenderPass* createRenderPass(const RenderPassDesc& desc) override;
    void destroyRenderPass(RenderPass* pass) override;
    
    CommandBuffer* createCommandList(const CommandListDesc& desc) override;
    void destroyCommandList(CommandBuffer* cmd) override;
    
    DescriptorSetLayout* createDescriptorSetLayout(const DescriptorSetLayoutDesc& desc) override;
    void destroyDescriptorSetLayout(DescriptorSetLayout* layout) override;
    
    DescriptorSet* createDescriptorSet(DescriptorSetLayout* layout) override;
    void destroyDescriptorSet(DescriptorSet* set) override;
    
    AccelerationStructure* createAccelerationStructure(const AccelerationStructureDesc& desc) override;
    void destroyAccelerationStructure(AccelerationStructure* as) override;
    
    bool isRayTracingSupported() const override { 
        return vk::instance.vkCreateRayTracingKHR != nullptr; 
    }
    bool isMeshShadingSupported() const override { return meshShadingSupported_; }
    bool isVariableRateShadingSupported() const override { return vrsSupported_; }
    bool isMeshletShadingSupported() const override { return meshletSupported_; }
    
    const GPUProperties& getProperties() const override { return properties_; }
    const GPUmemInfo& getMemoryInfo() const override { return memoryInfo_; }
    
    void waitIdle() override;
    void flush() override;
    
private:
    bool initVulkan(const GraphicsDeviceDesc& desc);
    bool initQueues();
    bool initCommandPool();
    bool initAccelStruct();
    
    uint32_t getQueueFamilyIndex(VkQueueFlags flags);
    bool supportsExtensions();
    bool supportsFeatures();
    
    VulkanInstance vk;
    VulkanSwapChain swapChain_;
    
    GPUProperties properties_;
    GPUmemInfo memoryInfo_;
    
    std::vector<VkExtensionProperties> availableExtensions_;
    std::vector<VkLayerProperties> availableLayers_;
    VkPhysicalDeviceFeatures features_;
    VkPhysicalDeviceVulkan13Features features13_;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures_;
    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures_;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures_;
    
    bool meshShadingSupported_ = false;
    bool vrsSupported_ = false;
    bool meshletSupported_ = false;
    bool rayTracingSupported_ = false;
    
    std::vector<std::unique_ptr<SwapChain>> swapChains_;
    std::vector<std::unique_ptr<Buffer>> buffers_;
    std::vector<std::unique_ptr<Texture>> textures_;
    std::vector<std::unique_ptr<ShaderModule>> shaders_;
    std::vector<std::unique_ptr<PipelineState>> pipelines_;
    std::vector<std::unique_ptr<CommandBuffer>> commandLists_;
    
    GraphicsDeviceDesc config_;
    std::vector<const char*> requiredExtensions_;
    std::vector<const char*> requiredLayers_;
};

GraphicsDevice* createGraphicsDevice() {
    return new VulkanGraphicsDevice();
}

void destroyGraphicsDevice(GraphicsDevice* device) {
    delete device;
}

}
}