#version 450

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_debug_printf : enable

#include "shared.h"

layout(location = 0) in vec3 i_position;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec2 i_uv;
layout(location = 3) in vec4 i_tangent;
layout(location = 4) in vec4 i_color;
layout(location = 5) in vec4 i_jointIndices;
layout(location = 6) in vec4 i_jointWeights;

layout(push_constant) uniform PushConstants {
    mat4 world;
    mat4 prevWorld;
    vec4 customData;
    uint flags;
} pc;

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

layout(location = 0) out VertexOutput vs_out;

void main() {
    vec3 position = i_position;
    vec3 normal = i_normal;
    vec2 uv = i_uv;
    vec4 tangent = i_tangent;
    vec4 color = i_color;
    
    vec4 worldPos = pc.world * vec4(position, 1.0);
    vec4 prevWorldPos = pc.prevWorld * vec4(position, 1.0);
    
    vec3 T = vec3(pc.world * vec4(tangent.xyz, 0.0));
    float handedness = tangent.w;
    vec3 B = cross(normal, T) * handedness;
    
    vs_out.position = vec4(position, 1.0);
    vs_out.worldPosition = worldPos;
    vs_out.worldNormal = normalize(mat3(pc.world) * normal);
    vs_out.worldTangent = normalize(T);
    vs_out.worldBinormal = normalize(B);
    vs_out.uv = uv;
    vs_out.color = color;
    vs_out.screenUV = (worldPos.xy / worldPos.w) * 0.5 + 0.5;
    vs_out.materialID = 0;
    vs_out.instanceID = gl_InstanceID;
    
    gl_Position = worldPos;
}