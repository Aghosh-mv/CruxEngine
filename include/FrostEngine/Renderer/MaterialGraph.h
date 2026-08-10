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
};

}
