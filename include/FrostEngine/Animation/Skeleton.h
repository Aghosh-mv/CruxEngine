#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Math.h"

namespace Frost {

struct Bone {
    i32 parent = -1;
    String name;
    Vec3 position{0, 0, 0};
    Quat rotation{Quat::identity()};
    Vec3 scale{1, 1, 1};
    Mat4 localTransform{Mat4::identity()};
    Mat4 inverseBindPose{Mat4::identity()};
    Mat4 worldTransform{Mat4::identity()};
    Mat4 animatedLocal{Mat4::identity()};

    Mat4 computeLocalTransform() const {
        return Mat4::translation(position) * Mat4::rotation(rotation) * Mat4::scaling(scale);
    }

    Vec3 getWorldPosition() const {
        Vec4 wp = worldTransform * Vec4(0, 0, 0, 1);
        return Vec3(wp.x, wp.y, wp.z);
    }

    Quat getWorldRotation() const {
        Mat3 rotMat;
        rotMat.m[0] = worldTransform.m[0]; rotMat.m[1] = worldTransform.m[1]; rotMat.m[2] = worldTransform.m[2];
        rotMat.m[3] = worldTransform.m[4]; rotMat.m[4] = worldTransform.m[5]; rotMat.m[5] = worldTransform.m[6];
        rotMat.m[6] = worldTransform.m[8]; rotMat.m[7] = worldTransform.m[9]; rotMat.m[8] = worldTransform.m[10];
        return Quat::fromMat3(rotMat);
    }
};

class Skeleton {
public:
    static constexpr u32 MAX_BONES = 256;

    Skeleton() = default;
    ~Skeleton() = default;

    i32 addBone(const String& name, i32 parentIndex = -1);
    void setBoneTransform(i32 boneIndex, const Vec3& pos, const Quat& rot, const Vec3& scale);
    i32 findBone(const char* name) const;
    void computeWorldTransforms();
    void reset();

    u32 boneCount() const { return (u32)bones_.size(); }
    Bone& bone(i32 index) { return bones_[index]; }
    const Bone& bone(i32 index) const { return bones_[index]; }
    Vector<Bone>& bones() { return bones_; }
    const Vector<Bone>& bones() const { return bones_; }

    void setRootMotion(bool enabled) { rootMotionEnabled_ = enabled; }
    bool rootMotionEnabled() const { return rootMotionEnabled_; }
    Vec3 rootMotionDelta() const { return rootMotionDelta_; }
    Quat rootMotionRotationDelta() const { return rootMotionRotDelta_; }
    void resetRootMotion() { rootMotionDelta_ = Vec3(0); rootMotionRotDelta_ = Quat::identity(); }
    void setRootMotionDelta(const Vec3& delta) { rootMotionDelta_ = delta; }
    void setRootMotionRotDelta(const Quat& rot) { rootMotionRotDelta_ = rot; }

    void setRootBone(i32 index) { rootBoneIndex_ = index; }
    i32 rootBoneIndex() const { return rootBoneIndex_; }

    Mat4 getBoneMatrix(i32 index) const;
    Mat4 getBonePaletteMatrix(i32 index) const;
    Mat4 getBoneLocalMatrix(i32 index) const;

    void getBonePositions(Vec3* positions) const;
    void getBoneRotations(Quat* rotations) const;
    void getBoneScales(Vec3* scales) const;

    void setBonePositions(const Vec3* positions);
    void setBoneRotations(const Quat* rotations);

    void computeBindPose();
    void setInverseBindPose(i32 boneIndex, const Mat4& ibp);

private:
    Vector<Bone> bones_;
    i32 rootBoneIndex_ = 0;
    bool rootMotionEnabled_ = false;
    Vec3 rootMotionDelta_{0, 0, 0};
    Quat rootMotionRotDelta_{Quat::identity()};
};

}
