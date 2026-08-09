#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Math.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimationClip.h"

namespace Frost {

enum class AnimationBlendMode : u8 {
    Override,
    Additive,
    Multiply,
    OverrideLow
};

struct AnimationLayer {
    String name;
    f32 weight = 1.0f;
    AnimationBlendMode blendMode = AnimationBlendMode::Override;
    bool enabled = true;
    bool boneMaskEnabled = false;
    bool boneMask[Skeleton::MAX_BONES] = {};
    void setAllBones(bool enabled) {
        for (u32 i = 0; i < Skeleton::MAX_BONES; i++) boneMask[i] = enabled;
    }
    void setBone(i32 bone, bool enabled) {
        if (bone >= 0 && bone < (i32)Skeleton::MAX_BONES) boneMask[bone] = enabled;
    }
};

struct AnimationState {
    enum class Status : u8 { Stopped, Playing, Paused };

    Status status = Status::Stopped;
    f32 currentTime = 0.0f;
    f32 speed = 1.0f;
    f32 weight = 1.0f;
    f32 blendTime = 0.0f;
    f32 blendDuration = 0.0f;
    AnimationClip* clip = nullptr;
    bool loop = true;
    i32 layer = 0;

    f32 normalizedTime() const {
        if (!clip) return 0.0f;
        return clip->normalizeTime(currentTime) / clip->getDuration();
    }
};

struct IKChain {
    i32 boneIndex = -1;
    Vec3 target{0, 0, 0};
    Vec3 poleTarget{0, 0, 0};
    i32 boneCount = 2;
    f32 threshold = 0.01f;
    i32 maxIterations = 10;
    bool enabled = true;
    f32 chainLength = 0.0f;
};

struct IKTwoBoneSolver {
    i32 boneIndex = -1;
    i32 midBoneIndex = -1;
    i32 endBoneIndex = -1;
    Vec3 poleTarget{0, 0, 0};
    Vec3 target{0, 0, 0};
    f32 reachDistance = 0.0f;
    bool enabled = true;
    f32 upperLength = 0.0f;
    f32 lowerLength = 0.0f;
};

struct LookAtConstraint {
    i32 boneIndex = -1;
    Vec3 target{0, 0, 0};
    Vec3 upHint{0, 1, 0};
    f32 weight = 1.0f;
    f32 minAngle = -90.0f;
    f32 maxAngle = 90.0f;
    bool enabled = true;
};

struct AimConstraint {
    i32 boneIndex = -1;
    Vec3 target{0, 0, 0};
    Vec3 aimAxis{0, 0, -1};
    Vec3 upAxis{0, 1, 0};
    f32 weight = 1.0f;
    bool enabled = true;
};

enum class StateID : u32 {
    Idle = 0,
    Walk,
    Run,
    Jump,
    Attack,
    Die,
    Custom
};

struct StateTransition {
    StateID from = StateID::Idle;
    StateID to = StateID::Idle;
    f32 blendDuration = 0.2f;
    f32(*condition)(void* userData) = nullptr;
    void* conditionData = nullptr;
    bool hasExitTime = false;
    f32 exitTime = 0.0f;
};

struct AnimationStateMachine {
    StateID currentState = StateID::Idle;
    StateID previousState = StateID::Idle;
    Vector<StateTransition> transitions;
    Vector<AnimationState> states;
    f32 blendTimer = 0.0f;
    f32 blendDuration = 0.0f;
    bool blending = false;
    f32 stateTime = 0.0f;

    void addTransition(StateID from, StateID to, f32 blendTime, f32(*condition)(void*), void* data = nullptr);
    void setState(StateID newState, f32 blend = 0.2f);
    void update(f32 dt);
    AnimationState* getState(StateID id);
    f32 getBlendProgress() const;
};

class AnimationSystem {
public:
    static constexpr u32 MAX_SKELETONS = 256;

    AnimationSystem();
    ~AnimationSystem();

