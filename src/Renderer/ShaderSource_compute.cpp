#include "Renderer/ShaderSource_compute.h"

#include <chrono>
#include <algorithm>
#include <utility>

namespace Frost {
namespace ShaderSource {

// ============================================================================
// SVOR: Inject direct sunlight into voxel leaf nodes
// ============================================================================
const char* svorInjectComp = R"(#version 460 core
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

struct Voxel {
    float r, g, b;
    float nx, ny, nz;
    float albedoR, albedoG, albedoB;
    uint emission;
    uint filled;
    uint _pad[2];
};

layout(std430, binding = 0) buffer VoxelBuffer {
    Voxel voxels[];
};

uniform vec3 u_sunDir;
uniform vec3 u_sunColor;
uniform float u_sunIntensity;
uniform uint u_voxelCount;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_voxelCount) return;

    Voxel v = voxels[idx];
    if (v.filled == 0u) return;

    float nDotL = max(dot(vec3(v.nx, v.ny, v.nz), normalize(-u_sunDir)), 0.0);

    v.r = v.albedoR * u_sunColor.r * u_sunIntensity * nDotL;
    v.g = v.albedoG * u_sunColor.g * u_sunIntensity * nDotL;
    v.b = v.albedoB * u_sunColor.b * u_sunIntensity * nDotL;

    if (v.emission == 1u) {
        v.r += v.albedoR * 3.0;
        v.g += v.albedoG * 3.0;
        v.b += v.albedoB * 3.0;
    }

    voxels[idx] = v;
}
)";

// ============================================================================
// SVOR: Propagate radiance UP the octree
// ============================================================================
const char* svorPropagateUpComp = R"(#version 460 core
layout(local_size_x = 256) in;

struct SVOChunk {
    uint children[8];
    uint leafData;
    uint childMask;
    uint level;
    uint _pad;
};

layout(std430, binding = 0) buffer NodeBuffer {
    SVOChunk nodes[];
};

struct Voxel {
    float r, g, b;
    float nx, ny, nz;
    float albedoR, albedoG, albedoB;
    uint emission;
    uint filled;
    uint _pad[2];
};

layout(std430, binding = 1) buffer VoxelBuffer {
    Voxel voxels[];
};

uniform uint u_nodeCount;

void main() {
    uint idx = gl_GlobalInvocationID.x + 1u;
    if (idx >= u_nodeCount) return;

    SVOChunk node = nodes[idx];
    if (node.childMask == 0u) return;

    float totalR = 0.0, totalG = 0.0, totalB = 0.0;
    uint childCount = 0u;

    for (uint c = 0u; c < 8u; c++) {
        if ((node.childMask & (1u << c)) != 0u && node.children[c] > 0u) {
            SVOChunk child = nodes[node.children[c]];
            if (child.leafData > 0u && child.leafData < voxels.length()) {
                Voxel cv = voxels[child.leafData];
                totalR += cv.r;
                totalG += cv.g;
                totalB += cv.b;
                childCount++;
            }
        }
    }

    if (childCount > 0u && node.leafData > 0u && node.leafData < voxels.length()) {
        float w = 1.0 / float(childCount);
        Voxel v = voxels[node.leafData];
        v.r = v.r * 0.5 + totalR * w * 0.5;
        v.g = v.g * 0.5 + totalG * w * 0.5;
        v.b = v.b * 0.5 + totalB * w * 0.5;
        voxels[node.leafData] = v;
    }
}
)";

// ============================================================================
// SVOR: Propagate radiance DOWN (ambient term from parent to children)
// ============================================================================
const char* svorPropagateDownComp = R"(#version 460 core
layout(local_size_x = 256) in;

struct SVOChunk {
    uint children[8];
    uint leafData;
    uint childMask;
    uint level;
    uint _pad;
};

layout(std430, binding = 0) buffer NodeBuffer {
    SVOChunk nodes[];
};

struct Voxel {
    float r, g, b;
    float nx, ny, nz;
    float albedoR, albedoG, albedoB;
    uint emission;
    uint filled;
    uint _pad[2];
};

layout(std430, binding = 1) buffer VoxelBuffer {
    Voxel voxels[];
};

uniform uint u_rootNode;
uniform float u_parentWeight;

