#pragma once

// ============================================================================
// FrostEngine ScriptBridge — C++ <-> Kitris interop
// ============================================================================
// Provides seamless binding between C++ engine types and Kitris VM.
// Uses template metaprogramming for zero-overhead bindings.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Math.h"
#include "Core/ECS.h"
#include "Renderer/Camera.h"
#include "Renderer/Material.h"
#include "Scripting/KitrisVM.h"

namespace Frost {
namespace Kitris {

// ---- Type traits for binding ----
template<typename T> struct KitrisType { static constexpr ValueType value = ValueType::Object; };
template<> struct KitrisType<bool> { static constexpr ValueType value = ValueType::Bool; };
template<> struct KitrisType<i32> { static constexpr ValueType value = ValueType::Int; };
template<> struct KitrisType<i64> { static constexpr ValueType value = ValueType::Int; };
template<> struct KitrisType<u32> { static constexpr ValueType value = ValueType::Int; };
template<> struct KitrisType<u64> { static constexpr ValueType value = ValueType::Int; };
template<> struct KitrisType<f32> { static constexpr ValueType value = ValueType::Float; };
template<> struct KitrisType<f64> { static constexpr ValueType value = ValueType::Float; };
template<> struct KitrisType<Vec2> { static constexpr ValueType value = ValueType::Vec2; };
template<> struct KitrisType<Vec3> { static constexpr ValueType value = ValueType::Vec3; };
template<> struct KitrisType<Vec4> { static constexpr ValueType value = ValueType::Vec4; };
template<> struct KitrisType<Mat4> { static constexpr ValueType value = ValueType::Mat4; };
template<> struct KitrisType<Quat> { static constexpr ValueType value = ValueType::Quat; };
template<> struct KitrisType<Color> { static constexpr ValueType value = ValueType::Color; };
template<> struct KitrisType<String> { static constexpr ValueType value = ValueType::String; };

// ---- C++ to Kitris conversion ----
template<typename T>
Value toKitris(const T& value) {
    return Value(value);
}

// Specializations
template<> inline Value toKitris<String>(const String& value) {
    return Value(value);
}
template<> inline Value toKitris<Vec2>(const Vec2& value) {
    return Value(value);
}
template<> inline Value toKitris<Vec3>(const Vec3& value) {
    return Value(value);
}
template<> inline Value toKitris<Vec4>(const Vec4& value) {
    return Value(value);
}
template<> inline Value toKitris<Mat4>(const Mat4& value) {
    return Value(value);
}
template<> inline Value toKitris<Quat>(const Quat& value) {
    return Value(value);
}
template<> inline Value toKitris<Color>(const Color& value) {
    return Value(value);
}

// ---- Kitris to C++ conversion ----
template<typename T>
T fromKitris(const Value& value) {
    return T();
}

template<> inline bool fromKitris<bool>(const Value& value) {
    return value.type == ValueType::Bool ? value.boolVal : value.isTruthy();
}

template<> inline i32 fromKitris<i32>(const Value& value) {
    if (value.type == ValueType::Int) return (i32)value.intVal;
    if (value.type == ValueType::Float) return (i32)value.floatVal;
    return 0;
}

template<> inline f32 fromKitris<f32>(const Value& value) {
    if (value.type == ValueType::Float) return (f32)value.floatVal;
    if (value.type == ValueType::Int) return (f32)value.intVal;
    return 0.0f;
}

template<> inline Vec2 fromKitris<Vec2>(const Value& value) {
    if (value.type == ValueType::Vec2 && value.vec2Val) return *value.vec2Val;
    return Vec2(0, 0);
}

template<> inline Vec3 fromKitris<Vec3>(const Value& value) {
    if (value.type == ValueType::Vec3 && value.vec3Val) return *value.vec3Val;
    return Vec3(0, 0, 0);
}

template<> inline Vec4 fromKitris<Vec4>(const Value& value) {
    if (value.type == ValueType::Vec4 && value.vec4Val) return *value.vec4Val;
    return Vec4(0, 0, 0, 0);
}

template<> inline Mat4 fromKitris<Mat4>(const Value& value) {
    if (value.type == ValueType::Mat4 && value.mat4Val) return *value.mat4Val;
    return Mat4::identity();
}

template<> inline Quat fromKitris<Quat>(const Value& value) {
    if (value.type == ValueType::Quat && value.quatVal) return *value.quatVal;
    return Quat::identity();
}

template<> inline Color fromKitris<Color>(const Value& value) {
    if (value.type == ValueType::Color && value.colorVal) return *value.colorVal;
    return Color(1, 1, 1, 1);
}

// ---- Function binding ----
template<typename Ret, typename... Args>
struct FunctionBinder;

template<typename Ret, typename... Args>
struct FunctionBinder<Ret(Args...)> {
    using FnPtr = Ret(*)(Args...);

