#include "FrostEngine/Renderer/LightingSystem.h"

namespace Frost {

Mat4 LightingSystem::computeCascadeViewProj(
    u32 /*cascadeIndex*/, const Vec3& lightDir,
    f32 splitNear, f32 splitFar,
    const Mat4& cameraView, const Mat4& cameraProj)
{
    Mat4 invViewProj = (cameraProj * cameraView).inverse();
    Mat4 invView = cameraView.inverse();

    Vec4 clipCorners[8] = {
        Vec4(-1, -1, -1, 1), Vec4( 1, -1, -1, 1),
        Vec4( 1,  1, -1, 1), Vec4(-1,  1, -1, 1),
        Vec4(-1, -1,  1, 1), Vec4( 1, -1,  1, 1),
        Vec4( 1,  1,  1, 1), Vec4(-1,  1,  1, 1),
    };

    Vec3 worldCorners[8];
    for (u32 i = 0; i < 8; i++) {
        Vec4 c = invViewProj * clipCorners[i];
        worldCorners[i] = Vec3(c.x / c.w, c.y / c.w, c.z / c.w);
    }

    Vec3 viewCorners[8];
    for (u32 i = 0; i < 8; i++) {
        Vec4 vc = cameraView * Vec4(worldCorners[i], 1.0f);
        viewCorners[i] = Vec3(vc.x, vc.y, vc.z);
    }

    Vec3 splitWorldCorners[8];
    for (u32 i = 0; i < 4; i++) {
        Vec3 dir = viewCorners[i].normalized();
        Vec4 wcNear = invView * Vec4(dir * splitNear, 1.0f);
        Vec4 wcFar = invView * Vec4(dir * splitFar, 1.0f);
        splitWorldCorners[i] = Vec3(wcNear.x, wcNear.y, wcNear.z);
        splitWorldCorners[i + 4] = Vec3(wcFar.x, wcFar.y, wcFar.z);
    }

    Vec3 center(0, 0, 0);
    for (u32 i = 0; i < 8; i++) {
        center = center + splitWorldCorners[i];
    }
    center = center / 8.0f;

    f32 radius = 0.0f;
    for (u32 i = 0; i < 8; i++) {
        f32 d = (splitWorldCorners[i] - center).length();
        if (d > radius) radius = d;
    }
    radius = std::ceil(radius * 16.0f) / 16.0f;

    Vec3 lightPos = center - lightDir * radius;
    Vec3 up(0, 1, 0);
    if (Mathf::abs(lightDir.dot(up)) > 0.99f) {
        up = Vec3(0, 0, -1);
    }
    Mat4 lightView = Mat4::lookAt(lightPos, center, up);

    f32 l = -radius;
    f32 r = radius;
    f32 b = -radius;
    f32 t = radius;
    f32 n = -radius * 2.0f;
    f32 f = radius * 2.0f;

    Mat4 lightProj = {};
    lightProj.m[0] = 2.0f / (r - l);
    lightProj.m[5] = 2.0f / (t - b);
    lightProj.m[10] = -2.0f / (f - n);
    lightProj.m[12] = -(r + l) / (r - l);
    lightProj.m[13] = -(t + b) / (t - b);
    lightProj.m[14] = -(f + n) / (f - n);
    lightProj.m[15] = 1.0f;

    return lightProj * lightView;
}

Vector<u32> LightingSystem::testLightVisibility(const Vec3& pos, f32 radius) const {
    Vector<u32> visible;
    for (u32 i = 0; i < lightCount_; i++) {
        if (!lights_[i].enabled) continue;
        const LightData& l = lights_[i];
        switch (l.type) {
            case LightType::Point: {
                f32 dist = (l.position - pos).length();
                if (dist <= radius + l.range) {
                    visible.push_back(i);
                }
                break;
            }
            case LightType::Spot: {
                f32 dist = (l.position - pos).length();
                if (dist <= radius + l.range) {
                    visible.push_back(i);
                }
                break;
            }
            case LightType::Directional: {
                visible.push_back(i);
                break;
            }
            default: break;
        }
    }
    return visible;
}

} // namespace Frost
