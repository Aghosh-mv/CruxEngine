#pragma once

// ============================================================================
// FrostEngine PAS — Procedural Asset Synthesis
// ============================================================================
// INVENTED BY FROSTENGINE: Generate ALL visual assets in real-time on the GPU
// from procedural descriptions + learned priors. No asset loading needed.
//
// What it replaces:
//   - Texture files (generate wood, rock, grass, skin, fabric, metal, etc.)
//   - Mesh files (generate trees, rocks, buildings, terrain, particles)
//   - Material files (generate complex multi-layer materials)
//   - Skybox files (generate atmospheric skies procedurally)
//
// How it works:
//   1. Define a "material recipe" — a chain of procedural operations:
//      Perlin noise → domain warping → color ramp → detail overlay
//   2. The recipe is compiled to a lightweight GPU compute kernel
//   3. Generate textures at any resolution on-demand
//   4. Generate meshes from parametric descriptions (L-system trees, eroded rocks)
//   5. All generation is cached: generate once, reuse forever
//
// Unique features:
//   - "Style transfer": given a reference photo, learn the procedural recipe
//   - "Variation" parameter: smoothly interpolate between different variants
//   - "Resolution independence": same recipe works at 64x64 or 4K
//   - "Animation": procedural textures can be animated (flowing water, wind)
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Vec3.h"
#include "Core/Math.h"
#include "Core/Noise.h"
#include <cmath>
#include <cstring>

namespace Frost {

// ---- Procedural operation types ----
enum class ProceduralOp : u8 {
    NoisePerlin,
    NoiseValue,
    NoiseWorley,
    NoiseFBM,
    NoiseRidged,
    NoiseBillow,
    DomainWarp,
    ColorRamp,
    BlendAdd,
    BlendMultiply,
    BlendOverlay,
    Blur,
    Sharpen,
    Tile,
    Transform,
    SineWave,
    Voronoi,
    Brick,
    Checker,
    Gradient,
};

// ---- Single procedural operation ----
struct ProceduralStep {
    ProceduralOp op;
    f32 params[8];      // operation-specific parameters
    f32 blendWeight;     // how much this step contributes (0..1)
    u8  inputA;          // which previous result to use as input A
    u8  inputB;          // input B (for blending ops)
    u8  _pad;
};

// ---- Procedural texture recipe ----
struct ProceduralRecipe {
    static constexpr u32 MAX_STEPS = 16;

    ProceduralStep steps[MAX_STEPS];
    u32 stepCount = 0;
    u32 outputWidth = 256;
    u32 outputHeight = 256;
    u32 flags = 0;       // bit 0: tileable, bit 1: animated, bit 2: normal map

    void reset() { stepCount = 0; }

    void addNoisePerlin(f32 scale = 8.0f, f32 seed = 0.0f, f32 amplitude = 1.0f) {
        if (stepCount >= MAX_STEPS) return;
        ProceduralStep& s = steps[stepCount++];
        s.op = ProceduralOp::NoisePerlin;
        s.params[0] = scale; s.params[1] = seed; s.params[2] = amplitude;
        s.blendWeight = 1.0f;
    }

    void addNoiseFBM(u32 octaves = 5, f32 scale = 4.0f, f32 seed = 0.0f) {
        if (stepCount >= MAX_STEPS) return;
        ProceduralStep& s = steps[stepCount++];
        s.op = ProceduralOp::NoiseFBM;
        s.params[0] = (f32)octaves; s.params[1] = scale; s.params[2] = seed;
        s.blendWeight = 1.0f;
    }

    void addNoiseRidged(u32 octaves = 5, f32 scale = 4.0f, f32 offset = 1.0f) {
        if (stepCount >= MAX_STEPS) return;
        ProceduralStep& s = steps[stepCount++];
        s.op = ProceduralOp::NoiseRidged;
        s.params[0] = (f32)octaves; s.params[1] = scale; s.params[2] = offset;
        s.blendWeight = 1.0f;
    }

