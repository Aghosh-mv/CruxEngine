#version 450

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_sampler_filter_linear : enable

#include "shared.h"

layout(set = 0, binding = 0) uniform CameraBlock { Camera camera; } camData;
layout(set = 0, binding = 1) uniform LightsBlock {
    Light lights[MAX_LIGHTS];
    LightIndexData lightIndices;
} lightData;

layout(set = 0, binding = 2) uniform texture2D t_albedo;
layout(set = 0, binding = 3) uniform texture2D t_normal;
layout(set = 0, binding = 4) uniform texture2D t_metallicRoughness;
layout(set = 0, binding = 5) uniform texture2D t_emissive;
layout(set = 0, binding = 6) uniform texture2D t_occlusion;
layout(set = 0, binding = 7) uniform texture2D t_depth;
layout(set = 0, binding = 8) uniform texture2D t_velocity;
layout(set = 0, binding = 9) uniform texture2D t_lighting;
layout(set = 0, binding = 10) uniform texture2D t_indirect;
layout(set = 0, binding = 11) uniform texture2D t_skybox;
layout(set = 0, binding = 12) uniform texture2D t_ssrtemp;
layout(set = 0, binding = 13) uniform texture2DArray t_gbuffer;
layout(set = 0, binding = 14) uniform 2DTexture t_shadowAtlas;
layout(set = 0, binding = 15) uniform 2DTexture t_ao;
layout(set = 0, binding = 16) uniform 2DTexture t_rtao;
layout(set = 0, binding = 17) uniform 2DTexture t_distortion;

layout(set = 0, binding = 18) uniform sampler s_Nearest;
layout(set = 0, binding = 19) uniform sampler s_Linear;
layout(set = 0, binding = 20) uniform sampler s_LinearClamp;
layout(set = 0, binding = 21) uniform sampler s_ShadowPCF;

layout(set = 1, binding = 0) uniform accelerationStructureEXT t_accel;
layout(set = 1, binding = 1) uniform Buffer { mat4 world; } instanceBuffer[];
layout(set = 1, binding = 2) uniform Buffer { mat4 prevWorld; } prevInstanceBuffer[];

struct VertexOutput {
    vec4 position;
    vec4 worldPosition;
    vec3 worldNormal;
    vec3 worldTangent;
    vec3 worldBinormal;
    vec2 uv;
    vec4 color;
    vec2 screenUV;
    uint materialID;
    flat uint instanceID;
};

layout(location = 0) in VertexOutput fs_in;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragNormal;
layout(location = 2) out vec4 fragPosition;
layout(location = 3) out vec4 fragVelocity;

layout(const_id = 0) const uint SHADING_MODEL = 0;
layout(const_id = 1) const uint FLAGS = 0;

#define MATERIAL_FLAG_USE_NORMAL_MAP    (1 << 0)
#define MATERIAL_FLAG_USE_EMIT          (1 << 1)
#define MATERIAL_FLAG_USE_OCCLUSION     (1 << 2)
#define MATERIAL_FLAG_TRANSPARENT        (1 << 3)
#define MATERIAL_FLAG_TWOSIDED         (1 << 4)
#define MATERIAL_FLAG_ALPHACLIP        (1 << 5)

vec3 getWorldPosition(vec2 screenUV) {
    float depth = textureLod(t_depth, screenUV, 0.0).r;
    vec4 ndc = vec4(screenUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = camData.camera.invProj * ndc;
    viewPos = viewPos / viewPos.w;
    vec4 worldPos = camData.camera.invView * viewPos;
    return worldPos.xyz;
}

vec3 getDeferredPosition(vec2 screenUV) {
    vec4 gbuffer0 = textureLod(t_gbuffer, vec3(screenUV, 0.0), 0.0);
    vec4 gbuffer1 = textureLod(t_gbuffer, vec3(screenUV, 1.0), 0.0);
    float depth = gbuffer0.w;
    vec4 ndc = vec4(screenUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = camData.camera.invProj * ndc;
    viewPos = viewPos / viewPos.w;
    vec4 worldPos = camData.camera.invView * viewPos;
    return worldPos.xyz;
}

vec3 getDeferredNormal(vec2 screenUV) {
    vec4 gbuffer1 = textureLod(t_gbuffer, vec3(screenUV, 1.0), .0);
    vec3 N = decodeNormal(gbuffer1.xy);
    return normalize(N * 2.0 - 1.0);
}

void evaluateLights(inout vec3 color, inout vec3 emissive, inout vec3 normal, 
                    inout vec3 worldPos, inout vec3 V, Material mat) {
    
    vec3 Lo = vec3(0.0);
    vec3 specularRadiance = vec3(0.0);
    
    vec3 F0 = vec3(0.16);
    F0 = mix(F0, mat.baseColor.rgb, mat.metallic);
    
    for(uint i = 0; i < MAX_LIGHTS; i++) {
        Light light = lightData.lights[i];
        if(light.type == 0) continue;
        
        vec3 L = vec3(0.0);
        float attenuation = 1.0;
        
        if(light.type == 1) {
            vec3 lightPos = light.position.xyz;
            vec3 toLight = lightPos - worldPos;
            float dist = length(toLight);
            L = toLight / dist;
            attenuation = punctualLightAttenuation(dist, light.radius);
        } else if(light.type == 2) {
            vec3 lightDir = -light.direction.xyz;
            L = lightDir;
            attenuation = 1.0;
        }
        
        vec3 radiance = light.color.rgb * light.intensity * light.temperature / 100.0;
        
        float NdotL = max(dot(normal, L), 0.0);
        
        float D = distributionGGX(normal, normalize(L + V), mat.roughness);
        float G = geometrySmith(normal, V, L, mat.roughness);
        vec3 F = fresnelSchlick(max(dot(V, normalize(L + V)), 0.0), F0);
        
        vec3 numerator = D * G * F;
        float denominator = 4.0 * max(dot(normal, V), 0.0) * NdotL + 0.0001;
        vec3 specular = numerator / denominator;
        
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - mat.metallic);
        
        Lo += (kD * mat.baseColor.rgb / PI + specular) * radiance * NdotL * attenuation;
    }
    
    color += Lo;
    emissive += emissive;
}

vec3 evaluateSunLight(vec3 N, vec3 V, vec3 worldPos, Material mat) {
    vec3 sunDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 sunColor = vec3(1.0, 0.98, 0.95) * 3.0;
    
    vec3 L = sunDir;
    vec3 H = normalize(V + L);
    
    float NdotL = max(dot(N, L), 0.0);
    
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, mat.baseColor.rgb, mat.metallic);
    
    float D = distributionGGX(N, H, mat.roughness);
    float G = geometrySmith(N, V, L, mat.roughness);
    vec3 F = fresnelSchlick(max(dot(V, H), 0.0), F0);
    
    vec3 numerator = D * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
    vec3 specular = numerator / denominator;
    
    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - mat.metallic);
    
    vec3 diffuse = kD * mat.baseColor.rgb / PI;
    vec3 Lo = (diffuse + specular) * sunColor * NdotL;
    
    return Lo;
}

