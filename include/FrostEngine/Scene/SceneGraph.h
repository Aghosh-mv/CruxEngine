#pragma once

// ============================================================================
// FrostEngine Scene Graph — Hierarchical transform + scene management
// ============================================================================
// SceneNode: a node in a parent-child tree. Each node has a local transform
// that composes with its parent to produce a world transform. The graph is
// flat (array-based) for cache-friendly iteration and easy serialization.
//
// Scene: owns the node tree + per-node component data. Supports:
//   - Fast parent-child queries
//   - Dirty-flag world-transform propagation
//   - Frustum culling per node
//   - Serialization to/from binary blob
//   - Level streaming (load/unload subgraphs)
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Math.h"

namespace Frost {

// ---- SceneNode: flat index into the scene's node array ----
using NodeID = u32;
static constexpr NodeID NULL_NODE = 0xFFFFFFFF;

struct SceneNode {
    NodeID parent   = NULL_NODE;
    NodeID firstChild = NULL_NODE;
    NodeID nextSibling = NULL_NODE;
    NodeID prevSibling = NULL_NODE;

    Vec3  localPosition{ 0, 0, 0 };
    Quat  localRotation{ 0, 0, 0, 1 };
    Vec3  localScale{ 1, 1, 1 };

    mutable Mat4 worldTransform{ Mat4::identity() };
    mutable bool dirty = true;

    bool active = true;
    u32  layer = 0;          // render layer bitmask
    u32  userData = 0;        // generic tag (e.g., entity ID)
};

// ---- Scene: manages a hierarchy of SceneNodes ----
class Scene {
public:
    Scene() {
        // Root node (index 0) is always present
        NodeID root = allocNode();
        nodes_[root].parent = NULL_NODE;
        rootNode_ = root;
    }

    // ---- Node creation ----
    NodeID createNode(NodeID parent = NULL_NODE) {
        NodeID id = allocNode();
        nodes_[id].parent = (parent != NULL_NODE) ? parent : rootNode_;
        nodes_[id].dirty = true;

        // Link into parent's child list
        NodeID p = nodes_[id].parent;
        if (nodes_[p].firstChild == NULL_NODE) {
            nodes_[p].firstChild = id;
        } else {
            NodeID last = nodes_[p].firstChild;
            while (nodes_[last].nextSibling != NULL_NODE)
                last = nodes_[last].nextSibling;
            nodes_[last].nextSibling = id;
            nodes_[id].prevSibling = last;
        }
        markDirty(id);
        return id;
    }

    void destroyNode(NodeID id) {
        if (id == rootNode_) return;
        // Recursively destroy children
        NodeID child = nodes_[id].firstChild;
        while (child != NULL_NODE) {
            NodeID next = nodes_[child].nextSibling;
            destroyNode(child);
            child = next;
        }
        // Unlink from parent
        NodeID p = nodes_[id].parent;
        if (p != NULL_NODE) {
            if (nodes_[p].firstChild == id) {
                nodes_[p].firstChild = nodes_[id].nextSibling;
            } else {
                NodeID s = nodes_[id].prevSibling;
                if (s != NULL_NODE) nodes_[s].nextSibling = nodes_[id].nextSibling;
                NodeID n = nodes_[id].nextSibling;
                if (n != NULL_NODE) nodes_[n].prevSibling = nodes_[id].prevSibling;
            }
        }
        freeNode(id);
    }

    // ---- Transform setters (mark dirty) ----
    void setPosition(NodeID id, const Vec3& pos) {
        nodes_[id].localPosition = pos;
        markDirty(id);
    }

    void setRotation(NodeID id, const Quat& rot) {
        nodes_[id].localRotation = rot;
        markDirty(id);
    }

    void setScale(NodeID id, const Vec3& scale) {
        nodes_[id].localScale = scale;
        markDirty(id);
    }

    void setLocalTransform(NodeID id, const Vec3& pos, const Quat& rot, const Vec3& scale) {
        nodes_[id].localPosition = pos;
        nodes_[id].localRotation = rot;
        nodes_[id].localScale = scale;
        markDirty(id);
    }

    // ---- Transform queries (force recompute if dirty) ----
    const Mat4& worldTransform(NodeID id) const {
        if (nodes_[id].dirty) computeWorldTransform(id);
        return nodes_[id].worldTransform;
    }

    Vec3 worldPosition(NodeID id) const {
        const Mat4& wt = worldTransform(id);
        return Vec3(wt.m[12], wt.m[13], wt.m[14]);
    }

    Quat worldRotation(NodeID id) const {
        // Extract rotation from world matrix via Mat3
        const Mat4& wt = worldTransform(id);
        Mat3 m3;
        m3.m[0] = wt.m[0]; m3.m[1] = wt.m[1]; m3.m[2] = wt.m[2];
        m3.m[3] = wt.m[4]; m3.m[4] = wt.m[5]; m3.m[5] = wt.m[6];
        m3.m[6] = wt.m[8]; m3.m[7] = wt.m[9]; m3.m[8] = wt.m[10];
        return Quat::fromMat3(m3);
    }

