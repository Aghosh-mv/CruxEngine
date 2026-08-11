#pragma once

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"
#include "Core/String.h"
#include "Core/Vector.h"
#include "Core/UniquePtr.h"
#include "Renderer/Types.h"

namespace Frost {

namespace Renderer { struct Texture; struct Sampler; }

struct Entity {
    u32 id = 0;
    
    Entity() = default;
    explicit Entity(u32 id) : id(id) {}
    
    bool operator==(const Entity& other) const { return id == other.id; }
    bool operator!=(const Entity& other) const { return id != other.id; }
    operator bool() const { return id != 0; }
    
    static Entity null() { return Entity(0); }
};

struct UUID {
    u64 lo = 0;
    u64 hi = 0;
    
    bool operator==(const UUID& other) const { return lo == other.lo && hi == other.hi; }
    bool operator!=(const UUID& other) const { return lo != other.lo || hi != other.hi; }
    operator bool() const { return lo != 0 || hi != 0; }
};

enum class ComponentType : u32 {
    Transform,
    Mesh,
    Material,
    Light,
    Camera,
    RigidBody,
    Collider,
    Script,
    Audio,
    Animator,
    ParticleSystem,
    SkinnedMesh,
    Skeleton,
    PostProcess,
    Custom,
};

struct IComponent {
    virtual ~IComponent() = default;
    virtual ComponentType getType() const = 0;
    virtual const char* getName() const = 0;
    virtual void onAttach(Entity e) = 0;
    virtual void onDetach(Entity e) = 0;
    virtual void onEnable() = 0;
    virtual void onDisable() = 0;
    virtual void onTransformChanged() = 0;
    virtual IComponent* clone() const = 0;
};

struct TransformComponent : IComponent {
    static constexpr ComponentType type = ComponentType::Transform;
    
    Vec3 position = Vec3::zero();
    Vec3 scale = Vec3::one();
    Vec3 rotation = Vec3::zero();
    Vec3 eulerAngles = Vec3::zero();
    
    Mat4 localToWorld = Mat4::identity();
    Mat4 worldToLocal = Mat4::identity();
    
    Entity parent;
    Vector<Entity> children;
    
    Vec3 forward() const { return localToWorld.forward(); }
    Vec3 right() const { return localToWorld.right(); }
    Vec3 up() const { return localToWorld.up(); }
    
    void lookAt(const Vec3& target) {
        localToWorld = Mat4::lookAt(position, target, Vec3::up());
    }
    
    void rotate(const Vec3& axis, f32 angle) {
        Mat4 rot = Mat4::rotation(axis, angle);
        localToWorld = localToWorld * rot;
        position = localToWorld.translation();
    }
    
    ComponentType getType() const override { return type; }
    const char* getName() const override { return "Transform"; }
    void onAttach(Entity e) override {}
    void onDetach(Entity e) override {}
    void onEnable() override {}
    void onDisable() override {}
    void onTransformChanged() override {}
    IComponent* clone() const override { return new TransformComponent(*this); }
};

enum class LightType : u8 {
    Directional,
    Point,
    Spot,
    Rect,
    Circle,
};

struct LightComponent : IComponent {
    static constexpr ComponentType type = ComponentType::Light;
    
    LightType lightType = LightType::Directional;
    Vec3 color = Vec3(1.0f);
    f32 intensity = 1.0f;
    f32 temperature = 6500.0f;
    f32 spreadAngle = 0.5f;
    f32 innerConeAngle = 0.0f;
    f32 outerConeAngle = 0.5f;
    f32 radius = 0.5f;
    f32 range = 10.0f;
    f32 shadowBias = 0.005f;
    i32 shadowRes = 1024;
    bool castShadows = true;
    bool castVolumetric = false;
    bool useContactShadows = false;
    bool runtimeLight = false;
    bool visible = true;
    bool affectBounce = true;
    Entity volumetricFog = Entity::null();
    
    Vec3 getDirection() const;
    Vec3 getPosition() const;
    f32 getIntensity() const;
    
    ComponentType getType() const override { return type; }
    const char* getName() const override { return "Light"; }
    void onAttach(Entity e) override {}
    void onDetach(Entity e) override {}
    void onEnable() override {}
    void onDisable() override {}
    void onTransformChanged() override {}
    IComponent* clone() const override { return new LightComponent(*this); }
};

struct CameraComponent : IComponent {
    static constexpr ComponentType type = ComponentType::Camera;
    
    enum class Projection : u8 {
        Perspective,
        Orthographic,
    };
    
    enum class ClearMask : u8 {
        None = 0,
        Color = 1 << 0,
        Depth = 1 << 1,
        Skybox = 1 << 2,
    };

    Projection projection = Projection::Perspective;
    ClearMask clearMask = static_cast<ClearMask>(static_cast<u8>(ClearMask::Depth) | static_cast<u8>(ClearMask::Skybox));
    Vec4 clearColor = Vec4(0.1f, 0.1f, 0.12f, 1.0f);
    