vec3 evaluateSkyLight(vec3 N, vec3 worldPos, vec3 V) {
    vec3 skyColor = vec3(0.5, 0.7, 1.0) * 0.2;
    vec3 groundColor = vec3(0.03, 0.03, 0.03);
    
    float skyWeight = 0.5 * (N.y + 1.0);
    vec3 ambient = mix(groundColor, skyColor, skyWeight);
    
    return ambient;
}

vec3 evaluateGlobalIllumination(vec3 N, vec3 V, vec3 worldPos, Material mat) {
    vec3 indirectDiffuse = textureLod(t_indirect, fs_in.screenUV, 0.0).rgb;
    vec3 indirectSpecular = textureLod(t_lighting, fs_in.screenUV, 0.0).rgb;
    
    vec3 irradiance = indirectDiffuse;
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, mat.baseColor.rgb, mat.metallic);
    
    vec3 diffuse = mat.baseColor.rgb / PI;
    vec3 specular = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, mat.roughness) * indirectSpecular;
    
    vec3 gi = diffuse * irradiance;
    gi *= (1.0 - mat.metallic);
    gi += specular;
    
    return gi;
}

vec3 calculateRayTracedShadows(vec3 N, vec3 worldPos) {
    rayQueryEXT rayQuery;
    rayQueryInitializeEXT(rayQuery, t_accel, gl_RayFlagsOpaqueEXT, 0xFF, worldPos, 0.001,
                         normalize(vec3(0.5, 1.0, 0.3)), 1000.0);
    
    while(rayQueryProceedEXT(rayQuery)) {}
    
    if(rayQueryGetIntersectionTypeEXT(rayQuery, false) == RayQueryCandidateIntersectionKHR) {
        return vec3(1.0);
    }
    return vec3(0.0);
}

void main() {
    Material mat;
    mat.baseColor = fs_in.color;
    mat.metallic = 0.0;
    mat.roughness = 0.5;
    mat.emissive = vec3(0.0);
    mat.emissiveIntensity = 0.0;
    mat.flags = FLAGS;
    
    vec4 albedoTex = texture(sampler2D(t_albedo, s_Linear), fs_in.uv);
    if((FLAGS & MATERIAL_FLAG_ALPHACLIP) != 0 && albedoTex.a < 0.5) {
        discard;
    }
    
    mat.baseColor *= albedoTex;
    
    vec4 mrTex = texture(sampler2D(t_metallicRoughness, s_Linear), fs_in.uv);
    mat.metallic = mrTex.b;
    mat.roughness = mrTex.g;
    
    vec3 N = normalize(fs_in.worldNormal);
    if((FLAGS & MATERIAL_FLAG_USE_NORMAL_MAP) != 0) {
        vec3 normalTex = texture(sampler2D(t_normal, s_Linear), fs_in.uv).rgb * 2.0 - 1.0;
        N = perturbNormal(N, fs_in.worldPosition.xyz, fs_in.uv, normalTex);
    }
    
    Material mat2 = mat;
    mat2.baseColor.rgb = sRGBToLinear(mat2.baseColor.rgb);
    
    vec3 V = normalize(camData.camera.position - fs_in.worldPosition.xyz);
    
    vec3 Lo = evaluateSunLight(N, V, fs_in.worldPosition.xyz, mat2);
    Lo += evaluateSkyLight(N, fs_in.worldPosition.xyz, V);
    Lo += evaluateGlobalIllumination(N, V, fs_in.worldPosition.xyz, mat2);
    
    if((FLAGS & MATERIAL_FLAG_USE_EMIT) != 0) {
        vec3 emitTex = texture(sampler2D(t_emissive, s_Linear), fs_in.uv).rgb;
        mat2.emissive += emitTex * 10.0;
    }
    
    vec3 color = mat2.baseColor.rgb * Lo;
    color += mat2.emissive;
    color += mat2.emissiveIntensity;
    
    vec3 oet_final = color;
    
    fragColor = vec4(color, mat2.baseColor.a);
    fragNormal = vec4(encodeNormal(N), 0.0, 1.0);
    fragPosition = vec4(fs_in.worldPosition.xyz, gl_FragCoord.z);
    fragVelocity = vec4(0.0, 0.0, 0.0, 1.0);
}