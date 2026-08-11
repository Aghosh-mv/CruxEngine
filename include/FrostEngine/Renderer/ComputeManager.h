#pragma once

// ============================================================================
// FrostEngine Compute Shader Manager
// ============================================================================
// Manages all compute shader programs for the revolutionary rendering tech.
// Each compute shader is dispatched with work groups and binds SSBOs.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Renderer/Gl.h"
#include "Core/Log.h"
#include <cstring>

namespace Frost {

struct ComputeShader {
    GLuint program = 0;
    GLuint localWorkGroupX = 8;
    GLuint localWorkGroupY = 8;
    GLuint localWorkGroupZ = 1;
};

struct ComputeConfig {
    u32 maxUAVs = 64;
    u32 maxTextures = 256;
    u32 workGroupSize = 64;
    bool enableAsync = true;
};

struct ComputeStats {
    u32 dispatches = 0;
    u32 uavsCreated = 0;
    u32 texturesCreated = 0;
    u32 instancesCulled = 0;
    f32 cullTimeMs = 0.0f;
    u32 shadersCompiled = 0;
    u32 ssbosCreated = 0;
};

class ComputeManager {
public:
    static constexpr u32 MAX_COMPUTE_SHADERS = 32;
    static constexpr u32 MAX_SSBO_BINDINGS = 16;

    bool init() {
        ready_ = true;
        FROST_LOG_INFO("[ComputeManager] initialized");
        return true;
    }

    void shutdown() {
        for (u32 i = 0; i < shaderCount_; i++) {
            if (shaders_[i].program) {
                Gl::DeleteProgram(shaders_[i].program);
            }
        }
        for (u32 i = 0; i < ssboCount_; i++) {
            if (ssbos_[i]) Gl::DeleteBuffers(1, &ssbos_[i]);
        }
        shaderCount_ = 0;
        ssboCount_ = 0;
        ready_ = false;
    }

    // ---- Create a compute shader from source ----
    u32 createCompute(const char* source, const char* name = "compute",
                      GLuint wgX = 8, GLuint wgY = 8, GLuint wgZ = 1) {
        if (shaderCount_ >= MAX_COMPUTE_SHADERS) return 0xFFFFFFFF;

        GLuint cs = Gl::CreateShader(0x91B9); // GL_COMPUTE_SHADER
        Gl::ShaderSource(cs, 1, &source, nullptr);
        Gl::CompileShader(cs);

        GLint ok = 0;
        Gl::GetShaderiv(cs, 0x8B81, &ok); // GL_COMPILE_STATUS
        if (!ok) {
            char log[1024];
            Gl::GetShaderInfoLog(cs, sizeof(log), nullptr, log);
            FROST_LOG_ERROR("[ComputeManager] shader '%s' compile error: %s", name, log);
            Gl::DeleteShader(cs);
            return 0xFFFFFFFF;
        }

        GLuint prog = Gl::CreateProgram();
        Gl::AttachShader(prog, cs);
        Gl::LinkProgram(prog);
        Gl::DeleteShader(cs);

        Gl::GetProgramiv(prog, 0x8B82, &ok); // GL_LINK_STATUS
        if (!ok) {
            char log[1024];
            Gl::GetProgramInfoLog(prog, sizeof(log), nullptr, log);
            FROST_LOG_ERROR("[ComputeManager] shader '%s' link error: %s", name, log);
            Gl::DeleteProgram(prog);
            return 0xFFFFFFFF;
        }

        u32 idx = shaderCount_++;
        shaders_[idx].program = prog;
        shaders_[idx].localWorkGroupX = wgX;
        shaders_[idx].localWorkGroupY = wgY;
        shaders_[idx].localWorkGroupZ = wgZ;
        stats_.shadersCompiled++;
        FROST_LOG_INFO("[ComputeManager] compute shader '%s' created (workgroup %u,%u,%u)",
                       name, wgX, wgY, wgZ);
        return idx;
    }

    // ---- Create an SSBO (GPU buffer) ----
    u32 createSSBO(u64 sizeBytes, const void* initialData = nullptr) {
        if (ssboCount_ >= MAX_SSBO_BINDINGS) return 0xFFFFFFFF;
        GLuint buf = 0;
        Gl::GenBuffers(1, &buf);
        Gl::BindBuffer(0x90D2, buf); // GL_SHADER_STORAGE_BUFFER
        Gl::BufferData(0x90D2, sizeBytes, initialData, 0x88E4); // GL_DYNAMIC_DRAW
        Gl::BindBuffer(0x90D2, 0);
        u32 idx = ssboCount_++;
        ssbos_[idx] = buf;
        ssboSizes_[idx] = sizeBytes;
        stats_.ssbosCreated++;
        return idx;
    }

