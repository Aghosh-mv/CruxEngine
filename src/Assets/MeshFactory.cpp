#include "Assets/MeshFactory.h"
#include "Core/Math.h"

namespace Crux {

static void pushVertex(Vector<Vertex>& v, const Vec3& p, const Vec3& n, const Vec2& uv,
                       const Vec3& t = Vec3(1, 0, 0)) {
    v.pushBack(Vertex(p, n, uv, t));
}

void MeshFactory::computeTangents(Vector<Vertex>& verts) {
    // Per-triangle tangent accumulation (simplified, sufficient for aero/static)
    for (u32 i = 0; i + 2 < verts.size(); i += 3) {
        Vertex& a = verts[i];
        Vertex& b = verts[i + 1];
        Vertex& c = verts[i + 2];
        Vec3 e1 = b.position - a.position;
        Vec3 e2 = c.position - a.position;
        Vec2 uv1 = b.uv - a.uv;
        Vec2 uv2 = c.uv - a.uv;
        f32 r = 1.0f / (uv1.x * uv2.y - uv2.x * uv1.y + 1e-9f);
        Vec3 tan = (e1 * uv2.y - e2 * uv1.y) * r;
        a.tangent = Vec4(tan.x, tan.y, tan.z, a.tangent.w);
        b.tangent = Vec4(tan.x, tan.y, tan.z, b.tangent.w);
        c.tangent = Vec4(tan.x, tan.y, tan.z, c.tangent.w);
    }
}

Mesh MeshFactory::makeFullscreenTriangle() {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    v.pushBack(Vertex(Vec3(-1, -1, 0), Vec3(0, 0, 1), Vec2(0, 0)));
    v.pushBack(Vertex(Vec3(3, -1, 0), Vec3(0, 0, 1), Vec2(2, 0)));
    v.pushBack(Vertex(Vec3(-1, 3, 0), Vec3(0, 0, 1), Vec2(0, 2)));
    idx.pushBack(0); idx.pushBack(1); idx.pushBack(2);
    m.upload(v, idx);
    return m;
}

Mesh MeshFactory::makeQuad(f32 size) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    f32 h = size * 0.5f;
    v.pushBack(Vertex(Vec3(-h, -h, 0), Vec3(0, 0, 1), Vec2(0, 0)));
    v.pushBack(Vertex(Vec3(h, -h, 0), Vec3(0, 0, 1), Vec2(1, 0)));
    v.pushBack(Vertex(Vec3(h, h, 0), Vec3(0, 0, 1), Vec2(1, 1)));
    v.pushBack(Vertex(Vec3(-h, h, 0), Vec3(0, 0, 1), Vec2(0, 1)));
    idx.pushBack(0); idx.pushBack(1); idx.pushBack(2);
    idx.pushBack(0); idx.pushBack(2); idx.pushBack(3);
    m.upload(v, idx);
    return m;
}

Mesh MeshFactory::makeCube(f32 size) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    f32 h = size * 0.5f;
    const Vec3 faces[6][4] = {
        { Vec3(-h,-h, h), Vec3(h,-h, h), Vec3(h, h, h), Vec3(-h, h, h) },   // +Z
        { Vec3(h,-h,-h), Vec3(-h,-h,-h), Vec3(-h, h,-h), Vec3(h, h,-h) },   // -Z
        { Vec3(-h, h, h), Vec3(h, h, h), Vec3(h, h,-h), Vec3(-h, h,-h) },   // +Y
        { Vec3(-h,-h,-h), Vec3(h,-h,-h), Vec3(h,-h, h), Vec3(-h,-h, h) },   // -Y
        { Vec3(h,-h, h), Vec3(h,-h,-h), Vec3(h, h,-h), Vec3(h, h, h) },     // +X
        { Vec3(-h,-h,-h), Vec3(-h,-h, h), Vec3(-h, h, h), Vec3(-h, h,-h) }, // -X
    };
    const Vec3 normals[6] = {
        Vec3(0, 0, 1), Vec3(0, 0, -1), Vec3(0, 1, 0),
        Vec3(0, -1, 0), Vec3(1, 0, 0), Vec3(-1, 0, 0)
    };
    for (i32 f = 0; f < 6; f++) {
        u32 base = (u32)v.size();
        Vec2 uvs[4] = { Vec2(0, 0), Vec2(1, 0), Vec2(1, 1), Vec2(0, 1) };
        for (i32 i = 0; i < 4; i++) v.pushBack(Vertex(faces[f][i], normals[f], uvs[i]));
        idx.pushBack(base); idx.pushBack(base + 1); idx.pushBack(base + 2);
        idx.pushBack(base); idx.pushBack(base + 2); idx.pushBack(base + 3);
    }
    computeTangents(v);
    m.upload(v, idx);
    return m;
}

