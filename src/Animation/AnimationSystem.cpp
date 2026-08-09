#include "Animation/AnimationSystem.h"
#include "Core/Log.h"
#include <cstring>
#include <cmath>

namespace Frost {

AnimationSystem::AnimationSystem() {
    memset(skeletons_, 0, sizeof(skeletons_));
}

AnimationSystem::~AnimationSystem() { shutdown(); }

bool AnimationSystem::init() {
    FROST_LOG_INFO("[AnimationSystem] Initialized");
    return true;
}

void AnimationSystem::shutdown() {
    skeletonCount_ = 0;
    clips_.clear();
    clipCount_ = 0;
    FROST_LOG_INFO("[AnimationSystem] Shutdown");
}

void AnimationSystem::update(f32 dt) {
    time_ += dt;

    for (u32 i = 0; i < skeletonCount_; i++) {
        SkeletonEntry& entry = skeletons_[i];
        if (!entry.active) continue;

        if (entry.primaryState.status == AnimationState::Status::Playing) {
            f32 prevTime = entry.primaryState.currentTime;
            entry.primaryState.currentTime += entry.primaryState.speed * dt;

            if (entry.primaryState.clip) {
                if (entry.primaryState.currentTime >= entry.primaryState.clip->getDuration()) {
                    if (entry.primaryState.loop) {
                        entry.primaryState.currentTime =
                            std::fmod(entry.primaryState.currentTime, entry.primaryState.clip->getDuration());
                        entry.primaryState.clip->resetEvents();
                    } else {
                        entry.primaryState.currentTime = entry.primaryState.clip->getDuration();
                        entry.primaryState.status = AnimationState::Status::Stopped;
                    }
                }

                if (entry.primaryState.clip->hasEvents()) {
                    for (auto& evt : entry.primaryState.clip->events) {
                        if (!evt.fired && prevTime < evt.time && entry.primaryState.currentTime >= evt.time) {
                            evt.fired = true;
                        }
                    }
                }

                if (entry.skeleton.rootMotionEnabled()) {
                    extractRootMotionDelta(entry.skeleton, *entry.primaryState.clip, prevTime, entry.primaryState.currentTime);
                }
            }
        }

        for (auto& bs : entry.blendStates) {
            if (bs.status == AnimationState::Status::Playing && bs.clip) {
                bs.currentTime += bs.speed * dt;
                if (bs.currentTime >= bs.clip->getDuration()) {
                    if (bs.loop) {
                        bs.currentTime = std::fmod(bs.currentTime, bs.clip->getDuration());
                    } else {
                        bs.currentTime = bs.clip->getDuration();
                        bs.status = AnimationState::Status::Stopped;
                    }
                }
                if (bs.blendDuration > 0.01f) {
                    bs.blendTime += dt;
                    bs.weight = Mathf::saturate(bs.blendTime / bs.blendDuration);
                }
            }
        }

        entry.stateMachine.update(dt);
        entry.stateMachine.stateTime += dt;

        evaluatePose(i);
        solveIK(i);
    }
}

u32 AnimationSystem::createSkeleton() {
    if (skeletonCount_ >= MAX_SKELETONS) {
        FROST_LOG_WARN("[AnimationSystem] Skeleton pool exhausted");
        return 0;
    }
    u32 id = nextSkeletonId_++;
    skeletons_[skeletonCount_].id = id;
    skeletons_[skeletonCount_].active = true;
    skeletonCount_++;
    return id;
}

void AnimationSystem::destroySkeleton(u32 skeletonId) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            skeletons_[i].active = false;
            if (i != skeletonCount_ - 1) {
                skeletons_[i] = skeletons_[skeletonCount_ - 1];
            }
            skeletonCount_--;
            return;
        }
    }
}

Skeleton* AnimationSystem::getSkeleton(u32 skeletonId) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) return &skeletons_[i].skeleton;
    }
    return nullptr;
}

u32 AnimationSystem::loadClip(const AnimationClip& clip) {
    if (clipCount_ >= MAX_CLIPS) {
        FROST_LOG_WARN("[AnimationSystem] Clip pool exhausted");
        return 0;
    }
    clips_.pushBack(clip);
    return clipCount_++;
}

void AnimationSystem::freeClip(u32 clipId) {
    if (clipId < clips_.size()) {
        clips_[clipId] = AnimationClip{};
    }
}