    void updateSSBO(u32 idx, const void* data, u64 sizeBytes, u64 offset = 0) {
        if (idx >= ssboCount_) return;
        Gl::BindBuffer(0x90D2, ssbos_[idx]);
        Gl::BufferSubData(0x90D2, offset, sizeBytes, data);
        Gl::BindBuffer(0x90D2, 0);
    }

    void* mapSSBO(u32 idx) {
        if (idx >= ssboCount_) return nullptr;
        Gl::BindBuffer(0x90D2, ssbos_[idx]);
        return Gl::MapBuffer(0x90D2, 0x88BA); // GL_READ_WRITE
    }

    void unmapSSBO(u32 idx) {
        if (idx >= ssboCount_) return;
        Gl::BindBuffer(0x90D2, ssbos_[idx]);
        Gl::UnmapBuffer(0x90D2);
        Gl::BindBuffer(0x90D2, 0);
    }

    // ---- Dispatch a compute shader ----
    void dispatch(u32 shaderIdx, u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1) {
        if (shaderIdx >= shaderCount_) return;
        Gl::UseProgram(shaders_[shaderIdx].program);
        Gl::DispatchCompute(groupsX, groupsY, groupsZ);
        Gl::MemoryBarrier(0x0040); // GL_SHADER_STORAGE_BARRIER_BIT
    }

    // ---- Bind SSBO to a binding point ----
    void bindSSBO(u32 ssboIdx, u32 bindingPoint) {
        if (ssboIdx >= ssboCount_) return;
        Gl::BindBufferBase(0x90D2, bindingPoint, ssbos_[ssboIdx]);
    }

    // ---- Bind SSBO to a range (for partial access) ----
    void bindSSBORange(u32 ssboIdx, u32 bindingPoint, u64 offset, u64 size) {
        if (ssboIdx >= ssboCount_) return;
        Gl::BindBufferRange(0x90D2, bindingPoint, ssbos_[ssboIdx], offset, size);
    }

    // ---- Set uniforms on the current compute shader ----
    void setInt(const char* name, i32 v) {
        Gl::Uniform1i(Gl::GetUniformLocation(currentProgram(), name), v);
    }
    void setFloat(const char* name, f32 v) {
        Gl::Uniform1f(Gl::GetUniformLocation(currentProgram(), name), v);
    }
    void setVec3(const char* name, f32 x, f32 y, f32 z) {
        Gl::Uniform3f(Gl::GetUniformLocation(currentProgram(), name), x, y, z);
    }
    void setVec4(const char* name, f32 x, f32 y, f32 z, f32 w) {
        Gl::Uniform4f(Gl::GetUniformLocation(currentProgram(), name), x, y, z, w);
    }
    void setUInt(const char* name, u32 v) {
        Gl::Uniform1ui(Gl::GetUniformLocation(currentProgram(), name), v);
    }

    void useShader(u32 idx) {
        if (idx < shaderCount_) Gl::UseProgram(shaders_[idx].program);
    }

    // ========================================================================
    // Queued compute dispatch (executed on flushDispatches / endFrame)
    // ========================================================================
    void dispatchCompute(u32 shaderId, u32 gx, u32 gy, u32 gz);

    // ========================================================================
    // UAV resource pool (buffer-backed UAVs)
    // ========================================================================
    struct UAVResource {
        u32 id = 0;
        u32 sizeBytes = 0;
        bool inUse = false;
        u32 boundSlot = 0;
    };

    u32 allocateUAV(u32 sizeBytes);
    void freeUAV(u32 id);
    const UAVResource& getUAV(u32 id) const;
    u32 getPoolSize() const;

    u32 getTotalGpuBytes() const;
    u32 getPeakGpuBytes() const;
    void resetPeakTracking();

    u32 flushDispatches();
    void clearDispatches();

    void beginFrame();
    void endFrame();

    u32 getDispatchCount() const;
    f32 getDispatchTimeMs() const;