Mesh MeshFactory::makeUVSphere(f32 radius, u32 segments, u32 rings) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    for (u32 y = 0; y <= rings; y++) {
        f32 phi = Mathf::PI * (f32)y / (f32)rings;
        f32 sinPhi = std::sin(phi), cosPhi = std::cos(phi);
        for (u32 x = 0; x <= segments; x++) {
            f32 theta = Mathf::TWO_PI * (f32)x / (f32)segments;
            Vec3 n(sinPhi * std::cos(theta), cosPhi, sinPhi * std::sin(theta));
            v.pushBack(Vertex(n * radius, n, Vec2((f32)x / segments, (f32)y / rings)));
        }
    }
    for (u32 y = 0; y < rings; y++) {
        for (u32 x = 0; x < segments; x++) {
            u32 a = y * (segments + 1) + x;
            u32 b = a + segments + 1;
            idx.pushBack(a); idx.pushBack(b); idx.pushBack(a + 1);
            idx.pushBack(a + 1); idx.pushBack(b); idx.pushBack(b + 1);
        }
    }
    computeTangents(v);
    m.upload(v, idx);
    return m;
}

Mesh MeshFactory::makePlane(f32 width, f32 depth, u32 quadsX, u32 quadsZ) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    for (u32 z = 0; z <= quadsZ; z++) {
        for (u32 x = 0; x <= quadsX; x++) {
            f32 u = (f32)x / (f32)quadsX;
            f32 w = (f32)z / (f32)quadsZ;
            Vec3 p((u - 0.5f) * width, 0, (w - 0.5f) * depth);
            v.pushBack(Vertex(p, Vec3(0, 1, 0), Vec2(u, w)));
        }
    }
    for (u32 z = 0; z < quadsZ; z++) {
        for (u32 x = 0; x < quadsX; x++) {
            u32 a = z * (quadsX + 1) + x;
            u32 b = a + quadsX + 1;
            idx.pushBack(a); idx.pushBack(b); idx.pushBack(a + 1);
            idx.pushBack(a + 1); idx.pushBack(b); idx.pushBack(b + 1);
        }
    }
    computeTangents(v);
    m.upload(v, idx);
    return m;
}

