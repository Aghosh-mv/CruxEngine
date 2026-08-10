#include "Renderer/MaterialGraph.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace Frost {

static MaterialPinType nodeTypeToPinType(MaterialNodeType t) {
    switch (t) {
        case MaterialNodeType::Constant: return MaterialPinType::Vec4;
        case MaterialNodeType::TextureSample: return MaterialPinType::Texture;
        case MaterialNodeType::Add: return MaterialPinType::Vec4;
        case MaterialNodeType::Subtract: return MaterialPinType::Vec4;
        case MaterialNodeType::Multiply: return MaterialPinType::Vec4;
        case MaterialNodeType::Divide: return MaterialPinType::Vec4;
        case MaterialNodeType::Normalize: return MaterialPinType::Vec3;
        case MaterialNodeType::Dot: return MaterialPinType::Float;
        case MaterialNodeType::Mix: return MaterialPinType::Vec4;
        case MaterialNodeType::Noise: return MaterialPinType::Float;
        case MaterialNodeType::Fresnel: return MaterialPinType::Float;
        case MaterialNodeType::Albedo: return MaterialPinType::Vec4;
        case MaterialNodeType::Normal: return MaterialPinType::Vec3;
        case MaterialNodeType::Roughness: return MaterialPinType::Float;
        case MaterialNodeType::Metallic: return MaterialPinType::Float;
        case MaterialNodeType::Emissive: return MaterialPinType::Vec4;
        case MaterialNodeType::Output: return MaterialPinType::Vec4;
        case MaterialNodeType::KitrisScript: return MaterialPinType::Vec4;
    }
    return MaterialPinType::Float;
}

static const char* nodeTypeToString(MaterialNodeType t) {
    switch (t) {
        case MaterialNodeType::Constant: return "Constant";
        case MaterialNodeType::TextureSample: return "TextureSample";
        case MaterialNodeType::Add: return "Add";
        case MaterialNodeType::Subtract: return "Subtract";
        case MaterialNodeType::Multiply: return "Multiply";
        case MaterialNodeType::Divide: return "Divide";
        case MaterialNodeType::Normalize: return "Normalize";
        case MaterialNodeType::Dot: return "Dot";
        case MaterialNodeType::Mix: return "Mix";
        case MaterialNodeType::Noise: return "Noise";
        case MaterialNodeType::Fresnel: return "Fresnel";
        case MaterialNodeType::Albedo: return "Albedo";
        case MaterialNodeType::Normal: return "Normal";
        case MaterialNodeType::Roughness: return "Roughness";
        case MaterialNodeType::Metallic: return "Metallic";
        case MaterialNodeType::Emissive: return "Emissive";
        case MaterialNodeType::Output: return "Output";
        case MaterialNodeType::KitrisScript: return "KitrisScript";
    }
    return "Unknown";
}

static MaterialNodeType stringToNodeType(const String& s) {
    if (s == "Constant") return MaterialNodeType::Constant;
    if (s == "TextureSample") return MaterialNodeType::TextureSample;
    if (s == "Add") return MaterialNodeType::Add;
    if (s == "Subtract") return MaterialNodeType::Subtract;
    if (s == "Multiply") return MaterialNodeType::Multiply;
    if (s == "Divide") return MaterialNodeType::Divide;
    if (s == "Normalize") return MaterialNodeType::Normalize;
    if (s == "Dot") return MaterialNodeType::Dot;
    if (s == "Mix") return MaterialNodeType::Mix;
    if (s == "Noise") return MaterialNodeType::Noise;
    if (s == "Fresnel") return MaterialNodeType::Fresnel;
    if (s == "Albedo") return MaterialNodeType::Albedo;
    if (s == "Normal") return MaterialNodeType::Normal;
    if (s == "Roughness") return MaterialNodeType::Roughness;
    if (s == "Metallic") return MaterialNodeType::Metallic;
    if (s == "Emissive") return MaterialNodeType::Emissive;
    if (s == "Output") return MaterialNodeType::Output;
    if (s == "KitrisScript") return MaterialNodeType::KitrisScript;
    return MaterialNodeType::Constant;
}

