#include "FrostEngine/FrostEngine.h"
#include <cstdio>
#include <chrono>

namespace Frost {

Engine& Engine::instance() {
    static Engine engine;
    return engine;
}

bool Engine::init(const EngineConfig& config) {
    config_ = config;
    FROST_LOG_INFO("Initializing FrostEngine");
    running_ = true;
    return true;
}

void Engine::shutdown() {
    FROST_LOG_INFO("Shutting down FrostEngine");
    running_ = false;
}

void Engine::run() {
    FROST_LOG_INFO("Starting main loop...");
    while(isRunning()) {
        deltaTime_ = 0.016f;
        totalTime_ += deltaTime_;
        frameCount_++;
        update(deltaTime_);
    }
    shutdown();
}

void Engine::update(f32 dt) {
}

void Engine::render() {
}

void shutdown() {
    instance().shutdown();
}

}