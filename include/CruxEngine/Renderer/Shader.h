#pragma once

#include "Core/Types.h"
#include "Core/Math.h"
#include <GL/gl.h>
#include <unordered_map>

namespace Crux {

// Compiled GLSL program with cached uniform locations for cheap binding.
class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Compiles vertex + fragment from source strings. Returns true on success.
    bool create(const char* vertexSrc, const char* fragmentSrc,
                const char* name = "shader");

    // Compiles a compute shader from source. Returns true on success.
    bool createCompute(const char* computeSrc, const char* name = "compute");

    // Links an additional geometry/compute stage if ever needed.
    void use() const;
    GLuint handle() const { return program_; }

    GLint location(const char* name) const;

    void setMat4(const char* name, const Mat4& m) const;
    void setMat3(const char* name, const Mat3& m) const;
    void setVec2(const char* name, const Vec2& v) const;
    void setVec3(const char* name, const Vec3& v) const;
    void setVec4(const char* name, const Vec4& v) const;
    void setFloat(const char* name, f32 v) const;
    void setInt(const char* name, i32 v) const;
    void setBool(const char* name, bool v) const;

private:
    GLuint compileStage(GLenum type, const char* src) const;
    GLuint program_ = 0;
    mutable std::unordered_map<u32, GLint> locCache_;
    GLint cachedLocation(const char* name) const;
};

}
