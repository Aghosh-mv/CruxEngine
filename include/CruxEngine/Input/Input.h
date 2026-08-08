#pragma once

// ============================================================================
// CruxEngine Input System — Keyboard, Mouse, Gamepad abstraction
// ============================================================================

#include "Core/Types.h"

namespace Crux {

enum class Key : u32 {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Enter, Escape, Tab, Backspace, Delete, Insert,
    Left, Right, Up, Down,
    LShift, RShift, LCtrl, RCtrl, LAlt, RAlt,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    COUNT
};

enum class MouseButton : u8 {
    Left = 0, Right, Middle, X1, X2, COUNT
};

struct InputState {
    bool keyDown[(u32)Key::COUNT] = {};
    bool keyPressed[(u32)Key::COUNT] = {};
    bool keyReleased[(u32)Key::COUNT] = {};
    bool mouseDown[(u32)MouseButton::COUNT] = {};
    bool mousePressed[(u32)MouseButton::COUNT] = {};
    bool mouseReleased[(u32)MouseButton::COUNT] = {};
    i32 mouseX = 0, mouseY = 0;
    i32 mouseDeltaX = 0, mouseDeltaY = 0;
    f32 scrollDelta = 0.0f;
    bool cursorLocked = false;

    void newFrame() {
        std::memset(keyPressed, 0, sizeof(keyPressed));
        std::memset(keyReleased, 0, sizeof(keyReleased));
        std::memset(mousePressed, 0, sizeof(mousePressed));
        std::memset(mouseReleased, 0, sizeof(mouseReleased));
        mouseDeltaX = 0;
        mouseDeltaY = 0;
        scrollDelta = 0.0f;
    }

    bool isKeyDown(Key k) const { return keyDown[(u32)k]; }
    bool isKeyPressed(Key k) const { return keyPressed[(u32)k]; }
    bool isKeyReleased(Key k) const { return keyReleased[(u32)k]; }
    bool isMouseDown(MouseButton b) const { return mouseDown[(u32)b]; }
    bool isMousePressed(MouseButton b) const { return mousePressed[(u32)b]; }

    void setKey(Key k, bool down) {
        u32 idx = (u32)k;
        if (down && !keyDown[idx]) keyPressed[idx] = true;
        if (!down && keyDown[idx]) keyReleased[idx] = true;
        keyDown[idx] = down;
    }

    void setMouse(MouseButton b, bool down) {
        u32 idx = (u32)b;
        if (down && !mouseDown[idx]) mousePressed[idx] = true;
        if (!down && mouseDown[idx]) mouseReleased[idx] = true;
        mouseDown[idx] = down;
    }
};

} // namespace Crux
