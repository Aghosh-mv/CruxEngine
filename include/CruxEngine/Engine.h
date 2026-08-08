#pragma once

#include "Core/Types.h"
#include "Core/String.h"
#include "Core/Vector.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"

namespace Crux {

struct EngineConfig {
    String appName = "CruxEngine";
    String appVersion = "0.1.0";
    u32 windowWidth = 1280;
    u32 windowHeight = 720;
    bool windowFullscreen = false;
    bool windowResizable = true;
    bool enableValidation = false;
    bool enableRayTracing = true;
    bool enableMeshShading = true;
    bool enableHDR = true;
    bool enableVSync = true;
    bool enableDebugUtils = true;
    u32 framesInFlight = 2;
};

struct InputState {
    struct Key { bool pressed = false; bool justPressed = false; bool justReleased = false; };
    struct Mouse { Vec2 position; Vec2 delta; struct Btn { bool left = false, right = false, middle = false; }; Btn btn; };
    Vector<Key> keys;
    Mouse mouse;
};

class Engine {
public:
    static Engine& instance();
    bool init(const EngineConfig& config = EngineConfig());
    void shutdown();
    void run();
    bool isRunning() const { return running_; }
    InputState& getInput() { return input_; }
    f32 getDeltaTime() const { return deltaTime_; }
    f32 getTime() const { return totalTime_; }
    
protected:
    Engine() = default;
    virtual ~Engine() = default;
    virtual void update(f32 deltaTime);
    virtual void render();
    bool running_ = false;
    f32 deltaTime_ = 0.016f;
    f32 totalTime_ = 0.0f;
    u32 frameCount_ = 0;
    EngineConfig config_;
    InputState input_;
};

void shutdown();

}