#include "Renderer/Mesh.h"
#include "Renderer/Gl.h"
#include "Core/Log.h"

namespace Crux {

Mesh::~Mesh() { destroy(); }

void Mesh::destroy() {
    if (vao_) Gl::DeleteVertexArrays(1, &vao_);
    if (vbo_) Gl::DeleteBuffers(1, &vbo_);
    if (ebo_) Gl::DeleteBuffers(1, &ebo_);
    if (instVbo_) Gl::DeleteBuffers(1, &instVbo_);
    vao_ = vbo_ = ebo_ = instVbo_ = 0;
    vertexCount_ = indexCount_ = instanceCount_ = 0;
    hasIndices_ = false;
}

void Mesh::setupAttributes() const {
    Gl::BindVertexArray(vao_);
    Gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    constexpr GLsizei stride = (GLsizei)sizeof(Vertex);

    Gl::EnableVertexAttribArray(0);
    Gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, position));
    Gl::EnableVertexAttribArray(1);
    Gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, normal));
    Gl::EnableVertexAttribArray(2);
    Gl::VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, tangent));
    Gl::EnableVertexAttribArray(3);
    Gl::VertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, uv));

    if (instVbo_) {
        Gl::BindBuffer(GL_ARRAY_BUFFER, instVbo_);
        Gl::EnableVertexAttribArray(4);
        Gl::VertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4), (void*)0);
        Gl::EnableVertexAttribArray(5);
        Gl::VertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4), (void*)16);
        Gl::EnableVertexAttribArray(6);
        Gl::VertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4), (void*)32);
        Gl::EnableVertexAttribArray(7);
        Gl::VertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4), (void*)48);
        Gl::VertexAttribDivisor(4, 1);
        Gl::VertexAttribDivisor(5, 1);
        Gl::VertexAttribDivisor(6, 1);
        Gl::VertexAttribDivisor(7, 1);
    }
    Gl::BindVertexArray(0);
}

void Mesh::upload(const Vector<Vertex>& verts, const Vector<u32>& indices) {
    destroy();
    Gl::GenVertexArrays(1, &vao_);
    Gl::GenBuffers(1, &vbo_);
    Gl::BindVertexArray(vao_);

    Gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    Gl::BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(Vertex)),
                   verts.data(), GL_STATIC_DRAW);

    if (!indices.empty()) {
        Gl::GenBuffers(1, &ebo_);
        Gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        Gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices.size() * sizeof(u32)),
                       indices.data(), GL_STATIC_DRAW);
        hasIndices_ = true;
    }

    setupAttributes();
    vertexCount_ = (u32)verts.size();
    indexCount_ = (u32)indices.size();
}

void Mesh::uploadInstances(const Vector<Mat4>& transforms) const {
    if (!vao_) return;
    Gl::BindVertexArray(vao_);
    if (!instVbo_) Gl::GenBuffers(1, &instVbo_);
    Gl::BindBuffer(GL_ARRAY_BUFFER, instVbo_);
    Gl::BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(transforms.size() * sizeof(Mat4)),
                   transforms.data(), GL_DYNAMIC_DRAW);
    Gl::BindVertexArray(0);
    instanceCount_ = (u32)transforms.size();
    if (instanceCount_) setupAttributes();
}

void Mesh::draw() const {
    if (!vao_) return;
    Gl::BindVertexArray(vao_);
    if (hasIndices_) {
        Gl::DrawElements(lineTopology_ ? GL_LINES : GL_TRIANGLES, (GLsizei)indexCount_,
                         GL_UNSIGNED_INT, nullptr);
    } else {
        Gl::DrawArrays(lineTopology_ ? GL_LINES : GL_TRIANGLES, 0, (GLsizei)vertexCount_);
    }
    Gl::BindVertexArray(0);
}

void Mesh::drawInstanced(u32 count) const {
    if (!vao_ || !instVbo_) return;
    Gl::BindVertexArray(vao_);
    if (hasIndices_) {
        Gl::DrawElementsInstanced(GL_TRIANGLES, (GLsizei)indexCount_, GL_UNSIGNED_INT,
                                  nullptr, (GLsizei)count);
    } else {
        Gl::DrawArraysInstanced(GL_TRIANGLES, 0, (GLsizei)vertexCount_, (GLsizei)count);
    }
    Gl::BindVertexArray(0);
}

void Mesh::drawLines() const {
    if (!vao_) return;
    Gl::BindVertexArray(vao_);
    if (hasIndices_) {
        Gl::DrawElements(GL_LINES, (GLsizei)indexCount_, GL_UNSIGNED_INT, nullptr);
    } else {
        Gl::DrawArrays(GL_LINES, 0, (GLsizei)vertexCount_);
    }
    Gl::BindVertexArray(0);
}

}