void main() {
    uint stack[16];
    uint stackDepth = 0u;
    stack[stackDepth++] = u_rootNode;

    while (stackDepth > 0u) {
        uint nodeIdx = stack[--stackDepth];
        SVOChunk node = nodes[nodeIdx];
        if (node.childMask == 0u) continue;

        float parentR = 0.0, parentG = 0.0, parentB = 0.0;
        if (node.leafData > 0u && node.leafData < voxels.length()) {
            Voxel pv = voxels[node.leafData];
            parentR = pv.r; parentG = pv.g; parentB = pv.b;
        }

        float w = u_parentWeight;
        for (uint c = 0u; c < 8u; c++) {
            if ((node.childMask & (1u << c)) != 0u && node.children[c] > 0u) {
                SVOChunk child = nodes[node.children[c]];
                if (child.leafData > 0u && child.leafData < voxels.length()) {
                    Voxel cv = voxels[child.leafData];
                    cv.r = cv.r * (1.0 - w) + parentR * w;
                    cv.g = cv.g * (1.0 - w) + parentG * w;
                    cv.b = cv.b * (1.0 - w) + parentB * w;
                    voxels[child.leafData] = cv;
                }
                if (stackDepth < 16u) stack[stackDepth++] = node.children[c];
            }
        }
    }
}
)";

// ============================================================================
// TCSM: Temporal reprojection of shadow maps
// ============================================================================
const char* tcsmReprojectComp = R"(#version 460 core
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D u_prevDepth;
layout(binding = 1) uniform sampler2D u_curDepth;
layout(r32f, binding = 2) writeonly uniform image2D u_outDepth;
layout(r8, binding = 3) writeonly uniform image2D u_outConfidence;

uniform mat4 u_prevLightVP;
uniform mat4 u_curLightVP;
uniform mat4 u_invCurLightVP;
uniform vec2 u_texelSize;
uniform uint u_maxAge;
uniform vec2 u_resolution;

struct TemporalPixel {
    float depth;
    float confidence;
    uint age;
};

shared TemporalPixel sharedTile[18][18];

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 tileCoord = ivec2(gl_LocalInvocationID.xy) + 1;
    if (coord.x >= int(u_resolution.x) || coord.y >= int(u_resolution.y)) return;

    // Load tile including 1px border for neighbor sampling
    vec2 uv = (vec2(coord) + 0.5) / u_resolution;
    float curDepth = texelFetch(u_curDepth, coord, 0).r;
    float prevDepth = texelFetch(u_prevDepth, coord, 0).r;

    // Reproject: find where this pixel was in the previous frame
    vec4 curClip = vec4(uv * 2.0 - 1.0, curDepth * 2.0 - 1.0, 1.0);
    vec4 worldPos = u_invCurLightVP * curClip;
    vec4 prevClip = u_prevLightVP * worldPos;
    vec2 prevUV = prevClip.xy / prevClip.w * 0.5 + 0.5;

    float confidence = 0.0;
    float outDepth = curDepth;
    uint age = 0u;

    if (prevUV.x >= 0.0 && prevUV.x <= 1.0 && prevUV.y >= 0.0 && prevUV.y <= 1.0) {
        ivec2 prevCoord = ivec2(prevUV * u_resolution);
        float reprojected = texelFetch(u_prevDepth, prevCoord, 0).r;

        // Accept if depths are similar (within 1% tolerance)
        if (abs(reprojected - curDepth) < 0.01) {
            outDepth = reprojected;
            confidence = 0.9;
            age = 0u;
        } else {
            confidence = 0.3;
            age = u_maxAge;
        }
    }

    imageStore(u_outDepth, coord, vec4(outDepth, 0, 0, 0));
    imageStore(u_outConfidence, coord, vec4(confidence, 0, 0, 0));
}
)";

// ============================================================================
// NRC: Train the neural radiance cache
// ============================================================================
const char* nrcTrainComp = R"(#version 460 core
layout(local_size_x = 64) in;

struct TrainingSample {
    vec3 position;
    vec3 direction;
    vec3 radiance;
    float weight;
};

layout(std430, binding = 0) buffer SampleBuffer {
    TrainingSample samples[];
};

layout(std430, binding = 1) buffer WeightBuffer {
    float weights[];
};

uniform uint u_sampleCount;
uniform float u_learningRate;
uniform uint u_frameCount;

