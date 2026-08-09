#include "Renderer/ShaderSource.h"

namespace Frost {
namespace ShaderSource {

const char* terrainVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
layout(location = 3) in vec2 a_uv;

uniform mat4 u_model;
uniform mat4 u_viewProj;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;
out float v_biome;
out float v_ao;
out float v_height;

void main() {
    vec4 world = u_model * vec4(a_pos, 1.0);
    v_worldPos = world.xyz;
    v_normal = normalize(mat3(u_model) * a_normal);
    v_uv = a_uv;
    v_biome = a_tangent.x;
    v_ao = a_tangent.y;
    v_height = world.y;
    gl_Position = u_viewProj * world;
}
)";

const char* terrainFrag = R"(#version 330 core
in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;
in float v_biome;
in float v_ao;
in float v_height;

out vec4 fragColor;

uniform vec3 u_camPos;
uniform vec3 u_sunDir;
uniform vec3 u_sunColor;
uniform float u_sunIntensity;
uniform mat4 u_lightVP0;
uniform mat4 u_lightVP1;
uniform sampler2D u_shadowMap0;
uniform sampler2D u_shadowMap1;
uniform float u_shadowEnabled;
uniform float u_shadowTexel;
uniform float u_cascadeDist;

uniform vec3 u_ambientSky;
uniform vec3 u_ambientGround;
uniform float u_ambientIntensity;

uniform float u_fogDensity;
uniform vec3 u_fogColor;

uniform sampler2D u_texGrass;
uniform sampler2D u_texRock;
uniform sampler2D u_texSnow;
uniform float u_snowHeight;
uniform float u_rockSlope;
uniform float u_tiling;
uniform float u_time;

uniform vec4 u_clipPlane;
uniform float u_clipEnabled;

const float PI = 3.14159265358979;

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float sampleShadow(sampler2D map, mat4 vp, vec3 worldPos, vec3 N, vec3 lightDir) {
    vec4 shadowCoord = vp * vec4(worldPos, 1.0);
    vec3 proj = shadowCoord.xyz / shadowCoord.w;
    proj = proj * 0.5 + 0.5;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0) return 1.0;
    float bias = max(0.0015 * (1.0 - dot(N, lightDir)), 0.0006);
    float shadow = 0.0;
    float texel = u_shadowTexel;
    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            vec2 offset = vec2(float(x), float(y)) * texel * 1.6;
            float depth = texture(map, proj.xy + offset).r;
            shadow += (proj.z - bias > depth) ? 1.0 : 0.0;
        }
    }
    return 1.0 - shadow / 25.0;
}

float shadowFactor(vec3 worldPos, vec3 N, vec3 lightDir, float viewDist) {
    if (u_shadowEnabled < 0.5) return 1.0;
    if (viewDist < u_cascadeDist) {
        return sampleShadow(u_shadowMap0, u_lightVP0, worldPos, N, lightDir);
    }
    return sampleShadow(u_shadowMap1, u_lightVP1, worldPos, N, lightDir);
}

