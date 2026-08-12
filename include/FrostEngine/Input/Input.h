#pragma once

// ============================================================================
// FrostEngine Input System — Keyboard, Mouse, Gamepad, Action bindings, events
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Math.h"

namespace Frost {

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

enum class GamepadButton : u8 {
    A = 0, B, X, Y,
    LB, RB, Back, Start, Guide,
    LeftStick, RightStick,
    DPadUp, DPadDown, DPadLeft, DPadRight,
    COUNT
};

enum class GamepadAxis : u8 {
    LeftX = 0, LeftY, RightX, RightY,
    LeftTrigger, RightTrigger,
    COUNT
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

struct GamepadState {
    bool connected = false;
    bool buttonDown[(u32)GamepadButton::COUNT] = {};
    bool buttonPressed[(u32)GamepadButton::COUNT] = {};
    bool buttonReleased[(u32)GamepadButton::COUNT] = {};
    f32 axis[(u32)GamepadAxis::COUNT] = {};
    f32 prevAxis[(u32)GamepadAxis::COUNT] = {};
    f32 leftStickMagnitude = 0.0f;
    f32 rightStickMagnitude = 0.0f;
    f32 lastActiveTime = 0.0f;
    f32 rumbleLow = 0.0f;
    f32 rumbleHigh = 0.0f;
    f32 rumbleDuration = 0.0f;
    i32 deviceId = -1;
    String name;

    void newFrame() {
        std::memset(buttonPressed, 0, sizeof(buttonPressed));
        std::memset(buttonReleased, 0, sizeof(buttonReleased));
        for (u32 i = 0; i < (u32)GamepadAxis::COUNT; i++) prevAxis[i] = axis[i];
        if (rumbleDuration > 0.0f) rumbleDuration -= 1.0f;
    }

    void setButton(GamepadButton b, bool down) {
        u32 idx = (u32)b;
        if (down && !buttonDown[idx]) buttonPressed[idx] = true;
        if (!down && buttonDown[idx]) buttonReleased[idx] = true;
        buttonDown[idx] = down;
    }
};

enum class InputEventType : u8 {
    KeyDown,
    KeyUp,
    MouseDown,
    MouseUp,
    MouseMove,
    Scroll,
    TextInput,
    GamepadDown,
    GamepadUp,
    GamepadAxis,
    GamepadConnect,
    GamepadDisconnect
};

struct InputEvent {
    InputEventType type = InputEventType::KeyDown;
    u32 deviceIndex = 0;
    u32 code = 0;
    f32 value = 0.0f;
    i32 mouseX = 0, mouseY = 0;
    u32 frame = 0;
    u64 timestamp = 0;
    char text[8] = {};
};

enum class InputAxisSource : u8 {
    None = 0,
    Key,
    MouseButton,
    GamepadButton,
    GamepadAxis,
    MouseDelta,
    MouseScroll
};

enum class InputActionType : u8 {
    Button = 0,
    Axis1D,
    Axis2D
};

struct InputBinding {
    InputAxisSource source = InputAxisSource::None;
    u32 code = 0;
    f32 scale = 1.0f;
    f32 deadzone = 0.0f;
    i32 gamepadIndex = -1;
};

struct InputAction {
    String name;
    InputActionType type = InputActionType::Button;
    Vector<InputBinding> bindings;
    bool held = false;
    bool heldLast = false;
    bool triggeredThisFrame = false;
    bool releasedThisFrame = false;
    f32 value = 0.0f;
    Vec2 value2D{0, 0};
    f32 holdTime = 0.0f;
    f32 lastTriggeredTime = 0.0f;
    u32 lastTriggeredFrame = 0;
};

class InputSystem {
public:
    static constexpr u32 MAX_GAMEPADS = 4;
    static constexpr u32 MAX_ACTIONS = 128;
    static constexpr u32 MAX_EVENTS = 1024;

    InputSystem();
    ~InputSystem();

    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;

    bool init();
    void shutdown();
    void newFrame();
    void update(f32 dt);

    // ---- Device feeding (called by the platform backend / window) ----
    void onKey(Key key, bool down);
    void onMouseButton(MouseButton btn, bool down);
    void onMouseMove(i32 x, i32 y);
    void onScroll(f32 delta);
    void onTextInput(const char* text);
    void onGamepadConnect(u32 index, const char* name);
    void onGamepadDisconnect(u32 index);
    void onGamepadButton(u32 index, GamepadButton btn, bool down);
    void onGamepadAxis(u32 index, GamepadAxis axis, f32 value);