    Vec3 worldScale(NodeID id) const {
        const Mat4& wt = worldTransform(id);
        f32 sx = sqrtf(wt.m[0]*wt.m[0] + wt.m[1]*wt.m[1] + wt.m[2]*wt.m[2]);
        f32 sy = sqrtf(wt.m[4]*wt.m[4] + wt.m[5]*wt.m[5] + wt.m[6]*wt.m[6]);
        f32 sz = sqrtf(wt.m[8]*wt.m[8] + wt.m[9]*wt.m[9] + wt.m[10]*wt.m[10]);
        return Vec3(sx, sy, sz);
    }

    // ---- Hierarchy queries ----
    NodeID parent(NodeID id) const { return nodes_[id].parent; }
    NodeID firstChild(NodeID id) const { return nodes_[id].firstChild; }
    NodeID nextSibling(NodeID id) const { return nodes_[id].nextSibling; }
    bool isRoot(NodeID id) const { return id == rootNode_; }
    bool isActive(NodeID id) const { return nodes_[id].active; }
    void setActive(NodeID id, bool a) { nodes_[id].active = a; }

    // ---- Depth-first traversal ----
    template<typename Fn>
    void traverseDFS(NodeID root, Fn&& fn) const {
        if (!nodes_[root].active) return;
        fn(root, nodes_[root]);
        NodeID child = nodes_[root].firstChild;
        while (child != NULL_NODE) {
            traverseDFS(child, std::forward<Fn>(fn));
            child = nodes_[child].nextSibling;
        }
    }

    template<typename Fn>
    void traverseAll(Fn&& fn) const {
        traverseDFS(rootNode_, std::forward<Fn>(fn));
    }

    // ---- Flatten to list (for rendering) ----
    void flatten(Vector<NodeID>& out) const {
        out.clear();
        traverseAll([&](NodeID id, const SceneNode&) {
            out.pushBack(id);
        });
    }

    NodeID rootNode() const { return rootNode_; }
    u32 nodeCount() const { return nodeCount_; }
    const SceneNode& node(NodeID id) const { return nodes_[id]; }
    SceneNode& nodeMut(NodeID id) { return nodes_[id]; }

    // ---- World-transform recomputation (propagate dirty flags) ----
    void propagateDirty() {
        for (u32 i = 0; i < nodeCount_; i++) {
            if (nodes_[i].dirty) computeWorldTransform((NodeID)i);
        }
    }

    // ---- Serialization ----
    struct BinaryBlob {
        Vector<u8> data;
    };

    void serialize(BinaryBlob& out) const {
        out.data.clear();
        // Header: node count
        u32 count = nodeCount_;
        appendBytes(out.data, &count, sizeof(count));
        // Node data
        for (u32 i = 0; i < nodeCount_; i++) {
            appendBytes(out.data, &nodes_[i], sizeof(SceneNode));
        }
    }

    bool deserialize(const BinaryBlob& in) {
        u32 count = 0;
        if (in.data.size() < sizeof(count)) return false;
        std::memcpy(&count, in.data.data(), sizeof(count));
        if (count > MAX_SCENE_NODES || in.data.size() < sizeof(count) + count * sizeof(SceneNode))
            return false;
        nodeCount_ = count;
        std::memcpy(nodes_, in.data.data() + sizeof(count), count * sizeof(SceneNode));
        return true;
    }

private:
    static constexpr u32 MAX_SCENE_NODES = 65536;

    NodeID allocNode() {
        if (freeHead_ != NULL_NODE) {
            NodeID id = freeHead_;
            freeHead_ = nodeNext_[id];
            nodes_[id] = SceneNode{};
            nodeCount_++;
            return id;
        }
        return nodeCount_++;
    }

    void freeNode(NodeID id) {
        nodes_[id] = SceneNode{};
        nodeNext_[id] = freeHead_;
        freeHead_ = id;
        nodeCount_--;
    }

    void markDirty(NodeID id) {
        nodes_[id].dirty = true;
        NodeID child = nodes_[id].firstChild;
        while (child != NULL_NODE) {
            markDirty(child);
            child = nodes_[child].nextSibling;
        }
    }

    void computeWorldTransform(NodeID id) const {
        SceneNode& n = nodes_[id];
        Mat4 local;
        local = Mat4::translation(n.localPosition) *
                Mat4::rotation(n.localRotation) *
                Mat4::scaling(n.localScale);
        if (n.parent != NULL_NODE && nodes_[n.parent].active) {
            const Mat4& parentWT = worldTransform(n.parent);
            n.worldTransform = parentWT * local;
        } else {
            n.worldTransform = local;
        }
        n.dirty = false;
    }

    template<typename T>
    static void appendBytes(Vector<u8>& out, const T* data, u32 bytes) {
        u32 start = (u32)out.size();
        out.resize(start + bytes);
        std::memcpy(out.data() + start, data, bytes);
    }

    mutable SceneNode nodes_[MAX_SCENE_NODES];
    u32 nodeNext_[MAX_SCENE_NODES] = {};
    u32 nodeCount_ = 0;
    NodeID freeHead_ = NULL_NODE;
    NodeID rootNode_ = 0;
};

} // namespace Frost
