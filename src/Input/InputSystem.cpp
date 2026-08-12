#include "Input/Input.h"
#include "Core/Log.h"
#include <cstring>
#include <cmath>

namespace Frost {

InputSystem::InputSystem() {
    memset(&state_, 0, sizeof(state_));
    memset(gamepads_, 0, sizeof(gamepads_));
    memset(actions_, 0, sizeof(actions_));
    memset(events_, 0, sizeof(events_));
}

InputSystem::~InputSystem() { shutdown(); }

bool InputSystem::init() {
    state_ = InputState();
    memset(gamepads_, 0, sizeof(gamepads_));
    memset(actions_, 0, sizeof(actions_));
    actionCount_ = 0;
    eventHead_ = 0;
    eventCount_ = 0;
    eventTail_ = 0;
    frame_ = 0;
    time_ = 0.0f;
    textInput_.clear();
    stats_ = Stats();
    FROST_LOG_INFO("[InputSystem] Initialized");
    return true;
}

void InputSystem::shutdown() {
    clearAll();
    FROST_LOG_INFO("[InputSystem] Shutdown");
}

void InputSystem::clearAll() {
    state_ = InputState();
    memset(gamepads_, 0, sizeof(gamepads_));
    for (u32 i = 0; i < MAX_ACTIONS; i++) {
        actions_[i].held = false;
        actions_[i].heldLast = false;
        actions_[i].triggeredThisFrame = false;
        actions_[i].releasedThisFrame = false;
        actions_[i].value = 0.0f;
        actions_[i].value2D = Vec2(0, 0);
        actions_[i].holdTime = 0.0f;
    }
    eventHead_ = 0;
    eventCount_ = 0;
    eventTail_ = 0;
    textInput_.clear();
}

void InputSystem::newFrame() {
    state_.newFrame();
    for (u32 i = 0; i < MAX_GAMEPADS; i++) gamepads_[i].newFrame();
}

void InputSystem::update(f32 dt) {
    u32 startFrame = frame_;
    frame_++;
    time_ += dt;

    evaluateActions(dt);

    if (windowFocused_) {
        stats_.framesProcessed++;
    }
    stats_.connectedGamepads = connectedGamepadCount();
    stats_.maxQueuedEvents = stats_.maxQueuedEvents > eventCount_ ? stats_.maxQueuedEvents : eventCount_;
}

void InputSystem::pushEvent(const InputEvent& ev) {
    events_[eventHead_] = ev;
    eventHead_ = (eventHead_ + 1) % MAX_EVENTS;
    if (eventCount_ < MAX_EVENTS) eventCount_++;
    stats_.eventsProcessed++;
}

const InputEvent& InputSystem::event(u32 index) const {
    return events_[(eventHead_ + MAX_EVENTS - eventCount_ + index) % MAX_EVENTS];
}

const InputEvent* InputSystem::nextEvent(u32& cursor) const {
    if (cursor >= eventCount_) return nullptr;
    const InputEvent* e = &events_[(eventHead_ + MAX_EVENTS - eventCount_ + cursor) % MAX_EVENTS];
    cursor++;
    return e;
}

void InputSystem::onKey(Key key, bool down) {
    u32 idx = (u32)key;
    if (idx >= (u32)Key::COUNT) return;
    if (!keyRepeatEnabled_ && down && state_.keyDown[idx]) return;
    state_.setKey(key, down);

    InputEvent ev;
    ev.type = down ? InputEventType::KeyDown : InputEventType::KeyUp;
    ev.code = idx;
    ev.frame = frame_;
    ev.timestamp = (u64)(time_ * 1000.0f);
    pushEvent(ev);
}

void InputSystem::onMouseButton(MouseButton btn, bool down) {
    u32 idx = (u32)btn;
    if (idx >= (u32)MouseButton::COUNT) return;
    state_.setMouse(btn, down);

    InputEvent ev;
    ev.type = down ? InputEventType::MouseDown : InputEventType::MouseUp;
    ev.code = idx;
    ev.mouseX = state_.mouseX;
    ev.mouseY = state_.mouseY;
    ev.frame = frame_;
    ev.timestamp = (u64)(time_ * 1000.0f);
    pushEvent(ev);
}

void InputSystem::onMouseMove(i32 x, i32 y) {
    state_.mouseDeltaX += x - state_.mouseX;
    state_.mouseDeltaY += y - state_.mouseY;
    state_.mouseX = x;
    state_.mouseY = y;

    InputEvent ev;
    ev.type = InputEventType::MouseMove;
    ev.mouseX = x;
    ev.mouseY = y;
    ev.value = (f32)(state_.mouseDeltaX + state_.mouseDeltaY) * 0.5f;
    ev.frame = frame_;
    ev.timestamp = (u64)(time_ * 1000.0f);
    pushEvent(ev);
}

void InputSystem::onScroll(f32 delta) {
    state_.scrollDelta += delta;

    InputEvent ev;
    ev.type = InputEventType::Scroll;
    ev.value = delta;
    ev.frame = frame_;
    ev.timestamp = (u64)(time_ * 1000.0f);
    pushEvent(ev);
}

void InputSystem::onTextInput(const char* text) {
    if (!text || !text[0]) return;
    textInput_.append(text);

    InputEvent ev;
    ev.type = InputEventType::TextInput;
    strncpy(ev.text, text, sizeof(ev.text) - 1);
    ev.frame = frame_;
    ev.timestamp = (u64)(time_ * 1000.0f);
    pushEvent(ev);
}

void InputSystem::onGamepadConnect(u32 index, const char* name) {
    if (index >= MAX_GAMEPADS) return;
    GamepadState& pad = gamepads_[index];
    pad.connected = true;
    pad.deviceId = (i32)index + 1;
    pad.name = name ? name : "Gamepad";
    memset(pad.buttonDown, 0, sizeof(pad.buttonDown));
    memset(pad.buttonPressed, 0, sizeof(pad.buttonPressed));
    memset(pad.buttonReleased, 0, sizeof(pad.buttonReleased));
    memset(pad.axis, 0, sizeof(pad.axis));
    memset(pad.prevAxis, 0, sizeof(pad.prevAxis));

    InputEvent ev;
    ev.type = InputEventType::GamepadConnect;
    ev.deviceIndex = index;
    ev.frame = frame_;
    ev.timestamp = (u64)(time_ * 1000.0f);
    pushEvent(ev);
}

void InputSystem::onGamepadDisconnect(u32 index) {
    if (index >= MAX_GAMEPADS) return;
    GamepadState& pad = gamepads_[index];
    pad.connected = false;
    pad.deviceId = -1;
    pad.name.clear();
    memset(pad.buttonDown, 0, sizeof(pad.buttonDown));
    memset(pad.axis, 0, sizeof(pad.axis));
    memset(pad.prevAxis, 0, sizeof(pad.prevAxis));

    InputEvent ev;
    ev.type = InputEventType::GamepadDisconnect;
    ev.deviceIndex = index;
    ev.frame = frame_;
    ev.timestamp = (u64)(time_ * 1000.0f);
    pushEvent(ev);
}

void InputSystem::onGamepadButton(u32 index, GamepadButton btn, bool down) {
    if (index >= MAX_GAMEPADS) return;
    GamepadState& pad = gamepads_[index];
    if (!pad.connected) return;
    u32 idx = (u32)btn;
    if (idx >= (u32)GamepadButton::COUNT) return;
    pad.setButton(btn, down);
    pad.lastActiveTime = time_;

    InputEvent ev;
    ev.type = down ? InputEventType::GamepadDown : InputEventType::GamepadUp;
    ev.deviceIndex = index;
    ev.code = idx;
    ev.frame = frame_;
    ev.timestamp = (u64)(time_ * 1000.0f);
    pushEvent(ev);
}

void InputSystem::onGamepadAxis(u32 index, GamepadAxis axis, f32 value) {
    if (index >= MAX_GAMEPADS) return;
    GamepadState& pad = gamepads_[index];
    if (!pad.connected) return;
    u32 idx = (u32)axis;
    if (idx >= (u32)GamepadAxis::COUNT) return;

    f32 raw = value;
    f32 dz = gamepadDeadzone_;
    f32 magnitude = fabsf(raw);
    if (magnitude < dz) {
        raw = 0.0f;
    } else {
        f32 t = (magnitude - dz) / (1.0f - dz);
        raw = Mathf::sign(value) * t;
    }

    f32 prev = pad.axis[idx];
    pad.axis[idx] = raw;

    if (fabsf(raw - prev) > 0.0001f) {
        pad.lastActiveTime = time_;
        InputEvent ev;
        ev.type = InputEventType::GamepadAxis;
        ev.deviceIndex = index;
        ev.code = idx;
        ev.value = raw;
        ev.frame = frame_;
        ev.timestamp = (u64)(time_ * 1000.0f);
        pushEvent(ev);
    }

    if (axis == GamepadAxis::LeftX || axis == GamepadAxis::LeftY) {
        pad.leftStickMagnitude = sqrtf(pad.axis[(u32)GamepadAxis::LeftX] * pad.axis[(u32)GamepadAxis::LeftX] +
                                       pad.axis[(u32)GamepadAxis::LeftY] * pad.axis[(u32)GamepadAxis::LeftY]);
    } else if (axis == GamepadAxis::RightX || axis == GamepadAxis::RightY) {
        pad.rightStickMagnitude = sqrtf(pad.axis[(u32)GamepadAxis::RightX] * pad.axis[(u32)GamepadAxis::RightX] +
                                        pad.axis[(u32)GamepadAxis::RightY] * pad.axis[(u32)GamepadAxis::RightY]);
    }
}

// ---- Keyboard / mouse queries ----

bool InputSystem::isKeyDown(Key key) const {
    u32 idx = (u32)key;
    return idx < (u32)Key::COUNT && state_.keyDown[idx];
}

bool InputSystem::isKeyJustPressed(Key key) const {
    u32 idx = (u32)key;
    return idx < (u32)Key::COUNT && state_.keyPressed[idx];
}

bool InputSystem::isKeyJustReleased(Key key) const {
    u32 idx = (u32)key;
    return idx < (u32)Key::COUNT && state_.keyReleased[idx];
}

bool InputSystem::isMouseDown(MouseButton btn) const {
    u32 idx = (u32)btn;
    return idx < (u32)MouseButton::COUNT && state_.mouseDown[idx];
}

bool InputSystem::isMouseJustPressed(MouseButton btn) const {
    u32 idx = (u32)btn;
    return idx < (u32)MouseButton::COUNT && state_.mousePressed[idx];
}

bool InputSystem::isMouseJustReleased(MouseButton btn) const {
    u32 idx = (u32)btn;
    return idx < (u32)MouseButton::COUNT && state_.mouseReleased[idx];
}

// ---- Gamepad queries ----

GamepadState& InputSystem::gamepad(u32 index) {
    if (index >= MAX_GAMEPADS) return gamepads_[0];
    return gamepads_[index];
}

const GamepadState& InputSystem::gamepad(u32 index) const {
    if (index >= MAX_GAMEPADS) return gamepads_[0];
    return gamepads_[index];
}

bool InputSystem::isGamepadConnected(u32 index) const {
    return index < MAX_GAMEPADS && gamepads_[index].connected;
}

bool InputSystem::isGamepadButtonDown(u32 index, GamepadButton btn) const {
    if (index >= MAX_GAMEPADS) return false;
    u32 idx = (u32)btn;
    return idx < (u32)GamepadButton::COUNT && gamepads_[index].buttonDown[idx];
}

bool InputSystem::isGamepadButtonJustPressed(u32 index, GamepadButton btn) const {
    if (index >= MAX_GAMEPADS) return false;
    u32 idx = (u32)btn;
    return idx < (u32)GamepadButton::COUNT && gamepads_[index].buttonPressed[idx];
}

bool InputSystem::isGamepadButtonJustReleased(u32 index, GamepadButton btn) const {
    if (index >= MAX_GAMEPADS) return false;
    u32 idx = (u32)btn;
    return idx < (u32)GamepadButton::COUNT && gamepads_[index].buttonReleased[idx];
}

f32 InputSystem::gamepadAxis(u32 index, GamepadAxis axis) const {
    if (index >= MAX_GAMEPADS) return 0.0f;
    u32 idx = (u32)axis;
    if (idx >= (u32)GamepadAxis::COUNT) return 0.0f;
    return gamepads_[index].axis[idx];
}

bool InputSystem::isAnyGamepadButtonPressed(u32 index) const {
    if (index >= MAX_GAMEPADS) return false;
    const GamepadState& pad = gamepads_[index];
    for (u32 i = 0; i < (u32)GamepadButton::COUNT; i++) {
        if (pad.buttonPressed[i]) return true;
    }
    return false;
}

u32 InputSystem::connectedGamepadCount() const {
    u32 count = 0;
    for (u32 i = 0; i < MAX_GAMEPADS; i++) {
        if (gamepads_[i].connected) count++;
    }
    return count;
}

void InputSystem::setRumble(u32 index, f32 low, f32 high, f32 duration) {
    if (index >= MAX_GAMEPADS) return;
    GamepadState& pad = gamepads_[index];
    pad.rumbleLow = Mathf::saturate(low);
    pad.rumbleHigh = Mathf::saturate(high);
    pad.rumbleDuration = duration > 0.0f ? duration : pad.rumbleDuration;
}

// ---- Action bindings ----

u32 InputSystem::createAction(const char* name, InputActionType type) {
    if (actionCount_ >= MAX_ACTIONS) return 0;
    u32 id = actionCount_ + 1;
    InputAction& action = actions_[actionCount_];
    action.name = name ? name : "";
    action.type = type;
    action.held = false;
    action.heldLast = false;
    action.triggeredThisFrame = false;
    action.releasedThisFrame = false;
    action.value = 0.0f;
    action.value2D = Vec2(0, 0);
    action.holdTime = 0.0f;
    action.lastTriggeredTime = 0.0f;
    action.lastTriggeredFrame = 0;
    action.bindings.clear();
    actionCount_++;
    return id;
}

bool InputSystem::bindAction(u32 actionId, const InputBinding& binding) {
    if (actionId == 0 || actionId > actionCount_) return false;
    InputAction& action = actions_[actionId - 1];
    action.bindings.push_back(binding);
    return true;
}

bool InputSystem::bindKey(u32 actionId, Key key, f32 scale) {
    InputBinding b;
    b.source = InputAxisSource::Key;
    b.code = (u32)key;
    b.scale = scale;
    return bindAction(actionId, b);
}

bool InputSystem::bindGamepadButton(u32 actionId, GamepadButton btn, u32 padIndex) {
    InputBinding b;
    b.source = InputAxisSource::GamepadButton;
    b.code = (u32)btn;
    b.gamepadIndex = (i32)padIndex;
    return bindAction(actionId, b);
}

bool InputSystem::bindGamepadAxis(u32 actionId, GamepadAxis axis, u32 padIndex, f32 deadzone) {
    InputBinding b;
    b.source = InputAxisSource::GamepadAxis;
    b.code = (u32)axis;
    b.gamepadIndex = (i32)padIndex;
    b.deadzone = deadzone;
    return bindAction(actionId, b);
}

bool InputSystem::bindMouseAxis(u32 actionId, bool horizontal) {
    InputBinding b;
    b.source = InputAxisSource::MouseDelta;
    b.code = horizontal ? 0 : 1;
    return bindAction(actionId, b);
}

bool InputSystem::bindScroll(u32 actionId) {
    InputBinding b;
    b.source = InputAxisSource::MouseScroll;
    return bindAction(actionId, b);
}

u32 InputSystem::findAction(const char* name) const {
    if (!name) return 0;
    for (u32 i = 0; i < actionCount_; i++) {
        if (actions_[i].name == name) return i + 1;
    }
    return 0;
}

bool InputSystem::isActionHeld(u32 actionId) const {
    if (actionId == 0 || actionId > actionCount_) return false;
    return actions_[actionId - 1].held;
}

bool InputSystem::isActionPressed(u32 actionId) const {
    if (actionId == 0 || actionId > actionCount_) return false;
    return actions_[actionId - 1].triggeredThisFrame;
}

bool InputSystem::isActionReleased(u32 actionId) const {
    if (actionId == 0 || actionId > actionCount_) return false;
    return actions_[actionId - 1].releasedThisFrame;
}

f32 InputSystem::getActionValue(u32 actionId) const {
    if (actionId == 0 || actionId > actionCount_) return 0.0f;
    return actions_[actionId - 1].value;
}

Vec2 InputSystem::getActionVector(u32 actionId) const {
    if (actionId == 0 || actionId > actionCount_) return Vec2(0, 0);
    return actions_[actionId - 1].value2D;
}

f32 InputSystem::getActionHoldTime(u32 actionId) const {
    if (actionId == 0 || actionId > actionCount_) return 0.0f;
    return actions_[actionId - 1].holdTime;
}

// ---- Internal ----

f32 InputSystem::applyDeadzone(f32 value, f32 deadzone) const {
    f32 mag = fabsf(value);
    if (mag <= deadzone) return 0.0f;
    return Mathf::sign(value) * (mag - deadzone) / (1.0f - deadzone);
}

void InputSystem::evaluateActions(f32 dt) {
    u32 activeActions = 0;

    for (u32 i = 0; i < actionCount_; i++) {
        InputAction& action = actions_[i];
        f32 scalar = 0.0f;
        Vec2 vec(0, 0);

        for (const InputBinding& b : action.bindings) {
            u32 padIndex = b.gamepadIndex >= 0 ? (u32)b.gamepadIndex : 0;
            if (padIndex >= MAX_GAMEPADS) padIndex = 0;

            switch (b.source) {
            case InputAxisSource::Key:
                if (b.code < (u32)Key::COUNT && state_.keyDown[b.code]) scalar += b.scale;
                break;
            case InputAxisSource::MouseButton:
                if (b.code < (u32)MouseButton::COUNT && state_.mouseDown[b.code]) scalar += b.scale;
                break;
            case InputAxisSource::GamepadButton:
                if (b.code < (u32)GamepadButton::COUNT && gamepads_[padIndex].buttonDown[b.code]) scalar += b.scale;
                break;
            case InputAxisSource::GamepadAxis: {
                if (b.code >= (u32)GamepadAxis::COUNT) break;
                f32 dz = b.deadzone > 0.0f ? b.deadzone : gamepadDeadzone_;
                f32 v = applyDeadzone(gamepads_[padIndex].axis[b.code], dz) * b.scale;
                if (action.type == InputActionType::Axis2D) {
                    if (b.code == (u32)GamepadAxis::LeftX || b.code == (u32)GamepadAxis::RightX) vec.x += v;
                    else if (b.code == (u32)GamepadAxis::LeftY || b.code == (u32)GamepadAxis::RightY) vec.y += v;
                    else scalar += v;
                } else {
                    scalar += v;
                }
                break;
            }
            case InputAxisSource::MouseDelta: {
                f32 v = (b.code == 0 ? (f32)state_.mouseDeltaX : (f32)state_.mouseDeltaY) * mouseSensitivity_ * b.scale;
                if (action.type == InputActionType::Axis2D) {
                    if (b.code == 0) vec.x += v;
                    else vec.y += v;
                } else {
                    scalar += v;
                }
                break;
            }
            case InputAxisSource::MouseScroll:
                scalar += state_.scrollDelta * b.scale;
                break;
            case InputAxisSource::None:
            default:
                break;
            }
        }

        f32 vecLen = vec.length();
        if (vecLen > 1.0f) {
            vec.x /= vecLen;
            vec.y /= vecLen;
        }

        f32 value = (action.type == InputActionType::Button) ? (scalar != 0.0f ? 1.0f : 0.0f) : scalar;
        if (action.type == InputActionType::Axis1D) value = Mathf::clamp(value, -1.0f, 1.0f);

        bool heldNow = false;
        if (action.type == InputActionType::Axis2D) {
            heldNow = vecLen > 0.001f;
        } else {
            heldNow = fabsf(value) > 0.001f;
        }

        action.triggeredThisFrame = heldNow && !action.heldLast;
        action.releasedThisFrame = !heldNow && action.heldLast;
        action.held = heldNow;
        action.heldLast = heldNow;
        action.value = value;
        action.value2D = vec;

        if (action.triggeredThisFrame) {
            action.lastTriggeredTime = time_;
            action.lastTriggeredFrame = frame_;
        }
        action.holdTime = heldNow ? (time_ - action.lastTriggeredTime) : 0.0f;

        if (!action.bindings.empty()) activeActions++;
    }

    stats_.activeActions = activeActions;
}

} // namespace Frost