// TinyMLP: 6→32→32→3 with leaky ReLU + sigmoid
void forwardPass(const float inp[6], out float out_r, out float out_g, out float out_b) {
    float h1[32];
    float h2[32];

    // Layer 1: 6 → 32
    for (uint i = 0u; i < 32u; i++) {
        float sum = weights[192u + i]; // bias
        for (uint j = 0u; j < 6u; j++) {
            sum += weights[i * 6u + j] * inp[j];
        }
        h1[i] = sum > 0.0 ? sum : sum * 0.01; // Leaky ReLU
    }

    // Layer 2: 32 → 32
    uint l2Off = 224u;
    for (uint i = 0u; i < 32u; i++) {
        float sum = weights[l2Off + 1024u + i]; // bias
        for (uint j = 0u; j < 32u; j++) {
            sum += weights[l2Off + i * 32u + j] * h1[j];
        }
        h2[i] = sum > 0.0 ? sum : sum * 0.01;
    }

    // Layer 3: 32 → 3
    uint l3Off = 1280u;
    float outArr[3];
    for (uint i = 0u; i < 3u; i++) {
        float sum = weights[l3Off + 96u + i]; // bias
        for (uint j = 0u; j < 32u; j++) {
            sum += weights[l3Off + i * 32u + j] * h2[j];
        }
        outArr[i] = 1.0 / (1.0 + exp(-sum)); // sigmoid
    }

    out_r = outArr[0];
    out_g = outArr[1];
    out_b = outArr[2];
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_sampleCount || idx >= 1024u) return;

    TrainingSample s = samples[idx];
    float inp[6];
    inp[0] = s.position.x * 0.01;
    inp[1] = s.position.y * 0.01;
    inp[2] = s.position.z * 0.01;
    inp[3] = s.direction.x;
    inp[4] = s.direction.y;
    inp[5] = s.direction.z;

    // Forward pass
    float predR, predG, predB;
    forwardPass(inp, predR, predG, predB);

    // Output gradient for backprop (simplified: direct delta update)
    float targetR = s.radiance.r;
    float targetG = s.radiance.g;
    float targetB = s.radiance.b;

    float lr = u_learningRate / (1.0 + float(u_frameCount) * 0.001);
    float errR = (predR - targetR) * predR * (1.0 - predR);
    float errG = (predG - targetG) * predG * (1.0 - predG);
    float errB = (predB - targetB) * predB * (1.0 - predB);

    // Simplified weight update (proportional to input * error)
    for (uint j = 0u; j < 6u; j++) {
        atomicAdd(weights[j], uint(-lr * errR * inp[j] * s.weight * 1000.0));
    }
}
)";

// ============================================================================
// NRC: Query the neural network for radiance at a position+direction
// ============================================================================
const char* nrcQueryComp = R"(#version 460 core
layout(local_size_x = 64) in;

struct QueryPoint {
    vec3 position;
    vec3 direction;
};

struct QueryResult {
    vec3 radiance;
    float confidence;
};

layout(std430, binding = 0) buffer QueryBuffer {
    QueryPoint queries[];
};

layout(std430, binding = 1) buffer ResultBuffer {
    QueryResult results[];
};

layout(std430, binding = 2) buffer WeightBuffer {
    float weights[];
};

uniform uint u_queryCount;

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_queryCount) return;

    QueryPoint q = queries[idx];
    float inp[6];
    inp[0] = q.position.x * 0.01;
    inp[1] = q.position.y * 0.01;
    inp[2] = q.position.z * 0.01;
    inp[3] = q.direction.x;
    inp[4] = q.direction.y;
    inp[5] = q.direction.z;

    // Layer 1: 6 → 32 with leaky ReLU
    float h1[32];
    for (uint i = 0u; i < 32u; i++) {
        float sum = weights[192u + i];
        for (uint j = 0u; j < 6u; j++) {
            sum += weights[i * 6u + j] * inp[j];
        }
        h1[i] = sum > 0.0 ? sum : sum * 0.01;
    }

    // Layer 2: 32 → 32
    float h2[32];
    uint l2Off = 224u;
    for (uint i = 0u; i < 32u; i++) {
        float sum = weights[l2Off + 1024u + i];
        for (uint j = 0u; j < 32u; j++) {
            sum += weights[l2Off + i * 32u + j] * h1[j];
        }
        h2[i] = sum > 0.0 ? sum : sum * 0.01;
    }

    // Layer 3: 32 → 3 with sigmoid
    uint l3Off = 1280u;
    float outArr[3];
    for (uint i = 0u; i < 3u; i++) {
        float sum = weights[l3Off + 96u + i];
        for (uint j = 0u; j < 32u; j++) {
            sum += weights[l3Off + i * 32u + j] * h2[j];
        }
        outArr[i] = 1.0 / (1.0 + exp(-sum));
    }

    results[idx].radiance = vec3(outArr[0], outArr[1], outArr[2]);
    results[idx].confidence = (outArr[0] + outArr[1] + outArr[2]) / 3.0;
}
)";