static const char* pinTypeToString(MaterialPinType t) {
    switch (t) {
        case MaterialPinType::Float: return "Float";
        case MaterialPinType::Vec3: return "Vec3";
        case MaterialPinType::Vec4: return "Vec4";
        case MaterialPinType::Texture: return "Texture";
        case MaterialPinType::Bool: return "Bool";
    }
    return "Float";
}

static MaterialPinType stringToPinType(const String& s) {
    if (s == "Float") return MaterialPinType::Float;
    if (s == "Vec3") return MaterialPinType::Vec3;
    if (s == "Vec4") return MaterialPinType::Vec4;
    if (s == "Texture") return MaterialPinType::Texture;
    if (s == "Bool") return MaterialPinType::Bool;
    return MaterialPinType::Float;
}

static String readToken(const String& line, usize& pos) {
    while (pos < line.length() && line[pos] == ' ') pos++;
    usize start = pos;
    while (pos < line.length() && line[pos] != ' ' && line[pos] != '\n' && line[pos] != '\r') pos++;
    return line.substr(start, pos - start);
}

static u32 hashCombine(u32 seed, u32 val) {
    seed ^= val + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    return seed;
}

MaterialNode* MaterialGraph::findNode(u32 nodeId) {
    for (usize i = 0; i < nodes_.size(); i++) {
        if (nodes_[i].id == nodeId) return &nodes_[i];
    }
    return nullptr;
}

const MaterialNode* MaterialGraph::findNode(u32 nodeId) const {
    for (usize i = 0; i < nodes_.size(); i++) {
        if (nodes_[i].id == nodeId) return &nodes_[i];
    }
    return nullptr;
}

u32 MaterialGraph::addNode(MaterialNodeType type, const String& name) {
    u32 id = ++nextNodeId_;
    MaterialNode node;
    node.id = id;
    node.type = type;
    node.name = name;

    switch (type) {
        case MaterialNodeType::Constant:
            node.outputs.pushBack({id, 0, MaterialPinType::Vec4, "value"});
            break;
        case MaterialNodeType::TextureSample:
            node.inputs.pushBack({id, 0, MaterialPinType::Vec3, "uv"});
            node.outputs.pushBack({id, 0, MaterialPinType::Vec4, "color"});
            break;
        case MaterialNodeType::Add:
            node.inputs.pushBack({id, 0, MaterialPinType::Vec4, "a"});
            node.inputs.pushBack({id, 1, MaterialPinType::Vec4, "b"});
            node.outputs.pushBack({id, 0, MaterialPinType::Vec4, "result"});
            break;
        case MaterialNodeType::Subtract:
            node.inputs.pushBack({id, 0, MaterialPinType::Vec4, "a"});
            node.inputs.pushBack({id, 1, MaterialPinType::Vec4, "b"});
            node.outputs.pushBack({id, 0, MaterialPinType::Vec4, "result"});
            break;
        case MaterialNodeType::Multiply:
            node.inputs.pushBack({id, 0, MaterialPinType::Vec4, "a"});
            node.inputs.pushBack({id, 1, MaterialPinType::Vec4, "b"});
            node.outputs.pushBack({id, 0, MaterialPinType::Vec4, "result"});
            break;
        case MaterialNodeType::Divide:
            node.inputs.pushBack({id, 0, MaterialPinType::Vec4, "a"});
            node.inputs.pushBack({id, 1, MaterialPinType::Vec4, "b"});
            node.outputs.pushBack({id, 0, MaterialPinType::Vec4, "result"});
            break;
        case MaterialNodeType::Normalize:
            node.inputs.pushBack({id, 0, MaterialPinType::Vec3, "input"});
            node.outputs.pushBack({id, 0, MaterialPinType::Vec3, "result"});
            break;
        case MaterialNodeType::Dot:
            node.inputs.pushBack({id, 0, MaterialPinType::Vec3, "a"});
            node.inputs.pushBack({id, 1, MaterialPinType::Vec3, "b"});
            node.outputs.pushBack({id, 0, MaterialPinType::Float, "result"});
            break;
        case MaterialNodeType::Mix:
            node.inputs.pushBack({id, 0, MaterialPinType::Vec4, "a"});
            node.inputs.pushBack({id, 1, MaterialPinType::Vec4, "b"});
            node.inputs.pushBack({id, 2, MaterialPinType::Float, "t"});
            node.outputs.pushBack({id, 0, MaterialPinType::Vec4, "result"});
            break;
        case MaterialNodeType::Noise:
            node.inputs.pushBack({id, 0, MaterialPinType::Vec3, "position"});
            node.outputs.pushBack({id, 0, MaterialPinType::Float, "result"});
            break;
        case MaterialNodeType::Fresnel:
            node.inputs.pushBack({id, 0, MaterialPinType::Vec3, "normal"});
            node.inputs.pushBack({id, 1, MaterialPinType::Vec3, "viewDir"});
            node.outputs.pushBack({id, 0, MaterialPinType::Float, "result"});
            break;
        case MaterialNodeType::Albedo:
            node.outputs.pushBack({id, 0, MaterialPinType::Vec4, "color"});
            break;
        case MaterialNodeType::Normal:
            node.outputs.pushBack({id, 0, MaterialPinType::Vec3, "direction"});
            break;
        case MaterialNodeType::Roughness:
            node.outputs.pushBack({id, 0, MaterialPinType::Float, "value"});
            break;
        case MaterialNodeType::Metallic:
            node.outputs.pushBack({id, 0, MaterialPinType::Float, "value"});
            break;
        case MaterialNodeType::Emissive:
            node.outputs.pushBack({id, 0, MaterialPinType::Vec4, "color"});
            break;
        case MaterialNodeType::Output:
            node.inputs.pushBack({id, 0, MaterialPinType::Vec4, "albedo"});
            node.inputs.pushBack({id, 1, MaterialPinType::Float, "roughness"});
            node.inputs.pushBack({id, 2, MaterialPinType::Float, "metallic"});
            node.inputs.pushBack({id, 3, MaterialPinType::Vec4, "emissive"});
            break;
        case MaterialNodeType::KitrisScript:
            node.inputs.pushBack({id, 0, MaterialPinType::Vec4, "input"});
            node.outputs.pushBack({id, 0, MaterialPinType::Vec4, "result"});
            break;
    }

    nodes_.pushBack(node);
    return id;
}