void main() {
    if (u_clipEnabled > 0.5 && dot(v_worldPos, u_clipPlane.xyz) < u_clipPlane.w) discard;

    vec3 N = normalize(v_normal);
    vec3 grass = texture(u_texGrass, v_uv * u_tiling).rgb;
    vec3 rock = texture(u_texRock, v_uv * u_tiling).rgb;
    vec3 snow = texture(u_texSnow, v_uv * u_tiling * 2.0).rgb;

    float slope = 1.0 - N.y;
    float rockMask = smoothstep(u_rockSlope - 0.10, u_rockSlope + 0.10, slope);
    float snowMask = smoothstep(u_snowHeight - 4.0, u_snowHeight + 4.0, v_height);
    vec3 baseColor = mix(grass, rock, rockMask);
    baseColor = mix(baseColor, snow, snowMask);

    // Biome tint from the per-vertex biome attribute (0=valley..6=ocean)
    float b = v_biome;
    vec3 forestTint = vec3(0.80, 1.22, 0.78);
    vec3 desertTint = vec3(1.32, 1.10, 0.70);
    vec3 mistTint = vec3(0.78, 0.95, 0.90);
    float bF = smoothstep(0.6, 1.4, b);
    baseColor = mix(baseColor, baseColor * forestTint, bF * 0.45);
    float bD = smoothstep(2.5, 3.5, b);
    baseColor = mix(baseColor, baseColor * desertTint, bD * 0.92);
    float bM = smoothstep(4.6, 5.4, b);
    baseColor = mix(baseColor, baseColor * mistTint, bM * 0.6);

    // lush valley lowlands, warm gold on upper slopes
    float lowland = 1.0 - smoothstep(22.0, 55.0, v_height);
    baseColor = mix(baseColor, baseColor * vec3(0.85, 1.32, 0.80), lowland * 0.5);
    float upland = smoothstep(55.0, 100.0, v_height);
    baseColor = mix(baseColor, baseColor * vec3(1.25, 1.12, 0.85), upland * 0.35);

    // per-pixel variation so ground never looks flat
    float detail = hash13(v_worldPos * 0.35);
    baseColor *= 0.92 + detail * 0.16;

    vec3 V = normalize(u_camPos - v_worldPos);
    vec3 L = normalize(-u_sunDir);
    float NdotL = max(dot(N, L), 0.0);
    float viewDist = distance(u_camPos, v_worldPos);
    float shadow = shadowFactor(v_worldPos, N, L, viewDist);
    vec3 direct = baseColor * u_sunColor * u_sunIntensity * NdotL * shadow;

    float NdotV = max(dot(N, V), 0.0);
    vec3 rim = u_sunColor * pow(1.0 - NdotV, 3.0) * 0.15 * (1.0 - shadow * 0.6);

    vec3 skyAmb = mix(u_ambientGround, u_ambientSky, N.y * 0.5 + 0.5) * u_ambientIntensity;
    vec3 ambient = skyAmb * baseColor * v_ao;

    vec3 color = ambient + direct + rim;

    // gentle saturation lift
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(lum), color, 1.22);

    // distance / height fog
    float dist = viewDist;
    float fog = 1.0 - exp(-u_fogDensity * u_fogDensity * dist * dist);
    fog = clamp(fog, 0.0, 1.0);
    color = mix(color, u_fogColor, fog);

    fragColor = vec4(color, 1.0);
}
)";

const char* waterVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 3) in vec2 a_uv;

uniform mat4 u_model;
uniform mat4 u_viewProj;
uniform float u_time;
uniform float u_waveAmplitude;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;
out vec2 v_screen;

void main() {
    v_uv = a_uv;
    vec3 p = a_pos;
    float y = 0.0;
    y += sin(p.x * 0.8 + u_time * 1.4) * 0.5;
    y += sin(p.z * 0.9 + u_time * 1.7) * 0.5;
    y += sin((p.x + p.z) * 0.5 + u_time * 0.9) * 0.4;
    p.y = y * u_waveAmplitude;
    vec4 world = u_model * vec4(p, 1.0);
    v_worldPos = world.xyz;

    float dx = 0.8 * cos(p.x * 0.8 + u_time * 1.4) * u_waveAmplitude;
    float dz = 0.9 * cos(p.z * 0.9 + u_time * 1.7) * u_waveAmplitude;
    v_normal = normalize(vec3(-dx, 1.0, -dz));

    vec4 clip = u_viewProj * world;
    v_screen = clip.xy / clip.w * 0.5 + 0.5;
    gl_Position = clip;
}
)";

const char* waterFrag = R"(#version 330 core
in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;
in vec2 v_screen;

out vec4 fragColor;

uniform vec3 u_camPos;
uniform vec3 u_sunDir;
uniform vec3 u_sunColor;
uniform float u_sunIntensity;
uniform vec3 u_waterColor;
uniform vec3 u_deepColor;
uniform float u_time;
uniform float u_fogDensity;
uniform vec3 u_fogColor;
uniform float u_waterLevel;

uniform sampler2D u_reflection;
uniform sampler2D u_refraction;
uniform float u_reflectionEnabled;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