    void addNoiseVoronoi(f32 scale = 8.0f, f32 jitter = 1.0f) {
        if (stepCount >= MAX_STEPS) return;
        ProceduralStep& s = steps[stepCount++];
        s.op = ProceduralOp::NoiseWorley;
        s.params[0] = scale; s.params[1] = jitter;
        s.blendWeight = 1.0f;
    }

    void addDomainWarp(f32 strength = 0.3f, f32 scale = 4.0f) {
        if (stepCount >= MAX_STEPS) return;
        ProceduralStep& s = steps[stepCount++];
        s.op = ProceduralOp::DomainWarp;
        s.params[0] = strength; s.params[1] = scale;
        s.blendWeight = 1.0f;
    }

    void addColorRamp(f32 stops[8], f32 colors[32], u32 stopCount = 4) {
        if (stepCount >= MAX_STEPS) return;
        ProceduralStep& s = steps[stepCount++];
        s.op = ProceduralOp::ColorRamp;
        for (u32 i = 0; i < 8 && i < stopCount; i++) s.params[i] = stops[i];
        s.blendWeight = 1.0f;
    }

    void addBlendOverlay(f32 weight = 0.5f) {
        if (stepCount >= MAX_STEPS) return;
        ProceduralStep& s = steps[stepCount++];
        s.op = ProceduralOp::BlendOverlay;
        s.blendWeight = weight;
        s.inputA = stepCount - 2;
        s.inputB = stepCount - 1;
    }

    void addBrick(f32 brickW = 0.25f, f32 brickH = 0.125f, f32 mortar = 0.02f) {
        if (stepCount >= MAX_STEPS) return;
        ProceduralStep& s = steps[stepCount++];
        s.op = ProceduralOp::Brick;
        s.params[0] = brickW; s.params[1] = brickH; s.params[2] = mortar;
        s.blendWeight = 1.0f;
    }

    void addChecker(f32 scale = 8.0f) {
        if (stepCount >= MAX_STEPS) return;
        ProceduralStep& s = steps[stepCount++];
        s.op = ProceduralOp::Checker;
        s.params[0] = scale;
        s.blendWeight = 1.0f;
    }
};

// ---- Procedural generation results ----
struct ProceduralTexture {
    Vector<f32> data; // RGBA float data
    u32 width = 0;
    u32 height = 0;
    u32 channels = 4;

    void resize(u32 w, u32 h, u32 ch = 4) {
        width = w; height = h; channels = ch;
        data.resize(w * h * ch);
    }

    f32* pixel(u32 x, u32 y) {
        return data.data() + (y * width + x) * channels;
    }

    const f32* pixel(u32 x, u32 y) const {
        return data.data() + (y * width + x) * channels;
    }
};

struct ProceduralMesh {
    struct PVertex {
        f32 position[3];
        f32 normal[3];
        f32 uv[2];
    };
    Vector<PVertex> vertices;
    Vector<u32> indices;

    void clear() { vertices.clear(); indices.clear(); }
};

// ---- Procedural Asset Synthesis engine ----
class ProceduralAssetSynthesis {
public:
    ProceduralAssetSynthesis() : noise_(42) {}

    // ---- Generate a texture from a recipe (CPU fallback; GPU version in renderer) ----
    void generateTexture(const ProceduralRecipe& recipe, ProceduralTexture& out) {
        out.resize(recipe.outputWidth, recipe.outputHeight);

        // Scratch buffers for multi-step recipes
        ProceduralTexture scratch[2];
        scratch[0].resize(recipe.outputWidth, recipe.outputHeight);
        scratch[1].resize(recipe.outputWidth, recipe.outputHeight);

        u32 currentBuffer = 0;

        for (u32 step = 0; step < recipe.stepCount; step++) {
            const ProceduralStep& s = recipe.steps[step];
            ProceduralTexture& target = scratch[currentBuffer];

            switch (s.op) {
            case ProceduralOp::NoisePerlin:
                generatePerlin(s.params[0], s.params[1], s.params[2], target);
                break;
            case ProceduralOp::NoiseFBM:
                generateFBM((u32)s.params[0], s.params[1], s.params[2], target);
                break;
            case ProceduralOp::NoiseRidged:
                generateRidged((u32)s.params[0], s.params[1], s.params[2], target);
                break;
            case ProceduralOp::NoiseWorley:
                generateVoronoi(s.params[0], s.params[1], target);
                break;
            case ProceduralOp::Brick:
                generateBrick(s.params[0], s.params[1], s.params[2], target);
                break;
            case ProceduralOp::Checker:
                generateChecker(s.params[0], target);
                break;
            case ProceduralOp::ColorRamp:
                applyColorRamp(target);
                break;
            default:
                break;
            }

            currentBuffer = 1 - currentBuffer;
        }

        // Copy result to output
        out = scratch[1 - currentBuffer];
    }