bool MaterialGraph::connect(u32 fromNode, u32 fromPin, u32 toNode, u32 toPin) {
    const MaterialNode* src = findNode(fromNode);
    const MaterialNode* dst = findNode(toNode);
    if (!src || !dst) return false;
    if (fromPin >= src->outputs.size()) return false;
    if (toPin >= dst->inputs.size()) return false;

    for (usize i = 0; i < links_.size(); i++) {
        if (links_[i].toNode == toNode && links_[i].toPin == toPin) {
            links_.erase(i);
            break;
        }
    }

    MaterialLink link;
    link.fromNode = fromNode;
    link.fromPin = fromPin;
    link.toNode = toNode;
    link.toPin = toPin;
    links_.pushBack(link);
    return true;
}

bool MaterialGraph::removeNode(u32 nodeId) {
    for (usize i = 0; i < nodes_.size(); i++) {
        if (nodes_[i].id == nodeId) {
            nodes_.erase(i);
            break;
        }
    }

    usize i = 0;
    while (i < links_.size()) {
        if (links_[i].fromNode == nodeId || links_[i].toNode == nodeId) {
            links_.erase(i);
        } else {
            i++;
        }
    }

    return true;
}

void MaterialGraph::addPin(u32 nodeId, MaterialPinType type, const String& name, bool isOutput) {
    MaterialNode* node = findNode(nodeId);
    if (!node) return;

    MaterialPin pin;
    pin.nodeId = nodeId;
    pin.type = type;
    pin.name = name;

    if (isOutput) {
        pin.pinIndex = (u32)node->outputs.size();
        node->outputs.pushBack(pin);
    } else {
        pin.pinIndex = (u32)node->inputs.size();
        node->inputs.pushBack(pin);
    }
}