AnimationClip* AnimationSystem::getClip(u32 clipId) {
    if (clipId < clips_.size()) return &clips_[clipId];
    return nullptr;
}

AnimationState& AnimationSystem::play(u32 skeletonId, u32 clipId, f32 blendDuration) {
    static AnimationState emptyState;
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            SkeletonEntry& entry = skeletons_[i];
            AnimationClip* clip = getClip(clipId);
            if (!clip) return emptyState;

            if (entry.primaryState.status == AnimationState::Status::Playing &&
                entry.primaryState.clip && blendDuration > 0.01f) {
                AnimationState bs = entry.primaryState;
                bs.blendTime = 0.0f;
                bs.blendDuration = blendDuration;
                bs.weight = 1.0f;
                entry.blendStates.pushBack(bs);
                if (entry.blendStates.size() > 4) {
                    entry.blendStates.erase(0);
                }
            }

            entry.primaryState.status = AnimationState::Status::Playing;
            entry.primaryState.clip = clip;
            entry.primaryState.currentTime = 0.0f;
            entry.primaryState.speed = 1.0f;
            entry.primaryState.weight = 1.0f;
            entry.primaryState.loop = clip->looping;
            entry.primaryState.blendDuration = blendDuration;
            entry.primaryState.blendTime = 0.0f;

            return entry.primaryState;
        }
    }
    return emptyState;
}

void AnimationSystem::pause(u32 skeletonId) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            skeletons_[i].primaryState.status = AnimationState::Status::Paused;
            return;
        }
    }
}

void AnimationSystem::resume(u32 skeletonId) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            skeletons_[i].primaryState.status = AnimationState::Status::Playing;
            return;
        }
    }
}

void AnimationSystem::stop(u32 skeletonId) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            skeletons_[i].primaryState.status = AnimationState::Status::Stopped;
            skeletons_[i].primaryState.currentTime = 0.0f;
            skeletons_[i].blendStates.clear();
            return;
        }
    }
}

void AnimationSystem::setSpeed(u32 skeletonId, f32 speed) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            skeletons_[i].primaryState.speed = speed;
            return;
        }
    }
}

void AnimationSystem::setWeight(u32 skeletonId, f32 weight) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            skeletons_[i].primaryState.weight = weight;
            return;
        }
    }
}

void AnimationSystem::blend(u32 skeletonId, u32 clipId, f32 targetWeight, f32 blendDuration) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            SkeletonEntry& entry = skeletons_[i];
            AnimationClip* clip = getClip(clipId);
            if (!clip) return;
            AnimationState bs;
            bs.status = AnimationState::Status::Playing;
            bs.clip = clip;
            bs.currentTime = 0.0f;
            bs.weight = 0.0f;
            bs.speed = 1.0f;
            bs.loop = clip->looping;
            bs.blendDuration = blendDuration;
            bs.blendTime = 0.0f;
            entry.blendStates.pushBack(bs);
            if (entry.blendStates.size() > 4) {
                entry.blendStates.erase(0);
            }
            return;
        }
    }
}

void AnimationSystem::crossFade(u32 skeletonId, u32 clipId, f32 fadeDuration) {
    blend(skeletonId, clipId, 1.0f, fadeDuration);
}

void AnimationSystem::addLayer(u32 skeletonId, const AnimationLayer& layer) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            skeletons_[i].layers.pushBack(layer);
            return;
        }
    }
}

void AnimationSystem::setLayerWeight(u32 skeletonId, u32 layerIndex, f32 weight) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId && layerIndex < skeletons_[i].layers.size()) {
            skeletons_[i].layers[layerIndex].weight = weight;
            return;
        }
    }
}

void AnimationSystem::setLayerBlendMode(u32 skeletonId, u32 layerIndex, AnimationBlendMode mode) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId && layerIndex < skeletons_[i].layers.size()) {
            skeletons_[i].layers[layerIndex].blendMode = mode;
            return;
        }
    }
}

void AnimationSystem::setBoneMask(u32 skeletonId, u32 layerIndex, i32 bone, bool enabled) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId && layerIndex < skeletons_[i].layers.size()) {
            skeletons_[i].layers[layerIndex].setBone(bone, enabled);
            return;
        }
    }
}

void AnimationSystem::addIKChain(u32 skeletonId, const IKChain& chain) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            skeletons_[i].ikChains.pushBack(chain);
            return;
        }
    }
}

