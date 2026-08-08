#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Renderer/Mesh.h"

namespace Crux {

// Builds primitive geometry procedurally. All meshes are unit-scaled unless
// noted; scale with entity transforms.
class MeshFactory {
public:
    static Mesh makeCube(f32 size = 1.0f);
    static Mesh makeUVSphere(f32 radius = 0.5f, u32 segments = 24, u32 rings = 16);
    static Mesh makeIcosphere(f32 radius = 0.5f, u32 subdivisions = 2);
    static Mesh makePlane(f32 width = 1.0f, f32 depth = 1.0f, u32 quadsX = 1, u32 quadsZ = 1);
    static Mesh makeCylinder(f32 radius = 0.5f, f32 height = 1.0f, u32 segments = 24);
    static Mesh makeCone(f32 radius = 0.5f, f32 height = 1.0f, u32 segments = 24);
    static Mesh makeTorus(f32 majorRadius = 0.5f, f32 minorRadius = 0.2f, u32 majorSegs = 32, u32 minorSegs = 16);
    static Mesh makeQuad(f32 size = 1.0f);
    static Mesh makeFullscreenTriangle();
    static Mesh makeIcosahedron();
    static Mesh makeCapsule(f32 radius = 0.5f, f32 height = 1.0f, u32 segments = 16);
    static Mesh makeArrow(f32 shaftLength = 1.0f, f32 headSize = 0.2f);
    static Mesh makeGrid(f32 size = 10.0f, u32 cells = 20, bool xzPlane = true);
    static Mesh makePyramid(f32 base = 1.0f, f32 height = 1.0f);
    static Mesh makeOctahedron(f32 radius = 0.5f);
    static Mesh makeConeTrunk(f32 radiusBottom = 0.5f, f32 radiusTop = 0.3f, f32 height = 1.0f, u32 segments = 24);
    static Mesh makeBillboard(u32 count = 1);

    // Merged terrain strip from a height grid; returns verts/indices for upload.
    static void buildTerrainGrid(const f32* heights, u32 w, u32 h,
                                 f32 cellSize, f32 heightScale,
                                 Vector<Vertex>& verts, Vector<u32>& indices);

private:
    static void computeTangents(Vector<Vertex>& verts);
};

}
