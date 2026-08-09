#pragma once

// Minimal OpenGL function loader for the FrostEngine renderer.
// Function pointers are resolved through glXGetProcAddressARB at startup
// so the engine has zero runtime dependencies beyond libGL and libX11.

#include "Core/Types.h"
#include <GL/gl.h>
#include <GL/glext.h>

namespace Frost {
namespace Gl {

#define FROST_GL_FUNCS(F) \
    F(ActiveTexture, void, (GLenum)) \
    F(AttachShader, void, (GLuint, GLuint)) \
    F(BindBuffer, void, (GLenum, GLuint)) \
    F(BindBufferBase, void, (GLenum, GLuint, GLuint)) \
    F(BindBufferRange, void, (GLenum, GLuint, GLuint, GLintptr, GLsizeiptr)) \
    F(BindFramebuffer, void, (GLenum, GLuint)) \
    F(BindImageTexture, void, (GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum)) \
    F(BindRenderbuffer, void, (GLenum, GLuint)) \
    F(BindTexture, void, (GLenum, GLuint)) \
    F(BindVertexArray, void, (GLuint)) \
    F(BlendFunc, void, (GLenum, GLenum)) \
    F(BlendFuncSeparate, void, (GLenum, GLenum, GLenum, GLenum)) \
    F(BlitFramebuffer, void, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    F(BufferData, void, (GLenum, GLsizeiptr, const void*, GLenum)) \
    F(BufferSubData, void, (GLenum, GLintptr, GLsizeiptr, const void*)) \
    F(CheckFramebufferStatus, GLenum, (GLenum)) \
    F(Clear, void, (GLbitfield)) \
    F(ClearColor, void, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    F(ClearDepth, void, (GLdouble)) \
    F(ClearBufferfv, void, (GLenum, GLint, const GLfloat*)) \
    F(CompileShader, void, (GLuint)) \
    F(CreateProgram, GLuint, ()) \
    F(CreateShader, GLuint, (GLenum)) \
    F(CullFace, void, (GLenum)) \
    F(DeleteBuffers, void, (GLsizei, const GLuint*)) \
    F(DeleteFramebuffers, void, (GLsizei, const GLuint*)) \
    F(DeleteProgram, void, (GLuint)) \
    F(DeleteRenderbuffers, void, (GLsizei, const GLuint*)) \
    F(DeleteShader, void, (GLuint)) \
    F(DeleteTextures, void, (GLsizei, const GLuint*)) \
    F(DeleteVertexArrays, void, (GLsizei, const GLuint*)) \
    F(DepthFunc, void, (GLenum)) \
    F(DepthMask, void, (GLboolean)) \
    F(DepthRange, void, (GLdouble, GLdouble)) \
    F(DetachShader, void, (GLuint, GLuint)) \
    F(Disable, void, (GLenum)) \
    F(DisableVertexAttribArray, void, (GLuint)) \
    F(DrawArrays, void, (GLenum, GLint, GLsizei)) \
    F(DrawArraysInstanced, void, (GLenum, GLint, GLsizei, GLsizei)) \
    F(DrawBuffers, void, (GLsizei, const GLenum*)) \
    F(DrawElements, void, (GLenum, GLsizei, GLenum, const void*)) \
    F(DrawElementsInstanced, void, (GLenum, GLsizei, GLenum, const void*, GLsizei)) \
    F(DispatchCompute, void, (GLuint, GLuint, GLuint)) \
    F(DispatchComputeIndirect, void, (GLintptr)) \
    F(Enable, void, (GLenum)) \
    F(EnableVertexAttribArray, void, (GLuint)) \
    F(FenceSync, GLsync, (GLenum, GLbitfield)) \
    F(ClientWaitSync, GLenum, (GLsync, GLbitfield, GLuint64)) \
    F(DeleteSync, void, (GLsync)) \
    F(Flush, void, ()) \
    F(FramebufferRenderbuffer, void, (GLenum, GLenum, GLenum, GLuint)) \
    F(FramebufferTexture, void, (GLenum, GLenum, GLuint, GLint)) \
    F(FramebufferTexture2D, void, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    F(FramebufferTexture3D, void, (GLenum, GLenum, GLenum, GLuint, GLint, GLint)) \
    F(FrontFace, void, (GLenum)) \
    F(GenerateMipmap, void, (GLenum)) \
    F(GenBuffers, void, (GLsizei, GLuint*)) \
    F(GenFramebuffers, void, (GLsizei, GLuint*)) \
    F(GenRenderbuffers, void, (GLsizei, GLuint*)) \
    F(GenTextures, void, (GLsizei, GLuint*)) \
    F(GenVertexArrays, void, (GLsizei, GLuint*)) \
    F(GetBufferSubData, void, (GLenum, GLintptr, GLsizeiptr, void*)) \
    F(GetError, GLenum, ()) \
    F(GetIntegerv, void, (GLenum, GLint*)) \
    F(GetProgramInfoLog, void, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    F(GetProgramiv, void, (GLuint, GLenum, GLint*)) \
    F(GetShaderInfoLog, void, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    F(GetShaderiv, void, (GLuint, GLenum, GLint*)) \
    F(GetString, const GLubyte*, (GLenum)) \
    F(GetUniformLocation, GLint, (GLuint, const GLchar*)) \
    F(GetAttribLocation, GLint, (GLuint, const GLchar*)) \
    F(GetUniformBlockIndex, GLuint, (GLuint, const GLchar*)) \
    F(UniformBlockBinding, void, (GLuint, GLuint, GLuint)) \
    F(GetProgramResourceIndex, GLuint, (GLuint, GLenum, const GLchar*)) \
    F(GetProgramResourceiv, void, (GLuint, GLenum, GLuint, GLsizei, const GLenum*, GLsizei, GLsizei*, GLint*)) \
    F(LineWidth, void, (GLfloat)) \
    F(LinkProgram, void, (GLuint)) \
    F(MapBuffer, void*, (GLenum, GLenum)) \
    F(UnmapBuffer, GLboolean, (GLenum)) \
    F(MemoryBarrier, void, (GLbitfield)) \
    F(PixelStorei, void, (GLenum, GLint)) \
    F(PointSize, void, (GLfloat)) \
    F(PolygonMode, void, (GLenum, GLenum)) \
    F(PolygonOffset, void, (GLfloat, GLfloat)) \
    F(ColorMask, void, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    F(RenderbufferStorage, void, (GLenum, GLenum, GLsizei, GLsizei)) \
    F(RenderbufferStorageMultisample, void, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    F(ShaderSource, void, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    F(TexImage2D, void, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    F(TexImage3D, void, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    F(TexParameterf, void, (GLenum, GLenum, GLfloat)) \
    F(TexParameteri, void, (GLenum, GLenum, GLint)) \
    F(TexStorage2D, void, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    F(TexStorage3D, void, (GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei)) \
    F(TexSubImage2D, void, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    F(Uniform1f, void, (GLint, GLfloat)) \
    F(Uniform1i, void, (GLint, GLint)) \
    F(Uniform1ui, void, (GLint, GLuint)) \
    F(Uniform2f, void, (GLint, GLfloat, GLfloat)) \
    F(Uniform3f, void, (GLint, GLfloat, GLfloat, GLfloat)) \
    F(Uniform4f, void, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    F(UniformMatrix4fv, void, (GLint, GLsizei, GLboolean, const GLfloat*)) \
    F(UseProgram, void, (GLuint)) \
    F(VertexAttribDivisor, void, (GLuint, GLuint)) \
    F(VertexAttribPointer, void, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    F(Viewport, void, (GLint, GLint, GLsizei, GLsizei)) \
    F(GetTexImage, void, (GLenum, GLint, GLenum, GLenum, void*)) \
    F(CopyTexImage2D, void, (GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint)) \
    F(ReadPixels, void, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))

#define FROST_DECLARE_FUNC(NAME, RET, ARGS) extern RET (*NAME) ARGS;
FROST_GL_FUNCS(FROST_DECLARE_FUNC)
#undef FROST_DECLARE_FUNC

// Loads all entry points. Returns true on success.
bool loadFunctions();

// Prints the current OpenGL error state, if any (debug helper).
const char* errorString(GLenum err);

}
}