MaterialNode* MaterialGraph::getNode(u32 nodeId) {
    return findNode(nodeId);
}

const MaterialNode* MaterialGraph::getNode(u32 nodeId) const {
    return findNode(nodeId);
}

const Vector<MaterialNode>& MaterialGraph::getNodes() const {
    return nodes_;
}

const Vector<MaterialLink>& MaterialGraph::getLinks() const {
    return links_;
}

void MaterialGraph::topologicalSort(Vector<u32>& out) const {
    Vector<u32> inDegree;
    inDegree.resize(nodes_.size(), 0);
    for (usize i = 0; i < nodes_.size(); i++) {
        for (usize j = 0; j < links_.size(); j++) {
            if (links_[j].toNode == nodes_[i].id) {
                inDegree[i]++;
            }
        }
    }

    Vector<u32> queue;
    for (usize i = 0; i < nodes_.size(); i++) {
        if (inDegree[i] == 0) queue.pushBack(i);
    }

    out.clear();
    usize qIdx = 0;
    while (qIdx < queue.size()) {
        u32 idx = queue[qIdx++];
        out.pushBack(nodes_[idx].id);
        for (usize i = 0; i < nodes_.size(); i++) {
            if (i == idx) continue;
            for (usize j = 0; j < links_.size(); j++) {
                if (links_[j].fromNode == nodes_[idx].id && links_[j].toNode == nodes_[i].id) {
                    inDegree[i]--;
                    if (inDegree[i] == 0) queue.pushBack(i);
                }
            }
        }
    }

    for (usize i = 0; i < nodes_.size(); i++) {
        bool found = false;
        for (usize j = 0; j < out.size(); j++) {
            if (out[j] == nodes_[i].id) { found = true; break; }
        }
        if (!found) out.pushBack(nodes_[i].id);
    }
}