vec3 skyColor(vec3 dir) {
    vec3 zenith = vec3(0.05, 0.32, 0.55);
    vec3 horizon = vec3(0.96, 0.79, 0.55);
    vec3 col = mix(horizon, zenith, pow(max(dir.y, 0.0), 0.5));
    col = mix(col, zenith, pow(max(dir.y, 0.0), 2.0));
    float sunDot = max(dot(dir, normalize(-u_sunDir)), 0.0);
    col += u_sunColor * u_sunIntensity * pow(sunDot, 900.0) * 40.0;
    col += u_sunColor * pow(sunDot, 24.0) * 0.55;
    return col;
}

void main() {
    vec3 N = normalize(v_normal);
    vec3 V = normalize(u_camPos - v_worldPos);
    vec3 L = normalize(-u_sunDir);

    float NdotV = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - NdotV, 5.0);
    fresnel = clamp(fresnel, 0.0, 1.0);

    // animated ripple normal for reflections / refractions
    vec2 waveUv = v_worldPos.xz * 0.18 + vec2(u_time * 0.06, u_time * 0.04);
    vec2 ripple = vec2(
        sin(waveUv.x + waveUv.y) * 0.006,
        cos(waveUv.y - waveUv.x) * 0.006
    );

    vec3 R = reflect(-V, N);

    vec3 reflected;
    if (u_reflectionEnabled > 0.5) {
        vec2 ruv = v_screen + ripple * 1.5;
        reflected = texture(u_reflection, ruv).rgb;
    } else {
        reflected = skyColor(R);
    }

    // refraction: sample the resolved scene with a slight displacement
    vec2 duv = clamp(v_screen + ripple + (L.xz * 0.004), 0.0, 1.0);
    vec3 refracted = texture(u_refraction, duv).rgb;

    float depthFactor = clamp((v_worldPos.y - u_waterLevel) * -0.15, 0.0, 1.0);
    vec3 deep = mix(u_waterColor, u_deepColor, depthFactor);

    vec3 base = mix(mix(refracted, deep, 0.45), reflected, fresnel);

    // sun specular glint
    float NdotL = max(dot(N, L), 0.0);
    vec3 H = normalize(V + L);
    float spec = pow(max(dot(N, H), 0.0), 256.0);
    base += u_sunColor * u_sunIntensity * spec * 3.0;

    // sun streak
    float streak = pow(max(dot(N, normalize(V + L)), 0.0), 90.0);
    base += u_sunColor * streak * 1.2;

    // caustic shimmer on the surface
    float caustic = 0.5 + 0.5 * sin(waveUv.x * 3.0 + u_time) * sin(waveUv.y * 3.0 - u_time);
    base += u_sunColor * caustic * 0.10 * (1.0 - fresnel);

    // foam speckle
    float foam = hash(floor(v_worldPos.xz * 0.9 + vec2(u_time * 0.6, -u_time * 0.4)));
    base += vec3(1.0, 0.97, 0.92) * step(0.93, foam) * 0.30;

    float dist = distance(u_camPos, v_worldPos);
    float fog = 1.0 - exp(-u_fogDensity * u_fogDensity * dist * dist);
    base = mix(base, u_fogColor, clamp(fog, 0.0, 1.0));

    float alpha = mix(0.72, 0.96, fresnel);
    fragColor = vec4(base, alpha);
}
)";

const char* skyVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_viewProj;
out vec3 v_dir;
void main() {
    v_dir = a_pos;
    vec4 pos = u_viewProj * vec4(a_pos, 1.0);
    gl_Position = pos.xyww;
    gl_Position.z = 1.0;
}
)";

const char* skyFrag = R"(#version 330 core
in vec3 v_dir;
out vec4 fragColor;

uniform vec3 u_sunDir;
uniform vec3 u_sunColor;
uniform float u_sunIntensity;
uniform float u_time;
uniform float u_nightFactor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; i++) {
        v += a * noise(p);
        p *= 2.07;
        a *= 0.5;
    }
    return v;
}

