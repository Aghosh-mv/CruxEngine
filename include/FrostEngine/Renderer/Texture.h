#pragma once

#include "Core/Types.h"
#include <GL/gl.h>

namespace Frost {

enum class TextureFilter { Nearest, Bilinear, Trilinear };
enum class TextureWrap { Repeat, Clamp, MirroredRepeat };

// 2D texture with optional mipmaps. Backs procedural and loaded assets.
class Texture2D {
public:
    Texture2D() = default;
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    Texture2D(Texture2D&& other) noexcept : id_(other.id_), w_(other.w_), h_(other.h_) {
        other.id_ = 0; other.w_ = 0; other.h_ = 0;
    }
    Texture2D& operator=(Texture2D&& other) noexcept {
        if (this != &other) {
            destroy();
            id_ = other.id_; w_ = other.w_; h_ = other.h_;
            other.id_ = 0; other.w_ = 0; other.h_ = 0;
        }
        return *this;
    }

    // Uploads RGBA8 image data. If mipmaps is true, mip chain is generated.
    void create(u32 w, u32 h, const u8* rgba = nullptr, bool mipmaps = true,
                TextureFilter filter = TextureFilter::Trilinear,
                TextureWrap wrap = TextureWrap::Repeat);

    // Creates a solid-color texture.
    void createSolid(u32 w, u32 h, const u8 rgba[4]);

    // Float (HDR) texture used for bloom ping-pong.
    void createFloat(u32 w, u32 h);

    void bind(u32 unit = 0) const;
    void destroy();

    u32 width() const { return w_; }
    u32 height() const { return h_; }
    GLuint handle() const { return id_; }

    // Reads pixels back to CPU (used for heightfield export / debug).
    void readPixels(u8* out) const;

private:
    GLuint id_ = 0;
    u32 w_ = 0, h_ = 0;
};

// Cube map used for the procedural skybox.
class Cubemap {
public:
    Cubemap() = default;
    ~Cubemap();

    Cubemap(const Cubemap&) = delete;
    Cubemap& operator=(const Cubemap&) = delete;

    Cubemap(Cubemap&& other) noexcept : id_(other.id_), size_(other.size_) {
        other.id_ = 0; other.size_ = 0;
    }
    Cubemap& operator=(Cubemap&& other) noexcept {
        if (this != &other) {
            destroy();
            id_ = other.id_; size_ = other.size_;
            other.id_ = 0; other.size_ = 0;
        }
        return *this;
    }

    // faces: 6 RGBA images (+x,-x,+y,-y,+z,-z).
    void create(u32 size, const u8* const* faces, TextureFilter filter = TextureFilter::Bilinear);
    void bind(u32 unit = 0) const;
    void destroy();

    u32 size() const { return size_; }
    GLuint handle() const { return id_; }

private:
    GLuint id_ = 0;
    u32 size_ = 0;
};

// Offscreen framebuffer with an optional color render target and depth,
// plus multisample support. Used for shadow maps, bloom and MSAA resolve.
class FrameBuffer {
public:
    FrameBuffer() = default;
    ~FrameBuffer();

    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;

    // Creates a color-only framebuffer (optional depth texture for shadow maps).
    bool create(u32 w, u32 h, bool useDepth, u32 samples = 1, bool floatColor = false);
    bool createCubeDepth(u32 size);   // cubemap depth target (point light shadows)
    // Raw (non-comparison) depth-only target for SSAO / post sampling.
    bool createDepth(u32 w, u32 h);
    void destroy();

    void bind() const;
    void bindRead() const;
    void bindDraw() const;
    void unbind() const;

    // Binds the depth texture to a texture unit (sampled in the main pass).
    void depthBind(u32 unit) const;

    void clear(const float color[4], bool clearDepth = true) const;
    void resize(u32 w, u32 h);

    GLuint color() const { return colorTex_; }
    GLuint depth() const { return depthTex_; }
    GLuint handle() const { return fbo_; }
    u32 width() const { return w_; }
    u32 height() const { return h_; }
    bool valid() const { return fbo_ != 0; }

private:
    void createInternal(u32 w, u32 h, bool useDepth, u32 samples, bool floatColor, bool cubeDepth);
    GLuint fbo_ = 0;
    GLuint colorTex_ = 0, depthTex_ = 0;
    GLuint colorRbo_ = 0, depthRbo_ = 0;
    u32 w_ = 0, h_ = 0;
    bool cubeDepth_ = false;
};

}