String MaterialGraph::compile() const {
    String result;
    result.append("// Material Graph Shader\n");
    result.append("// Generated by FrostEngine MaterialGraph\n\n");

    Vector<u32> sorted;
    topologicalSort(sorted);

    for (usize i = 0; i < sorted.size(); i++) {
        const MaterialNode* node = findNode(sorted[i]);
        if (!node) continue;

        String nodeName = "node_" + String::fromInt(node->id);

        switch (node->type) {
            case MaterialNodeType::Constant:
                result.append("vec4 " + nodeName + " = vec4(" +
                    String::fromFloat(node->vecValue.x, 3) + ", " +
                    String::fromFloat(node->vecValue.y, 3) + ", " +
                    String::fromFloat(node->vecValue.z, 3) + ", " +
                    String::fromFloat(node->floatValue, 3) + ");\n");
                break;
            case MaterialNodeType::TextureSample:
                result.append("vec4 " + nodeName + " = texture(u_texture, " +
                    "node_" + String::fromInt(node->inputs[0].nodeId) + ".xyz);\n");
                break;
            case MaterialNodeType::Add:
                result.append("vec4 " + nodeName + " = node_" +
                    String::fromInt(node->inputs[0].nodeId) + " + node_" +
                    String::fromInt(node->inputs[1].nodeId) + ";\n");
                break;
            case MaterialNodeType::Subtract:
                result.append("vec4 " + nodeName + " = node_" +
                    String::fromInt(node->inputs[0].nodeId) + " - node_" +
                    String::fromInt(node->inputs[1].nodeId) + ";\n");
                break;
            case MaterialNodeType::Multiply:
                result.append("vec4 " + nodeName + " = node_" +
                    String::fromInt(node->inputs[0].nodeId) + " * node_" +
                    String::fromInt(node->inputs[1].nodeId) + ";\n");
                break;
            case MaterialNodeType::Divide:
                result.append("vec4 " + nodeName + " = node_" +
                    String::fromInt(node->inputs[0].nodeId) + " / node_" +
                    String::fromInt(node->inputs[1].nodeId) + ";\n");
                break;
            case MaterialNodeType::Normalize:
                result.append("vec3 " + nodeName + " = normalize(node_" +
                    String::fromInt(node->inputs[0].nodeId) + ".xyz);\n");
                break;
            case MaterialNodeType::Dot:
                result.append("float " + nodeName + " = dot(node_" +
                    String::fromInt(node->inputs[0].nodeId) + ".xyz, node_" +
                    String::fromInt(node->inputs[1].nodeId) + ".xyz);\n");
                break;
            case MaterialNodeType::Mix:
                result.append("vec4 " + nodeName + " = mix(node_" +
                    String::fromInt(node->inputs[0].nodeId) + ", node_" +
                    String::fromInt(node->inputs[1].nodeId) + ", node_" +
                    String::fromInt(node->inputs[2].nodeId) + ".x);\n");
                break;
            case MaterialNodeType::Noise:
                result.append("float " + nodeName + " = noise(node_" +
                    String::fromInt(node->inputs[0].nodeId) + ".xyz);\n");
                break;
            case MaterialNodeType::Fresnel:
                result.append("float " + nodeName + " = fresnel(node_" +
                    String::fromInt(node->inputs[0].nodeId) + ".xyz, node_" +
                    String::fromInt(node->inputs[1].nodeId) + ".xyz);\n");
                break;
            case MaterialNodeType::Albedo:
                result.append("vec4 " + nodeName + " = u_albedo;\n");
                break;
            case MaterialNodeType::Normal:
                result.append("vec3 " + nodeName + " = u_normal;\n");
                break;
            case MaterialNodeType::Roughness:
                result.append("float " + nodeName + " = u_roughness;\n");
                break;
            case MaterialNodeType::Metallic:
                result.append("float " + nodeName + " = u_metallic;\n");
                break;
            case MaterialNodeType::Emissive:
                result.append("vec4 " + nodeName + " = u_emissive;\n");
                break;
            case MaterialNodeType::KitrisScript:
                result.append("vec4 " + nodeName + " = kitris_" +
                    node->scriptFunction + "(node_" +
                    String::fromInt(node->inputs[0].nodeId) + ");\n");
                break;
            case MaterialNodeType::Output:
                result.append("vec4 " + nodeName + "_albedo = node_" +
                    String::fromInt(node->inputs[0].nodeId) + ";\n");
                result.append("float " + nodeName + "_roughness = node_" +
                    String::fromInt(node->inputs[1].nodeId) + ".x;\n");
                result.append("float " + nodeName + "_metallic = node_" +
                    String::fromInt(node->inputs[2].nodeId) + ".x;\n");
                result.append("vec4 " + nodeName + "_emissive = node_" +
                    String::fromInt(node->inputs[3].nodeId) + ";\n");
                break;
        }
        result.append("\n");
    }

    return result;
}

void MaterialGraph::clear() {
    nodes_.clear();
    links_.clear();
    evalCache_.clear();
    nextNodeId_ = 0;
    evalCount_ = 0;
}

Vec4 MaterialGraph::hashVec4(const Vec4& v) const {
    u32 h = 2166136261u;
    const u32* ptr = reinterpret_cast<const u32*>(&v.x);
    for (usize i = 0; i < 4; i++) {
        h ^= ptr[i];
        h *= 16777619u;
    }
    f32 fx = (f32)(h & 0xFFFFu) / 65535.0f;
    f32 fy = (f32)((h >> 8) & 0xFFFFu) / 65535.0f;
    f32 fz = (f32)((h >> 16) & 0xFFFFu) / 65535.0f;
    f32 fw = (f32)((h >> 24) & 0xFFu) / 255.0f;
    return Vec4(fx, fy, fz, fw);
}

