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
        // Cleanup SSBOs
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

    GLuint program(u32 idx) const { return idx < shaderCount_ ? shaders_[idx].program : 0; }
    GLuint ssbo(u32 idx) const { return idx < ssboCount_ ? ssbos_[idx] : 0; }
    u64 ssboSize(u32 idx) const { return idx < ssboCount_ ? ssboSizes_[idx] : 0; }
    u32 shaderCount() const { return shaderCount_; }
    u32 ssboCount() const { return ssboCount_; }
    bool ready() const { return ready_; }

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
};

} // namespace Frost
