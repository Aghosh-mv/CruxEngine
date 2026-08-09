#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Math.h"

namespace Frost {

struct Blackboard {
    struct Entry {
        enum class Type : u8 { Int, Float, Bool, Vec3, String, Pointer };
        Type type = Type::Int;
        i64 intValue = 0;
        f32 floatValue = 0.0f;
        bool boolValue = false;
        Vec3 vecValue{0, 0, 0};
        String strValue;
        void* ptrValue = nullptr;
    };

    static constexpr u32 MAX_ENTRIES = 64;
    Entry entries[MAX_ENTRIES];
    u32 entryCount = 0;
    String names[MAX_ENTRIES];

    void setInt(const char* key, i64 value);
    void setFloat(const char* key, f32 value);
    void setBool(const char* key, bool value);
    void setVec3(const char* key, const Vec3& value);
    void setString(const char* key, const char* value);
    void setPointer(const char* key, void* value);

    i64 getInt(const char* key) const;
    f32 getFloat(const char* key) const;
    bool getBool(const char* key) const;
    Vec3 getVec3(const char* key) const;
    const char* getString(const char* key) const;
    void* getPointer(const char* key) const;

    i32 findEntry(const char* key) const;
    bool hasKey(const char* key) const;
    void removeKey(const char* key);
    void clear();
    u32 keyCount() const { return entryCount; }
};

enum class BTStatus : u8 { Success, Failure, Running };

class BTNode {
public:
    virtual ~BTNode() = default;
    virtual BTStatus tick(Blackboard& bb, f32 dt) = 0;
    virtual void reset() {}
    const char* name = "BTNode";
};

class BTSelector : public BTNode {
public:
    Vector<BTNode*> children;
    i32 runningIndex = -1;

    BTStatus tick(Blackboard& bb, f32 dt) override;
    void reset() override { runningIndex = -1; for (auto* c : children) if (c) c->reset(); }
};

class BTSequence : public BTNode {
public:
    Vector<BTNode*> children;
    i32 runningIndex = -1;

    BTStatus tick(Blackboard& bb, f32 dt) override;
    void reset() override { runningIndex = -1; for (auto* c : children) if (c) c->reset(); }
};

class BTDecorator : public BTNode {
public:
    BTNode* child = nullptr;

    BTStatus tick(Blackboard& bb, f32 dt) override {
        if (!child) return BTStatus::Failure;
        return child->tick(bb, dt);
    }
};

class BTCondition : public BTDecorator {
public:
    bool(*condition)(const Blackboard&) = nullptr;

    BTStatus tick(Blackboard& bb, f32 dt) override {
        if (condition && !condition(bb)) return BTStatus::Failure;
        if (!child) return BTStatus::Failure;
        return child->tick(bb, dt);
    }
};

class BTLimiter : public BTDecorator {
public:
    i32 maxRuns = 1;
    i32 runCount = 0;

    BTStatus tick(Blackboard& bb, f32 dt) override {
        if (runCount >= maxRuns) return BTStatus::Failure;
        runCount++;
        if (!child) return BTStatus::Failure;
        return child->tick(bb, dt);
    }
    void reset() override { runCount = 0; }
};

class BTInverter : public BTDecorator {
public:
    BTStatus tick(Blackboard& bb, f32 dt) override {
        if (!child) return BTStatus::Failure;
        BTStatus result = child->tick(bb, dt);
        if (result == BTStatus::Success) return BTStatus::Failure;
        if (result == BTStatus::Failure) return BTStatus::Success;
        return BTStatus::Running;
    }
};

class BTRepeater : public BTDecorator {
public:
    i32 repeatCount = -1;
    i32 currentCount = 0;