void AnimationSystem::addTwoBoneIK(u32 skeletonId, const IKTwoBoneSolver& solver) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            skeletons_[i].twoBoneIK.pushBack(solver);
            return;
        }
    }
}

void AnimationSystem::addLookAt(u32 skeletonId, const LookAtConstraint& constraint) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            skeletons_[i].lookAtConstraints.pushBack(constraint);
            return;
        }
    }
}

void AnimationSystem::addAimAt(u32 skeletonId, const AimConstraint& constraint) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            skeletons_[i].aimConstraints.pushBack(constraint);
            return;
        }
    }
}

void AnimationSystem::solveIK(u32 skeletonId) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            Skeleton& skel = skeletons_[i].skeleton;
            for (auto& chain : skeletons_[i].ikChains) {
                if (chain.enabled) solveFABRIK(skel, chain);
            }
            for (auto& solver : skeletons_[i].twoBoneIK) {
                if (solver.enabled) solveTwoBoneIK(skeletonId, solver);
            }
            for (auto& constraint : skeletons_[i].lookAtConstraints) {
                if (constraint.enabled) solveLookAt(skel, constraint);
            }
            for (auto& constraint : skeletons_[i].aimConstraints) {
                if (constraint.enabled) solveAimAt(skel, constraint);
            }
            return;
        }
    }
}

void AnimationSystem::solveFABRIK(Skeleton& skeleton, const IKChain& chain) {
    if (chain.boneIndex < 0 || chain.boneIndex >= (i32)skeleton.boneCount()) return;
    if (chain.boneCount < 2) return;

    skeleton.computeWorldTransforms();

    Vector<i32> boneIndices;
    i32 current = chain.boneIndex;
    for (i32 b = 0; b < chain.boneCount && current >= 0; b++) {
        boneIndices.pushBack(current);
        current = skeleton.bone(current).parent;
    }

    Vector<f32> boneLengths;
    f32 totalLength = 0.0f;
    for (u32 b = 0; b < boneIndices.size() - 1; b++) {
        Vec3 p0 = skeleton.getBoneMatrix(boneIndices[b]).translation();
        Vec3 p1 = skeleton.getBoneMatrix(boneIndices[b + 1]).translation();
        f32 len = (p1 - p0).length();
        boneLengths.pushBack(len);
        totalLength += len;
    }

    if (totalLength < 0.001f) return;

    Vector<Vec3> positions;
    for (u32 b = 0; b < boneIndices.size(); b++) {
        positions.pushBack(skeleton.getBoneMatrix(boneIndices[b]).translation());
    }

    Vec3 rootPos = positions[0];

    for (int iter = 0; iter < chain.maxIterations; iter++) {
        Vec3 endEffector = positions.back();
        f32 dist = (chain.target - endEffector).length();
        if (dist < chain.threshold) break;

        positions.back() = chain.target;

        for (i32 b = (i32)positions.size() - 2; b >= 0; b--) {
            Vec3 dir = (positions[b] - positions[b + 1]).normalized();
            f32 len = (b < (i32)boneLengths.size()) ? boneLengths[b] : boneLengths.back();
            positions[b] = positions[b + 1] + dir * len;
        }

        positions[0] = rootPos;

        for (u32 b = 1; b < positions.size(); b++) {
            Vec3 dir = (positions[b] - positions[b - 1]).normalized();
            f32 len = (b - 1 < boneLengths.size()) ? boneLengths[b - 1] : boneLengths.back();
            positions[b] = positions[b - 1] + dir * len;
        }

        for (u32 b = 0; b < boneIndices.size() - 1; b++) {
            Bone& bone = skeleton.bone(boneIndices[b]);
            Vec3 bonePos = positions[b];
            Vec3 childPos = positions[b + 1];
            Vec3 toChild = (childPos - bonePos).normalized();
            Vec3 currentForward = bone.getWorldRotation().forward();
            f32 dot = Mathf::clamp(currentForward.dot(toChild), -1.0f, 1.0f);
            f32 angle = std::acos(dot);
            Vec3 axis = currentForward.cross(toChild);
            if (axis.lengthSquared() > 0.0001f) {
                axis = axis.normalized();
                Quat deltaRot(axis, angle);
                bone.rotation = (deltaRot * bone.rotation).normalized();
            }
        }
    }
}

