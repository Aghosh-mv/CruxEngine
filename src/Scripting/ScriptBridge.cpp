#include "FrostEngine/Scripting/ScriptBridge.h"

namespace Frost {
namespace Kitris {

void EngineBridge::registerAll(VM& vm, void* enginePtr) {
    (void)enginePtr;

    // Entity system
    vm.registerNative("ecs_createEntity", ecs_createEntity);
    vm.registerNative("ecs_destroyEntity", ecs_destroyEntity);
    vm.registerNative("ecs_hasComponent", ecs_hasComponent);
    vm.registerNative("ecs_getComponent", ecs_getComponent);
    vm.registerNative("ecs_setComponent", ecs_setComponent);
    vm.registerNative("ecs_addComponent", ecs_addComponent);
    vm.registerNative("ecs_removeComponent", ecs_removeComponent);
    vm.registerNative("ecs_query", ecs_query);

    // Transform
    vm.registerNative("transform_getPosition", transform_getPosition);
    vm.registerNative("transform_setPosition", transform_setPosition);
    vm.registerNative("transform_getRotation", transform_getRotation);
    vm.registerNative("transform_setRotation", transform_setRotation);
    vm.registerNative("transform_getScale", transform_getScale);
    vm.registerNative("transform_setScale", transform_setScale);
    vm.registerNative("transform_translate", transform_translate);
    vm.registerNative("transform_rotate", transform_rotate);
    vm.registerNative("transform_lookAt", transform_lookAt);

    // Renderer
    vm.registerNative("renderer_setMesh", renderer_setMesh);
    vm.registerNative("renderer_setMaterial", renderer_setMaterial);
    vm.registerNative("renderer_addLight", renderer_addLight);
    vm.registerNative("renderer_setCamera", renderer_setCamera);

    // Input
    vm.registerNative("input_keyDown", input_keyDown);
    vm.registerNative("input_keyPressed", input_keyPressed);
    vm.registerNative("input_mousePosition", input_mousePosition);
    vm.registerNative("input_mouseDelta", input_mouseDelta);

    // Time
    vm.registerNative("time_delta", time_delta);
    vm.registerNative("time_now", time_now);
    vm.registerNative("time_frame", time_frame);

    // Math
    vm.registerNative("math_sin", math_sin);
    vm.registerNative("math_cos", math_cos);
    vm.registerNative("math_lerp", math_lerp);

    // Debug
    vm.registerNative("debug_log", debug_log);
    vm.registerNative("debug_drawLine", debug_drawLine);
    vm.registerNative("debug_drawBox", debug_drawBox);
    vm.registerNative("debug_drawSphere", debug_drawSphere);

    // Physics
    vm.registerNative("physics_applyForce", physics_applyForce);
    vm.registerNative("physics_applyImpulse", physics_applyImpulse);
    vm.registerNative("physics_setVelocity", physics_setVelocity);
    vm.registerNative("physics_getVelocity", physics_getVelocity);
}

// ---- Entity system ----
Value EngineBridge::ecs_createEntity(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value((i64)0);
}

Value EngineBridge::ecs_destroyEntity(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::ecs_hasComponent(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(false);
}

Value EngineBridge::ecs_getComponent(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::ecs_setComponent(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::ecs_addComponent(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::ecs_removeComponent(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::ecs_query(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    Value v;
    v.type = ValueType::Array;
    v.arrayVal = new Vector<Value>();
    return v;
}

// ---- Transform ----
Value EngineBridge::transform_getPosition(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(Vec3(0, 0, 0));
}

Value EngineBridge::transform_setPosition(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::transform_getRotation(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(Quat::identity());
}

Value EngineBridge::transform_setRotation(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::transform_getScale(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(Vec3(1, 1, 1));
}

Value EngineBridge::transform_setScale(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::transform_translate(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::transform_rotate(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::transform_lookAt(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

// ---- Renderer ----
Value EngineBridge::renderer_setMesh(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::renderer_setMaterial(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::renderer_addLight(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value((i64)0);
}

Value EngineBridge::renderer_setCamera(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

// ---- Input ----
Value EngineBridge::input_keyDown(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(false);
}

Value EngineBridge::input_keyPressed(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(false);
}

Value EngineBridge::input_mousePosition(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(Vec2(0, 0));
}

Value EngineBridge::input_mouseDelta(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(Vec2(0, 0));
}

// ---- Time ----
Value EngineBridge::time_delta(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value((f64)0.016);
}

Value EngineBridge::time_now(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value((f64)0.0);
}

Value EngineBridge::time_frame(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value((i64)0);
}

// ---- Math ----
Value EngineBridge::math_sin(VM& vm, Value* args, u32 argc) {
    (void)vm;
    if (argc < 1) return Value((f64)0.0);
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(sinf((f32)x));
}

Value EngineBridge::math_cos(VM& vm, Value* args, u32 argc) {
    (void)vm;
    if (argc < 1) return Value((f64)0.0);
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(cosf((f32)x));
}

Value EngineBridge::math_lerp(VM& vm, Value* args, u32 argc) {
    (void)vm;
    if (argc < 3) return Value((f64)0.0);
    f64 a = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    f64 b = (args[1].type == ValueType::Float) ? args[1].floatVal : (f64)args[1].intVal;
    f64 t = (args[2].type == ValueType::Float) ? args[2].floatVal : (f64)args[2].intVal;
    return Value(a + (b - a) * t);
}

// ---- Debug ----
Value EngineBridge::debug_log(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::debug_drawLine(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::debug_drawBox(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::debug_drawSphere(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

// ---- Physics ----
Value EngineBridge::physics_applyForce(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::physics_applyImpulse(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::physics_setVelocity(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value EngineBridge::physics_getVelocity(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(Vec3(0, 0, 0));
}

}
}
