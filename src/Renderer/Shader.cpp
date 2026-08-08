#include "Renderer/Shader.h"
#include "Renderer/Gl.h"
#include "Core/Log.h"
#include <cstring>

namespace Crux {

Shader::~Shader() {
    if (program_) Gl::DeleteProgram(program_);
}

GLuint Shader::compileStage(GLenum type, const char* src) const {
    GLuint sh = Gl::CreateShader(type);
    Gl::ShaderSource(sh, 1, &src, nullptr);
    Gl::CompileShader(sh);
    GLint ok = 0;
    Gl::GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        Gl::GetShaderInfoLog(sh, sizeof(log), nullptr, log);
        CRUX_LOG_ERROR("[Shader] compile error: %s", log);
        Gl::DeleteShader(sh);
        return 0;
    }
    return sh;
}

bool Shader::create(const char* vertexSrc, const char* fragmentSrc, const char* name) {
    GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSrc);
    if (!vs) return false;
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSrc);
    if (!fs) { Gl::DeleteShader(vs); return false; }

    program_ = Gl::CreateProgram();
    Gl::AttachShader(program_, vs);
    Gl::AttachShader(program_, fs);
    Gl::LinkProgram(program_);

    GLint ok = 0;
    Gl::GetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        Gl::GetProgramInfoLog(program_, sizeof(log), nullptr, log);
        CRUX_LOG_ERROR("[Shader:%s] link error: %s", name, log);
        Gl::DeleteProgram(program_); program_ = 0;
        Gl::DeleteShader(vs); Gl::DeleteShader(fs);
        return false;
    }

    Gl::DetachShader(program_, vs);
    Gl::DetachShader(program_, fs);
    Gl::DeleteShader(vs);
    Gl::DeleteShader(fs);
    return true;
}

bool Shader::createCompute(const char* computeSrc, const char* name) {
    GLuint cs = compileStage(GL_COMPUTE_SHADER, computeSrc);
    if (!cs) return false;

    program_ = Gl::CreateProgram();
    Gl::AttachShader(program_, cs);
    Gl::LinkProgram(program_);

    GLint ok = 0;
    Gl::GetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        Gl::GetProgramInfoLog(program_, sizeof(log), nullptr, log);
        CRUX_LOG_ERROR("[Shader:%s] link error: %s", name, log);
        Gl::DeleteProgram(program_); program_ = 0;
        Gl::DeleteShader(cs);
        return false;
    }

    Gl::DetachShader(program_, cs);
    Gl::DeleteShader(cs);
    return true;
}

void Shader::use() const { Gl::UseProgram(program_); }

GLint Shader::cachedLocation(const char* name) const {
    u32 h = 2166136261u;
    for (const char* p = name; *p; p++) h = (h ^ (u8)*p) * 16777619u;
    auto it = locCache_.find(h);
    if (it != locCache_.end()) return it->second;
    GLint loc = Gl::GetUniformLocation(program_, name);
    locCache_[h] = loc;
    return loc;
}

GLint Shader::location(const char* name) const { return cachedLocation(name); }

void Shader::setMat4(const char* name, const Mat4& m) const {
    Gl::UniformMatrix4fv(cachedLocation(name), 1, GL_FALSE, m.data());
}
void Shader::setMat3(const char* name, const Mat3& m) const {
    Gl::UniformMatrix4fv(cachedLocation(name), 1, GL_FALSE, m.m);
}
void Shader::setVec2(const char* name, const Vec2& v) const {
    Gl::Uniform2f(cachedLocation(name), v.x, v.y);
}
void Shader::setVec3(const char* name, const Vec3& v) const {
    Gl::Uniform3f(cachedLocation(name), v.x, v.y, v.z);
}
void Shader::setVec4(const char* name, const Vec4& v) const {
    Gl::Uniform4f(cachedLocation(name), v.x, v.y, v.z, v.w);
}
void Shader::setFloat(const char* name, f32 v) const {
    Gl::Uniform1f(cachedLocation(name), v);
}
void Shader::setInt(const char* name, i32 v) const {
    Gl::Uniform1i(cachedLocation(name), v);
}
void Shader::setBool(const char* name, bool v) const {
    Gl::Uniform1i(cachedLocation(name), v ? 1 : 0);
}

}
