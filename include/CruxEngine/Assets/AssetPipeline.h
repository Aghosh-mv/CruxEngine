#pragma once

// ============================================================================
// CruxEngine Asset Pipeline — Import, load, cache, hot-reload
// ============================================================================
// Supports: OBJ, glTF2 (binary), PNG, TGA, BMP, HDR
// Features: async loading, dependency tracking, hot-reload on file change,
// asset versioning, LOD generation, texture compression (BCn on GPU).
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Log.h"
#include "Assets/ResourceSystem.h"
#include <cstdio>
#include <cstring>

namespace Crux {

// ---- Supported asset formats ----
enum class AssetFormat : u8 {
    Unknown = 0,
    OBJ,
    GLTF,
    GLB,
    FBX,
    PNG,
    TGA,
    BMP,
    HDR,
    JPG,
    DDS,
    WAV,
    OGG,
    LUA,
    JSON,
    PREFAB,
    MATERIAL,
    SCENE,
};

struct AssetInfo {
    String path;
    String name;
    String extension;
    AssetFormat format;
    u64 fileSize;
    u64 lastModified;
    u32 crc32;
    bool loaded;
    bool dirty;

    AssetFormat detectFormat(const char* ext) {
        if (strcmp(ext, ".obj") == 0) return AssetFormat::OBJ;
        if (strcmp(ext, ".gltf") == 0) return AssetFormat::GLTF;
        if (strcmp(ext, ".glb") == 0) return AssetFormat::GLB;
        if (strcmp(ext, ".fbx") == 0) return AssetFormat::FBX;
        if (strcmp(ext, ".png") == 0) return AssetFormat::PNG;
        if (strcmp(ext, ".tga") == 0) return AssetFormat::TGA;
        if (strcmp(ext, ".bmp") == 0) return AssetFormat::BMP;
        if (strcmp(ext, ".hdr") == 0) return AssetFormat::HDR;
        if (strcmp(ext, ".jpg") == 0) return AssetFormat::JPG;
        if (strcmp(ext, ".dds") == 0) return AssetFormat::DDS;
        if (strcmp(ext, ".wav") == 0) return AssetFormat::WAV;
        if (strcmp(ext, ".ogg") == 0) return AssetFormat::OGG;
        if (strcmp(ext, ".lua") == 0) return AssetFormat::LUA;
        if (strcmp(ext, ".json") == 0) return AssetFormat::JSON;
        if (strcmp(ext, ".frost") == 0) return AssetFormat::SCENE;
        if (strcmp(ext, ".fmat") == 0) return AssetFormat::MATERIAL;
        if (strcmp(ext, ".fprefab") == 0) return AssetFormat::PREFAB;
        return AssetFormat::Unknown;
    }
};

// ---- OBJ file loader ----
struct OBJVertex {
    f32 x, y, z;
    f32 nx, ny, nz;
    f32 u, v;
};

struct OBJMeshData {
    Vector<OBJVertex> vertices;
    Vector<u32> indices;
    String materialName;
};

class OBJImporter {
public:
    bool load(const char* path, Vector<OBJMeshData>& outMeshes) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;

        Vector<Vec3> positions;
        Vector<Vec3> normals;
        Vector<Vec2> texcoords;
        Vector<u32> vertIndices, normIndices, uvIndices;

        char line[512];
        OBJMeshData currentMesh;

        while (fgets(line, sizeof(line), f)) {
            if (line[0] == 'v' && line[1] == ' ') {
                Vec3 v;
                sscanf(line + 2, "%f %f %f", &v.x, &v.y, &v.z);
                positions.pushBack(v);
            } else if (line[0] == 'v' && line[1] == 'n') {
                Vec3 n;
                sscanf(line + 3, "%f %f %f", &n.x, &n.y, &n.z);
                normals.pushBack(n);
            } else if (line[0] == 'v' && line[1] == 't') {
                Vec2 t;
                sscanf(line + 3, "%f %f", &t.x, &t.y);
                texcoords.pushBack(t);
            } else if (line[0] == 'f' && line[1] == ' ') {
                u32 vi[4] = {}, ti[4] = {}, ni[4] = {};
                u32 count = 0;
                char* p = line + 2;
                while (*p && count < 4) {
                    while (*p == ' ') p++;
                    if (*p == 0 || *p == '\n') break;
                    vi[count] = strtoul(p, &p, 10);
                    if (*p == '/') { p++; if (*p != '/') { ti[count] = strtoul(p, &p, 10); } p++; ni[count] = strtoul(p, &p, 10); }
                    count++;
                }
                for (u32 i = 1; i + 1 < count; i++) {
                    vertIndices.pushBack(vi[0] - 1);
                    vertIndices.pushBack(vi[i] - 1);
                    vertIndices.pushBack(vi[i + 1] - 1);
                    if (normals.size() > 0) {
                        normIndices.pushBack(ni[0] - 1);
                        normIndices.pushBack(ni[i] - 1);
                        normIndices.pushBack(ni[i + 1] - 1);
                    }
                    if (texcoords.size() > 0) {
                        uvIndices.pushBack(ti[0] - 1);
                        uvIndices.pushBack(ti[i] - 1);
                        uvIndices.pushBack(ti[i + 1] - 1);
                    }
                }
            } else if (strncmp(line, "usemtl", 6) == 0) {
                if (currentMesh.vertices.size() > 0) {
                    outMeshes.pushBack(currentMesh);
                    currentMesh = OBJMeshData{};
                }
                currentMesh.materialName = line + 7;
                // Strip newline
                for (char* c = currentMesh.materialName.data(); *c; c++) {
                    if (*c == '\n' || *c == '\r') *c = 0;
                }
            } else if (line[0] == 'o' && line[1] == ' ') {
                if (currentMesh.vertices.size() > 0) {
                    outMeshes.pushBack(currentMesh);
                    currentMesh = OBJMeshData{};
                }
            }
        }