void AnimationSystem::solveTwoBoneIK(u32 skeletonId, IKTwoBoneSolver& solver) {
    Skeleton* skel = getSkeleton(skeletonId);
    if (!skel) return;
    if (solver.boneIndex < 0 || solver.midBoneIndex < 0 || solver.endBoneIndex < 0) return;
    if (solver.boneIndex >= (i32)skel->boneCount() ||
        solver.midBoneIndex >= (i32)skel->boneCount() ||
        solver.endBoneIndex >= (i32)skel->boneCount()) return;

    skel->computeWorldTransforms();

    Vec3 rootPos = skel->getBoneMatrix(solver.boneIndex).translation();
    Vec3 midPos = skel->getBoneMatrix(solver.midBoneIndex).translation();
    Vec3 endPos = skel->getBoneMatrix(solver.endBoneIndex).translation();

    f32 upperLen = (midPos - rootPos).length();
    f32 lowerLen = (endPos - midPos).length();

    if (upperLen < 0.001f || lowerLen < 0.001f) return;

    Vec3 toTarget = solver.target - rootPos;
    f32 targetDist = toTarget.length();

    f32 totalLen = upperLen + lowerLen;
    targetDist = Mathf::clamp(targetDist, 0.01f, totalLen - 0.01f);

    f32 cosAngle = (upperLen * upperLen + targetDist * targetDist - lowerLen * lowerLen) /
                   (2.0f * upperLen * targetDist);
    cosAngle = Mathf::clamp(cosAngle, -1.0f, 1.0f);
    f32 angle = std::acos(cosAngle);

    Vec3 upDir = (midPos - rootPos).normalized();
    Vec3 poleDir = toTarget.normalized();
    Vec3 normal = upDir.cross(poleDir).normalized();

    if (normal.lengthSquared() > 0.0001f) {
        Quat deltaRot(normal, angle);
        Bone& upperBone = skel->bone(solver.boneIndex);
        upperBone.rotation = (deltaRot * upperBone.rotation).normalized();
    }

    Vec3 midPosNew = skel->getBoneMatrix(solver.midBoneIndex).translation();
    Vec3 toEnd = (endPos - midPosNew).normalized();
    Vec3 toTargetMid = (solver.target - midPosNew).normalized();
    Vec3 normalMid = toEnd.cross(toTargetMid).normalized();
    if (normalMid.lengthSquared() > 0.0001f) {
        f32 dotMid = Mathf::clamp(toEnd.dot(toTargetMid), -1.0f, 1.0f);
        f32 angleMid = std::acos(dotMid);
        Quat deltaRotMid(normalMid, angleMid);
        Bone& midBone = skel->bone(solver.midBoneIndex);
        midBone.rotation = (deltaRotMid * midBone.rotation).normalized();
    }
}

void AnimationSystem::solveIKChain(Skeleton& skeleton, const IKChain& chain) {
    solveFABRIK(skeleton, chain);
}

void AnimationSystem::solveLookAt(Skeleton& skeleton, const LookAtConstraint& constraint) {
    if (constraint.boneIndex < 0 || constraint.boneIndex >= (i32)skeleton.boneCount()) return;

    skeleton.computeWorldTransforms();
    Bone& bone = skeleton.bone(constraint.boneIndex);
    Vec3 bonePos = skeleton.getBoneMatrix(constraint.boneIndex).translation();
    Vec3 toTarget = constraint.target - bonePos;
    if (toTarget.lengthSquared() < 0.0001f) return;

    Vec3 targetDir = toTarget.normalized();
    Vec3 currentDir = bone.getWorldRotation().forward();

    Quat currentRot = bone.getWorldRotation();
    Quat targetRot = Quat::lookRotation(targetDir, constraint.upHint);
    Quat deltaRot = targetRot * currentRot.inverse();

    f32 angle = std::acos(Mathf::clamp(currentDir.dot(targetDir), -1.0f, 1.0f));
    f32 minRad = constraint.minAngle * Mathf::DEG2RAD;
    f32 maxRad = constraint.maxAngle * Mathf::DEG2RAD;
    angle = Mathf::clamp(angle, minRad, maxRad);

    Vec3 axis = currentDir.cross(targetDir);
    if (axis.lengthSquared() > 0.0001f) {
        axis = axis.normalized();
        Quat limitedRot(axis, angle);
        bone.rotation = (limitedRot * bone.rotation * constraint.weight +
                         bone.rotation * (1.0f - constraint.weight)).normalized();
    }
}

