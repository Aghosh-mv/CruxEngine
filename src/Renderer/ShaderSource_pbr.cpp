#include "Renderer/ShaderSource.h"

namespace Crux {
namespace ShaderSource {

const char* pbrVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
layout(location = 3) in vec2 a_uv;
layout(location = 4) in mat4 a_model;   // instanced matrix

uniform mat4 u_model;
uniform mat4 u_viewProj;
uniform float u_useInstancing;

out vec3 v_worldPos;
out vec3 v_normal;
out vec4 v_tangent;
out vec2 v_uv;

void main() {
    mat4 model = (u_useInstancing > 0.5) ? a_model : u_model;
    vec4 world = model * vec4(a_pos, 1.0);
    v_worldPos = world.xyz;
    mat3 normalMat = mat3(transpose(inverse(model)));
    v_normal = normalMat * a_normal;
    v_tangent = vec4(normalMat * a_tangent.xyz, a_tangent.w);
    v_uv = a_uv;
    gl_Position = u_viewProj * world;
}
)";

const char* pbrFrag = R"(#version 330 core
in vec3 v_worldPos;
in vec3 v_normal;
in vec4 v_tangent;
in vec2 v_uv;

out vec4 fragColor;

uniform vec3 u_camPos;
uniform float u_time;
uniform mat4 u_viewProj;

// Sun (two cascaded shadow maps)
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

// SSAO
uniform sampler2D u_aoTex;
uniform float u_aoEnabled;

// Ambient
uniform vec3 u_ambientSky;
uniform vec3 u_ambientGround;
uniform float u_ambientIntensity;

// Point lights
struct PointLight { vec3 position; vec3 color; float intensity; float range; vec4 pad; };
uniform int u_pointLightCount;
uniform PointLight u_pointLights[16];

uniform float u_fogDensity;
uniform vec3 u_fogColor;

// Material
uniform vec3 u_baseColor;
uniform float u_metallic;
uniform float u_roughness;
uniform float u_ao;
uniform vec3 u_emission;
uniform float u_emissionStrength;
uniform float u_normalStrength;
uniform float u_unlit;
uniform float u_opacity;
uniform float u_toon;
uniform sampler2D u_albedoMap;
uniform sampler2D u_normalMap;
uniform sampler2D u_metalMap;
uniform sampler2D u_roughMap;
uniform sampler2D u_aoMap;
uniform float u_hasAlbedo;
uniform float u_hasNormal;
uniform float u_hasMetal;
uniform float u_hasRough;
uniform float u_hasAO;

// Reflection pass clip plane
uniform vec4 u_clipPlane;
uniform float u_clipEnabled;

const float PI = 3.14159265358979;

vec3 getNormal() {
    if (u_hasNormal > 0.5) {
        vec3 n = texture(u_normalMap, v_uv).xyz * 2.0 - 1.0;
        n.xy *= u_normalStrength;
        n = normalize(n);
        vec3 T = normalize(v_tangent.xyz);
        vec3 N = normalize(v_normal);
        T = normalize(T - dot(T, N) * N);
        vec3 B = cross(N, T) * v_tangent.w;
        return normalize(mat3(T, B, N) * n);
    }
    return normalize(v_normal);
}

float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float sampleShadow(sampler2D map, mat4 vp, vec3 worldPos, vec3 N, vec3 lightDir) {
    vec4 shadowCoord = vp * vec4(worldPos, 1.0);
    vec3 proj = shadowCoord.xyz / shadowCoord.w;
    proj = proj * 0.5 + 0.5;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0) return 1.0;
    float bias = max(0.0004 * (1.0 - dot(N, lightDir)), 0.00015);
    float shadow = 0.0;
    float texel = u_shadowTexel;
    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            vec2 offset = vec2(float(x), float(y)) * texel * 1.5;
            float depth = texture(map, proj.xy + offset).r;
            shadow += (proj.z - bias > depth) ? 1.0 : 0.0;
        }
    }
    shadow /= 25.0;
    return 1.0 - shadow;
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

    vec3 baseColor = u_baseColor;
    if (u_hasAlbedo > 0.5) baseColor *= texture(u_albedoMap, v_uv).rgb;
    float metallic = u_metallic;
    if (u_hasMetal > 0.5) metallic *= texture(u_metalMap, v_uv).r;
    float roughness = clamp(u_roughness, 0.02, 1.0);
    if (u_hasRough > 0.5) roughness = clamp(roughness * texture(u_roughMap, v_uv).g, 0.02, 1.0);
    float ao = u_ao;
    if (u_hasAO > 0.5) ao *= texture(u_aoMap, v_uv).r;

    // screen-space ambient occlusion
    if (u_aoEnabled > 0.5) {
        vec4 sc = u_viewProj * vec4(v_worldPos, 1.0);
        if (sc.w > 0.0) {
            vec2 uv = clamp(sc.xy / sc.w * 0.5 + 0.5, 0.0, 1.0);
            float aoScene = texture(u_aoTex, uv).r;
            ao *= mix(1.0, aoScene, 0.8);
        }
    }

    vec3 N = getNormal();
    vec3 V = normalize(u_camPos - v_worldPos);
    vec3 L = normalize(-u_sunDir);
    vec3 R = reflect(-V, N);

    vec3 F0 = mix(vec3(0.04), baseColor, metallic);

    // --- direct sun
    vec3 radiance = u_sunColor * u_sunIntensity;
    float NdotL = max(dot(N, L), 0.0);
    vec3 Hv = normalize(V + L);
    float NdotH = max(dot(N, Hv), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float D = distributionGGX(NdotH, roughness);
    vec3 F = fresnelSchlick(max(dot(Hv, V), 0.0), F0);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 spec = (D * F * G) / max(4.0 * NdotV * NdotL, 0.0001);
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kd * baseColor / PI;
    float viewDist = distance(u_camPos, v_worldPos);
    float shadow = shadowFactor(v_worldPos, N, L, viewDist);
    vec3 direct = (diffuse + spec) * radiance * NdotL * shadow;

    // --- point lights
    vec3 pointContrib = vec3(0.0);
    for (int i = 0; i < u_pointLightCount; i++) {
        vec3 pl = u_pointLights[i].position;
        vec3 lc = u_pointLights[i].color * u_pointLights[i].intensity;
        vec3 toLight = pl - v_worldPos;
        float dist = length(toLight);
        float range = u_pointLights[i].range;
        float atten = 1.0 / (1.0 + dist * dist);
        atten *= smoothstep(range, range * 0.2, dist);
        vec3 Li = normalize(toLight);
        float ndotl = max(dot(N, Li), 0.0);
        vec3 hv = normalize(V + Li);
        float ndoth = max(dot(N, hv), 0.0);
        float Dp = distributionGGX(ndoth, roughness);
        vec3 Fp = fresnelSchlick(max(dot(hv, V), 0.0), F0);
        float Gp = geometrySmith(NdotV, ndotl, roughness);
        vec3 specp = (Dp * Fp * Gp) / max(4.0 * NdotV * ndotl, 0.0001);
        vec3 kdp = (1.0 - Fp) * (1.0 - metallic);
        pointContrib += (kdp * baseColor / PI + specp) * lc * ndotl * atten;
    }

    // --- ambient (hemisphere + emissive glow approximation)
    vec3 skyAmb = mix(u_ambientGround, u_ambientSky, N.y * 0.5 + 0.5) * u_ambientIntensity;
    vec3 ambient = skyAmb * baseColor * ao;

    vec3 color = ambient + direct + pointContrib;

    // --- cel / anime shading for stylized characters
    if (u_toon > 0.5) {
        float bandNdotL = floor(max(dot(N, L), 0.0) * 4.0 + 0.001) / 4.0;
        direct = (diffuse + spec * 0.25) * radiance * bandNdotL * shadow;
        // strong rim light
        float rim = pow(1.0 - NdotV, 3.0);
        direct += u_sunColor * rim * 0.35;
        vec3 bandAmb = skyAmb * baseColor * ao;
        color = bandAmb + direct;
        for (int i = 0; i < u_pointLightCount && i < 4; i++) {
            vec3 pl = u_pointLights[i].position;
            vec3 lc = u_pointLights[i].color * u_pointLights[i].intensity;
            vec3 toLight = pl - v_worldPos;
            float dist = length(toLight);
            float range = u_pointLights[i].range;
            float atten = 1.0 / (1.0 + dist * dist) * smoothstep(range, range * 0.2, dist);
            vec3 Li = normalize(toLight);
            float ndl = floor(max(dot(N, Li), 0.0) * 3.0 + 0.001) / 3.0;
            color += baseColor * lc * ndl * atten;
        }
    }

    // --- emission
    color += u_emission * u_emissionStrength;

    // --- fog
    float dist = viewDist;
    float fogFactor = 1.0 - exp(-u_fogDensity * u_fogDensity * dist * dist);
    color = mix(color, u_fogColor, clamp(fogFactor, 0.0, 1.0));

    if (u_unlit > 0.5) color = baseColor;

    fragColor = vec4(color, u_opacity);
}
)";

}
}