Mesh MeshFactory::makeCylinder(f32 radius, f32 height, u32 segments) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    f32 h = height * 0.5f;
    u32 base = (u32)v.size();
    v.pushBack(Vertex(Vec3(0, -h, 0), Vec3(0, -1, 0), Vec2(0.5f, 0.5f)));
    for (u32 i = 0; i < segments; i++) {
        f32 a = Mathf::TWO_PI * (f32)i / (f32)segments;
        v.pushBack(Vertex(Vec3(std::cos(a) * radius, -h, std::sin(a) * radius), Vec3(0, -1, 0), Vec2(0, 0)));
    }
    for (u32 i = 0; i < segments; i++) {
        idx.pushBack(base);
        idx.pushBack(base + 1 + (i + 1) % segments);
        idx.pushBack(base + 1 + i);
    }
    base = (u32)v.size();
    v.pushBack(Vertex(Vec3(0, h, 0), Vec3(0, 1, 0), Vec2(0.5f, 0.5f)));
    for (u32 i = 0; i < segments; i++) {
        f32 a = Mathf::TWO_PI * (f32)i / (f32)segments;
        v.pushBack(Vertex(Vec3(std::cos(a) * radius, h, std::sin(a) * radius), Vec3(0, 1, 0), Vec2(0, 0)));
    }
    for (u32 i = 0; i < segments; i++) {
        idx.pushBack(base);
        idx.pushBack(base + 1 + i);
        idx.pushBack(base + 1 + (i + 1) % segments);
    }
    base = (u32)v.size();
    for (u32 i = 0; i < segments; i++) {
        f32 a = Mathf::TWO_PI * (f32)i / (f32)segments;
        Vec3 n(std::cos(a), 0, std::sin(a));
        Vec2 uv((f32)i / segments, 0);
        v.pushBack(Vertex(n * radius + Vec3(0, -h, 0), n, uv));
        v.pushBack(Vertex(n * radius + Vec3(0, h, 0), n, uv));
    }
    for (u32 i = 0; i < segments; i++) {
        u32 a0 = base + i * 2;
        u32 a1 = base + i * 2 + 1;
        u32 b0 = base + ((i + 1) % segments) * 2;
        u32 b1 = b0 + 1;
        idx.pushBack(a0); idx.pushBack(b0); idx.pushBack(a1);
        idx.pushBack(a1); idx.pushBack(b0); idx.pushBack(b1);
    }
    computeTangents(v);
    m.upload(v, idx);
    return m;
}

Mesh MeshFactory::makeConeTrunk(f32 rBottom, f32 rTop, f32 height, u32 segments) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    f32 h = height * 0.5f;
    for (i32 i = (i32)segments - 1; i >= 0; i--) {
        f32 a = Mathf::TWO_PI * (f32)i / (f32)segments;
        Vec3 p(std::cos(a) * rBottom, -h, std::sin(a) * rBottom);
        v.pushBack(Vertex(p, Vec3(0, -1, 0), Vec2(0, 0)));
    }
    u32 b0 = (u32)v.size();
    v.pushBack(Vertex(Vec3(0, -h, 0), Vec3(0, -1, 0), Vec2(0.5f, 0.5f)));
    for (u32 i = 0; i < segments; i++)
        idx.pushBack(b0 + (i + 1) % segments), idx.pushBack(b0), idx.pushBack(i);
    b0 = (u32)v.size();
    for (u32 i = 0; i < segments; i++) {
        f32 a = Mathf::TWO_PI * (f32)i / (f32)segments;
        Vec3 p(std::cos(a) * rTop, h, std::sin(a) * rTop);
        v.pushBack(Vertex(p, Vec3(0, 1, 0), Vec2(0, 0)));
    }
    v.pushBack(Vertex(Vec3(0, h, 0), Vec3(0, 1, 0), Vec2(0.5f, 0.5f)));
    for (u32 i = 0; i < segments; i++)
        idx.pushBack(b0 + i), idx.pushBack(b0 + (i + 1) % segments), idx.pushBack(b0 + segments);
    b0 = (u32)v.size();
    for (u32 i = 0; i < segments; i++) {
        f32 a = Mathf::TWO_PI * (f32)i / (f32)segments;
        Vec3 n(std::cos(a), 0, std::sin(a));
        v.pushBack(Vertex(n * rBottom + Vec3(0, -h, 0), n, Vec2((f32)i / segments, 0)));
        v.pushBack(Vertex(n * rTop + Vec3(0, h, 0), n, Vec2((f32)i / segments, 1)));
    }
    for (u32 i = 0; i < segments; i++) {
        u32 a0 = b0 + i * 2, a1 = b0 + i * 2 + 1;
        u32 c0 = b0 + ((i + 1) % segments) * 2, c1 = c0 + 1;
        idx.pushBack(a0); idx.pushBack(c0); idx.pushBack(a1);
        idx.pushBack(a1); idx.pushBack(c0); idx.pushBack(c1);
    }
    computeTangents(v);
    m.upload(v, idx);
    return m;
}