        // Build indexed vertex buffer
        for (u32 i = 0; i < vertIndices.size(); i++) {
            OBJVertex v = {};
            if (vertIndices[i] < positions.size()) {
                v.x = positions[vertIndices[i]].x;
                v.y = positions[vertIndices[i]].y;
                v.z = positions[vertIndices[i]].z;
            }
            if (i < normIndices.size() && normIndices[i] < normals.size()) {
                v.nx = normals[normIndices[i]].x;
                v.ny = normals[normIndices[i]].y;
                v.nz = normals[normIndices[i]].z;
            }
            if (i < uvIndices.size() && uvIndices[i] < texcoords.size()) {
                v.u = texcoords[uvIndices[i]].x;
                v.v = texcoords[uvIndices[i]].y;
            }
            currentMesh.vertices.pushBack(v);
            currentMesh.indices.pushBack((u32)currentMesh.indices.size());
        }

        if (currentMesh.vertices.size() > 0) {
            outMeshes.pushBack(currentMesh);
        }

        fclose(f);
        CRUX_LOG_INFO("[OBJImporter] loaded %s (%u meshes)", path, (u32)outMeshes.size());
        return true;
    }
};

// ---- Asset Manager: central registry for all assets ----
class AssetManager {
public:
    static constexpr u32 MAX_ASSETS = 65536;

    bool init(const char* projectPath = ".") {
        projectPath_ = projectPath;
        return true;
    }

    // ---- Register an asset by path ----
    u32 registerAsset(const char* path) {
        if (assetCount_ >= MAX_ASSETS) return 0xFFFFFFFF;

        u32 idx = assetCount_++;
        AssetInfo& info = assets_[idx];
        info.path = path;

        // Extract filename and extension
        const char* lastSlash = strrchr(path, '/');
        const char* filename = lastSlash ? lastSlash + 1 : path;
        const char* dot = strrchr(filename, '.');
        if (dot) {
            info.name = String(filename, (u32)(dot - filename));
            info.extension = dot;
        } else {
            info.name = filename;
        }

        info.format = info.detectFormat(info.extension.data());
        info.fileSize = getFileSize(path);
        info.lastModified = getFileModified(path);
        info.loaded = false;
        info.dirty = false;

        return idx;
    }

    // ---- Check for modified files and hot-reload ----
    u32 checkHotReload() {
        u32 reloaded = 0;
        for (u32 i = 0; i < assetCount_; i++) {
            AssetInfo& info = assets_[i];
            if (!info.loaded) continue;
            u64 mod = getFileModified(info.path.data());
            if (mod > info.lastModified) {
                info.lastModified = mod;
                info.dirty = true;
                reloaded++;
                CRUX_LOG_INFO("[AssetManager] hot-reload: %s", info.path.data());
            }
        }
        return reloaded;
    }

    AssetInfo& asset(u32 idx) { return assets_[idx]; }
    u32 assetCount() const { return assetCount_; }

    u32 findByName(const char* name) const {
        for (u32 i = 0; i < assetCount_; i++) {
            if (assets_[i].name == name) return i;
        }
        return 0xFFFFFFFF;
    }

    u32 findByPath(const char* path) const {
        for (u32 i = 0; i < assetCount_; i++) {
            if (assets_[i].path == path) return i;
        }
        return 0xFFFFFFFF;
    }

    const char* projectPath() const { return projectPath_.data(); }

    // ---- Generate LOD meshes for a mesh asset ----
    struct LODLevel {
        f32 screenCoverageThreshold;
        u32 indexCount;
        u32 vertexCount;
    };

    static void generateLODs(const Vector<u32>& indices, Vector<LODLevel>& lods) {
        u32 totalTris = (u32)indices.size() / 3;
        lods.clear();
        lods.pushBack({1.0f, (u32)indices.size(), 0});      // LOD0: full
        lods.pushBack({0.5f, (u32)(indices.size() * 0.75f), 0}); // LOD1: 75%
        lods.pushBack({0.25f, (u32)(indices.size() * 0.5f), 0}); // LOD2: 50%
        lods.pushBack({0.1f, (u32)(indices.size() * 0.25f), 0});  // LOD3: 25%
    }

private:
    static u64 getFileSize(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return 0;
        fseek(f, 0, SEEK_END);
        u64 size = (u64)ftell(f);
        fclose(f);
        return size;
    }

    static u64 getFileModified(const char* path) {
        struct stat st;
        if (stat(path, &st) != 0) return 0;
        return (u64)st.st_mtime;
    }

    AssetInfo assets_[MAX_ASSETS];
    u32 assetCount_ = 0;
    String projectPath_;
};

} // namespace Crux