void AnimationSystem::solveAimAt(Skeleton& skeleton, const AimConstraint& constraint) {
    if (constraint.boneIndex < 0 || constraint.boneIndex >= (i32)skeleton.boneCount()) return;

    skeleton.computeWorldTransforms();
    Bone& bone = skeleton.bone(constraint.boneIndex);
    Vec3 bonePos = skeleton.getBoneMatrix(constraint.boneIndex).translation();
    Vec3 toTarget = constraint.target - bonePos;
    if (toTarget.lengthSquared() < 0.0001f) return;

    Vec3 targetDir = toTarget.normalized();
    Vec3 currentDir = constraint.aimAxis;
    Quat currentRot = bone.getWorldRotation();
    currentDir = currentRot * currentDir;

    Vec3 axis = currentDir.cross(targetDir);
    f32 angle = std::acos(Mathf::clamp(currentDir.dot(targetDir), -1.0f, 1.0f));

    if (axis.lengthSquared() > 0.0001f && angle > 0.001f) {
        axis = axis.normalized();
        Quat deltaRot(axis, angle);
        bone.rotation = (deltaRot * bone.rotation * constraint.weight +
                         bone.rotation * (1.0f - constraint.weight)).normalized();
    }
}

void AnimationSystem::enableRootMotion(u32 skeletonId) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            skeletons_[i].skeleton.setRootMotion(true);
            return;
        }
    }
}

Vec3 AnimationSystem::extractRootMotion(u32 skeletonId) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            Vec3 delta = skeletons_[i].skeleton.rootMotionDelta();
            skeletons_[i].skeleton.resetRootMotion();
            return delta;
        }
    }
    return Vec3(0);
}

AnimationStateMachine& AnimationSystem::getStateMachine(u32 skeletonId) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            return skeletons_[i].stateMachine;
        }
    }
    static AnimationStateMachine empty;
    return empty;
}

void AnimationSystem::transitionTo(u32 skeletonId, StateID target, f32 blendDuration) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            skeletons_[i].stateMachine.setState(target, blendDuration);
            return;
        }
    }
}

void AnimationSystem::evaluatePose(u32 skeletonId) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            Skeleton& skel = skeletons_[i].skeleton;
            SkeletonEntry& entry = skeletons_[i];

            if (entry.primaryState.status == AnimationState::Status::Playing && entry.primaryState.clip) {
                f32 time = entry.primaryState.clip->normalizeTime(entry.primaryState.currentTime);
                sampleClip(*entry.primaryState.clip, skel, time,
                           entry.primaryState.weight, AnimationBlendMode::Override);
            }

            for (auto& bs : entry.blendStates) {
                if (bs.status == AnimationState::Status::Playing && bs.clip) {
                    f32 blendW = bs.weight;
                    if (bs.blendDuration > 0.01f && bs.blendTime < bs.blendDuration) {
                        blendW = Mathf::saturate(bs.blendTime / bs.blendDuration) * bs.weight;
                    }
                    f32 time = bs.clip->normalizeTime(bs.currentTime);
                    sampleClip(*bs.clip, skel, time, blendW, AnimationBlendMode::Override);
                }
            }

            for (auto& layer : entry.layers) {
                if (!layer.enabled) continue;
                if (entry.primaryState.status == AnimationState::Status::Playing && entry.primaryState.clip) {
                    f32 time = entry.primaryState.clip->normalizeTime(entry.primaryState.currentTime);
                    sampleClip(*entry.primaryState.clip, skel, time, layer.weight, layer.blendMode, &layer);
                }
            }

            skel.computeWorldTransforms();
            applyPose(skeletonId);
            return;
        }
    }
}

void AnimationSystem::applyPose(u32 skeletonId) {
    Skeleton* skel = getSkeleton(skeletonId);
    if (!skel) return;

    for (u32 i = 0; i < skel->boneCount(); i++) {
        Bone& bone = skel->bone(i);
        bone.localTransform = bone.computeLocalTransform();
    }

    for (u32 i = 0; i < skel->boneCount(); i++) {
        Bone& bone = skel->bone(i);
        if (bone.parent >= 0) {
            bone.worldTransform = skel->bone(bone.parent).worldTransform * bone.localTransform;
        } else {
            bone.worldTransform = bone.localTransform;
        }
    }
}