// ============================================================================
// GPU-Driven: Frustum + occlusion culling compute shader
// ============================================================================
const char* gpuCullComp = R"(#version 460 core
layout(local_size_x = 64) in;

struct ObjectData {
    vec4 center;        // xyz = bounding sphere center, w = radius
    mat4 matrix;
    uint meshID;
    uint materialID;
    uint flags;
    uint lodLevel;
};

struct DrawCommand {
    uint count;
    uint instanceCount;
    uint firstIndex;
    uint baseVertex;
    uint baseInstance;
};

layout(std430, binding = 0) buffer ObjectBuffer {
    ObjectData objects[];
};

layout(std430, binding = 1) buffer DrawBuffer {
    DrawCommand draws[];
};

layout(std430, binding = 2) buffer VisibilityBuffer {
    uint visibility[];
};

layout(std430, binding = 3) buffer CounterBuffer {
    uint drawCount;
};

uniform mat4 u_viewProj;
uniform vec4 u_frustumPlanes[6];
uniform vec3 u_camPos;
uniform vec4 u_lodDistances;
uniform uint u_objectCount;

bool inFrustum(vec4 sphere) {
    for (uint i = 0u; i < 6u; i++) {
        float d = dot(u_frustumPlanes[i].xyz, sphere.xyz) + u_frustumPlanes[i].w;
        if (d < -sphere.w) return false;
    }
    return true;
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_objectCount) return;

    ObjectData obj = objects[idx];
    visibility[idx] = 0u;

    if (!inFrustum(obj.center)) return;

    // LOD selection
    float dist = distance(u_camPos, obj.center.xyz) - obj.center.w;
    uint lod = 0u;
    if (dist > u_lodDistances.w) lod = 3u;
    else if (dist > u_lodDistances.z) lod = 2u;
    else if (dist > u_lodDistances.y) lod = 1u;
    objects[idx].lodLevel = lod;

    visibility[idx] = 1u;

    // Append draw command (atomic to avoid race conditions)
    uint drawIdx = atomicAdd(drawCount, 1u);
    draws[drawIdx].count = 0u;
    draws[drawIdx].instanceCount = 1u;
    draws[drawIdx].firstIndex = 0u;
    draws[drawIdx].baseVertex = 0u;
    draws[drawIdx].baseInstance = idx;
}
)";

// ============================================================================
// Auto-Exposure: Compute luminance histogram
// ============================================================================
const char* autoExposureComp = R"(#version 460 core
layout(local_size_x = 256) in;

layout(binding = 0) uniform sampler2D u_scene;
layout(std430, binding = 1) buffer HistogramBuffer {
    uint histogram[256];
};

uniform vec2 u_resolution;
uniform float u_minLogLum;
uniform float u_maxLogLum;

shared uint sharedHist[256];

void main() {
    uint lid = gl_LocalInvocationID.x;
    sharedHist[lid] = 0u;
    barrier();

    // Each workgroup processes a tile of the image
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x < int(u_resolution.x) && pixel.y < int(u_resolution.y)) {
        vec3 color = texelFetch(u_scene, pixel, 0).rgb;
        float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
        float logLum = clamp((log2(lum + 0.0001) - u_minLogLum) / (u_maxLogLum - u_minLogLum), 0.0, 1.0);
        uint bin = uint(logLum * 255.0);
        atomicAdd(sharedHist[bin], 1u);
    }

    barrier();
    atomicAdd(histogram[lid], sharedHist[lid]);
}
)";

// ============================================================================
// SSR: Screen-Space Reflections
// ============================================================================
const char* ssrComp = R"(#version 460 core
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D u_scene;
layout(binding = 1) uniform sampler2D u_depth;
layout(binding = 2) uniform sampler2D u_normal;
layout(rgba8, binding = 3) writeonly uniform image2D u_output;

uniform mat4 u_invViewProj;
uniform mat4 u_viewProj;
uniform vec3 u_camPos;
uniform vec2 u_resolution;
uniform uint u_maxSteps;
uniform float u_maxDistance;
uniform float u_thickness;

vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = u_invViewProj * clip;
    return world.xyz / world.w;
}

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= int(u_resolution.x) || coord.y >= int(u_resolution.y)) return;

    vec2 uv = (vec2(coord) + 0.5) / u_resolution;
    float depth = texelFetch(u_depth, coord, 0).r;
    if (depth >= 1.0 - 1e-5) {
        imageStore(u_output, coord, vec4(0));
        return;
    }

    vec3 worldPos = reconstructWorldPos(uv, depth);
    vec3 normal = texelFetch(u_normal, coord, 0).rgb * 2.0 - 1.0;
    vec3 viewDir = normalize(u_camPos - worldPos);
    vec3 reflectDir = reflect(-viewDir, normal);

    // Ray march in screen space
    vec3 rayStart = worldPos;
    vec3 rayEnd = worldPos + reflectDir * u_maxDistance;
    float stepSize = 1.0 / float(u_maxSteps);

    vec3 rayColor = vec3(0);
    float hitAlpha = 0.0;

    for (uint i = 1u; i < u_maxSteps; i++) {
        vec3 samplePos = mix(rayStart, rayEnd, float(i) * stepSize);
        vec4 clip = u_viewProj * vec4(samplePos, 1.0);
        vec2 sampleUV = clip.xy / clip.w * 0.5 + 0.5;
        float sampleDepth = texture(u_depth, sampleUV).r;

        vec3 sampleWorld = reconstructWorldPos(sampleUV, sampleDepth);
        float dist = distance(samplePos, sampleWorld);

        if (dist < u_thickness && clip.z / clip.w < 1.0 && sampleUV.x > 0.0 && sampleUV.x < 1.0 && sampleUV.y > 0.0 && sampleUV.y < 1.0) {
            rayColor = texture(u_scene, sampleUV).rgb;
            hitAlpha = 1.0 - float(i) * stepSize;
            break;
        }
    }

    imageStore(u_output, coord, vec4(rayColor, hitAlpha));
}
)";

// ============================================================================
// Contact Shadows: Screen-space ray march along light direction
// ============================================================================
const char* contactShadowComp = R"(#version 460 core
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D u_depth;
layout(r8, binding = 1) writeonly uniform image2D u_shadow;

uniform mat4 u_invViewProj;
uniform vec3 u_sunDir;
uniform vec2 u_resolution;
uniform uint u_maxSteps;
uniform float u_maxDistance;
uniform float u_thickness;

vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = u_invViewProj * clip;
    return world.xyz / world.w;
}

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= int(u_resolution.x) || coord.y >= int(u_resolution.y)) return;

    vec2 uv = (vec2(coord) + 0.5) / u_resolution;
    float depth = texelFetch(u_depth, coord, 0).r;
    if (depth >= 1.0 - 1e-5) {
        imageStore(u_shadow, coord, vec4(1.0));
        return;
    }

    vec3 worldPos = reconstructWorldPos(uv, depth);
    vec3 rayDir = normalize(-u_sunDir);
    float stepSize = u_maxDistance / float(u_maxSteps);

    float shadow = 1.0;
    for (uint i = 1u; i < u_maxSteps; i++) {
        vec3 samplePos = worldPos + rayDir * (float(i) * stepSize);
        vec4 clip = u_viewProj * vec4(samplePos, 1.0);
        vec2 sampleUV = clip.xy / clip.w * 0.5 + 0.5;
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) break;

        float sceneDepth = texture(u_depth, sampleUV).r;
        vec3 scenePos = reconstructWorldPos(sampleUV, sceneDepth);
        float behind = distance(samplePos, scenePos);

        if (behind < u_thickness && clip.z / clip.w < 1.0) {
            shadow *= 0.5;
        }
    }

    imageStore(u_shadow, coord, vec4(shadow));
}
)";

// ============================================================================
// Volumetric Fog: Ray-marched participating media
// ============================================================================
const char* volumetricFogComp = R"(#version 460 core
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D u_depth;
layout(binding = 1) uniform sampler2D u_scene;
layout(rgba16f, binding = 2) writeonly uniform image2D u_output;