void main() {
    vec3 dir = normalize(v_dir);
    float h = dir.y;

    // Day palette: deep teal zenith, sea-green mid sky, warm golden horizon
    vec3 dayZenith = vec3(0.05, 0.32, 0.55);
    vec3 dayMid = vec3(0.28, 0.55, 0.70);
    vec3 dayHorizon = vec3(0.96, 0.79, 0.55);

    // Night palette: deep indigo zenith, violet mid, faint ember horizon
    vec3 nightZenith = vec3(0.012, 0.02, 0.09);
    vec3 nightMid = vec3(0.05, 0.08, 0.20);
    vec3 nightHorizon = vec3(0.16, 0.12, 0.22);

    vec3 zenith = mix(dayZenith, nightZenith, u_nightFactor);
    vec3 midSky = mix(dayMid, nightMid, u_nightFactor);
    vec3 horizon = mix(dayHorizon, nightHorizon, u_nightFactor);

    vec3 color = mix(horizon, midSky, pow(max(h, 0.0), 0.5));
    color = mix(color, zenith, pow(max(h, 0.0), 2.0));

    // warm underglow hugging the horizon
    color += vec3(0.32, 0.20, 0.09) * exp(-abs(h) * 4.5) * 0.9 * (1.0 - u_nightFactor * 0.85);

    // sun disc + layered glow + rays
    vec3 sunDir = normalize(-u_sunDir);
    float sunDot = max(dot(dir, sunDir), 0.0);
    color += u_sunColor * u_sunIntensity * pow(sunDot, 1600.0) * 60.0 * (1.0 - u_nightFactor);
    color += u_sunColor * pow(sunDot, 40.0) * 0.7 * (1.0 - u_nightFactor);
    color += u_sunColor * pow(sunDot, 10.0) * 0.22 * (1.0 - u_nightFactor);
    color += u_sunColor * pow(sunDot, 3.0) * 0.06 * (1.0 - u_nightFactor);
    color += u_sunColor * pow(max(dot(dir, sunDir), 0.0), 6.0) * 0.10 * (1.0 - u_nightFactor);

    // moon at night
    float moonDot = max(dot(dir, -sunDir), 0.0);
    color += vec3(0.85, 0.9, 1.0) * u_nightFactor * pow(moonDot, 1200.0) * 3.0;
    color += vec3(0.6, 0.7, 0.95) * u_nightFactor * pow(moonDot, 40.0) * 0.5;

    // drifting clouds, two layers, warm-lit
    float cloudH = pow(max(h, 0.0), 2.6);
    vec2 cp = dir.xz / (dir.y + 0.25);
    vec3 cloudColor = vec3(1.0, 0.93, 0.84);
    float clouds = fbm(cp * 0.45 + vec2(u_time * 0.006, 0.0));
    clouds = smoothstep(0.46, 0.88, clouds);
    color = mix(color, cloudColor, clouds * cloudH * 0.85);
    float haze = fbm(cp * 1.8 + vec2(-u_time * 0.004, u_time * 0.002));
    haze = smoothstep(0.40, 0.62, haze) * 0.35;
    color = mix(color, cloudColor, haze * cloudH * 0.5);

    // faint stars at night
    if (h > 0.30) {
        vec2 sp = dir.xz / (dir.y + 0.01);
        float stars = step(0.9955, hash(floor(sp * 80.0)));
        stars *= smoothstep(0.30, 0.85, h);
        color += vec3(0.95) * stars * (0.12 + 0.9 * u_nightFactor);
    }

    fragColor = vec4(color, 1.0);
}
)";

const char* particleVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;      // color packed as normal
layout(location = 2) in vec4 a_tangent;     // size & alpha
layout(location = 3) in vec2 a_uv;          // unused

uniform mat4 u_viewProj;
uniform mat4 u_model;
uniform float u_pointSize;
uniform float u_time;

out vec3 v_color;
out float v_alpha;

void main() {
    v_color = a_normal;
    v_alpha = a_tangent.w;
    vec4 pos = u_model * vec4(a_pos, 1.0);
    gl_Position = u_viewProj * pos;
    gl_PointSize = u_pointSize * a_tangent.x;
}
)";

const char* particleFrag = R"(#version 330 core
in vec3 v_color;
in float v_alpha;
out vec4 fragColor;