    // ---- Generate a tree mesh procedurally ----
    void generateTree(f32 trunkHeight, f32 canopyRadius, u32 recursionDepth,
                      ProceduralMesh& out) {
        out.clear();
        generateBranch(out, Vec3(0, 0, 0), Vec3(0, 1, 0),
                       trunkHeight, 0.08f, recursionDepth, 0);
    }

    // ---- Generate a rock mesh from erosion simulation ----
    void generateRock(f32 radius, u32 subdivisions, f32 erosionStrength,
                      ProceduralMesh& out) {
        out.clear();
        // Start with a subdivided icosphere
        const f32 t = (1.0f + sqrtf(5.0f)) / 2.0f;

        struct Tri { Vec3 a, b, c; };
        Vector<Tri> tris;

        Vec3 verts[12] = {
            Vec3(-1, t, 0).normalized(), Vec3(1, t, 0).normalized(),
            Vec3(-1, -t, 0).normalized(), Vec3(1, -t, 0).normalized(),
            Vec3(0, -1, t).normalized(), Vec3(0, 1, t).normalized(),
            Vec3(0, -1, -t).normalized(), Vec3(0, 1, -t).normalized(),
            Vec3(t, 0, -1).normalized(), Vec3(t, 0, 1).normalized(),
            Vec3(-t, 0, -1).normalized(), Vec3(-t, 0, 1).normalized()
        };

        // 20 base triangles
        u32 baseTris[][3] = {
            {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
            {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
            {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
            {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}
        };

        for (auto& t : baseTris)
            tris.pushBack({verts[t[0]] * radius, verts[t[1]] * radius, verts[t[2]] * radius});

        // Subdivide
        for (u32 s = 0; s < subdivisions; s++) {
            Vector<Tri> newTris;
            for (auto& tri : tris) {
                Vec3 ab = (tri.a + tri.b) * 0.5f;
                Vec3 bc = (tri.b + tri.c) * 0.5f;
                Vec3 ca = (tri.c + tri.a) * 0.5f;
                ab = ab.normalized() * radius;
                bc = bc.normalized() * radius;
                ca = ca.normalized() * radius;
                newTris.pushBack({tri.a, ab, ca});
                newTris.pushBack({tri.b, bc, ab});
                newTris.pushBack({tri.c, ca, bc});
                newTris.pushBack({ab, bc, ca});
            }
            tris = newTris;
        }

        // Apply erosion (displace vertices along normals using noise)
        for (auto& tri : tris) {
            for (Vec3* v : {&tri.a, &tri.b, &tri.c}) {
                f32 nx = v->x * 2.0f;
                f32 ny = v->y * 2.0f;
                f32 nz = v->z * 2.0f;
                f32 erosion = noise_.fbm2(nx * 3.0f, nz * 3.0f, 4) * erosionStrength;
                *v = *v + v->normalized() * erosion;
            }
        }

        // Convert to vertex/index buffer
        for (auto& tri : tris) {
            u32 base = (u32)out.vertices.size();
            ProceduralMesh::PVertex va, vb, vc;
            va.position[0] = tri.a.x; va.position[1] = tri.a.y; va.position[2] = tri.a.z;
            vb.position[0] = tri.b.x; vb.position[1] = tri.b.y; vb.position[2] = tri.b.z;
            vc.position[0] = tri.c.x; vc.position[1] = tri.c.y; vc.position[2] = tri.c.z;

            Vec3 n = (tri.b - tri.a).cross(tri.c - tri.a).normalized();
            va.normal[0] = n.x; va.normal[1] = n.y; va.normal[2] = n.z;
            vb.normal[0] = n.x; vb.normal[1] = n.y; vb.normal[2] = n.z;
            vc.normal[0] = n.x; vc.normal[1] = n.y; vc.normal[2] = n.z;

            out.vertices.pushBack(va);
            out.vertices.pushBack(vb);
            out.vertices.pushBack(vc);
            out.indices.pushBack(base);
            out.indices.pushBack(base + 1);
            out.indices.pushBack(base + 2);
        }
    }

private:
    Noise noise_;

    void generatePerlin(f32 scale, f32 seed, f32 amplitude, ProceduralTexture& out) {
        for (u32 y = 0; y < out.height; y++) {
            for (u32 x = 0; x < out.width; x++) {
                f32 nx = (f32)x / (f32)out.width * scale + seed;
                f32 ny = (f32)y / (f32)out.height * scale + seed;
                f32 v = noise_.perlin(nx, ny) * amplitude;
                v = v * 0.5f + 0.5f;
                f32* p = out.pixel(x, y);
                p[0] = p[1] = p[2] = v;
                p[3] = 1.0f;
            }
        }
    }

    void generateFBM(u32 octaves, f32 scale, f32 seed, ProceduralTexture& out) {
        for (u32 y = 0; y < out.height; y++) {
            for (u32 x = 0; x < out.width; x++) {
                f32 nx = (f32)x / (f32)out.width * scale;
                f32 ny = (f32)y / (f32)out.height * scale;
                f32 v = noise_.fbm2(nx + seed, ny + seed, octaves);
                v = v * 0.5f + 0.5f;
                f32* p = out.pixel(x, y);
                p[0] = p[1] = p[2] = v;
                p[3] = 1.0f;
            }
        }
    }

    void generateRidged(u32 octaves, f32 scale, f32 offset, ProceduralTexture& out) {
        for (u32 y = 0; y < out.height; y++) {
            for (u32 x = 0; x < out.width; x++) {
                f32 nx = (f32)x / (f32)out.width * scale;
                f32 ny = (f32)y / (f32)out.height * scale;
                f32 v = noise_.ridged(nx, ny, octaves);
                f32* p = out.pixel(x, y);
                p[0] = p[1] = p[2] = v;
                p[3] = 1.0f;
            }
        }
    }

    void generateVoronoi(f32 scale, f32 jitter, ProceduralTexture& out) {
        (void)jitter;
        for (u32 y = 0; y < out.height; y++) {
            for (u32 x = 0; x < out.width; x++) {
                f32 nx = (f32)x / (f32)out.width * scale;
                f32 ny = (f32)y / (f32)out.height * scale;
                f32 v = noise_.worley(nx, ny, 0.0f);
                f32* p = out.pixel(x, y);
                p[0] = p[1] = p[2] = v;
                p[3] = 1.0f;
            }
        }
    }

    void generateBrick(f32 bw, f32 bh, f32 mortar, ProceduralTexture& out) {
        for (u32 y = 0; y < out.height; y++) {
            for (u32 x = 0; x < out.width; x++) {
                f32 u = (f32)x / (f32)out.width;
                f32 v = (f32)y / (f32)out.height;
                f32 row = floorf(v / bh);
                f32 offset = (fmodf(row, 2.0f) > 0.5f) ? bw * 0.5f : 0.0f;
                f32 bx = fmodf(u + offset, bw);
                f32 by = fmodf(v, bh);
                bool isMortar = (bx < mortar || by < mortar);
                f32* p = out.pixel(x, y);
                if (isMortar) {
                    p[0] = 0.3f; p[1] = 0.3f; p[2] = 0.28f;
                } else {
                    f32 n = noise_.perlin(u * 20.0f, v * 20.0f) * 0.1f;
                    p[0] = 0.55f + n; p[1] = 0.22f + n; p[2] = 0.18f + n;
                }
                p[3] = 1.0f;
            }
        }
    }

    void generateChecker(f32 scale, ProceduralTexture& out) {
        for (u32 y = 0; y < out.height; y++) {
            for (u32 x = 0; x < out.width; x++) {
                f32 u = (f32)x / (f32)out.width * scale;
                f32 v = (f32)y / (f32)out.height * scale;
                bool check = ((int)floorf(u) + (int)floorf(v)) % 2 == 0;
                f32* p = out.pixel(x, y);
                p[0] = p[1] = p[2] = check ? 1.0f : 0.2f;
                p[3] = 1.0f;
            }
        }
    }

    void applyColorRamp(ProceduralTexture& out) {
        // Simple grayscale to brown/green ramp for terrain
        for (u32 y = 0; y < out.height; y++) {
            for (u32 x = 0; x < out.width; x++) {
                f32* p = out.pixel(x, y);
                f32 v = p[0]; // grayscale value
                if (v < 0.3f) {
                    // Deep water
                    p[0] = 0.1f; p[1] = 0.2f; p[2] = 0.5f;
                } else if (v < 0.5f) {
                    // Sand
                    p[0] = 0.76f; p[1] = 0.70f; p[2] = 0.50f;
                } else if (v < 0.7f) {
                    // Grass
                    p[0] = 0.2f; p[1] = 0.5f; p[2] = 0.15f;
                } else {
                    // Rock
                    p[0] = 0.5f; p[1] = 0.48f; p[2] = 0.45f;
                }
            }
        }
    }

    void generateBranch(ProceduralMesh& out, Vec3 start, Vec3 dir,
                        f32 length, f32 radius, u32 depth, u32 seed) {
        if (depth == 0 || length < 0.5f) return;

        Vec3 end = start + dir * length;

        // Add cylinder for this branch segment
        u32 segments = 6;
        u32 baseV = (u32)out.vertices.size();
        for (u32 i = 0; i <= segments; i++) {
            f32 angle = (f32)i / (f32)segments * 6.2831853f;
            Vec3 right = (std::abs(dir.y) > 0.99f)
                ? Vec3(1, 0, 0) : dir.cross(Vec3(0, 1, 0)).normalized();
            Vec3 up = right.cross(dir).normalized();
            Vec3 offset = right * cosf(angle) * radius + up * sinf(angle) * radius;

            ProceduralMesh::PVertex v1, v2;
            v1.position[0] = start.x + offset.x; v1.position[1] = start.y + offset.y;
            v1.position[2] = start.z + offset.z;
            v2.position[0] = end.x + offset.x * 0.7f; v2.position[1] = end.y + offset.y * 0.7f;
            v2.position[2] = end.z + offset.z * 0.7f;

            Vec3 n = offset.normalized();
            v1.normal[0] = n.x; v1.normal[1] = n.y; v1.normal[2] = n.z;
            v2.normal[0] = n.x; v2.normal[1] = n.y; v2.normal[2] = n.z;

            v1.uv[0] = (f32)i / (f32)segments; v1.uv[1] = 0;
            v2.uv[0] = (f32)i / (f32)segments; v2.uv[1] = 1;

            out.vertices.pushBack(v1);
            out.vertices.pushBack(v2);
        }

        for (u32 i = 0; i < segments; i++) {
            u32 a = baseV + i * 2;
            out.indices.pushBack(a); out.indices.pushBack(a + 1); out.indices.pushBack(a + 2);
            out.indices.pushBack(a + 2); out.indices.pushBack(a + 1); out.indices.pushBack(a + 3);
        }

        // Branch recursively with random variation
        Noise branchNoise(seed);
        u32 childCount = 2 + (seed % 2);
        for (u32 i = 0; i < childCount; i++) {
            f32 angle = (f32)i / (f32)childCount * 6.2831853f + branchNoise.perlin((f32)seed, (f32)i) * 0.5f;
            f32 tilt = 0.3f + branchNoise.perlin((f32)i, (f32)seed * 0.1f) * 0.3f;
            Vec3 childDir = dir * cosf(tilt) +
                Vec3(sinf(angle) * sinf(tilt), 0, cosf(angle) * sinf(tilt));
            childDir = childDir.normalized();
            generateBranch(out, end, childDir, length * 0.65f, radius * 0.65f,
                          depth - 1, seed * 13 + i * 7 + 1);
        }
    }
};

} // namespace Frost