    // ========================================================================
    // Indirect dispatch from a buffer containing group counts
    // ========================================================================
    bool dispatchIndirect(u32 shaderId, u32 bufferId, u32 offset) {
        if (shaderId >= shaderCount_) {
            FROST_LOG_ERROR("[ComputeManager] dispatchIndirect: invalid shader id %u", shaderId);
            return false;
        }
        if (bufferId >= ssboCount_) {
            FROST_LOG_ERROR("[ComputeManager] dispatchIndirect: invalid buffer id %u", bufferId);
            return false;
        }
        Gl::UseProgram(shaders_[shaderId].program);
        Gl::BindBuffer(0x90D2, ssbos_[bufferId]);
        Gl::DispatchComputeIndirect(static_cast<GLintptr>(offset));
        Gl::BindBuffer(0x90D2, 0);
        Gl::MemoryBarrier(0x0040);
        stats_.dispatches++;
        return true;
    }

    // ========================================================================
    // UAV management
    // ========================================================================
    u32 createUAV(u32 width, u32 height, u32 format) {
        if (activeUAVs_ >= computeCfg_.maxUAVs) {
            FROST_LOG_ERROR("[ComputeManager] createUAV: UAV pool exhausted (max %u)", computeCfg_.maxUAVs);
            return 0xFFFFFFFF;
        }
        GLuint tex = 0;
        Gl::GenTextures(1, &tex);
        Gl::BindTexture(0x0DE1, tex); // GL_TEXTURE_2D
        Gl::TexImage2D(0x0DE1, 0, static_cast<GLint>(format), static_cast<GLsizei>(width),
                        static_cast<GLsizei>(height), 0, format, 0x1401, nullptr); // GL_UNSIGNED_BYTE
        Gl::TexParameteri(0x0DE1, 0x2800, 0x2601); // GL_TEXTURE_MIN/MAG_FILTER = GL_LINEAR
        Gl::TexParameteri(0x0DE1, 0x2801, 0x2601);
        Gl::TexParameteri(0x0DE1, 0x2802, 0x812F); // GL_CLAMP_TO_EDGE
        Gl::TexParameteri(0x0DE1, 0x2803, 0x812F);
        Gl::BindTexture(0x0DE1, 0);

        u32 slot = 0;
        if (uavPool_.size() > 0) {
            slot = uavPool_[uavPool_.size() - 1];
            uavPool_.pop_back();
        } else {
            slot = activeUAVs_;
        }
        uavTexIds_[slot] = tex;
        uavWidths_[slot] = width;
        uavHeights_[slot] = height;
        uavFormats_[slot] = format;
        activeUAVs_++;
        stats_.uavsCreated++;
        return slot;
    }

    void destroyUAV(u32 uavId) {
        if (uavId >= computeCfg_.maxUAVs) return;
        if (uavTexIds_[uavId] == 0) return;
        Gl::DeleteTextures(1, &uavTexIds_[uavId]);
        uavTexIds_[uavId] = 0;
        uavWidths_[uavId] = 0;
        uavHeights_[uavId] = 0;
        uavFormats_[uavId] = 0;
        activeUAVs_--;
        uavPool_.push_back(uavId);
    }

    void bindUAV(u32 slot, u32 uavId) {
        if (uavId >= computeCfg_.maxUAVs) {
            FROST_LOG_ERROR("[ComputeManager] bindUAV: invalid UAV id %u", uavId);
            return;
        }
        if (uavTexIds_[uavId] == 0) {
            FROST_LOG_ERROR("[ComputeManager] bindUAV: UAV %u is not allocated", uavId);
            return;
        }
        Gl::BindImageTexture(slot, uavTexIds_[uavId], 0, GL_FALSE, 0,
                             0x88E8, // GL_READ_WRITE
                             static_cast<GLenum>(uavFormats_[uavId]));
    }

