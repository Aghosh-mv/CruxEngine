#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/HashMap.h"
#include "Core/String.h"
#include "Core/Vec3.h"
#include "Core/Math.h"

namespace Frost {

enum class MaterialPinType : u8 {
    Float,
    Vec3,
    Vec4,
    Texture,
    Bool
};

struct MaterialPin {
    u32 nodeId = 0;
    u32 pinIndex = 0;
    MaterialPinType type = MaterialPinType::Float;
    String name;
};

enum class MaterialNodeType : u8 {
    Constant,
    TextureSample,
    Add,
    Subtract,
    Multiply,
    Divide,
    Normalize,
    Dot,
    Mix,
    Noise,
    Fresnel,
    Albedo,
    Normal,
    Roughness,
    Metallic,
    Emissive,
    Output,
    KitrisScript
};

struct MaterialNode {
    u32 id = 0;
    MaterialNodeType type = MaterialNodeType::Constant;
    Vector<MaterialPin> inputs;
    Vector<MaterialPin> outputs;
    Vec3 vecValue;
    f32 floatValue = 0.0f;
    String textureName;
    String scriptFunction;
    String name;
};

struct MaterialLink {
    u32 fromNode = 0;
    u32 fromPin = 0;
    u32 toNode = 0;
    u32 toPin = 0;
};

struct MaterialGraphResult {
    Vec4 albedo;
    f32 roughness = 0.5f;
    f32 metallic = 0.0f;
    Vec4 emissive;
    bool valid = false;
};

struct MaterialGraphStats {
    u32 nodes = 0;
    u32 links = 0;
    u32 scriptsBound = 0;
    u64 evaluations = 0;
};

struct MaterialTextureBinding {
    u32 textureId = 0;
    u32 slot = 0;
};

struct MaterialInstance {
    u32 baseMaterialId = 0;
    HashMap<String, f32> parameterValues;
    Vector<MaterialTextureBinding> textureBindings;
    f32 opacity = 1.0f;
    f32 roughnessMultiplier = 1.0f;
    f32 metallicMultiplier = 1.0f;
};

struct ShaderPermutation {
    u32 materialId = 0;
    u32 featureFlags = 0;
    Vector<String> defines;
};

class MaterialGraph {
public:
    MaterialGraph() = default;

    u32 addNode(MaterialNodeType type, const String& name);
    bool connect(u32 fromNode, u32 fromPin, u32 toNode, u32 toPin);
    bool removeNode(u32 nodeId);
    void addPin(u32 nodeId, MaterialPinType type, const String& name, bool isOutput);

    MaterialNode* getNode(u32 nodeId);
    const MaterialNode* getNode(u32 nodeId) const;

    const Vector<MaterialNode>& getNodes() const;
    const Vector<MaterialLink>& getLinks() const;

    String compile() const;
    void clear();

    Vec4 evaluate(u32 nodeId, const Vec3& uv, const Vec3& worldPos, f32 time) const;
    MaterialGraphResult evaluateMaterial(const Vec3& uv, const Vec3& worldPos, f32 time) const;

    bool bindScriptFunction(const String& nodeName, const String& functionName);
    Vec4 callKitrisScript(const String& functionName, const Vec4& arg, f32 time) const;

    MaterialGraphStats getStats() const;

    String serialize() const;
    bool deserialize(const String& text);

    u32 createMaterialInstance(u32 baseMaterialId);
    void destroyMaterialInstance(u32 id);
    void setInstanceParameter(u32 id, const char* name, f32 value);
    void setInstanceTexture(u32 id, u32 textureId, u32 slot);
    f32 getInstanceParameter(u32 id, const char* name) const;
    MaterialInstance* getMaterialInstance(u32 id);
    const MaterialInstance* getMaterialInstance(u32 id) const;

    u32 compilePermutation(u32 materialId, u32 featureFlags);
    ShaderPermutation* getPermutation(u32 id);
    const ShaderPermutation* getPermutation(u32 id) const;
    u32 getPermutationCount() const;
    u32 getCompiledPermutationCount() const;
    void invalidatePermutations();
    void remapParameter(u32 permutationId, u32 parameterIndex, const char* newName);

private:
    MaterialNode* findNode(u32 nodeId);
    const MaterialNode* findNode(u32 nodeId) const;
    Vec4 evaluateNode(u32 nodeId, const Vec3& uv, const Vec3& worldPos, f32 time) const;
    void topologicalSort(Vector<u32>& out) const;
    Vec4 hashVec4(const Vec4& v) const;

    Vector<MaterialNode> nodes_;
    Vector<MaterialLink> links_;
    mutable HashMap<u32, Vec4> evalCache_;
    u32 nextNodeId_ = 0;
    mutable u64 evalCount_ = 0;

    Vector<MaterialInstance> materialInstances_;
    Vector<ShaderPermutation> shaderPermutations_;
    u32 maxInstances_ = 256;
    HashMap<u32, u32> permutationCache_;
    u32 compiledPermutations_ = 0;
    f32 compileTimeMs_ = 0.0f;
};

}