void main() {
    vec2 p = gl_PointCoord - vec2(0.5);
    float d = length(p);
    float a = smoothstep(0.5, 0.1, d) * v_alpha;
    if (a < 0.01) discard;
    fragColor = vec4(v_color, a);
}
)";

const char* shadowVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 4) in mat4 a_model;
uniform mat4 u_model;
uniform float u_useInstancing;
uniform mat4 u_lightVP;
void main() {
    mat4 model = (u_useInstancing > 0.5) ? a_model : u_model;
    gl_Position = u_lightVP * model * vec4(a_pos, 1.0);
}
)";

const char* shadowFrag = R"(#version 330 core
uniform float u_depthBias;
out vec4 fragColor;
void main() {
    float d = gl_FragCoord.z - u_depthBias;
    fragColor = vec4(d, d, d, 1.0);
}
)";

const char* terrainShadowVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 3) in vec2 a_uv;
uniform mat4 u_model;
uniform mat4 u_lightVP;
uniform float u_heightScale;
uniform float u_heightOffset;
uniform sampler2D u_heightMap;
void main() {
    vec4 world = u_model * vec4(a_pos, 1.0);
    float h = texture(u_heightMap, a_uv).r;
    world.y += u_heightOffset + h * u_heightScale;
    gl_Position = u_lightVP * world;
}
)";

// Depth prepass for SSAO (cheap: position + instanced matrix only)
const char* depthVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 4) in mat4 a_model;
uniform mat4 u_model;
uniform float u_useInstancing;
uniform mat4 u_viewProj;
void main() {
    mat4 model = (u_useInstancing > 0.5) ? a_model : u_model;
    gl_Position = u_viewProj * model * vec4(a_pos, 1.0);
}
)";

const char* depthFrag = R"(#version 330 core
void main() {}
)";

const char* postVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
out vec2 v_uv;
void main() {
    v_uv = a_pos.xy * 0.5 + 0.5;
    gl_Position = vec4(a_pos.xy, 0.0, 1.0);
}
)";

const char* postFrag = R"(#version 330 core
in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_scene;
uniform float u_exposure;
uniform float u_gamma;
uniform float u_vignette;
uniform vec2 u_resolution;
uniform float u_time;

void main() {
    vec3 color = texture(u_scene, v_uv).rgb;
    // vignette
    vec2 uv = v_uv - 0.5;
    float d = length(uv);
    color *= 1.0 - u_vignette * smoothstep(0.35, 0.85, d);
    // exposure + tone map (ACES-ish approximation)
    color = 1.0 - exp(-color * u_exposure);
    // gamma
    color = pow(color, vec3(1.0 / u_gamma));
    fragColor = vec4(color, 1.0);
}
)";

const char* blurVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
out vec2 v_uv;
void main() {
    v_uv = a_pos.xy * 0.5 + 0.5;
    gl_Position = vec4(a_pos.xy, 0.0, 1.0);
}
)";

const char* blurFrag = R"(#version 330 core
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_texture;
uniform vec2 u_direction;
uniform float u_radius;

void main() {
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    for (int i = -6; i <= 6; i++) {
        float w = exp(-0.5 * float(i) * float(i) / (u_radius * u_radius));
        sum += texture(u_texture, v_uv + u_direction * float(i) * 0.0015).rgb * w;
        weights += w;
    }
    fragColor = vec4(sum / weights, 1.0);
}
)";

// Screen-space god rays / light shafts from the sun
const char* godrayFrag = R"(#version 330 core
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_scene;
uniform vec2 u_sunScreen;
uniform float u_strength;

void main() {
    vec2 sun = u_sunScreen;
    vec2 delta = v_uv - sun;
    float dist = length(delta) + 1e-5;
    vec2 stepDir = delta / dist;

    vec3 col = vec3(0.0);
    vec2 uv = v_uv;
    const int N = 20;
    for (int i = 0; i < N; i++) {
        uv -= stepDir * dist * 0.02;
        col += texture(u_scene, uv).rgb;
    }
    col /= float(N);

    vec3 scene = texture(u_scene, v_uv).rgb;
    float luma = dot(scene, vec3(0.299, 0.587, 0.114));
    float mask = smoothstep(0.55, 1.1, luma);
    fragColor = vec4(col * mask * u_strength * (1.0 - dist * 0.5), 1.0);
}
)";

