#pragma once

#include "Core/Types.h"
#include "Core/Math.h"
#include "Core/Vector.h"
#include <GL/gl.h>

namespace Frost {

struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec4 tangent;      // xyz = tangent, w = handedness
    Vec2 uv;

    Vertex() = default;
    Vertex(const Vec3& p, const Vec3& n, const Vec2& uv, const Vec3& t = Vec3(1, 0, 0))
        : position(p), normal(n), tangent(t.x, t.y, t.z, 1.0f), uv(uv) {}
};

// GPU mesh: interleaved vertex buffer + index buffer. Optional per-instance
// matrix buffer for cheap instanced rendering of trees/rocks/grass.
class Mesh {
public:
    Mesh() = default;
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    Mesh(Mesh&& other) noexcept
        : vao_(other.vao_), vbo_(other.vbo_), ebo_(other.ebo_), instVbo_(other.instVbo_),
          vertexCount_(other.vertexCount_), indexCount_(other.indexCount_),
          instanceCount_(other.instanceCount_), hasIndices_(other.hasIndices_),
          lineTopology_(other.lineTopology_) {
        other.vao_ = other.vbo_ = other.ebo_ = other.instVbo_ = 0;
        other.vertexCount_ = other.indexCount_ = other.instanceCount_ = 0;
    }
    Mesh& operator=(Mesh&& other) noexcept {
        if (this != &other) {
            destroy();
            vao_ = other.vao_; vbo_ = other.vbo_; ebo_ = other.ebo_; instVbo_ = other.instVbo_;
            vertexCount_ = other.vertexCount_; indexCount_ = other.indexCount_;
            instanceCount_ = other.instanceCount_;
            hasIndices_ = other.hasIndices_; lineTopology_ = other.lineTopology_;
            other.vao_ = other.vbo_ = other.ebo_ = other.instVbo_ = 0;
            other.vertexCount_ = other.indexCount_ = other.instanceCount_ = 0;
        }
        return *this;
    }

    void upload(const Vector<Vertex>& verts, const Vector<u32>& indices);
    void uploadInstances(const Vector<Mat4>& transforms) const;
    void destroy();

    void draw() const;                       // indexed draw
    void drawInstanced(u32 count) const;     // draw current instance count
    void drawLines() const;                  // GL_LINES topology

    void setLineTopology(bool line) { lineTopology_ = line; }

    u32 vertexCount() const { return vertexCount_; }
    u32 indexCount() const { return indexCount_; }
    u32 instanceCount() const { return instanceCount_; }
    void setInstanceCount(u32 c) { instanceCount_ = c; }

    void setupAttributes() const;

private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    mutable GLuint instVbo_ = 0;
    u32 vertexCount_ = 0;
    u32 indexCount_ = 0;
    mutable u32 instanceCount_ = 0;
    bool hasIndices_ = false;
    bool lineTopology_ = false;
};

}