void AnimationSystem::sampleClip(const AnimationClip& clip, Skeleton& skeleton,
                                  f32 time, f32 weight, AnimationBlendMode mode, const AnimationLayer* layer) {
    for (u32 b = 0; b < skeleton.boneCount() && b < clip.positionKeys.size(); b++) {
        if (layer && layer->boneMaskEnabled && !layer->boneMask[b]) continue;

        Vec3 pos;
        Quat rot;
        Vec3 scl(1);
        clip.getPositions(time, b, pos);
        clip.getRotations(time, b, rot);
        clip.getScales(time, b, scl);

        Bone& bone = skeleton.bone(b);
        if (mode == AnimationBlendMode::Override || bone.parent < 0) {
            bone.position = bone.position * (1.0f - weight) + pos * weight;
            bone.rotation = Quat::slerp(bone.rotation, rot, weight);
            bone.scale = bone.scale * (1.0f - weight) + scl * weight;
        } else if (mode == AnimationBlendMode::Additive) {
            bone.position += pos * weight;
            Quat addRot = rot * bone.rotation.inverse();
            bone.rotation = (Quat::slerp(Quat::identity(), addRot, weight) * bone.rotation).normalized();
            bone.scale = bone.scale + (scl - Vec3(1,1,1)) * weight;
        } else if (mode == AnimationBlendMode::Multiply) {
            bone.position = bone.position * (1.0f - weight) + (bone.position * pos) * weight;
            bone.rotation = Quat::slerp(bone.rotation, bone.rotation * rot, weight);
            bone.scale = bone.scale * (1.0f - weight) + (bone.scale * scl) * weight;
        }
    }
}

void AnimationSystem::computeBoneChain(Skeleton& skeleton) {
    skeleton.computeWorldTransforms();
}

void AnimationSystem::extractRootMotionDelta(Skeleton& skeleton, const AnimationClip& clip, f32 prevTime, f32 currentTime) {
    if (clip.positionKeys.empty() || clip.positionKeys[0].empty()) return;

    Vec3 prevPos, currPos;
    clip.getPositions(prevTime, 0, prevPos);
    clip.getPositions(currentTime, 0, currPos);
    skeleton.resetRootMotion();

    Vec3 delta = currPos - prevPos;
    skeleton.setRootMotionDelta(delta);

    Quat prevRot, currRot;
    clip.getRotations(prevTime, 0, prevRot);
    clip.getRotations(currentTime, 0, currRot);
    skeleton.setRootMotionRotDelta(currRot * prevRot.inverse());
}

void AnimationSystem::compressAnimationClip(AnimationClip& clip) {
    clip.compress();
    clip.removeRedundantKeyframes();
}

f32 AnimationSystem::getTime(u32 skeletonId) const {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            return skeletons_[i].primaryState.currentTime;
        }
    }
    return 0.0f;
}

bool AnimationSystem::isPlaying(u32 skeletonId) const {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            return skeletons_[i].primaryState.status == AnimationState::Status::Playing;
        }
    }
    return false;
}

