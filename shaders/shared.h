#version 450

#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
#extension GL_EXT_nonuniform_qualifier : enable

precision highp float;

const float PI = 3.14159265359;
const float INV_PI = 0.31830988618;
const float SQRT2 = 1.41421356237;
const float EPSILON = 0.0001;

const uint WORKGROUP_SIZE = 256;
const uint MAX_LIGHTS = 256;
const uint MAX_SHADOW_CASCADES = 4;
const uint MAX_MIP_LEVELS = 16;
const uint MAX_BONES = 256;
const uint MAX_INSTANCES = 65536;

struct Camera {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 invView;
    mat4 invProj;
    vec3 position;
    float nearPlane;
    vec3 forward;
    float farPlane;
    vec3 right;
    float aspectRatio;
    vec3 up;
    float fov;
    uint frameIndex;
};

struct Light {
    vec4 position;
    vec4 direction;
    vec4 color;
    float intensity;
    float temperature;
    float radius;
    float range;
    uint type;
    uint shadowIndex;
    float innerCone;
    float outerCone;
    uint castShadows;
    uint padding;
};

struct LightIndexData {
    uint directional;
    uint point;
    uint spot;
    uint rect;
    vec3 padding;
};

struct Material {
    vec4 baseColor;
    float metallic;
    float roughness;
    float normalScale;
    float occlusionStrength;
    float emissiveIntensity;
    uint flags;
    vec3 emissive;
    float alphaCutoff;
    vec4 animTransform;
    vec4 detailNormal;
    float detailNormalScale;
    float heightScale;
    uint textureMask;
    vec2 padding;
};

struct InstanceData {
    mat4 world;
    mat4 prevWorld;
    vec4 customData;
    vec2 padding;
};

struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 uv;
    vec4 tangent;
    vec4 color;
    vec4 jointIndices;
    vec4 jointWeights;
    vec3 positionMin;
    float padding1;
    vec3 positionMax;
    float padding2;
};

struct BlitVertex {
    vec2 position;
    vec2 uv;
};

vec3 linearToSRGB(vec3 color) {
    vec3 c = max(color, vec3(0.0));
    vec3 c1 = c * 12.92;
    vec3 c2 = pow(c, vec3(1.0/2.4)) * 1.055 - 0.055;
    return mix(c1, c2, step(0.0031308, c));
}

vec3 sRGBToLinear(vec3 color) {
    vec3 c = max(color, vec3(0.0));
    vec3 c1 = c / 12.92;
    vec3 c2 = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(c1, c2, step(0.04045, c));
}

float saturation(float x) {
    return clamp(x, 0.0, 1.0);
}

vec3 saturation(vec3 x) {
    return clamp(x, vec3(0.0), vec3(1.0));
}

vec4 saturation(vec4 x) {
    return clamp(x, vec4(0.0), vec4(1.0));
}

vec3TonemapACES(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 reinhardTonemap(vec3 x) {
    return x / (1.0 + x);
}

vec3 fibonacciSphere(uint i, uint n, float radius) {
    float k = (i + 0.5) / float(n);
    float phi = 2.0 * PI * (1.0 - (sqrt(5.0) - 1.0) / 2.0) * k;
    float y = 1.0 - (2.0 * k - 1.0);
    float r = sqrt(max(0.0, 1.0 - y * y));
    return radius * vec3(cos(phi) * r, y, sin(phi) * r);
}

uint hash(uint x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

uint hash(uint x, uint y) {
    return hash(x ^ hash(y) * 0x9e3779b9u);
}

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx+p3.yz)*p3.zy);
}

vec3 hash33(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453123);
}

vec4 hash44(vec4 p) {
    p = vec4(dot(p, vec4(127.1, 311.7, 74.7, 113.5)),
             dot(p, vec4(269.5, 183.3, 246.1, 124.6)),
             dot(p, vec4(419.2, 371.9, 127.5, 317.3)),
             dot(p, vec4(323.1, 219.5, 198.2, 112.1)));
    return fract(sin(p) * 43758.5453);
}

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}

vec3 perturbNormal(vec3 N, vec3 p, vec2 uv, vec3 normal) {
    mat3 TBN = cotangentFrame(N, p, uv);
    vec3 n = normal * 2.0 - 1.0;
    return normalize(TBN * n);
}

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return nom / max(denom, EPSILON);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float attenuation(float distance, float radius) {
    return clamp(1.0 - (distance * distance) / (radius * radius), 0.0, 1.0);
    distance = distance * distance;
    return 1.0 / (distance + 1.0);
}

float punctualLightAttenuation(float distance, float radius) {
    return attenuation(distance, radius);
}

vec2 encodeNormal(vec3 n) {
    n = normalize(n);
    vec2 enc = n.xy / (n.z + 1.0);
    if(n.z < 0.0) {
        enc = -enc;
    }
    return enc * 0.5 + 0.5;
}

vec3 decodeNormal(vec2 e) {
    vec2 f = e * 2.0 - 1.0;
    float z = 1.0 - f.x * f.x - f.y * f.y;
    z = z > 0.0 ? sqrt(z) : 0.0;
    return vec3(f.x, f.y, z);
}

float pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

float interpolate(float a, float b, float t) {
    return mix(a, b, t);
}

vec3 interpolate(vec3 a, vec3 b, float t) {
    return mix(a, b, t);
}

vec4 interpolate(vec4 a, vec4 b, float t) {
    return mix(a, b, t);
}

mat3 interpolate(mat3 a, mat3 b, float t) {
    mat3 r;
    for(int i = 0; i < 3; i++) {
        for(int j = 0; j < 3; j++) {
            r[i][j] = mix(a[i][j], b[i][j], t);
        }
    }
    return r;
}

vec3 computeViewDirection(vec3 worldPos, vec3 camPos) {
    return normalize(camPos - worldPos);
}

vec3 computeNormal(vec3 worldPos, vec3 baseNormal, vec3 dx, vec3 dy) {
    vec3 n1 = dFdx(worldPos);
    vec3 n2 = dFdy(worldPos);
    vec3 normal = normalize(cross(n1, n2));
    if(dot(normal, baseNormal) < 0.0) {
        normal = -normal;
    }
    return normal;
}

void unproject(vec3 clipPos, mat4 invProj, mat4 invView, out vec3 worldPos) {
    vec4 ndc = clipPos;
    vec4 viewPos = invProj * ndc;
    viewPos = viewPos / viewPos.w;
    worldPos = (invView * viewPos).xyz;
}

float computeLODLevel(vec3 worldPos, mat4 view) {
    return 0.0;
}

float calculateShininess(float roughness) {
    return (2.0 / (roughness * roughness)) - 2.0;
}

float3x3 getTangentToWorld3x3(vec3 N, vec3 T, float handedness) {
    vec3 T = normalize(T - dot(T, N) * N);
    vec3 B = handedness * cross(N, T);
    return float3x3(T, B, N);
}

uint packedUint(uint x, uint y, uint z, uint w) {
    return (x << 0) | (y << 8) | (z << 16) | (w << 24);
}

uint packedU16(uint x, uint y) {
    return (x & 0xFFFF) | ((y & 0xFFFF) << 16);
}