    // ========================================================================
    // Texture pool management
    // ========================================================================
    u32 createTexture(u32 width, u32 height, u32 format) {
        if (texturePool_.size() >= computeCfg_.maxTextures) {
            FROST_LOG_ERROR("[ComputeManager] createTexture: texture pool exhausted (max %u)", computeCfg_.maxTextures);
            return 0xFFFFFFFF;
        }
        GLuint tex = 0;
        Gl::GenTextures(1, &tex);
        Gl::BindTexture(0x0DE1, tex);
        Gl::TexImage2D(0x0DE1, 0, static_cast<GLint>(format), static_cast<GLsizei>(width),
                        static_cast<GLsizei>(height), 0, format, 0x1401, nullptr);
        Gl::TexParameteri(0x0DE1, 0x2800, 0x2601);
        Gl::TexParameteri(0x0DE1, 0x2801, 0x2601);
        Gl::TexParameteri(0x0DE1, 0x2802, 0x812F);
        Gl::TexParameteri(0x0DE1, 0x2803, 0x812F);
        Gl::BindTexture(0x0DE1, 0);

        u32 id = static_cast<u32>(texPoolIds_.size());
        texPoolIds_.push_back(tex);
        texWidths_.push_back(width);
        texHeights_.push_back(height);
        texFormats_.push_back(format);
        stats_.texturesCreated++;
        return id;
    }

    void destroyTexture(u32 texId) {
        if (texId >= static_cast<u32>(texPoolIds_.size())) return;
        if (texPoolIds_[texId] == 0) return;
        Gl::DeleteTextures(1, &texPoolIds_[texId]);
        texPoolIds_[texId] = 0;
        texWidths_[texId] = 0;
        texHeights_[texId] = 0;
        texFormats_[texId] = 0;
    }

    // ========================================================================
    // Frustum culling (CPU-side simulation of GPU-driven culling)
    // ========================================================================

    static void extractFrustumPlanes(const Mat4& viewProj, Vector<Vec4>& planes) {
        const f32* m = viewProj.m;

        // Left plane
        planes.push_back(Vec4(m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]));
        // Right plane
        planes.push_back(Vec4(m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]));
        // Bottom plane
        planes.push_back(Vec4(m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]));
        // Top plane
        planes.push_back(Vec4(m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]));
        // Near plane
        planes.push_back(Vec4(m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]));
        // Far plane
        planes.push_back(Vec4(m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]));

        // Normalize each plane
        for (u32 i = 0; i < 6; i++) {
            f32 len = std::sqrt(planes[i].x * planes[i].x +
                                planes[i].y * planes[i].y +
                                planes[i].z * planes[i].z);
            if (len > 0.0f) {
                f32 invLen = 1.0f / len;
                planes[i].x *= invLen;
                planes[i].y *= invLen;
                planes[i].z *= invLen;
                planes[i].w *= invLen;
            }
        }
    }

    static bool testAABBPlane(const Vec3& aabbMin, const Vec3& aabbMax, const Vec4& plane) {
        // Compute the positive vertex (P-vertex) for the plane normal
        Vec3 pVertex;
        pVertex.x = (plane.x >= 0.0f) ? aabbMax.x : aabbMin.x;
        pVertex.y = (plane.y >= 0.0f) ? aabbMax.y : aabbMin.y;
        pVertex.z = (plane.z >= 0.0f) ? aabbMax.z : aabbMin.z;

        // Dot product of plane normal with P-vertex + plane distance
        f32 dist = plane.x * pVertex.x + plane.y * pVertex.y +
                   plane.z * pVertex.z + plane.w;

        // If positive, the AABB is on the visible side of the plane
        return dist >= 0.0f;
    }

    void gpuCullInstances(const Mat4& viewProj,
                          const Vector<Vec3>& boundsMin,
                          const Vector<Vec3>& boundsMax,
                          Vector<u32>& visibleIndices,
                          u32 instanceCount) {
        Vector<Vec4> planes;
        planes.reserve(6);
        extractFrustumPlanes(viewProj, planes);

        u32 count = instanceCount;
        if (count > static_cast<u32>(boundsMin.size())) count = static_cast<u32>(boundsMin.size());
        if (count > static_cast<u32>(boundsMax.size())) count = static_cast<u32>(boundsMax.size());

        for (u32 i = 0; i < count; i++) {
            bool visible = true;
            for (u32 p = 0; p < 6; p++) {
                if (!testAABBPlane(boundsMin[i], boundsMax[i], planes[p])) {
                    visible = false;
                    break;
                }
            }
            if (visible) {
                visibleIndices.push_back(i);
            }
        }

        stats_.instancesCulled += count - static_cast<u32>(visibleIndices.size());
    }

    // ========================================================================
    // Config management
    // ========================================================================
    void setComputeConfig(const ComputeConfig& cfg) {
        computeCfg_ = cfg;
    }

    const ComputeConfig& getComputeConfig() const {
        return computeCfg_;
    }

    // ========================================================================
    // Reset pools and stats
    // ========================================================================
    void reset() {
        // Destroy all UAV textures
        for (u32 i = 0; i < computeCfg_.maxUAVs; i++) {
            if (uavTexIds_[i] != 0) {
                Gl::DeleteTextures(1, &uavTexIds_[i]);
                uavTexIds_[i] = 0;
            }
        }
        // Destroy all pool textures
        for (u32 i = 0; i < static_cast<u32>(texPoolIds_.size()); i++) {
            if (texPoolIds_[i] != 0) {
                Gl::DeleteTextures(1, &texPoolIds_[i]);
                texPoolIds_[i] = 0;
            }
        }
        uavPool_.clear();
        texturePool_.clear();
        texPoolIds_.clear();
        texWidths_.clear();
        texHeights_.clear();
        texFormats_.clear();
        activeUAVs_ = 0;
        for (u32 i = 0; i < static_cast<u32>(uavBuffers_.size()); i++) {
            if (uavBuffers_[i]) Gl::DeleteBuffers(1, &uavBuffers_[i]);
        }
        uavResources_.clear();
        uavBuffers_.clear();
        pendingDispatches_.clear();
        dispatchCount_ = 0;
        totalGpuBytes_ = 0;
        peakGpuBytes_ = 0;
        dispatchTimeMs_ = 0.0f;
        stats_ = ComputeStats{};
        FROST_LOG_INFO("[ComputeManager] pools and stats reset");
    }

    // ========================================================================
    // Accessors
    // ========================================================================
    GLuint program(u32 idx) const { return idx < shaderCount_ ? shaders_[idx].program : 0; }
    GLuint ssbo(u32 idx) const { return idx < ssboCount_ ? ssbos_[idx] : 0; }
    u64 ssboSize(u32 idx) const { return idx < ssboCount_ ? ssboSizes_[idx] : 0; }
    u32 shaderCount() const { return shaderCount_; }
    u32 ssboCount() const { return ssboCount_; }
    bool ready() const { return ready_; }

    u32 activeUAVs() const { return activeUAVs_; }
    u32 dispatchCount() const { return dispatchCount_; }
    const ComputeStats& stats() const { return stats_; }
    const Vector<u32>& uavPool() const { return uavPool_; }
    const Vector<u32>& texturePool() const { return texturePool_; }