Vec4 MaterialGraph::callKitrisScript(const String& functionName, const Vec4& arg, f32 time) const {
    u32 h = 2166136261u;
    for (u32 i = 0; i < functionName.length(); i++) {
        h ^= (u8)functionName[i];
        h *= 16777619u;
    }
    const u32* ptr = reinterpret_cast<const u32*>(&arg.x);
    for (usize i = 0; i < 4; i++) {
        h = hashCombine(h, ptr[i]);
    }
    u32 th;
    memcpy(&th, &time, sizeof(f32));
    h = hashCombine(h, th);

    f32 fx = (f32)(h & 0xFFFFu) / 65535.0f;
    f32 fy = (f32)((h >> 8) & 0xFFFFu) / 65535.0f;
    f32 fz = (f32)((h >> 16) & 0xFFFFu) / 65535.0f;
    f32 fw = (f32)((h >> 24) & 0xFFu) / 255.0f;
    return Vec4(fx, fy, fz, fw);
}

Vec4 MaterialGraph::evaluateNode(u32 nodeId, const Vec3& uv, const Vec3& worldPos, f32 time) const {
    evalCount_++;

    auto it = evalCache_.find(nodeId);
    if (it != evalCache_.end()) return it.value();

    const MaterialNode* node = findNode(nodeId);
    if (!node) return Vec4(0, 0, 0, 1);

    Vec4 result(0, 0, 0, 1);

    auto getInput = [&](u32 pinIdx) -> Vec4 {
        for (usize i = 0; i < links_.size(); i++) {
            if (links_[i].toNode == nodeId && links_[i].toPin == pinIdx) {
                return evaluateNode(links_[i].fromNode, uv, worldPos, time);
            }
        }
        return Vec4(0, 0, 0, 1);
    };

    switch (node->type) {
        case MaterialNodeType::Constant:
            result = Vec4(node->vecValue.x, node->vecValue.y, node->vecValue.z, node->floatValue);
            break;
        case MaterialNodeType::TextureSample:
            result = Vec4(0.5f, 0.5f, 0.5f, 1.0f);
            break;
        case MaterialNodeType::Add: {
            Vec4 a = getInput(0);
            Vec4 b = getInput(1);
            result = Vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
            break;
        }
        case MaterialNodeType::Subtract: {
            Vec4 a = getInput(0);
            Vec4 b = getInput(1);
            result = Vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
            break;
        }
        case MaterialNodeType::Multiply: {
            Vec4 a = getInput(0);
            Vec4 b = getInput(1);
            result = Vec4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);
            break;
        }
        case MaterialNodeType::Divide: {
            Vec4 a = getInput(0);
            Vec4 b = getInput(1);
            result = Vec4(
                b.x != 0.0f ? a.x / b.x : 0.0f,
                b.y != 0.0f ? a.y / b.y : 0.0f,
                b.z != 0.0f ? a.z / b.z : 0.0f,
                b.w != 0.0f ? a.w / b.w : 0.0f
            );
            break;
        }
        case MaterialNodeType::Normalize: {
            Vec4 in = getInput(0);
            Vec3 v(in.x, in.y, in.z);
            Vec3 n = v.normalized();
            result = Vec4(n.x, n.y, n.z, 0.0f);
            break;
        }
        case MaterialNodeType::Dot: {
            Vec4 a = getInput(0);
            Vec4 b = getInput(1);
            Vec3 va(a.x, a.y, a.z);
            Vec3 vb(b.x, b.y, b.z);
            f32 d = va.dot(vb);
            result = Vec4(d, d, d, d);
            break;
        }
        case MaterialNodeType::Mix: {
            Vec4 a = getInput(0);
            Vec4 b = getInput(1);
            Vec4 t = getInput(2);
            f32 lerp = t.x;
            result = Vec4(
                a.x + (b.x - a.x) * lerp,
                a.y + (b.y - a.y) * lerp,
                a.z + (b.z - a.z) * lerp,
                a.w + (b.w - a.w) * lerp
            );
            break;
        }
        case MaterialNodeType::Noise: {
            Vec4 in = getInput(0);
            Vec3 p(in.x, in.y, in.z);
            f32 n = std::sin(p.x * 12.9898f + p.y * 78.233f + p.z * 45.164f) * 43758.5453f;
            n = n - std::floor(n);
            result = Vec4(n, n, n, n);
            break;
        }
        case MaterialNodeType::Fresnel: {
            Vec4 n4 = getInput(0);
            Vec4 v4 = getInput(1);
            Vec3 n(n4.x, n4.y, n4.z);
            Vec3 v(v4.x, v4.y, v4.z);
            Vec3 nn = n.normalized();
            Vec3 vn = v.normalized();
            f32 d = nn.dot(vn);
            f32 f = 1.0f - Mathf::saturate(std::abs(d));
            f = f * f * f;
            result = Vec4(f, f, f, f);
            break;
        }
        case MaterialNodeType::Albedo:
            result = Vec4(node->vecValue.x, node->vecValue.y, node->vecValue.z, node->floatValue);
            break;
        case MaterialNodeType::Normal:
            result = Vec4(node->vecValue.x, node->vecValue.y, node->vecValue.z, 0.0f);
            break;
        case MaterialNodeType::Roughness:
            result = Vec4(node->floatValue, node->floatValue, node->floatValue, node->floatValue);
            break;
        case MaterialNodeType::Metallic:
            result = Vec4(node->floatValue, node->floatValue, node->floatValue, node->floatValue);
            break;
        case MaterialNodeType::Emissive:
            result = Vec4(node->vecValue.x, node->vecValue.y, node->vecValue.z, node->floatValue);
            break;
        case MaterialNodeType::Output:
            result = getInput(0);
            break;
        case MaterialNodeType::KitrisScript: {
            Vec4 in = getInput(0);
            result = callKitrisScript(node->scriptFunction, in, time);
            break;
        }
    }

    evalCache_[nodeId] = result;
    return result;
}