    // ---- Keyboard / mouse queries ----
    bool isKeyDown(Key key) const;
    bool isKeyJustPressed(Key key) const;
    bool isKeyJustReleased(Key key) const;
    bool isMouseDown(MouseButton btn) const;
    bool isMouseJustPressed(MouseButton btn) const;
    bool isMouseJustReleased(MouseButton btn) const;
    Vec2 mousePosition() const { return Vec2((f32)state_.mouseX, (f32)state_.mouseY); }
    Vec2 mouseDelta() const { return Vec2((f32)state_.mouseDeltaX, (f32)state_.mouseDeltaY); }
    f32 scrollDelta() const { return state_.scrollDelta; }

    // ---- Gamepad queries ----
    GamepadState& gamepad(u32 index);
    const GamepadState& gamepad(u32 index) const;
    bool isGamepadConnected(u32 index) const;
    bool isGamepadButtonDown(u32 index, GamepadButton btn) const;
    bool isGamepadButtonJustPressed(u32 index, GamepadButton btn) const;
    bool isGamepadButtonJustReleased(u32 index, GamepadButton btn) const;
    f32 gamepadAxis(u32 index, GamepadAxis axis) const;
    bool isAnyGamepadButtonPressed(u32 index) const;
    u32 connectedGamepadCount() const;
    void setRumble(u32 index, f32 low, f32 high, f32 duration = 0.0f);

    // ---- Action bindings ----
    u32 createAction(const char* name, InputActionType type = InputActionType::Button);
    bool bindAction(u32 actionId, const InputBinding& binding);
    bool bindKey(u32 actionId, Key key, f32 scale = 1.0f);
    bool bindGamepadButton(u32 actionId, GamepadButton btn, u32 padIndex = 0);
    bool bindGamepadAxis(u32 actionId, GamepadAxis axis, u32 padIndex = 0, f32 deadzone = 0.2f);
    bool bindMouseAxis(u32 actionId, bool horizontal);
    bool bindScroll(u32 actionId);
    u32 findAction(const char* name) const;
    bool isActionHeld(u32 actionId) const;
    bool isActionPressed(u32 actionId) const;
    bool isActionReleased(u32 actionId) const;
    f32 getActionValue(u32 actionId) const;
    Vec2 getActionVector(u32 actionId) const;
    f32 getActionHoldTime(u32 actionId) const;

    // ---- Config ----
    void setMouseSensitivity(f32 s) { mouseSensitivity_ = s; }
    f32 mouseSensitivity() const { return mouseSensitivity_; }
    void setGamepadDeadzone(f32 dz) { gamepadDeadzone_ = dz; }
    f32 gamepadDeadzone() const { return gamepadDeadzone_; }
    void setKeyRepeatEnabled(bool enabled) { keyRepeatEnabled_ = enabled; }
    bool keyRepeatEnabled() const { return keyRepeatEnabled_; }
    void setWindowFocused(bool focused) { windowFocused_ = focused; }
    bool windowFocused() const { return windowFocused_; }

    // ---- Events ----
    u32 eventCount() const { return eventCount_; }
    const InputEvent& event(u32 index) const;
    const InputEvent* nextEvent(u32& cursor) const;

    // ---- Misc ----
    const String& textInput() const { return textInput_; }
    void clearTextInput() { textInput_.clear(); }
    void clearAll();
    const InputState& state() const { return state_; }
    InputState& state() { return state_; }
    u32 frame() const { return frame_; }
    f32 time() const { return time_; }

    struct Stats {
        u32 eventsProcessed = 0;
        u32 framesProcessed = 0;
        f32 avgUpdateMs = 0.0f;
        u32 connectedGamepads = 0;
        u32 activeActions = 0;
        u32 maxQueuedEvents = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    void pushEvent(const InputEvent& ev);
    f32 applyDeadzone(f32 value, f32 deadzone) const;
    f32 pollBinding(const InputBinding& binding, Vec2& out2D) const;
    void evaluateActions(f32 dt);

    InputState state_;
    GamepadState gamepads_[MAX_GAMEPADS];
    InputAction actions_[MAX_ACTIONS];
    u32 actionCount_ = 0;

    InputEvent events_[MAX_EVENTS];
    u32 eventHead_ = 0;
    u32 eventCount_ = 0;
    u32 eventTail_ = 0;

    u32 frame_ = 0;
    f32 time_ = 0.0f;
    f32 mouseSensitivity_ = 1.0f;
    f32 gamepadDeadzone_ = 0.2f;
    bool keyRepeatEnabled_ = true;
    bool windowFocused_ = true;
    String textInput_;

    Stats stats_;
    f32 frameTimeAccum_ = 0.0f;
    u32 frameTimeSamples_ = 0;
};

} // namespace Frost