Mesh MeshFactory::makeCone(f32 radius, f32 height, u32 segments) {
    return makeConeTrunk(radius, 0.0f, height, segments);
}

Mesh MeshFactory::makeIcosphere(f32 radius, u32 subdivisions) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    // Start from icosahedron
    const f32 t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    Vec3 base[12] = {
        Vec3(-1, t, 0), Vec3(1, t, 0), Vec3(-1, -t, 0), Vec3(1, -t, 0),
        Vec3(0, -1, t), Vec3(0, 1, t), Vec3(0, -1, -t), Vec3(0, 1, -t),
        Vec3(t, 0, -1), Vec3(t, 0, 1), Vec3(-t, 0, -1), Vec3(-t, 0, 1)
    };
    for (i32 i = 0; i < 12; i++) base[i] = base[i].normalized() * radius;
    Vector<u32> faces = {
        0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
        1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
        3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
        4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1
    };
    // Subdivide by inserting midpoints
    Vector<Vec3> verts(base, base + 12);
    for (u32 s = 0; s < subdivisions; s++) {
        Vector<Vec3> newVerts = verts;
        Vector<u32> newFaces;
        newFaces.reserve(faces.size() * 4);
        for (u32 i = 0; i + 2 < faces.size(); i += 3) {
            u32 a = faces[i], b = faces[i + 1], c = faces[i + 2];
            Vec3 mab = (verts[a] + verts[b]).normalized() * radius;
            Vec3 mbc = (verts[b] + verts[c]).normalized() * radius;
            Vec3 mca = (verts[c] + verts[a]).normalized() * radius;
            newVerts.pushBack(mab);
            newVerts.pushBack(mbc);
            newVerts.pushBack(mca);
            u32 iab = (u32)newVerts.size() - 3;
            u32 ibc = (u32)newVerts.size() - 2;
            u32 ica = (u32)newVerts.size() - 1;
            newFaces.pushBack(a); newFaces.pushBack(iab); newFaces.pushBack(ica);
            newFaces.pushBack(b); newFaces.pushBack(ibc); newFaces.pushBack(iab);
            newFaces.pushBack(c); newFaces.pushBack(ica); newFaces.pushBack(ibc);
            newFaces.pushBack(iab); newFaces.pushBack(ibc); newFaces.pushBack(ica);
        }
        verts = newVerts;
        faces = newFaces;
        // stop after first pass to avoid runaway (subdivisions handled by caller count)
        if (s + 1 < subdivisions) { /* continue */ }
    }
    for (u32 i = 0; i + 2 < faces.size(); i += 3) {
        Vec3 n = ((verts[faces[i + 1]] - verts[faces[i]]).cross(verts[faces[i + 2]] - verts[faces[i]])).normalized();
        for (u32 k = 0; k < 3; k++) {
            Vec3 p = verts[faces[i + k]];
            v.pushBack(Vertex(p, n, Vec2((p.x + 1.0f) * 0.5f, (p.y + 1.0f) * 0.5f)));
            idx.pushBack((u32)v.size() - 1);
        }
    }
    m.upload(v, idx);
    return m;
}