bool AnimationSystem::hasAnimationEvent(u32 skeletonId, const char* eventName) {
    for (u32 i = 0; i < skeletonCount_; i++) {
        if (skeletons_[i].id == skeletonId) {
            if (skeletons_[i].primaryState.clip) {
                for (auto& evt : skeletons_[i].primaryState.clip->events) {
                    if (evt.fired && strcmp(evt.name.c_str(), eventName) == 0) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

f32 AnimationSystem::getClipDuration(u32 clipId) const {
    if (clipId < clips_.size()) return clips_[clipId].getDuration();
    return 0.0f;
}

// ---- Skeleton ----

i32 Skeleton::addBone(const String& name, i32 parentIndex) {
    Bone bone;
    bone.name = name;
    bone.parent = parentIndex;
    bones_.pushBack(bone);
    return (i32)bones_.size() - 1;
}

void Skeleton::setBoneTransform(i32 boneIndex, const Vec3& pos, const Quat& rot, const Vec3& scale) {
    if (boneIndex >= 0 && boneIndex < (i32)bones_.size()) {
        bones_[boneIndex].position = pos;
        bones_[boneIndex].rotation = rot;
        bones_[boneIndex].scale = scale;
    }
}

i32 Skeleton::findBone(const char* name) const {
    for (u32 i = 0; i < bones_.size(); i++) {
        if (bones_[i].name == name) return (i32)i;
    }
    return -1;
}

void Skeleton::computeWorldTransforms() {
    for (u32 i = 0; i < bones_.size(); i++) {
        bones_[i].localTransform = bones_[i].computeLocalTransform();
    }
    for (u32 i = 0; i < bones_.size(); i++) {
        if (bones_[i].parent >= 0 && bones_[i].parent < (i32)bones_.size()) {
            bones_[i].worldTransform = bones_[bones_[i].parent].worldTransform * bones_[i].localTransform;
        } else {
            bones_[i].worldTransform = bones_[i].localTransform;
        }
    }
}

void Skeleton::reset() {
    for (auto& b : bones_) {
        b.position = Vec3(0);
        b.rotation = Quat::identity();
        b.scale = Vec3(1, 1, 1);
        b.localTransform = Mat4::identity();
        b.worldTransform = Mat4::identity();
    }
    resetRootMotion();
}

Mat4 Skeleton::getBoneMatrix(i32 index) const {
    if (index >= 0 && index < (i32)bones_.size()) return bones_[index].worldTransform;
    return Mat4::identity();
}

Mat4 Skeleton::getBonePaletteMatrix(i32 index) const {
    if (index >= 0 && index < (i32)bones_.size()) {
        return bones_[index].worldTransform * bones_[index].inverseBindPose;
    }
    return Mat4::identity();
}

Mat4 Skeleton::getBoneLocalMatrix(i32 index) const {
    if (index >= 0 && index < (i32)bones_.size()) return bones_[index].localTransform;
    return Mat4::identity();
}

void Skeleton::getBonePositions(Vec3* positions) const {
    for (u32 i = 0; i < bones_.size(); i++) {
        positions[i] = bones_[i].getWorldPosition();
    }
}

void Skeleton::getBoneRotations(Quat* rotations) const {
    for (u32 i = 0; i < bones_.size(); i++) {
        rotations[i] = bones_[i].getWorldRotation();
    }
}

void Skeleton::getBoneScales(Vec3* scales) const {
    for (u32 i = 0; i < bones_.size(); i++) {
        scales[i] = bones_[i].scale;
    }
}

void Skeleton::setBonePositions(const Vec3* positions) {
    for (u32 i = 0; i < bones_.size(); i++) {
        bones_[i].position = positions[i];
    }
}

void Skeleton::setBoneRotations(const Quat* rotations) {
    for (u32 i = 0; i < bones_.size(); i++) {
        bones_[i].rotation = rotations[i];
    }
}

void Skeleton::computeBindPose() {
    computeWorldTransforms();
    for (u32 i = 0; i < bones_.size(); i++) {
        bones_[i].inverseBindPose = bones_[i].worldTransform.inverse();
    }
}

void Skeleton::setInverseBindPose(i32 boneIndex, const Mat4& ibp) {
    if (boneIndex >= 0 && boneIndex < (i32)bones_.size()) {
        bones_[boneIndex].inverseBindPose = ibp;
    }
}

// ---- AnimationStateMachine ----

void AnimationStateMachine::addTransition(StateID from, StateID to, f32 blendTime,
                                           f32(*condition)(void*), void* data) {
    StateTransition t;
    t.from = from;
    t.to = to;
    t.blendDuration = blendTime;
    t.condition = condition;
    t.conditionData = data;
    transitions.pushBack(t);
}

void AnimationStateMachine::setState(StateID newState, f32 blend) {
    if (newState == currentState) return;
    previousState = currentState;
    currentState = newState;
    blendTimer = 0.0f;
    blendDuration = blend;
    blending = (blend > 0.01f);
    stateTime = 0.0f;
}

void AnimationStateMachine::update(f32 dt) {
    if (blending) {
        blendTimer += dt;
        if (blendTimer >= blendDuration) {
            blendTimer = blendDuration;
            blending = false;
        }
    }

    for (auto& trans : transitions) {
        if (trans.from == currentState) {
            bool shouldTransition = false;
            if (trans.condition) {
                shouldTransition = trans.condition(trans.conditionData) > 0.5f;
            }
            if (trans.hasExitTime && stateTime < trans.exitTime) {
                shouldTransition = false;
            }
            if (shouldTransition) {
                setState(trans.to, trans.blendDuration);
                break;
            }
        }
    }
}

AnimationState* AnimationStateMachine::getState(StateID id) {
    u32 idx = (u32)id;
    if (idx < states.size()) return &states[idx];
    return nullptr;
}

f32 AnimationStateMachine::getBlendProgress() const {
    if (!blending || blendDuration < 0.001f) return 1.0f;
    return Mathf::saturate(blendTimer / blendDuration);
}

}
