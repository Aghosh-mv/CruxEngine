#pragma once

#include "Core/Types.h"
#include "Core/Math.h"

namespace Frost {

struct WindowConfig {
    const char* title = "FrostEngine";
    u32 width = 1280;
    u32 height = 720;
    bool fullscreen = false;
    bool vsync = true;
    bool resizable = true;
    u32 multisamples = 4;
};

enum class Key {
    A=0, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    Escape, Enter, Tab, Space, Backspace, Shift, Control, Alt,
    ArrowUp, ArrowDown, ArrowLeft, ArrowRight,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Minus, Equal, LeftBracket, RightBracket, Semicolon, Apostrophe, Comma, Period, Slash, Backslash, Grave,
    LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt,
    Count
};

enum class MouseButton { Left = 0, Right, Middle, Count };

// Owns the X11 display, GLX window and OpenGL context. Translates raw X events
// into engine input state (pressed/justPressed/justReleased, mouse delta).
class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool init(const WindowConfig& config);
    void shutdown();

    void pollEvents();          // process pending X events + swap buffers
    bool shouldClose() const { return shouldClose_; }
    void requestClose() { shouldClose_ = true; }

    u32 width() const { return width_; }
    u32 height() const { return height_; }
    bool isFocused() const { return focused_; }

    bool isKeyPressed(Key key) const;
    bool isKeyJustPressed(Key key) const;
    bool isKeyJustReleased(Key key) const;

    bool isMousePressed(MouseButton btn) const;
    bool isMouseJustPressed(MouseButton btn) const;
    bool isMouseJustReleased(MouseButton btn) const;

    Vec2 mousePosition() const { return mousePos_; }
    Vec2 mouseDelta() const { return mouseDelta_; }

    void setCursorVisible(bool visible);
    void setCursorCaptured(bool captured);
    bool cursorCaptured() const { return captured_; }
    void setFullscreen(bool fullscreen);

private:
    struct Impl;
    Impl* impl_ = nullptr;
    u32 width_ = 0, height_ = 0;
    bool shouldClose_ = false;
    bool focused_ = true;
    bool captured_ = false;
    Vec2 mousePos_, mouseDelta_;
};

}