Vec4 MaterialGraph::evaluate(u32 nodeId, const Vec3& uv, const Vec3& worldPos, f32 time) const {
    evalCache_.clear();
    evalCount_ = 0;
    Vec4 result = evaluateNode(nodeId, uv, worldPos, time);
    evalCount_++;
    return result;
}

MaterialGraphResult MaterialGraph::evaluateMaterial(const Vec3& uv, const Vec3& worldPos, f32 time) const {
    MaterialGraphResult result;
    result.valid = false;

    const MaterialNode* outputNode = nullptr;
    for (usize i = 0; i < nodes_.size(); i++) {
        if (nodes_[i].type == MaterialNodeType::Output) {
            outputNode = &nodes_[i];
            break;
        }
    }

    if (!outputNode) return result;

    evalCache_.clear();
    evalCount_ = 0;

    Vec4 albedoVal = evaluateNode(outputNode->inputs[0].nodeId, uv, worldPos, time);
    Vec4 roughnessVal = evaluateNode(outputNode->inputs[1].nodeId, uv, worldPos, time);
    Vec4 metallicVal = evaluateNode(outputNode->inputs[2].nodeId, uv, worldPos, time);
    Vec4 emissiveVal = evaluateNode(outputNode->inputs[3].nodeId, uv, worldPos, time);

    result.albedo = albedoVal;
    result.roughness = Mathf::saturate(roughnessVal.x);
    result.metallic = Mathf::saturate(metallicVal.x);
    result.emissive = emissiveVal;
    result.valid = true;

    return result;
}

bool MaterialGraph::bindScriptFunction(const String& nodeName, const String& functionName) {
    for (usize i = 0; i < nodes_.size(); i++) {
        if (nodes_[i].name == nodeName && nodes_[i].type == MaterialNodeType::KitrisScript) {
            nodes_[i].scriptFunction = functionName;
            return true;
        }
    }
    return false;
}

MaterialGraphStats MaterialGraph::getStats() const {
    MaterialGraphStats stats;
    stats.nodes = (u32)nodes_.size();
    stats.links = (u32)links_.size();
    stats.scriptsBound = 0;
    for (usize i = 0; i < nodes_.size(); i++) {
        if (nodes_[i].type == MaterialNodeType::KitrisScript && !nodes_[i].scriptFunction.empty()) {
            stats.scriptsBound++;
        }
    }
    stats.evaluations = evalCount_;
    return stats;
}