    BTStatus tick(Blackboard& bb, f32 dt) override {
        if (!child) return BTStatus::Failure;
        if (repeatCount >= 0 && currentCount >= repeatCount) return BTStatus::Success;
        currentCount++;
        BTStatus result = child->tick(bb, dt);
        if (result == BTStatus::Running) return BTStatus::Running;
        if (repeatCount < 0) return BTStatus::Running;
        return BTStatus::Success;
    }
    void reset() override { currentCount = 0; }
};

class BTSucceeder : public BTDecorator {
public:
    BTStatus tick(Blackboard& bb, f32 dt) override {
        if (!child) return BTStatus::Success;
        child->tick(bb, dt);
        return BTStatus::Success;
    }
};

class BTUntilFail : public BTDecorator {
public:
    BTStatus tick(Blackboard& bb, f32 dt) override {
        if (!child) return BTStatus::Running;
        BTStatus result = child->tick(bb, dt);
        if (result == BTStatus::Failure) return BTStatus::Success;
        return BTStatus::Running;
    }
};

class BTCooldown : public BTDecorator {
public:
    f32 cooldownTime = 1.0f;
    f32 timer = 0.0f;

    BTStatus tick(Blackboard& bb, f32 dt) override {
        if (timer > 0.0f) {
            timer -= dt;
            return BTStatus::Failure;
        }
        if (!child) return BTStatus::Failure;
        BTStatus result = child->tick(bb, dt);
        if (result != BTStatus::Running) {
            timer = cooldownTime;
        }
        return result;
    }
    void reset() override { timer = 0.0f; }
};

class BTTimeLimit : public BTDecorator {
public:
    f32 maxTime = 5.0f;
    f32 timer = 0.0f;

    BTStatus tick(Blackboard& bb, f32 dt) override {
        timer += dt;
        if (timer >= maxTime) return BTStatus::Failure;
        if (!child) return BTStatus::Failure;
        return child->tick(bb, dt);
    }
    void reset() override { timer = 0.0f; }
};

class BTBlackboardCheck : public BTDecorator {
public:
    String key;
    i64 checkValue = 0;
    enum class CheckType : u8 { Exists, Equals, NotEquals, Greater, Less };
    CheckType checkType = CheckType::Exists;

    BTStatus tick(Blackboard& bb, f32 dt) override {
        if (!child) return BTStatus::Failure;
        bool result = false;
        switch (checkType) {
        case CheckType::Exists: result = bb.hasKey(key.c_str()); break;
        case CheckType::Equals: result = bb.getInt(key.c_str()) == checkValue; break;
        case CheckType::NotEquals: result = bb.getInt(key.c_str()) != checkValue; break;
        case CheckType::Greater: result = bb.getInt(key.c_str()) > checkValue; break;
        case CheckType::Less: result = bb.getInt(key.c_str()) < checkValue; break;
        }
        if (!result) return BTStatus::Failure;
        return child->tick(bb, dt);
    }
};

class BTLeaf : public BTNode {
public:
    BTStatus(*action)(Blackboard&, f32 dt) = nullptr;

    BTStatus tick(Blackboard& bb, f32 dt) override {
        if (action) return action(bb, dt);
        return BTStatus::Failure;
    }
};

class BTParallel : public BTNode {
public:
    Vector<BTNode*> children;
    i32 successThreshold = -1;

    BTStatus tick(Blackboard& bb, f32 dt) override;
};

class BTTree {
public:
    BTTree() = default;
    ~BTTree() { clear(); }

    void setRoot(BTNode* root) { root_ = root; }
    BTStatus tick(Blackboard& bb, f32 dt) {
        if (!root_) return BTStatus::Failure;
        return root_->tick(bb, dt);
    }
    void reset() {
        if (root_) root_->reset();
    }

    template<typename T, typename... Args>
    T* createNode(Args&&... args) {
        T* node = new T(std::forward<Args>(args)...);
        nodes_.pushBack(node);
        return node;
    }

    void clear() {
        for (auto* n : nodes_) delete n;
        nodes_.clear();
        root_ = nullptr;
    }

    u32 nodeCount() const { return (u32)nodes_.size(); }

private:
    BTNode* root_ = nullptr;
    Vector<BTNode*> nodes_;
};

}