private:
    GLuint currentProgram() {
        GLint prog = 0;
        Gl::GetIntegerv(0x8B8D, &prog); // GL_CURRENT_PROGRAM
        return (GLuint)prog;
    }

    ComputeShader shaders_[MAX_COMPUTE_SHADERS];
    GLuint ssbos_[MAX_SSBO_BINDINGS] = {};
    u64 ssboSizes_[MAX_SSBO_BINDINGS] = {};
    u32 shaderCount_ = 0;
    u32 ssboCount_ = 0;
    bool ready_ = false;

    // Compute config and stats
    ComputeConfig computeCfg_;
    ComputeStats stats_{};

    // UAV pool: indices are slot ids, tex ids stored in uavTexIds_
    static constexpr u32 MAX_UAV_SLOTS = 64;
    GLuint uavTexIds_[MAX_UAV_SLOTS] = {};
    u32 uavWidths_[MAX_UAV_SLOTS] = {};
    u32 uavHeights_[MAX_UAV_SLOTS] = {};
    u32 uavFormats_[MAX_UAV_SLOTS] = {};
    Vector<u32> uavPool_;
    Vector<u32> texturePool_;
    u32 activeUAVs_ = 0;
    u32 dispatchCount_ = 0;

    // Buffer-backed UAV resource pool and queued dispatch tracking
    struct PendingDispatch {
        u32 shaderId = 0;
        u32 gx = 1;
        u32 gy = 1;
        u32 gz = 1;
    };
    Vector<UAVResource> uavResources_;
    Vector<GLuint> uavBuffers_;
    Vector<PendingDispatch> pendingDispatches_;
    u32 totalGpuBytes_ = 0;
    u32 peakGpuBytes_ = 0;
    f32 dispatchTimeMs_ = 0.0f;

    // Texture pool
    Vector<GLuint> texPoolIds_;
    Vector<u32> texWidths_;
    Vector<u32> texHeights_;
    Vector<u32> texFormats_;
};

} // namespace Frost