    f32 fov = 60.0f;
    f32 orthoSize = 5.0f;
    f32 nearPlane = 0.1f;
    f32 farPlane = 1000.0f;
    f32 focalLength = 50.0f;
    f32 aperture = 2.8f;
    
    u32 renderWidth = 1920;
    u32 renderHeight = 1080;
    
    i32 prevRenderTargetId = -1;
    i32 renderTargetId = -1;
    
    bool autoSyncAspectRatio = true;
    bool usePhysicalUnits = false;
    bool enableExposure = true;
    bool enableChromaticAberration = false;
    bool enableVignette = false;
    bool enableDepthOfField = false;
    bool enableBloom = true;
    bool enableMotionBlur = false;
    bool enableTemporalAA = true;
    float focalDistance = 10.0f;
    float hyperfocalDistance = 10.0f;
    float maxBlur = 1.0f;
    
    Vec3 getForward() const { return transform ? transform->forward() : Vec3::forward(); }
    Vec3 getRight() const { return transform ? transform->right() : Vec3::right(); }
    Vec3 getUp() const { return transform ? transform->up() : Vec3::up(); }
    
    Mat4 getViewMatrix() const;
    Mat4 getProjectionMatrix() const;
    Mat4 getViewProjectionMatrix() const;
    
    void setFov(f32 f);
    void setNearPlane(f32 n);
    void setFarPlane(f32 f);
    void setOrthoSize(f32 size);
    void setAspectRatio(f32 aspect);
    void getResolution(u32& w, u32& h) const;
    
    TransformComponent* transform = nullptr;
    
    ComponentType getType() const override { return type; }
    const char* getName() const override { return "Camera"; }
    void onAttach(Entity e) override {}
    void onDetach(Entity e) override {}
    void onEnable() override {}
    void onDisable() override {}
    void onTransformChanged() override {}
    IComponent* clone() const override { return new CameraComponent(*this); }
};

enum class PrimitiveType : u8 {
    None,
    Box,
    Sphere,
    Capsule,
    Cylinder,
    Cone,
    Plane,
    Quad,
    Point,
    Line,
    Bvh,
    Convex,
    Mesh,
};

struct BoundingBox {
    Vec3 min = Vec3(1e30f);
    Vec3 max = Vec3(-1e30f);
    
    BoundingBox() = default;
    BoundingBox(const Vec3& min, const Vec3& max) : min(min), max(max) {}
    BoundingBox(const Vec3& center, f32 halfExtent) 
        : min(center - halfExtent), max(center + halfExtent) {}
    
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extents() const { return max - min; }
    f32 radius() const { return extents().length() * 0.5f; }
    
    void expand(const Vec3& point) {
        min = min.min(point);
        max = max.max(point);
    }
    void expand(const BoundingBox& other) {
        min = min.min(other.min);
        max = max.max(other.max);
    }
    
    bool intersects(const BoundingBox& other) const {
        return (min.x <= other.max.x && max.x >= other.min.x) &&
               (min.y <= other.max.y && max.y >= other.min.y) &&
               (min.z <= other.max.z && max.z >= other.min.z);
    }
    bool contains(const Vec3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }
    
    static BoundingBox empty() { return BoundingBox(Vec3(1e30f), Vec3(-1e30f)); }
};

struct BoundingSphere {
    Vec3 center = Vec3::zero();
    f32 radius = 0.0f;
    
    BoundingSphere() = default;
    BoundingSphere(const Vec3& c, f32 r) : center(c), radius(r) {}
    
    bool intersects(const BoundingSphere& other) const {
        f32 distSq = (center - other.center).lengthSquared();
        f32 radSum = radius + other.radius;
        return distSq <= radSum * radSum;
    }
    bool contains(const Vec3& point) const {
        return (point - center).lengthSquared() <= radius * radius;
    }
    
    static BoundingSphere empty() { return BoundingSphere(Vec3::zero(), 0.0f); }
};

struct MeshComponent : IComponent {
    static constexpr ComponentType type = ComponentType::Mesh;
    
    PrimitiveType primitiveType = PrimitiveType::Box;
    Vector<Vec3> vertices;
    Vector<u32> indices;
    Vector<Vec3> normals;
    Vector<Vec2> uvs;
    Vector<Vec4> tangents;
    Vector<Vec4> colors;
    Vector<Vec4> skinIndices;
    Vector<Vec4> skinWeights;
    
    u32 materialId = 0;
    u32 skeletonId = 0;
    bool castShadows = true;
    bool receiveShadows = true;
    bool culling = true;
    bool frustumCulled = true;
    bool batched = false;
    bool visible = true;
    
    BoundingBox getBoundingBox() const;
    BoundingSphere getBoundingSphere() const;
    