    static Value call(VM& vm, Value* args, u32 argc, FnPtr fn) {
        return invoke(vm, args, argc, fn, std::index_sequence_for<Args...>{});
    }

private:
    template<std::size_t... I>
    static Value invoke(VM& vm, Value* args, u32 argc, FnPtr fn, std::index_sequence<I...>) {
        if (argc != sizeof...(Args)) {
            vm.runtimeError("Argument count mismatch: expected %zu, got %u", sizeof...(Args), argc);
            return Value();
        }
        if constexpr (std::is_void_v<Ret>) {
            fn(fromKitris<Args>(args[I])...);
            return Value();
        } else {
            return toKitris(fn(fromKitris<Args>(args[I])...));
        }
    }
};

// ---- Member function binding ----
template<typename T>
struct MemberFunctionBinder;

template<typename Class, typename Ret, typename... Args>
struct MemberFunctionBinder<Ret(Class::*)(Args...)> {
    using FnPtr = Ret(Class::*)(Args...);

    static Value call(VM& vm, Value* args, u32 argc, FnPtr fn) {
        if (argc < 1 + sizeof...(Args)) {
            vm.runtimeError("Missing 'this' argument for member function");
            return Value();
        }
        // First arg is 'this' - would need object wrapping
        return Value();
    }
};

// ---- Class registration ----
class ClassRegistry {
public:
    template<typename T>
    static void registerClass(VM& vm, const String& name) {
        // Register type info, constructors, methods, properties
    }

    template<typename T, typename... Args>
    static void registerConstructor(VM& vm, const String& name) {
        // Register constructor
    }

    template<typename T, typename Ret, typename... Args>
    static void registerMethod(VM& vm, const String& className, const String& methodName, Ret(T::*method)(Args...)) {
        // Register member function
    }

    template<typename T, typename PropType>
    static void registerProperty(VM& vm, const String& className, const String& propName, PropType T::*prop) {
        // Register property getter/setter
    }
};

// ---- Engine API registration ----
class EngineBridge {
public:
    static void registerAll(VM& vm, void* enginePtr);

    // Entity system
    static Value ecs_createEntity(VM&, Value*, u32);
    static Value ecs_destroyEntity(VM&, Value*, u32);
    static Value ecs_hasComponent(VM&, Value*, u32);
    static Value ecs_getComponent(VM&, Value*, u32);
    static Value ecs_setComponent(VM&, Value*, u32);
    static Value ecs_addComponent(VM&, Value*, u32);
    static Value ecs_removeComponent(VM&, Value*, u32);
    static Value ecs_query(VM&, Value*, u32);

    // Transform
    static Value transform_getPosition(VM&, Value*, u32);
    static Value transform_setPosition(VM&, Value*, u32);
    static Value transform_getRotation(VM&, Value*, u32);
    static Value transform_setRotation(VM&, Value*, u32);
    static Value transform_getScale(VM&, Value*, u32);
    static Value transform_setScale(VM&, Value*, u32);
    static Value transform_translate(VM&, Value*, u32);
    static Value transform_rotate(VM&, Value*, u32);
    static Value transform_lookAt(VM&, Value*, u32);

    // Renderer
    static Value renderer_setMesh(VM&, Value*, u32);
    static Value renderer_setMaterial(VM&, Value*, u32);
    static Value renderer_addLight(VM&, Value*, u32);
    static Value renderer_setCamera(VM&, Value*, u32);

    // Input
    static Value input_keyDown(VM&, Value*, u32);
    static Value input_keyPressed(VM&, Value*, u32);
    static Value input_mousePosition(VM&, Value*, u32);
    static Value input_mouseDelta(VM&, Value*, u32);

    // Time
    static Value time_delta(VM&, Value*, u32);
    static Value time_now(VM&, Value*, u32);
    static Value time_frame(VM&, Value*, u32);

    // Math
    static Value math_sin(VM&, Value*, u32);
    static Value math_cos(VM&, Value*, u32);
    static Value math_lerp(VM&, Value*, u32);

    // Debug
    static Value debug_log(VM&, Value*, u32);
    static Value debug_drawLine(VM&, Value*, u32);
    static Value debug_drawBox(VM&, Value*, u32);
    static Value debug_drawSphere(VM&, Value*, u32);

    // Physics
    static Value physics_applyForce(VM&, Value*, u32);
    static Value physics_applyImpulse(VM&, Value*, u32);
    static Value physics_setVelocity(VM&, Value*, u32);
    static Value physics_getVelocity(VM&, Value*, u32);
};

}
}