uniform mat4 u_invViewProj;
uniform vec3 u_camPos;
uniform vec3 u_fogColor;
uniform float u_fogDensity;
uniform float u_fogHeight;
uniform float u_time;
uniform vec2 u_resolution;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1,0)), u.x),
               mix(hash(i + vec2(0,1)), hash(i + vec2(1,1)), u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * noise(p);
        p *= 2.07;
        a *= 0.5;
    }
    return v;
}

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= int(u_resolution.x) || coord.y >= int(u_resolution.y)) return;

    vec2 uv = (vec2(coord) + 0.5) / u_resolution;
    vec4 sceneColor = texelFetch(u_scene, coord, 0);
    float sceneDepth = texelFetch(u_depth, coord, 0).r;

    if (sceneDepth >= 1.0 - 1e-5) {
        imageStore(u_output, coord, sceneColor);
        return;
    }

    vec4 clip = vec4(uv * 2.0 - 1.0, sceneDepth * 2.0 - 1.0, 1.0);
    vec4 worldW = u_invViewProj * clip;
    vec3 worldPos = worldW.xyz / worldW.w;
    float viewDist = distance(u_camPos, worldPos);

    // Height-based fog density
    float heightDiff = max(worldPos.y - u_fogHeight, 0.0);
    float heightFog = exp(-heightDiff * 0.1);

    // Animated noise for wispy fog
    vec2 fogUV = worldPos.xz * 0.003 + vec2(u_time * 0.02, u_time * 0.01);
    float fogNoise = fbm(fogUV * 3.0) * 0.5 + 0.5;

    float fog = 1.0 - exp(-u_fogDensity * u_fogDensity * viewDist * viewDist * heightFog * fogNoise);
    fog = clamp(fog, 0.0, 1.0);

    vec3 finalColor = mix(sceneColor.rgb, u_fogColor, fog);
    imageStore(u_output, coord, vec4(finalColor, sceneColor.a));
}
)";

// ============================================================================
// TAA: Temporal Anti-Aliasing resolve
// ============================================================================
const char* taaResolveComp = R"(#version 460 core
layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0) uniform sampler2D u_current;
layout(binding = 1) uniform sampler2D u_history;
layout(binding = 2) uniform sampler2D u_depth;
layout(rgba8, binding = 3) writeonly uniform image2D u_output;

uniform mat4 u_invViewProj;
uniform mat4 u_prevViewProj;
uniform vec2 u_resolution;
uniform float u_alpha;

vec3 clipAABB(vec3 minCol, vec3 maxCol, vec3 p) {
    return clamp(p, minCol, maxCol);
}

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= int(u_resolution.x) || coord.y >= int(u_resolution.y)) return;

    vec2 uv = (vec2(coord) + 0.5) / u_resolution;

    // Current frame sample
    vec3 current = texelFetch(u_current, coord, 0).rgb;

    // Neighborhood clamp (3x3 min/max)
    vec3 minCol = vec3(1e10);
    vec3 maxCol = vec3(-1e10);
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec3 s = texelFetch(u_current, coord + ivec2(x, y), 0).rgb;
            minCol = min(minCol, s);
            maxCol = max(maxCol, s);
        }
    }

    // Reproject current pixel to previous frame
    float depth = texelFetch(u_depth, coord, 0).r;
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos = u_invViewProj * clip;
    vec4 prevClip = u_prevViewProj * worldPos;
    vec2 prevUV = prevClip.xy / prevClip.w * 0.5 + 0.5;

    // Sample history with neighborhood clamping
    vec3 history = vec3(0);
    if (prevUV.x >= 0.0 && prevUV.x <= 1.0 && prevUV.y >= 0.0 && prevUV.y <= 1.0) {
        history = texture(u_history, prevUV).rgb;
        history = clipAABB(minCol, maxCol, history);
    } else {
        history = current;
    }
    // Blend
    vec3 result = mix(history, current, u_alpha);
    imageStore(u_output, coord, vec4(result, 1.0));
}
)";

// ============================================================================
// Compute shader template system
// ============================================================================

