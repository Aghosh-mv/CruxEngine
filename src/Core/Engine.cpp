#include "CruxEngine/CruxEngine.h"
#include <cstdio>
#include <chrono>

namespace Crux {

Engine& Engine::instance() {
    static Engine engine;
    return engine;
}

bool Engine::init(const EngineConfig& config) {
    config_ = config;
    CRUX_LOG_INFO("Initializing CruxEngine");
    running_ = true;
    return true;
}

void Engine::shutdown() {
    CRUX_LOG_INFO("Shutting down CruxEngine");
    running_ = false;
}

void Engine::run() {
    CRUX_LOG_INFO("Starting main loop...");
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