    bool init();
    void shutdown();
    void update(f32 dt);

    u32 createSkeleton();
    void destroySkeleton(u32 skeletonId);
    Skeleton* getSkeleton(u32 skeletonId);

    u32 loadClip(const AnimationClip& clip);
    void freeClip(u32 clipId);
    AnimationClip* getClip(u32 clipId);

    AnimationState& play(u32 skeletonId, u32 clipId, f32 blendDuration = 0.2f);
    void pause(u32 skeletonId);
    void resume(u32 skeletonId);
    void stop(u32 skeletonId);
    void setSpeed(u32 skeletonId, f32 speed);
    void setWeight(u32 skeletonId, f32 weight);

    void blend(u32 skeletonId, u32 clipId, f32 targetWeight, f32 blendDuration);
    void crossFade(u32 skeletonId, u32 clipId, f32 fadeDuration);

    void addLayer(u32 skeletonId, const AnimationLayer& layer);
    void setLayerWeight(u32 skeletonId, u32 layerIndex, f32 weight);
    void setLayerBlendMode(u32 skeletonId, u32 layerIndex, AnimationBlendMode mode);
    void setBoneMask(u32 skeletonId, u32 layerIndex, i32 bone, bool enabled);

    void addIKChain(u32 skeletonId, const IKChain& chain);
    void addTwoBoneIK(u32 skeletonId, const IKTwoBoneSolver& solver);
    void addLookAt(u32 skeletonId, const LookAtConstraint& constraint);
    void addAimAt(u32 skeletonId, const AimConstraint& constraint);
    void solveIK(u32 skeletonId);
    void solveTwoBoneIK(u32 skeletonId, IKTwoBoneSolver& solver);
    void solveFABRIK(Skeleton& skeleton, const IKChain& chain);
    void solveLookAt(Skeleton& skeleton, const LookAtConstraint& constraint);
    void solveAimAt(Skeleton& skeleton, const AimConstraint& constraint);

    void enableRootMotion(u32 skeletonId);
    Vec3 extractRootMotion(u32 skeletonId);

    AnimationStateMachine& getStateMachine(u32 skeletonId);
    void transitionTo(u32 skeletonId, StateID target, f32 blendDuration = 0.2f);

    void evaluatePose(u32 skeletonId);
    void applyPose(u32 skeletonId);

    f32 getTime(u32 skeletonId) const;
    bool isPlaying(u32 skeletonId) const;
    bool hasAnimationEvent(u32 skeletonId, const char* eventName);
    f32 getClipDuration(u32 clipId) const;

private:
    void sampleClip(const AnimationClip& clip, Skeleton& skeleton, f32 time, f32 weight, AnimationBlendMode mode, const AnimationLayer* layer = nullptr);
    void computeBoneChain(Skeleton& skeleton);
    void solveIKChain(Skeleton& skeleton, const IKChain& chain);
    void extractRootMotionDelta(Skeleton& skeleton, const AnimationClip& clip, f32 prevTime, f32 currentTime);
    void compressAnimationClip(AnimationClip& clip);

    struct SkeletonEntry {
        u32 id = 0;
        Skeleton skeleton;
        AnimationState primaryState;
        Vector<AnimationState> blendStates;
        Vector<AnimationLayer> layers;
        Vector<IKChain> ikChains;
        Vector<IKTwoBoneSolver> twoBoneIK;
        Vector<LookAtConstraint> lookAtConstraints;
        Vector<AimConstraint> aimConstraints;
        AnimationStateMachine stateMachine;
        bool active = false;
    };

    SkeletonEntry skeletons_[MAX_SKELETONS];
    u32 skeletonCount_ = 0;
    u32 nextSkeletonId_ = 1;

    Vector<AnimationClip> clips_;
    u32 clipCount_ = 0;

    static constexpr u32 MAX_CLIPS = 1024;
    f32 time_ = 0.0f;
};

}
