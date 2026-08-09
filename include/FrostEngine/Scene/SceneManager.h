#pragma once

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/Vector.h"
#include "Core/UniquePtr.h"
#include "Core/String.h"
#include "Scene/Entity.h"

namespace Frost {

enum class RenderPath : u8 {
    Forward,
    Deferred,
    ForwardPlus,
    VirtualGeometry,
};

enum class ShadowQuality : u8 {
    Off,
    Low,
    Medium,
    High,
    Ultra,
};

enum class GIQuality : u8 {
    Off,
    Low,
    Medium,
    High,
};

struct RenderSettings {
    RenderPath renderPath = RenderPath::Deferred;
    
    bool enableHDR = true;
    bool enableTonemapping = true;
    bool enableBloom = true;
    bool enableSSR = true;
    bool enableScreenSpaceAO = true;
    bool enableContactShadows = true;
    bool enableRayTracedGI = true;
    bool enableVirtualShadowMaps = true;
    bool enableNanite = true;
    bool enableLumen = true;
    
    ShadowQuality shadowQuality = ShadowQuality::High;
    GIQuality giQuality = GIQuality::High;
    
    u32 shadowMapSize = 4096;
    u32 ssaoSamples = 32;
    u32 ssrSamples = 64;
    u32 bloomThreshold = 1;
    u32 bloomIntensity = 1;
    
    float exposure = 1.0f;
    float gamma = 2.2f;
    float minLuminance = 0.0f;
    float maxLuminance = 1000.0f;
    
    float farDistance = 10000.0f;
    float lodDistance = 1000.0f;
    float cullDistance = 10000.0f;
    u32 maxVisibleLights = 256;
    u32 maxVisibleMeshes = 65536;
    
    float volumetricDensity = 0.01f;
    float fogStart = 100.0f;
    float fogEnd = 1000.0f;
    Vec3 fogColor = Vec3(0.5f, 0.6f, 0.7f);
    
    u32 voxelResolution = 256;
    float indirectIntensity = 1.0f;
    float emissiveIntensity = 1.0f;
};

struct ViewData {
    Mat4 view;
    Mat4 proj;
    Mat4 viewProj;
    Mat4 invView;
    Mat4 invProj;
    Mat4 prevViewProj;
    Vec3 position;
    Vec3 right;
    Vec3 up;
    Vec3 forward;
    float nearPlane;
    float farPlane;
    float fov;
    float aspectRatio;
    u32 width;
    u32 height;
    u32 frameIndex;
    float time;
};

class SceneManager {
public:
    static SceneManager& instance();
    
    void init();
    void shutdown();
    
    Entity createEntity(const String& name = String());
    void destroyEntity(Entity entity);
    
    template<typename T>
    T* addComponent(Entity entity) {
        auto* component = new T();
        component->onAttach(entity);
        components_.push_back(component);
        return component;
    }
    
    template<typename T>
    T* getComponent(Entity entity) {
        for(auto* comp : components_) {
            if(comp->getType() == T::type) {
                return static_cast<T*>(comp);
            }
        }
        return nullptr;
    }
    
    void removeComponent(Entity entity, ComponentType type);
    
    void update(f32 deltaTime);
    void updateTransforms();
    void updateCulling(const ViewData& view);
    
    void setSkybox(Texture* skybox, const Vec3& tint, float intensity);
    void setFog(const Vec3& color, float density, float start, float end);
    void setMainCamera(Entity camera);
    void setActiveCamera(Entity camera);
    
    Entity getMainCamera() const { return mainCamera_; }
    Entity getActiveCamera() const { return activeCamera_; }
    
    const Vector<Entity>& getEntities() const { return entities_; }
    const Vector<LightComponent*>& getLights() const { return lights_; }
    const Vector<MeshComponent*>& getMeshes() const { return meshes_; }
    
    RenderSettings& getSettings() { return settings_; }
    const RenderSettings& getSettings() const { return settings_; }
    
private:
    SceneManager() = default;
    ~SceneManager() = default;
    
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    
    Vector<Entity> entities_;
    Vector<IComponent*> components_;
    Vector<LightComponent*> lights_;
    Vector<MeshComponent*> meshes_;
    Vector<MaterialComponent*> materials_;
    
    Entity mainCamera_ = Entity::null();
    Entity activeCamera_ = Entity::null();
    
    RenderSettings settings_;
    
    Texture* skybox_ = nullptr;
    Vec3 skyTint_ = Vec3::one();
    float skyIntensity_ = 1.0f;
    Vec3 fogColor_ = Vec3(0.5f, 0.6f, 0.7f);
    float fogDensity_ = 0.0f;
    float fogStart_ = 100.0f;
    float fogEnd_ = 1000.0f;
    
    u32 entityIdCounter_ = 1;
};

}
}