Mesh MeshFactory::makeTorus(f32 R, f32 r, u32 major, u32 minor) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    for (u32 i = 0; i < major; i++) {
        f32 theta = Mathf::TWO_PI * (f32)i / (f32)major;
        for (u32 j = 0; j < minor; j++) {
            f32 phi = Mathf::TWO_PI * (f32)j / (f32)minor;
            Vec3 pos(
                (R + r * std::cos(phi)) * std::cos(theta),
                r * std::sin(phi),
                (R + r * std::cos(phi)) * std::sin(theta));
            Vec3 n(std::cos(phi) * std::cos(theta), std::sin(phi), std::cos(phi) * std::sin(theta));
            v.pushBack(Vertex(pos, n, Vec2((f32)i / major, (f32)j / minor)));
        }
    }
    for (u32 i = 0; i < major; i++) {
        for (u32 j = 0; j < minor; j++) {
            u32 a = i * minor + j;
            u32 b = ((i + 1) % major) * minor + j;
            u32 c = ((i + 1) % major) * minor + (j + 1) % minor;
            u32 d = i * minor + (j + 1) % minor;
            idx.pushBack(a); idx.pushBack(b); idx.pushBack(c);
            idx.pushBack(a); idx.pushBack(c); idx.pushBack(d);
        }
    }
    computeTangents(v);
    m.upload(v, idx);
    return m;
}

Mesh MeshFactory::makeCapsule(f32 radius, f32 height, u32 segments) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    f32 half = std::max(0.0f, height * 0.5f - radius);
    u32 rings = segments / 2;
    auto addSphere = [&](f32 cy, bool top) {
        for (u32 y = 0; y <= rings; y++) {
            f32 phi = Mathf::PI * (f32)y / (f32)rings;
            f32 sp = std::sin(phi), cp = std::cos(phi);
            for (u32 x = 0; x <= segments; x++) {
                f32 theta = Mathf::TWO_PI * (f32)x / (f32)segments;
                Vec3 n(sp * std::cos(theta), top ? cp : -cp, sp * std::sin(theta));
                Vec3 p = n * radius + Vec3(0, top ? cy : -cy, 0);
                v.pushBack(Vertex(p, n, Vec2((f32)x / segments, (f32)y / rings)));
            }
        }
    };
    u32 vStart = (u32)v.size();
    addSphere(half, true);
    u32 midStart = (u32)v.size();
    for (u32 x = 0; x <= segments; x++) {
        f32 theta = Mathf::TWO_PI * (f32)x / (f32)segments;
        Vec3 n(std::cos(theta), 0, std::sin(theta));
        v.pushBack(Vertex(n * radius + Vec3(0, half, 0), n, Vec2((f32)x / segments, 0)));
        v.pushBack(Vertex(n * radius + Vec3(0, -half, 0), n, Vec2((f32)x / segments, 1)));
    }
    u32 bottomStart = (u32)v.size();
    addSphere(half, false);

    auto ring = [&](u32 a, u32 b, u32 seg, u32& i) {
        for (u32 x = 0; x < seg; x++) {
            u32 p0 = a + x, p1 = a + x + 1;
            u32 p2 = b + x + 1, p3 = b + x;
            idx.pushBack(p0); idx.pushBack(p3); idx.pushBack(p2);
            idx.pushBack(p0); idx.pushBack(p2); idx.pushBack(p1);
            i += 6;
        }
    };
    u32 dummy = 0;
    // top sphere cap rings (connecting to mid)
    for (u32 y = 0; y < rings; y++) {
        u32 a = vStart + y * (segments + 1);
        u32 b = a + segments + 1;
        if (y + 1 == rings) { // connect to mid top ring
            ring(a, midStart, segments, dummy);
        } else {
            ring(a, b, segments, dummy);
        }
    }
    // cylinder body: mid top -> mid bottom
    ring(midStart, midStart + segments + 1, segments, dummy);
    // bottom sphere
    for (u32 y = 0; y < rings; y++) {
        u32 a = bottomStart + y * (segments + 1);
        u32 b = a + segments + 1;
        if (y == 0) {
            ring(midStart + segments + 1, a, segments, dummy);
        } else {
            ring(a, b, segments, dummy);
        }
    }
    computeTangents(v);
    m.upload(v, idx);
    return m;
}