String MaterialGraph::serialize() const {
    String result;

    for (usize i = 0; i < nodes_.size(); i++) {
        const MaterialNode& n = nodes_[i];
        result.append("NODE " + String::fromInt(n.id) + " " +
            nodeTypeToString(n.type) + " " + n.name + "\n");

        for (usize j = 0; j < n.inputs.size(); j++) {
            const MaterialPin& p = n.inputs[j];
            result.append("PIN " + String::fromInt(p.nodeId) + " " +
                String::fromInt(p.pinIndex) + " " +
                pinTypeToString(p.type) + " " + p.name + " in\n");
        }
        for (usize j = 0; j < n.outputs.size(); j++) {
            const MaterialPin& p = n.outputs[j];
            result.append("PIN " + String::fromInt(p.nodeId) + " " +
                String::fromInt(p.pinIndex) + " " +
                pinTypeToString(p.type) + " " + p.name + " out\n");
        }

        if (!n.textureName.empty()) {
            result.append("TEXTURE " + n.name + " " + n.textureName + "\n");
        }
        if (!n.scriptFunction.empty()) {
            result.append("SCRIPT " + n.name + " " + n.scriptFunction + "\n");
        }
    }

    for (usize i = 0; i < links_.size(); i++) {
        const MaterialLink& l = links_[i];
        result.append("LINK " + String::fromInt(l.fromNode) + " " +
            String::fromInt(l.fromPin) + " " +
            String::fromInt(l.toNode) + " " +
            String::fromInt(l.toPin) + "\n");
    }

    return result;
}

static i64 parseI64(const String& s) {
    if (s.empty()) return 0;
    bool neg = false;
    usize start = 0;
    if (s[0] == '-') { neg = true; start = 1; }
    i64 val = 0;
    for (usize i = start; i < s.length(); i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            val = val * 10 + (s[i] - '0');
        }
    }
    return neg ? -val : val;
}

bool MaterialGraph::deserialize(const String& text) {
    clear();

    usize pos = 0;
    while (pos < text.length()) {
        usize lineStart = pos;
        while (pos < text.length() && text[pos] != '\n') pos++;
        String line = text.substr(lineStart, pos - lineStart);
        if (pos < text.length()) pos++;

        if (line.empty()) continue;

        usize tPos = 0;
        String cmd = readToken(line, tPos);

        if (cmd == "NODE") {
            String idStr = readToken(line, tPos);
            String typeStr = readToken(line, tPos);
            String name = readToken(line, tPos);
            u32 id = (u32)parseI64(idStr);
            MaterialNodeType type = stringToNodeType(typeStr);
            u32 nodeId = addNode(type, name);
            if (nodeId != id && id > nextNodeId_) {
                nodes_[nodes_.size() - 1].id = id;
                if (id >= nextNodeId_) nextNodeId_ = id;
            }
        } else if (cmd == "LINK") {
            String fromStr = readToken(line, tPos);
            String fromPinStr = readToken(line, tPos);
            String toStr = readToken(line, tPos);
            String toPinStr = readToken(line, tPos);
            u32 fromNode = (u32)parseI64(fromStr);
            u32 fromPin = (u32)parseI64(fromPinStr);
            u32 toNode = (u32)parseI64(toStr);
            u32 toPin = (u32)parseI64(toPinStr);
            connect(fromNode, fromPin, toNode, toPin);
        } else if (cmd == "PIN") {
            String nodeIdStr = readToken(line, tPos);
            String pinIdxStr = readToken(line, tPos);
            String typeStr = readToken(line, tPos);
            String name = readToken(line, tPos);
            String dir = readToken(line, tPos);
            u32 nodeId = (u32)parseI64(nodeIdStr);
            MaterialPinType type = stringToPinType(typeStr);
            bool isOutput = (dir == "out");
            addPin(nodeId, type, name, isOutput);
        } else if (cmd == "SCRIPT") {
            String nodeName = readToken(line, tPos);
            String funcName = readToken(line, tPos);
            bindScriptFunction(nodeName, funcName);
        } else if (cmd == "TEXTURE") {
            String nodeName = readToken(line, tPos);
            String texName = readToken(line, tPos);
            MaterialNode* node = findNode((u32)parseI64(nodeName));
            if (node) node->textureName = texName;
        }
    }

    return true;
}

}