    ComponentType getType() const override { return type; }
    const char* getName() const override { return "Mesh"; }
    void onAttach(Entity e) override {}
    void onDetach(Entity e) override {}
    void onEnable() override {}
    void onDisable() override {}
    void onTransformChanged() override {}
    IComponent* clone() const override { return new MeshComponent(*this); }
};

struct MaterialComponent : IComponent {
    static constexpr ComponentType type = ComponentType::Material;
    
    struct TextureSlot {
        String name;
        Renderer::Texture* texture = nullptr;
        Renderer::Sampler* sampler = nullptr;
    };
    
    struct Parameter {
        String name;
        enum class Type : u8 { Float, Vec2, Vec3, Vec4, Int, Bool, Texture } type;
        union Value {
            f32 floatValue;
            Vec2 vec2Value;
            Vec3 vec3Value;
            Vec4 vec4Value;
            i32 intValue;
            bool boolValue;
        } value;
    };
    
    String shaderName;
    u32 priority = 0;
    bool depthWrite = true;
    bool depthTest = true;
    bool stencilTest = false;
    bool blending = false;
    Renderer::BlendFunc srcBlend = Renderer::BlendFunc::SrcAlpha;
    Renderer::BlendFunc dstBlend = Renderer::BlendFunc::OneMinusSrcAlpha;
    Renderer::CompareOp depthFunc = Renderer::CompareOp::LessOrEqual;
    u8 stencilRef = 0;
    
    Vector<TextureSlot> textures;
    Vector<Parameter> parameters;
    
    void setTexture(const String& name, Renderer::Texture* tex, Renderer::Sampler* samp = nullptr);
    void setFloat(const String& name, f32 value);
    void setVec3(const String& name, const Vec3& value);
    void setVec4(const String& name, const Vec4& value);
    
    ComponentType getType() const override { return type; }
    const char* getName() const override { return "Material"; }
    void onAttach(Entity e) override {}
    void onDetach(Entity e) override {}
    void onEnable() override {}
    void onDisable() override {}
    void onTransformChanged() override {}
    IComponent* clone() const override { return new MaterialComponent(*this); }
};

struct RigidBodyComponent : IComponent {
    static constexpr ComponentType type = ComponentType::RigidBody;
    
    enum class BodyType : u8 {
        Dynamic,
        Static,
        Kinematic,
    };
    
    enum class Constraint : u8 {
        None = 0,
        LockPosition = 1 << 0,
        LockRotation = 1 << 1,
        DisableGravity = 1 << 2,
        KinematicPosition = 1 << 3,
        KinematicRotation = 1 << 4,
    };
    
    BodyType bodyType = BodyType::Dynamic;
    u32 constraints = static_cast<u32>(Constraint::None);
    f32 mass = 1.0f;
    f32 drag = 0.0f;
    f32 angularDrag = 0.05f;
    f32 linearDamping = 0.0f;
    f32 angularDamping = 0.0f;
    Vec3 centerOfMass = Vec3::zero();
    Vec3 inertia = Vec3::one();
    
    Vec3 velocity = Vec3::zero();
    Vec3 angularVelocity = Vec3::zero();
    f32 maxVelocity = 500.0f;
    f32 maxAngularVelocity = 100.0f;
    
    bool continuousCollision = false;
    bool freeze = false;
    Entity linkedEntity;
    
    void addForce(const Vec3& force);
    void addImpulse(const Vec3& impulse);
    void addTorque(const Vec3& torque);
    void addAngularImpulse(const Vec3& impulse);
    void setVelocity(const Vec3& vel);
    Vec3 getVelocity() const;
    void setAngularVelocity(const Vec3& vel);
    Vec3 getAngularVelocity() const;
    
    ComponentType getType() const override { return type; }
    const char* getName() const override { return "RigidBody"; }
    void onAttach(Entity e) override {}
    void onDetach(Entity e) override {}
    void onEnable() override {}
    void onDisable() override {}
    void onTransformChanged() override {}
    IComponent* clone() const override { return new RigidBodyComponent(*this); }
};

struct ColliderComponent : IComponent {
    static constexpr ComponentType type = ComponentType::Collider;
    
    enum class Shape : u8 {
        Box,
        Sphere,
        Capsule,
        Mesh,
        Convex,
    };
    
    Shape shape = Shape::Box;
    Vec3 size = Vec3::one();
    f32 radius = 0.5f;
    f32 height = 1.0f;
    Vec3 offset = Vec3::zero();
    bool isTrigger = false;
    u32 layer = 0;
    u32 mask = 0xFFFFFFFF;
    
    ComponentType getType() const override { return type; }
    const char* getName() const override { return "Collider"; }
    void onAttach(Entity e) override {}
    void onDetach(Entity e) override {}
    void onEnable() override {}
    void onDisable() override {}
    void onTransformChanged() override {}
    IComponent* clone() const override { return new ColliderComponent(*this); }
};

}
