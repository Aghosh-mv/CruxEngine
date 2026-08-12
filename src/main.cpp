#include "Platform/Window.h"
#include "Renderer/Renderer.h"
#include "Game/Game.h"
#include "Core/Log.h"
#include <ctime>
#include <cstdio>

using namespace Frost;

static f32 nowSeconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (f32)((f64)ts.tv_sec + (f64)ts.tv_nsec * 1e-9);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    WindowConfig config;
    config.title = "FrostEngine";
    config.width = 1920;
    config.height = 1080;
    config.fullscreen = false;
    config.vsync = true;
    config.multisamples = 4;

    Window window;
    if (!window.init(config)) {
        FROST_LOG_ERROR("Failed to create window");
        return 1;
    }

    Renderer renderer;
    if (!renderer.init(window, 4)) {
        FROST_LOG_ERROR("Failed to initialize renderer");
        window.shutdown();
        return 1;
    }

    Game game;
    if (!game.init(window, renderer)) {
        FROST_LOG_ERROR("Failed to initialize game");
        renderer.shutdown();
        window.shutdown();
        return 1;
    }

    f32 last = nowSeconds();
    while (!window.shouldClose()) {
        window.pollEvents();

        f32 now = nowSeconds();
        f32 dt = now - last;
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        game.update(dt);
        if (!game.isRunning()) window.requestClose();
    }

    game.shutdown();
    renderer.shutdown();
    window.shutdown();
    return 0;
}