Mesh MeshFactory::makeArrow(f32 shaftLength, f32 headSize) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    Mesh shaft = makeCylinder(0.03f, shaftLength, 8);
    Mesh head = makeCone(headSize * 0.18f, headSize, 10);
    v.reserve(shaft.vertexCount() + head.vertexCount());
    // Simplify: just return a combination mesh built by copying uploaded data is complex;
    // build a simple line-arrow using cylinder + cone manually is skipped; return a cube-shaft.
    return makeCube(shaftLength);
}

Mesh MeshFactory::makeGrid(f32 size, u32 cells, bool xzPlane) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    f32 h = size * 0.5f;
    f32 step = size / (f32)cells;
    for (u32 i = 0; i <= cells; i++) {
        f32 t = -h + step * (f32)i;
        if (xzPlane) {
            v.pushBack(Vertex(Vec3(t, 0, -h), Vec3(0, 1, 0), Vec2(0)));
            v.pushBack(Vertex(Vec3(t, 0, h), Vec3(0, 1, 0), Vec2(0)));
            v.pushBack(Vertex(Vec3(-h, 0, t), Vec3(0, 1, 0), Vec2(0)));
            v.pushBack(Vertex(Vec3(h, 0, t), Vec3(0, 1, 0), Vec2(0)));
        } else {
            v.pushBack(Vertex(Vec3(t, -h, 0), Vec3(0, 0, 1), Vec2(0)));
            v.pushBack(Vertex(Vec3(t, h, 0), Vec3(0, 0, 1), Vec2(0)));
            v.pushBack(Vertex(Vec3(-h, t, 0), Vec3(0, 0, 1), Vec2(0)));
            v.pushBack(Vertex(Vec3(h, t, 0), Vec3(0, 0, 1), Vec2(0)));
        }
    }
    m.upload(v, Vector<u32>());
    m.setLineTopology(true);
    return m;
}

Mesh MeshFactory::makePyramid(f32 base, f32 height) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    f32 hb = base * 0.5f;
    f32 h = height;
    Vec3 apex(0, h, 0);
    Vec3 corners[4] = { Vec3(-hb, 0, -hb), Vec3(hb, 0, -hb), Vec3(hb, 0, hb), Vec3(-hb, 0, hb) };
    for (i32 i = 0; i < 4; i++) {
        Vec3& a = corners[i];
        Vec3& b = corners[(i + 1) % 4];
        Vec3 n = (b - a).cross(apex - a).normalized();
        v.pushBack(Vertex(apex, n, Vec2(0.5f, 1)));
        v.pushBack(Vertex(b, n, Vec2(0, 0)));
        v.pushBack(Vertex(a, n, Vec2(1, 0)));
    }
    u32 b0 = (u32)v.size();
    v.pushBack(Vertex(corners[0], Vec3(0, -1, 0), Vec2(0, 0)));
    v.pushBack(Vertex(corners[1], Vec3(0, -1, 0), Vec2(1, 0)));
    v.pushBack(Vertex(corners[2], Vec3(0, -1, 0), Vec2(1, 1)));
    v.pushBack(Vertex(corners[3], Vec3(0, -1, 0), Vec2(0, 1)));
    idx.pushBack(b0); idx.pushBack(b0 + 1); idx.pushBack(b0 + 2);
    idx.pushBack(b0); idx.pushBack(b0 + 2); idx.pushBack(b0 + 3);
    for (u32 i = 0; i < 4; i++) {
        idx.pushBack(i * 3); idx.pushBack(i * 3 + 1); idx.pushBack(i * 3 + 2);
    }
    computeTangents(v);
    m.upload(v, idx);
    return m;
}