namespace {

constexpr u32 kMaxIncludeDepth = 32u;

String trimStr(const String& s) {
    usize start = 0;
    usize end = s.size();
    while (start < end) {
        char c = s[start];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        start++;
    }
    while (end > start) {
        char c = s[end - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        end--;
    }
    return s.substr(start, end - start);
}

u32 parseU32(const String& s) {
    u32 value = 0;
    for (usize i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            value = value * 10u + (u32)(c - '0');
        } else if (c == 'u' || c == 'U' || c == ' ' || c == '\t') {
            continue;
        } else {
            break;
        }
    }
    return value;
}

void extractDefines(const String& src, Vector<String>& defines) {
    usize pos = 0;
    while (pos < src.size()) {
        usize f = src.find("#define", pos);
        if (f == String::npos) break;
        usize lineEnd = src.find('\n', f);
        if (lineEnd == String::npos) lineEnd = src.size();
        String body = trimStr(src.substr(f + 7, lineEnd - f - 7));
        if (!body.empty() && !body.startsWith("FROST_SPEC(")) {
            defines.push_back(body);
        }
        pos = (lineEnd == src.size()) ? src.size() : lineEnd + 1;
    }
}

void extractSpecializationConstants(const String& src, Vector<SpecializationValue>& specs) {
    usize pos = 0;
    u32 ord = 0;
    while (pos < src.size()) {
        usize f = src.find("FROST_SPEC", pos);
        if (f == String::npos) break;
        usize paren = f + 10;
        while (paren < src.size() && (src[paren] == ' ' || src[paren] == '\t')) paren++;
        if (paren >= src.size() || src[paren] != '(') {
            pos = f + 10;
            continue;
        }
        usize comma = src.find(',', paren);
        usize close = src.find(')', paren);
        if (comma == String::npos || close == String::npos || comma > close) {
            pos = f + 10;
            continue;
        }
        SpecializationValue sv;
        sv.constantId = ord;
        sv.value = parseU32(trimStr(src.substr(comma + 1, close - comma - 1)));
        specs.push_back(sv);
        pos = close + 1;
        ord++;
    }
}

} // anonymous namespace

u32 ShaderSource_compute::registerTemplate(const char* name, const char* source) {
    if (!name || !source) return 0xFFFFFFFF;
    for (usize i = 0; i < templates_.size(); i++) {
        if (templates_[i].name == name) return (u32)i;
    }
    ComputeShaderTemplate tpl;
    tpl.name = name;
    tpl.source = source;
    extractDefines(tpl.source, tpl.defines);
    extractSpecializationConstants(tpl.source, tpl.specializationConstants);
    specializationCount_ += (u32)tpl.specializationConstants.size();
    templates_.push_back(std::move(tpl));
    return (u32)(templates_.size() - 1);
}

u32 ShaderSource_compute::instantiateTemplate(u32 templateId, const Vector<SpecializationValue>& specializations) {
    if (templateId >= templates_.size()) return 0xFFFFFFFF;

    String key;
    key.format("@%u@", (unsigned)templateId);
    Vector<SpecializationValue> sorted;
    for (usize i = 0; i < specializations.size(); i++) {
        sorted.push_back(specializations[i]);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const SpecializationValue& a, const SpecializationValue& b) {
                  return a.constantId < b.constantId;
              });
    for (usize i = 0; i < sorted.size(); i++) {
        String part;
        part.format("c%u=%u;", (unsigned)sorted[i].constantId, (unsigned)sorted[i].value);
        key.append(part);
    }

    auto it = templateCache_.find(key);
    if (it != templateCache_.end()) {
        return it.value();
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    const ComputeShaderTemplate& tpl = templates_[templateId];
    String resolved = resolveIncludes(tpl.source, templateId, 0);
    String finalSource = applySpecializations(resolved, tpl, specializations);

    u32 handle = generatedShaders_ + 1u;
    generatedShaders_ = handle;
    templateCache_[key] = handle;

    auto t1 = std::chrono::high_resolution_clock::now();
    generateTimeMs_ += std::chrono::duration<f32, std::milli>(t1 - t0).count();

    (void)finalSource;
    return handle;
}

void ShaderSource_compute::specializeConstant(u32 constantId, u32 value) {
    if (templates_.empty()) return;
    ComputeShaderTemplate& tpl = templates_.back();
    for (usize i = 0; i < tpl.specializationConstants.size(); i++) {
        if (tpl.specializationConstants[i].constantId == constantId) {
            tpl.specializationConstants[i].value = value;
            templateCache_.clear();
            return;
        }
    }
    tpl.specializationConstants.push_back(SpecializationValue{constantId, value});
    specializationCount_++;
    templateCache_.clear();
}

const ComputeShaderTemplate& ShaderSource_compute::getTemplate(u32 id) const {
    static const ComputeShaderTemplate kEmptyTemplate;
    if (id >= templates_.size()) return kEmptyTemplate;
    return templates_[id];
}