// Screen-space ambient occlusion (cheap depth-only version)
const char* ssaoFrag = R"(#version 330 core
in vec2 v_uv;
out float fragColor;
uniform sampler2D u_depth;
uniform vec2 u_resolution;
uniform float u_radius;
uniform float u_power;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    float d0 = texture(u_depth, v_uv).r;
    if (d0 >= 1.0 - 1e-5) { fragColor = 1.0; return; }

    // noise rotation
    float ang = hash12(v_uv * u_resolution) * 6.28318;
    vec2 r = vec2(cos(ang), sin(ang));

    float occ = 0.0;
    const int N = 14;
    for (int i = 0; i < N; i++) {
        float t = float(i) / float(N);
        float a = t * 6.28318 * 2.0 + ang;
        float rad = u_radius * (0.3 + t * 1.4);
        vec2 dir = vec2(cos(a), sin(a)) * r.x + vec2(-sin(a), cos(a)) * r.y;
        vec2 uv2 = v_uv + dir * (rad / u_resolution);
        uv2 = clamp(uv2, 0.001, 0.999);
        float d1 = texture(u_depth, uv2).r;
        float diff = d0 - d1;   // >0 means neighbor is closer to camera
        if (diff > 0.0) {
            float w = 1.0 - length(dir) / (u_radius * 2.0);
            occ += smoothstep(0.0003, 0.02, diff) * w;
        }
    }
    occ /= float(N);
    fragColor = clamp(1.0 - occ * u_power, 0.0, 1.0);
}
)";

const char* compositeFrag = R"(#version 330 core
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform sampler2D u_godRays;
uniform float u_bloomStrength;
uniform float u_godRayStrength;
uniform float u_exposure;
uniform float u_gamma;
uniform float u_vignette;
uniform float u_time;
uniform vec2 u_resolution;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    // chromatic aberration: sample channels at slightly different offsets
    float ca = 0.0012;
    vec3 color;
    color.r = texture(u_scene, v_uv + vec2(ca, 0.0)).r;
    color.g = texture(u_scene, v_uv).g;
    color.b = texture(u_scene, v_uv - vec2(ca, 0.0)).b;

    vec3 bloom = texture(u_bloom, v_uv).rgb;
    vec3 god = texture(u_godRays, v_uv).rgb;
    color += bloom * u_bloomStrength;
    color += god * u_godRayStrength;

    // filmic ACES tonemap
    color = aces(color * u_exposure);
    color = pow(color, vec3(1.0 / u_gamma));

    // cinematic saturation boost
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(lum), color, 1.30);

    // contrast lift
    color = (color - 0.5) * 1.07 + 0.5;
    color = clamp(color, 0.0, 1.0);

    // film grain
    float g = (hash12(v_uv * u_resolution + fract(u_time)) - 0.5);
    color += g * 0.014;

    // vignette
    vec2 uv = v_uv - 0.5;
    color *= 1.0 - u_vignette * smoothstep(0.35, 0.9, length(uv));

    fragColor = vec4(color, 1.0);
}
)";

const char* textVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 3) in vec2 a_uv;
uniform mat4 u_proj;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = u_proj * vec4(a_pos, 1.0);
}
)";

const char* textFrag = R"(#version 330 core
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_font;
uniform vec4 u_color;
void main() {
    float a = texture(u_font, v_uv).a;
    fragColor = vec4(u_color.rgb, u_color.a * a);
}
)";

const char* hudRectFrag = R"(#version 330 core
out vec4 fragColor;
uniform vec4 u_color;
void main() {
    fragColor = u_color;
}
)";

const char* debugVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_viewProj;
void main() {
    gl_Position = u_viewProj * vec4(a_pos, 1.0);
}
)";

const char* debugFrag = R"(#version 330 core
uniform vec4 u_color;
out vec4 fragColor;
void main() {
    fragColor = u_color;
}
)";

}
}