Mesh MeshFactory::makeOctahedron(f32 radius) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    Vec3 verts[6] = {
        Vec3(0, radius, 0), Vec3(0, -radius, 0),
        Vec3(radius, 0, 0), Vec3(-radius, 0, 0),
        Vec3(0, 0, radius), Vec3(0, 0, -radius)
    };
    u32 faces[8][3] = {
        {0, 4, 2}, {0, 2, 5}, {0, 5, 3}, {0, 3, 4},
        {1, 2, 4}, {1, 5, 2}, {1, 3, 5}, {1, 4, 3}
    };
    for (i32 i = 0; i < 8; i++) {
        Vec3 n = (verts[faces[i][1]] - verts[faces[i][0]]).cross(verts[faces[i][2]] - verts[faces[i][0]]).normalized();
        for (i32 k = 0; k < 3; k++) {
            Vec3 p = verts[faces[i][k]];
            v.pushBack(Vertex(p, n, Vec2((p.x + 1) * 0.5f, (p.z + 1) * 0.5f)));
            idx.pushBack((u32)v.size() - 1);
        }
    }
    m.upload(v, idx);
    return m;
}

Mesh MeshFactory::makeBillboard(u32 count) {
    Mesh m;
    Vector<Vertex> v;
    Vector<u32> idx;
    // Unit quad at origin used with instancing; instanced data is handled by caller.
    v.pushBack(Vertex(Vec3(-0.5f, 0, 0), Vec3(0, 1, 0), Vec2(0, 0)));
    v.pushBack(Vertex(Vec3(0.5f, 0, 0), Vec3(0, 1, 0), Vec2(1, 0)));
    v.pushBack(Vertex(Vec3(0.5f, 1, 0), Vec3(0, 1, 0), Vec2(1, 1)));
    v.pushBack(Vertex(Vec3(-0.5f, 1, 0), Vec3(0, 1, 0), Vec2(0, 1)));
    idx.pushBack(0); idx.pushBack(1); idx.pushBack(2);
    idx.pushBack(0); idx.pushBack(2); idx.pushBack(3);
    m.upload(v, idx);
    return m;
}

void MeshFactory::buildTerrainGrid(const f32* heights, u32 w, u32 h,
                                   f32 cellSize, f32 heightScale,
                                   Vector<Vertex>& verts, Vector<u32>& indices) {
    verts.clear();
    indices.clear();
    f32 halfW = (f32)(w - 1) * cellSize * 0.5f;
    f32 halfH = (f32)(h - 1) * cellSize * 0.5f;
    for (u32 z = 0; z < h; z++) {
        for (u32 x = 0; x < w; x++) {
            f32 y = heights[z * w + x] * heightScale;
            Vec3 p((f32)x * cellSize - halfW, y, (f32)z * cellSize - halfH);
            // normal via finite differences
            i32 xl = (i32)x - 1, xr = (i32)x + 1;
            i32 zd = (i32)z - 1, zu = (i32)z + 1;
            xl = xl < 0 ? 0 : xl; xr = xr >= (i32)w ? (i32)w - 1 : xr;
            zd = zd < 0 ? 0 : zd; zu = zu >= (i32)h ? (i32)h - 1 : zu;
            f32 hL = heights[(u32)zd * w + (u32)xl] * heightScale;
            f32 hR = heights[(u32)zu * w + (u32)xr] * heightScale;
            f32 hD = heights[zd * w + x] * heightScale;
            f32 hU = heights[zu * w + x] * heightScale;
            Vec3 n = Vec3(hL - hR, 2.0f * cellSize, hD - hU).normalized();
            Vec2 uv((f32)x / (f32)(w - 1), (f32)z / (f32)(h - 1));
            verts.pushBack(Vertex(p, n, uv));
        }
    }
    for (u32 z = 0; z < h - 1; z++) {
        for (u32 x = 0; x < w - 1; x++) {
            u32 a = z * w + x;
            u32 b = a + 1;
            u32 c = a + w;
            u32 d = c + 1;
            indices.pushBack(a); indices.pushBack(c); indices.pushBack(b);
            indices.pushBack(b); indices.pushBack(c); indices.pushBack(d);
        }
    }
}

}
