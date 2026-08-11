#pragma once

// ============================================================================
// FrostEngine Scene Serializer — Save/load scenes to binary
// ============================================================================
// Binary format: header + node tree + component data + asset references
// Also supports JSON export for human-readable debugging.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Log.h"
#include "Scene/SceneGraph.h"
#include <cstdio>
#include <cstring>

namespace Frost {

// ---- Scene file format (binary) ----
static constexpr u32 SCENE_MAGIC = 0x46525354; // "FRST"
static constexpr u32 SCENE_VERSION = 1;

struct SceneFileHeader {
    u32 magic;
    u32 version;
    u32 nodeCount;
    u32 componentCount;
    u64 dataOffset;
    u64 dataSize;
    char author[64];
    char description[256];
};

// ---- Component data types for serialization ----
enum class SerializedComponentType : u16 {
    None = 0,
    Transform,
    Light,
    Camera,
    MeshRenderer,
    Collider,
    Script,
    Audio,
    Particle,
    LODGroup,
    NavMesh,
    COUNT
};

struct SerializedComponentHeader {
    SerializedComponentType type;
    u32 entityID;
    u32 dataSize;
};

// ---- Serialized entity record (transform + component payload) ----
struct SerializedEntity {
    u32 id = 0;
    Vec3 position{ 0, 0, 0 };
    Vec3 rotation{ 0, 0, 0 };
    Vec3 scale{ 1, 1, 1 };
    Vector<u16> componentTypes;
    Vector<u8> componentData;
};

// ---- On-disk scene header (versioned) ----
struct SerializationHeader {
    u32 magic;
    u32 version;
    u32 entityCount;
    u32 componentCount;
    u32 dataSize;
};

class SceneSerializer {
public:
    bool saveBinary(const char* path, const Scene& scene,
                    const Vector<u32>& entityIDs,
                    const char* author = "FrostEngine",
                    const char* description = "") {
        FILE* f = fopen(path, "wb");
        if (!f) {
            FROST_LOG_ERROR("[SceneSerializer] cannot open %s for writing", path);
            return false;
        }

        // Collect all nodes
        Vector<NodeID> nodes;
        scene.flatten(nodes);

        SceneFileHeader header = {};
        header.magic = SCENE_MAGIC;
        header.version = SCENE_VERSION;
        header.nodeCount = (u32)nodes.size();
        header.componentCount = 0; // Will be filled during component serialization
        header.dataOffset = sizeof(SceneFileHeader);
        strncpy(header.author, author, 63);
        strncpy(header.description, description, 255);

        // Write placeholder header (we'll update data size later)
        fwrite(&header, sizeof(header), 1, f);

        // Write node data
        u64 dataStart = ftell(f);
        for (u32 i = 0; i < nodes.size(); i++) {
            NodeID id = nodes[i];
            const SceneNode& node = scene.node(id);

            // Write node ID + parent relationship
            fwrite(&id, sizeof(id), 1, f);
            NodeID parent = node.parent;
            fwrite(&parent, sizeof(parent), 1, f);

            // Write transform
            fwrite(&node.localPosition, sizeof(Vec3), 1, f);
            fwrite(&node.localRotation, sizeof(Quat), 1, f);
            fwrite(&node.localScale, sizeof(Vec3), 1, f);

            // Write flags
            u32 flags = node.active ? 1 : 0;
            fwrite(&flags, sizeof(flags), 1, f);

            // Write entity ID association
            u32 entityID = (i < entityIDs.size()) ? entityIDs[i] : 0xFFFFFFFF;
            fwrite(&entityID, sizeof(entityID), 1, f);
        }

        // Update header with data size
        u64 dataEnd = ftell(f);
        header.dataSize = dataEnd - dataStart;
        header.dataOffset = dataStart;
        fseek(f, 0, SEEK_SET);
        fwrite(&header, sizeof(header), 1, f);

        fclose(f);
        FROST_LOG_INFO("[SceneSerializer] saved %u nodes to %s", header.nodeCount, path);
        return true;
    }

