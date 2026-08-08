#include "Renderer/Texture.h"
#include "Renderer/Gl.h"
#include "Core/Log.h"

namespace Crux {

static GLenum toGlFilter(TextureFilter f) {
    switch (f) {
        case TextureFilter::Nearest: return GL_NEAREST;
        case TextureFilter::Bilinear: return GL_LINEAR;
        default: return GL_LINEAR_MIPMAP_LINEAR;
    }
}

static GLenum toGlWrap(TextureWrap w) {
    switch (w) {
        case TextureWrap::Clamp: return GL_CLAMP_TO_EDGE;
        case TextureWrap::MirroredRepeat: return GL_MIRRORED_REPEAT;
        default: return GL_REPEAT;
    }
}

Texture2D::~Texture2D() { destroy(); }

void Texture2D::create(u32 w, u32 h, const u8* rgba, bool mipmaps,
                       TextureFilter filter, TextureWrap wrap) {
    destroy();
    w_ = w; h_ = h;
    Gl::GenTextures(1, &id_);
    Gl::BindTexture(GL_TEXTURE_2D, id_);
    Gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)toGlFilter(filter));
    Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                      (GLint)(filter == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR));
    Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)toGlWrap(wrap));
    Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)toGlWrap(wrap));
    if (mipmaps && filter == TextureFilter::Trilinear) {
        Gl::GenerateMipmap(GL_TEXTURE_2D);
    }
    Gl::BindTexture(GL_TEXTURE_2D, 0);
}

void Texture2D::createSolid(u32 w, u32 h, const u8 rgba[4]) {
    u8* data = new u8[(usize)w * h * 4];
    for (u32 i = 0; i < w * h * 4; i += 4) {
        data[i] = rgba[0]; data[i + 1] = rgba[1]; data[i + 2] = rgba[2]; data[i + 3] = rgba[3];
    }
    create(w, h, data, false, TextureFilter::Nearest, TextureWrap::Repeat);
    delete[] data;
}

void Texture2D::createFloat(u32 w, u32 h) {
    destroy();
    w_ = w; h_ = h;
    Gl::GenTextures(1, &id_);
    Gl::BindTexture(GL_TEXTURE_2D, id_);
    Gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, (GLsizei)w, (GLsizei)h, 0,
                   GL_RGBA, GL_FLOAT, nullptr);
    Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    Gl::BindTexture(GL_TEXTURE_2D, 0);
}

void Texture2D::bind(u32 unit) const {
    Gl::ActiveTexture(GL_TEXTURE0 + unit);
    Gl::BindTexture(GL_TEXTURE_2D, id_);
}

void Texture2D::destroy() {
    if (id_) Gl::DeleteTextures(1, &id_);
    id_ = 0; w_ = h_ = 0;
}

void Texture2D::readPixels(u8* out) const {
    Gl::BindTexture(GL_TEXTURE_2D, id_);
    Gl::GetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
    Gl::BindTexture(GL_TEXTURE_2D, 0);
}

Cubemap::~Cubemap() { destroy(); }

void Cubemap::create(u32 size, const u8* const* faces, TextureFilter filter) {
    destroy();
    size_ = size;
    Gl::GenTextures(1, &id_);
    Gl::BindTexture(GL_TEXTURE_CUBE_MAP, id_);
    GLenum targets[6] = {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
    };
    for (i32 i = 0; i < 6; i++) {
        Gl::TexImage2D(targets[i], 0, GL_RGBA8, (GLsizei)size, (GLsizei)size, 0,
                       GL_RGBA, GL_UNSIGNED_BYTE, faces ? faces[i] : nullptr);
    }
    Gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, (GLint)toGlFilter(filter));
    Gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER,
                      (GLint)(filter == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR));
    Gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    Gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    Gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    Gl::BindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void Cubemap::bind(u32 unit) const {
    Gl::ActiveTexture(GL_TEXTURE0 + unit);
    Gl::BindTexture(GL_TEXTURE_CUBE_MAP, id_);
}

void Cubemap::destroy() {
    if (id_) Gl::DeleteTextures(1, &id_);
    id_ = 0; size_ = 0;
}

FrameBuffer::~FrameBuffer() { destroy(); }