bool ShaderSource_compute::hasTemplate(const char* name) const {
    if (!name) return false;
    for (usize i = 0; i < templates_.size(); i++) {
        if (templates_[i].name == name) return true;
    }
    return false;
}

Vector<String> ShaderSource_compute::parseIncludes(const char* source) {
    Vector<String> result;
    if (!source) return result;
    String src(source);
    usize pos = 0;
    while (pos < src.size()) {
        usize inc = src.find("#include", pos);
        if (inc == String::npos) break;
        usize open = inc + 8;
        while (open < src.size() && (src[open] == ' ' || src[open] == '\t')) open++;
        if (open >= src.size()) break;
        char closeChar = 0;
        if (src[open] == '"') closeChar = '"';
        else if (src[open] == '<') closeChar = '>';
        if (!closeChar) { pos = inc + 8; continue; }
        usize nameStart = open + 1;
        usize close = src.find(closeChar, nameStart);
        if (close == String::npos) break;
        result.push_back(src.substr(nameStart, close - nameStart));
        pos = close + 1;
    }
    return result;
}

String ShaderSource_compute::resolveIncludes(const String& source, u32 selfId, u32 depth) {
    if (depth > kMaxIncludeDepth) return source;
    if (depth > includeDepth_) includeDepth_ = depth;

    String result;
    usize pos = 0;
    while (pos < source.size()) {
        usize inc = source.find("#include", pos);
        if (inc == String::npos) {
            result.append(source.substr(pos));
            break;
        }
        result.append(source.substr(pos, inc - pos));
        usize open = inc + 8;
        while (open < source.size() && (source[open] == ' ' || source[open] == '\t')) open++;
        if (open >= source.size()) {
            result.append("#include");
            break;
        }
        char closeChar = 0;
        if (source[open] == '"') closeChar = '"';
        else if (source[open] == '<') closeChar = '>';
        if (!closeChar) {
            result.append("#include");
            pos = inc + 8;
            continue;
        }
        usize nameStart = open + 1;
        usize close = source.find(closeChar, nameStart);
        if (close == String::npos) {
            result.append(source.substr(inc));
            break;
        }
        String includeName = source.substr(nameStart, close - nameStart);
        bool resolved = false;
        for (usize i = 0; i < templates_.size(); i++) {
            if ((u32)i == selfId) continue;
            if (templates_[i].name == includeName) {
                result.append(resolveIncludes(templates_[i].source, (u32)i, depth + 1));
                resolved = true;
                break;
            }
        }
        if (!resolved) {
            result.append(source.substr(inc, close - inc + 1));
        }
        pos = close + 1;
    }
    return result;
}

String ShaderSource_compute::applySpecializations(const String& source,
                                                  const ComputeShaderTemplate& tpl,
                                                  const Vector<SpecializationValue>& overrides) {
    String result;
    usize pos = 0;
    u32 ord = 0;
    while (pos < source.size()) {
        usize f = source.find("FROST_SPEC", pos);
        if (f == String::npos) {
            result.append(source.substr(pos));
            break;
        }
        result.append(source.substr(pos, f - pos));
        usize paren = f + 10;
        while (paren < source.size() && (source[paren] == ' ' || source[paren] == '\t')) paren++;
        if (paren >= source.size() || source[paren] != '(') {
            result.append("#define FROST_SPEC");
            pos = f + 10;
            continue;
        }
        usize comma = source.find(',', paren);
        usize close = source.find(')', paren);
        if (comma == String::npos || close == String::npos || comma > close) {
            usize end = (close == String::npos) ? source.size() : close + 1;
            result.append(source.substr(f, end - f));
            pos = end;
            continue;
        }
        String name = trimStr(source.substr(paren + 1, comma - paren - 1));
        String defText = trimStr(source.substr(comma + 1, close - comma - 1));

        u32 value = 0;
        if (ord < tpl.specializationConstants.size()) {
            value = tpl.specializationConstants[ord].value;
        } else {
            value = parseU32(defText);
        }
        for (usize i = 0; i < overrides.size(); i++) {
            if (overrides[i].constantId == ord) {
                value = overrides[i].value;
                break;
            }
        }

        result.append("#define ");
        result.append(name);
        result.append(' ');
        String valStr;
        valStr.format("%u", (unsigned)value);
        result.append(valStr);
        pos = close + 1;
        ord++;
    }
    return result;
}

}
}