    bool loadBinary(const char* path, Scene& scene, Vector<u32>& entityIDs) {
        FILE* f = fopen(path, "rb");
        if (!f) {
            FROST_LOG_ERROR("[SceneSerializer] cannot open %s", path);
            return false;
        }

        SceneFileHeader header;
        fread(&header, sizeof(header), 1, f);

        if (header.magic != SCENE_MAGIC) {
            FROST_LOG_ERROR("[SceneSerializer] invalid magic in %s", path);
            fclose(f);
            return false;
        }
        if (header.version != SCENE_VERSION) {
            FROST_LOG_ERROR("[SceneSerializer] version mismatch in %s", path);
            fclose(f);
            return false;
        }

        entityIDs.clear();
        for (u32 i = 0; i < header.nodeCount; i++) {
            NodeID id, parent;
            Vec3 pos, scale;
            Quat rot;
            u32 flags;
            u32 entityID;

            fread(&id, sizeof(id), 1, f);
            fread(&parent, sizeof(parent), 1, f);
            fread(&pos, sizeof(Vec3), 1, f);
            fread(&rot, sizeof(Quat), 1, f);
            fread(&scale, sizeof(Vec3), 1, f);
            fread(&flags, sizeof(flags), 1, f);
            fread(&entityID, sizeof(entityID), 1, f);

            NodeID newID = scene.createNode(parent);
            scene.setPosition(newID, pos);
            scene.setRotation(newID, rot);
            scene.setScale(newID, scale);
            scene.setActive(newID, (flags & 1) != 0);

            entityIDs.pushBack(entityID);
        }

        fclose(f);
        FROST_LOG_INFO("[SceneSerializer] loaded %u nodes from %s", header.nodeCount, path);
        return true;
    }

    // ---- JSON export (for debugging) ----
    bool saveJSON(const char* path, const Scene& scene) {
        FILE* f = fopen(path, "w");
        if (!f) return false;

        Vector<NodeID> nodes;
        scene.flatten(nodes);

        fprintf(f, "{\n  \"nodes\": [\n");
        for (u32 i = 0; i < nodes.size(); i++) {
            const SceneNode& node = scene.node(nodes[i]);
            fprintf(f, "    {\n");
            fprintf(f, "      \"id\": %u,\n", (u32)nodes[i]);
            fprintf(f, "      \"parent\": %u,\n", (u32)node.parent);
            fprintf(f, "      \"position\": [%.3f, %.3f, %.3f],\n",
                    node.localPosition.x, node.localPosition.y, node.localPosition.z);
            fprintf(f, "      \"rotation\": [%.3f, %.3f, %.3f, %.3f],\n",
                    node.localRotation.x, node.localRotation.y, node.localRotation.z, node.localRotation.w);
            fprintf(f, "      \"scale\": [%.3f, %.3f, %.3f],\n",
                    node.localScale.x, node.localScale.y, node.localScale.z);
            fprintf(f, "      \"active\": %s\n", node.active ? "true" : "false");
            fprintf(f, "    }%s\n", (i + 1 < nodes.size()) ? "," : "");
        }
        fprintf(f, "  ]\n}\n");

        fclose(f);
        FROST_LOG_INFO("[SceneSerializer] exported JSON to %s", path);
        return true;
    }

    // ---- Versioned binary scene serialization (hardened) ----

    u32 serializeEntity(const void* entity);
    u32 serializeScene();
    u32 deserializeEntity(const void* buffer, u32 offset);
    u32 deserializeScene(const void* buffer, u32 size);

    bool writeToFile(const char* path);
    bool readFromFile(const char* path);

    u32 getSavedCount() const { return savedCount_; }
    u32 getLoadedCount() const { return loadedCount_; }
    u32 getBytesWritten() const { return bytesWritten_; }
    u32 getBytesRead() const { return bytesRead_; }
    bool lastWriteSuccessful() const { return lastWriteOk_; }
    void resetStats();
    u32 getFileVersion() const { return fileVersion_; }
    void setFileVersion(u32 v) { fileVersion_ = v; }

private:
    Vector<SerializedEntity> serializedEntities_;
    Vector<u8> buffer_;
    u32 deserializeSize_ = 0;
    u32 fileVersion_ = 1;
    u32 savedCount_ = 0;
    u32 loadedCount_ = 0;
    u32 bytesWritten_ = 0;
    u32 bytesRead_ = 0;
    bool lastWriteOk_ = false;
    bool lastReadOk_ = false;

    static constexpr u32 MAX_ENTITIES = 1000000;
    static constexpr u16 MAX_COMPONENTS_PER_ENTITY = 64;
    static constexpr u32 MAX_COMPONENT_BYTES = 16 * 1024 * 1024;
};

} // namespace Frost