void FrameBuffer::createInternal(u32 w, u32 h, bool useDepth, u32 samples,
                                 bool floatColor, bool cubeDepth) {
    cubeDepth_ = cubeDepth;
    w_ = w; h_ = h;
    Gl::GenFramebuffers(1, &fbo_);
    Gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_);

    if (cubeDepth) {
        Gl::GenTextures(1, &depthTex_);
        Gl::BindTexture(GL_TEXTURE_CUBE_MAP, depthTex_);
        for (i32 i = 0; i < 6; i++) {
            Gl::TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_DEPTH_COMPONENT32F,
                           (GLsizei)w, (GLsizei)h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        }
        Gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        Gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        Gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        Gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        Gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        Gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_TEXTURE_CUBE_MAP_POSITIVE_X, depthTex_, 0);
    } else if (useDepth) {
        Gl::GenTextures(1, &depthTex_);
        Gl::BindTexture(GL_TEXTURE_2D, depthTex_);
        Gl::TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, (GLsizei)w, (GLsizei)h, 0,
                       GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        Gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex_, 0);
    }

    if (!cubeDepth) {
        if (samples > 1) {
            Gl::GenRenderbuffers(1, &colorRbo_);
            Gl::BindRenderbuffer(GL_RENDERBUFFER, colorRbo_);
            Gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, (GLsizei)samples,
                                               floatColor ? GL_RGBA16F : GL_RGBA8,
                                               (GLsizei)w, (GLsizei)h);
            Gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                        GL_RENDERBUFFER, colorRbo_);
            if (useDepth) {
                Gl::GenRenderbuffers(1, &depthRbo_);
                Gl::BindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
                Gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, (GLsizei)samples,
                                                   GL_DEPTH_COMPONENT32F, (GLsizei)w, (GLsizei)h);
                Gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                            GL_RENDERBUFFER, depthRbo_);
            }
        } else {
            if (floatColor) {
                Gl::GenTextures(1, &colorTex_);
                Gl::BindTexture(GL_TEXTURE_2D, colorTex_);
                Gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, (GLsizei)w, (GLsizei)h, 0,
                               GL_RGBA, GL_FLOAT, nullptr);
                Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                Gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                         GL_TEXTURE_2D, colorTex_, 0);
            } else {
                Gl::GenTextures(1, &colorTex_);
                Gl::BindTexture(GL_TEXTURE_2D, colorTex_);
                Gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0,
                               GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                Gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                         GL_TEXTURE_2D, colorTex_, 0);
            }
        }
    }

    if (Gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        CRUX_LOG_ERROR("[FrameBuffer] framebuffer incomplete");
    }
    Gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool FrameBuffer::create(u32 w, u32 h, bool useDepth, u32 samples, bool floatColor) {
    destroy();
    createInternal(w, h, useDepth, samples, floatColor, false);
    return valid();
}

bool FrameBuffer::createCubeDepth(u32 size) {
    destroy();
    createInternal(size, size, true, 1, false, true);
    return valid();
}

bool FrameBuffer::createDepth(u32 w, u32 h) {
    destroy();
    w_ = w; h_ = h;
    cubeDepth_ = false;
    Gl::GenFramebuffers(1, &fbo_);
    Gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    Gl::GenTextures(1, &depthTex_);
    Gl::BindTexture(GL_TEXTURE_2D, depthTex_);
    Gl::TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, (GLsizei)w, (GLsizei)h, 0,
                   GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    Gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    Gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex_, 0);
    GLenum none = GL_NONE;
    Gl::DrawBuffers(1, &none);
    bool ok = Gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!ok) CRUX_LOG_ERROR("[FrameBuffer] raw depth framebuffer incomplete");
    Gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok && fbo_ != 0;
}

void FrameBuffer::destroy() {
    if (fbo_) Gl::DeleteFramebuffers(1, &fbo_);
    if (colorTex_) Gl::DeleteTextures(1, &colorTex_);
    if (depthTex_) Gl::DeleteTextures(1, &depthTex_);
    if (colorRbo_) Gl::DeleteRenderbuffers(1, &colorRbo_);
    if (depthRbo_) Gl::DeleteRenderbuffers(1, &depthRbo_);
    fbo_ = colorTex_ = depthTex_ = colorRbo_ = depthRbo_ = 0;
}

void FrameBuffer::bind() const { Gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_); }
void FrameBuffer::bindRead() const { Gl::BindFramebuffer(GL_READ_FRAMEBUFFER, fbo_); }
void FrameBuffer::bindDraw() const { Gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_); }
void FrameBuffer::unbind() const { Gl::BindFramebuffer(GL_FRAMEBUFFER, 0); }

void FrameBuffer::depthBind(u32 unit) const {
    Gl::ActiveTexture(GL_TEXTURE0 + unit);
    Gl::BindTexture(GL_TEXTURE_2D, depthTex_);
}

void FrameBuffer::clear(const float color[4], bool clearDepth) const {
    bind();
    Gl::ClearColor(color[0], color[1], color[2], color[3]);
    GLbitfield bits = GL_COLOR_BUFFER_BIT;
    if (clearDepth && depthTex_) bits |= GL_DEPTH_BUFFER_BIT;
    Gl::Clear(bits);
    unbind();
}

void FrameBuffer::resize(u32 w, u32 h) {
    if (w == w_ && h == h_) return;
    bool useDepth = depthTex_ || depthRbo_;
    u32 samples = colorRbo_ ? 4 : 1;
    bool floatColor = false;
    destroy();
    createInternal(w, h, useDepth, samples, floatColor, cubeDepth_);
}

}
