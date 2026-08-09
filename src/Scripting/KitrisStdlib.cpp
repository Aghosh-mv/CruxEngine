#include "FrostEngine/Scripting/KitrisStdlib.h"
#include <cmath>
#include <cstdlib>

namespace Frost {
namespace Kitris {

void Stdlib::registerAll(VM& vm) {
    // Math
    vm.registerNative("math_sin", math_sin);
    vm.registerNative("math_cos", math_cos);
    vm.registerNative("math_tan", math_tan);
    vm.registerNative("math_asin", math_asin);
    vm.registerNative("math_acos", math_acos);
    vm.registerNative("math_atan2", math_atan2);
    vm.registerNative("math_sqrt", math_sqrt);
    vm.registerNative("math_log", math_log);
    vm.registerNative("math_exp", math_exp);
    vm.registerNative("math_pow", math_pow);
    vm.registerNative("math_floor", math_floor);
    vm.registerNative("math_ceil", math_ceil);
    vm.registerNative("math_round", math_round);
    vm.registerNative("math_abs", math_abs);
    vm.registerNative("math_min", math_min);
    vm.registerNative("math_max", math_max);
    vm.registerNative("math_clamp", math_clamp);
    vm.registerNative("math_lerp", math_lerp);
    vm.registerNative("math_smoothstep", math_smoothstep);

    // Random
    vm.registerNative("random_int", random_int);
    vm.registerNative("random_float", random_float);
    vm.registerNative("random_vec3", random_vec3);
    vm.registerNative("random_in_circle", random_in_circle);
    vm.registerNative("random_in_sphere", random_in_sphere);

    // Vectors
    vm.registerNative("vec2_new", vec2_new);
    vm.registerNative("vec3_new", vec3_new);
    vm.registerNative("vec4_new", vec4_new);
    vm.registerNative("vec_add", vec_add);
    vm.registerNative("vec_sub", vec_sub);
    vm.registerNative("vec_mul", vec_mul);
    vm.registerNative("vec_div", vec_div);
    vm.registerNative("vec_dot", vec_dot);
    vm.registerNative("vec_cross", vec_cross);
    vm.registerNative("vec_len", vec_len);
    vm.registerNative("vec_normalize", vec_normalize);
    vm.registerNative("vec_lerp", vec_lerp);
    vm.registerNative("vec_distance", vec_distance);
    vm.registerNative("vec_reflect", vec_reflect);

    // Matrices
    vm.registerNative("mat4_identity", mat4_identity);
    vm.registerNative("mat4_translation", mat4_translation);
    vm.registerNative("mat4_rotation", mat4_rotation);
    vm.registerNative("mat4_scaling", mat4_scaling);
    vm.registerNative("mat4_mul", mat4_mul);
    vm.registerNative("mat4_inverse", mat4_inverse);
    vm.registerNative("mat4_lookat", mat4_lookat);
    vm.registerNative("mat4_perspective", mat4_perspective);

    // Quaternions
    vm.registerNative("quat_identity", quat_identity);
    vm.registerNative("quat_from_euler", quat_from_euler);
    vm.registerNative("quat_mul", quat_mul);
    vm.registerNative("quat_slerp", quat_slerp);
    vm.registerNative("quat_look_rotation", quat_look_rotation);

    // Colors
    vm.registerNative("color_rgb", color_rgb);
    vm.registerNative("color_rgba", color_rgba);
    vm.registerNative("color_lerp", color_lerp);
    vm.registerNative("color_hsv_to_rgb", color_hsv_to_rgb);

    // Collections
    vm.registerNative("array_new", array_new);
    vm.registerNative("array_push", array_push);
    vm.registerNative("array_pop", array_pop);
    vm.registerNative("array_len", array_len);
    vm.registerNative("array_get", array_get);
    vm.registerNative("array_set", array_set);
    vm.registerNative("array_clear", array_clear);
    vm.registerNative("map_new", map_new);
    vm.registerNative("map_set", map_set);
    vm.registerNative("map_get", map_get);
    vm.registerNative("map_has", map_has);
    vm.registerNative("map_remove", map_remove);
    vm.registerNative("map_keys", map_keys);

    // Strings
    vm.registerNative("string_new", string_new);
    vm.registerNative("string_len", string_len);
    vm.registerNative("string_sub", string_sub);
    vm.registerNative("string_find", string_find);
    vm.registerNative("string_split", string_split);
    vm.registerNative("string_join", string_join);
    vm.registerNative("string_format", string_format);
    vm.registerNative("string_upper", string_upper);
    vm.registerNative("string_lower", string_lower);

    // Engine API
    vm.registerNative("entity_spawn", entity_spawn);
    vm.registerNative("entity_despawn", entity_despawn);
    vm.registerNative("entity_find", entity_find);
    vm.registerNative("entity_query", entity_query);
    vm.registerNative("component_get", component_get);
    vm.registerNative("component_set", component_set);
    vm.registerNative("component_add", component_add);
    vm.registerNative("component_remove", component_remove);
    vm.registerNative("transform_get", transform_get);
    vm.registerNative("transform_set", transform_set);
    vm.registerNative("transform_translate", transform_translate);
    vm.registerNative("transform_rotate", transform_rotate);
    vm.registerNative("transform_scale", transform_scale);
    vm.registerNative("mesh_set", mesh_set);
    vm.registerNative("material_set", material_set);
    vm.registerNative("light_set", light_set);
    vm.registerNative("camera_set", camera_set);
    vm.registerNative("physics_force", physics_force);
    vm.registerNative("physics_impulse", physics_impulse);
    vm.registerNative("input_key_down", input_key_down);
    vm.registerNative("input_key_pressed", input_key_pressed);
    vm.registerNative("input_mouse_pos", input_mouse_pos);
    vm.registerNative("time_delta", time_delta);
    vm.registerNative("time_now", time_now);
    vm.registerNative("debug_log", debug_log);
    vm.registerNative("debug_draw_line", debug_draw_line);
    vm.registerNative("debug_draw_box", debug_draw_box);

    // Coroutines
    vm.registerNative("coroutine_spawn", coroutine_spawn);
    vm.registerNative("coroutine_yield", coroutine_yield);
    vm.registerNative("coroutine_wait", coroutine_wait);
    vm.registerNative("coroutine_wait_seconds", coroutine_wait_seconds);

    // Channels
    vm.registerNative("channel_new", channel_new);
    vm.registerNative("channel_send", channel_send);
    vm.registerNative("channel_receive", channel_receive);
    vm.registerNative("channel_close", channel_close);
}

// ---- Math ----
Value Stdlib::math_sin(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("math_sin requires 1 argument"); return Value(); }
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(sinf((f32)x));
}

Value Stdlib::math_cos(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("math_cos requires 1 argument"); return Value(); }
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(cosf((f32)x));
}

Value Stdlib::math_tan(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("math_tan requires 1 argument"); return Value(); }
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(tanf((f32)x));
}

Value Stdlib::math_asin(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("math_asin requires 1 argument"); return Value(); }
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(asinf((f32)x));
}

Value Stdlib::math_acos(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("math_acos requires 1 argument"); return Value(); }
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(acosf((f32)x));
}

Value Stdlib::math_atan2(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("math_atan2 requires 2 arguments"); return Value(); }
    f64 y = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    f64 x = (args[1].type == ValueType::Float) ? args[1].floatVal : (f64)args[1].intVal;
    return Value(atan2f((f32)y, (f32)x));
}

Value Stdlib::math_sqrt(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("math_sqrt requires 1 argument"); return Value(); }
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(sqrtf((f32)x));
}

Value Stdlib::math_log(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("math_log requires 1 argument"); return Value(); }
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(logf((f32)x));
}

Value Stdlib::math_exp(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("math_exp requires 1 argument"); return Value(); }
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(expf((f32)x));
}

Value Stdlib::math_pow(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("math_pow requires 2 arguments"); return Value(); }
    f64 base = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    f64 exp = (args[1].type == ValueType::Float) ? args[1].floatVal : (f64)args[1].intVal;
    return Value(powf((f32)base, (f32)exp));
}

Value Stdlib::math_floor(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("math_floor requires 1 argument"); return Value(); }
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(floorf((f32)x));
}

Value Stdlib::math_ceil(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("math_ceil requires 1 argument"); return Value(); }
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(ceilf((f32)x));
}

Value Stdlib::math_round(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("math_round requires 1 argument"); return Value(); }
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(roundf((f32)x));
}

Value Stdlib::math_abs(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("math_abs requires 1 argument"); return Value(); }
    if (args[0].type == ValueType::Int) return Value(args[0].intVal < 0 ? -args[0].intVal : args[0].intVal);
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    return Value(fabsf((f32)x));
}

Value Stdlib::math_min(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("math_min requires 2 arguments"); return Value(); }
    f64 a = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    f64 b = (args[1].type == ValueType::Float) ? args[1].floatVal : (f64)args[1].intVal;
    return Value(a < b ? a : b);
}

Value Stdlib::math_max(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("math_max requires 2 arguments"); return Value(); }
    f64 a = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    f64 b = (args[1].type == ValueType::Float) ? args[1].floatVal : (f64)args[1].intVal;
    return Value(a > b ? a : b);
}

Value Stdlib::math_clamp(VM& vm, Value* args, u32 argc) {
    if (argc < 3) { vm.runtimeError("math_clamp requires 3 arguments"); return Value(); }
    f64 x = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    f64 lo = (args[1].type == ValueType::Float) ? args[1].floatVal : (f64)args[1].intVal;
    f64 hi = (args[2].type == ValueType::Float) ? args[2].floatVal : (f64)args[2].intVal;
    return Value(x < lo ? lo : (x > hi ? hi : x));
}

Value Stdlib::math_lerp(VM& vm, Value* args, u32 argc) {
    if (argc < 3) { vm.runtimeError("math_lerp requires 3 arguments"); return Value(); }
    f64 a = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    f64 b = (args[1].type == ValueType::Float) ? args[1].floatVal : (f64)args[1].intVal;
    f64 t = (args[2].type == ValueType::Float) ? args[2].floatVal : (f64)args[2].intVal;
    return Value(a + (b - a) * t);
}

Value Stdlib::math_smoothstep(VM& vm, Value* args, u32 argc) {
    if (argc < 3) { vm.runtimeError("math_smoothstep requires 3 arguments"); return Value(); }
    f64 edge0 = (args[0].type == ValueType::Float) ? args[0].floatVal : (f64)args[0].intVal;
    f64 edge1 = (args[1].type == ValueType::Float) ? args[1].floatVal : (f64)args[1].intVal;
    f64 x = (args[2].type == ValueType::Float) ? args[2].floatVal : (f64)args[2].intVal;
    f32 t = (f32)((x - edge0) / (edge1 - edge0));
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return Value(t * t * (3.0f - 2.0f * t));
}

// ---- Random ----
Value Stdlib::random_int(VM& vm, Value* args, u32 argc) {
    (void)vm;
    i64 lo = (argc > 0) ? (args[0].type == ValueType::Int ? args[0].intVal : 0) : 0;
    i64 hi = (argc > 1) ? (args[1].type == ValueType::Int ? args[1].intVal : 100) : 100;
    return Value(lo + (rand() % (i64)(hi - lo + 1)));
}

Value Stdlib::random_float(VM& vm, Value* args, u32 argc) {
    (void)vm;
    (void)args;
    (void)argc;
    return Value((f64)rand() / (f64)RAND_MAX);
}

Value Stdlib::random_vec3(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(Vec3(
        (f32)rand() / (f32)RAND_MAX * 2.0f - 1.0f,
        (f32)rand() / (f32)RAND_MAX * 2.0f - 1.0f,
        (f32)rand() / (f32)RAND_MAX * 2.0f - 1.0f
    ));
}

Value Stdlib::random_in_circle(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    f32 angle = (f32)rand() / (f32)RAND_MAX * 6.2831853f;
    f32 r = sqrtf((f32)rand() / (f32)RAND_MAX);
    return Value(Vec2(cosf(angle) * r, sinf(angle) * r));
}

Value Stdlib::random_in_sphere(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    f32 theta = (f32)rand() / (f32)RAND_MAX * 6.2831853f;
    f32 phi = acosf(2.0f * (f32)rand() / (f32)RAND_MAX - 1.0f);
    f32 r = cbrtf((f32)rand() / (f32)RAND_MAX);
    return Value(Vec3(
        r * sinf(phi) * cosf(theta),
        r * sinf(phi) * sinf(theta),
        r * cosf(phi)
    ));
}

// ---- Vectors ----
Value Stdlib::vec2_new(VM& vm, Value* args, u32 argc) {
    (void)vm;
    f32 x = (argc > 0 && args[0].type == ValueType::Float) ? (f32)args[0].floatVal : 0.0f;
    f32 y = (argc > 1 && args[1].type == ValueType::Float) ? (f32)args[1].floatVal : 0.0f;
    return Value(Vec2(x, y));
}

Value Stdlib::vec3_new(VM& vm, Value* args, u32 argc) {
    (void)vm;
    f32 x = (argc > 0) ? ((args[0].type == ValueType::Float) ? (f32)args[0].floatVal : (f32)args[0].intVal) : 0.0f;
    f32 y = (argc > 1) ? ((args[1].type == ValueType::Float) ? (f32)args[1].floatVal : (f32)args[1].intVal) : 0.0f;
    f32 z = (argc > 2) ? ((args[2].type == ValueType::Float) ? (f32)args[2].floatVal : (f32)args[2].intVal) : 0.0f;
    return Value(Vec3(x, y, z));
}

Value Stdlib::vec4_new(VM& vm, Value* args, u32 argc) {
    (void)vm;
    f32 x = (argc > 0) ? ((args[0].type == ValueType::Float) ? (f32)args[0].floatVal : (f32)args[0].intVal) : 0.0f;
    f32 y = (argc > 1) ? ((args[1].type == ValueType::Float) ? (f32)args[1].floatVal : (f32)args[1].intVal) : 0.0f;
    f32 z = (argc > 2) ? ((args[2].type == ValueType::Float) ? (f32)args[2].floatVal : (f32)args[2].intVal) : 0.0f;
    f32 w = (argc > 3) ? ((args[3].type == ValueType::Float) ? (f32)args[3].floatVal : (f32)args[3].intVal) : 0.0f;
    return Value(Vec4(x, y, z, w));
}

Value Stdlib::vec_add(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("vec_add requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::Vec3 && args[1].type == ValueType::Vec3) {
        return Value(*args[0].vec3Val + *args[1].vec3Val);
    }
    if (args[0].type == ValueType::Vec2 && args[1].type == ValueType::Vec2) {
        return Value(*args[0].vec2Val + *args[1].vec2Val);
    }
    return Value();
}

Value Stdlib::vec_sub(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("vec_sub requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::Vec3 && args[1].type == ValueType::Vec3) {
        return Value(*args[0].vec3Val - *args[1].vec3Val);
    }
    return Value();
}

Value Stdlib::vec_mul(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("vec_mul requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::Vec3 && args[1].type == ValueType::Float) {
        return Value(*args[0].vec3Val * (f32)args[1].floatVal);
    }
    if (args[0].type == ValueType::Vec3 && args[1].type == ValueType::Vec3) {
        return Value(Vec3(args[0].vec3Val->x * args[1].vec3Val->x,
                          args[0].vec3Val->y * args[1].vec3Val->y,
                          args[0].vec3Val->z * args[1].vec3Val->z));
    }
    return Value();
}

Value Stdlib::vec_div(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("vec_div requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::Vec3 && args[1].type == ValueType::Float) {
        f32 s = (f32)args[1].floatVal;
        if (s != 0.0f) return Value(*args[0].vec3Val / s);
    }
    return Value();
}

Value Stdlib::vec_dot(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("vec_dot requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::Vec3 && args[1].type == ValueType::Vec3) {
        return Value((f64)args[0].vec3Val->dot(*args[1].vec3Val));
    }
    return Value((f64)0.0);
}

Value Stdlib::vec_cross(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("vec_cross requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::Vec3 && args[1].type == ValueType::Vec3) {
        return Value(args[0].vec3Val->cross(*args[1].vec3Val));
    }
    return Value();
}

Value Stdlib::vec_len(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("vec_len requires 1 argument"); return Value(); }
    if (args[0].type == ValueType::Vec3) return Value((f64)args[0].vec3Val->length());
    if (args[0].type == ValueType::Vec2) return Value((f64)args[0].vec2Val->length());
    return Value((f64)0.0);
}

Value Stdlib::vec_normalize(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("vec_normalize requires 1 argument"); return Value(); }
    if (args[0].type == ValueType::Vec3) return Value(args[0].vec3Val->normalized());
    return Value();
}

Value Stdlib::vec_lerp(VM& vm, Value* args, u32 argc) {
    if (argc < 3) { vm.runtimeError("vec_lerp requires 3 arguments"); return Value(); }
    if (args[0].type == ValueType::Vec3 && args[1].type == ValueType::Vec3 && args[2].type == ValueType::Float) {
        f32 t = (f32)args[2].floatVal;
        return Value(*args[0].vec3Val * (1.0f - t) + *args[1].vec3Val * t);
    }
    return Value();
}

Value Stdlib::vec_distance(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("vec_distance requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::Vec3 && args[1].type == ValueType::Vec3) {
        return Value((f64)(*args[0].vec3Val - *args[1].vec3Val).length());
    }
    return Value((f64)0.0);
}

Value Stdlib::vec_reflect(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("vec_reflect requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::Vec3 && args[1].type == ValueType::Vec3) {
        Vec3& v = *args[0].vec3Val;
        Vec3& n = *args[1].vec3Val;
        return Value(v - n * 2.0f * v.dot(n));
    }
    return Value();
}

// ---- Matrices ----
Value Stdlib::mat4_identity(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(Mat4::identity());
}

Value Stdlib::mat4_translation(VM& vm, Value* args, u32 argc) {
    (void)vm;
    Vec3 t(0, 0, 0);
    if (argc > 0 && args[0].type == ValueType::Vec3) t = *args[0].vec3Val;
    return Value(Mat4::translation(t));
}

Value Stdlib::mat4_rotation(VM& vm, Value* args, u32 argc) {
    (void)vm;
    if (argc > 0 && args[0].type == ValueType::Quat) {
        return Value(Mat4::rotation(*args[0].quatVal));
    }
    return Value(Mat4::identity());
}

Value Stdlib::mat4_scaling(VM& vm, Value* args, u32 argc) {
    (void)vm;
    Vec3 s(1, 1, 1);
    if (argc > 0 && args[0].type == ValueType::Vec3) s = *args[0].vec3Val;
    return Value(Mat4::scaling(s));
}

Value Stdlib::mat4_mul(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("mat4_mul requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::Mat4 && args[1].type == ValueType::Mat4) {
        return Value(*args[0].mat4Val * *args[1].mat4Val);
    }
    return Value(Mat4::identity());
}

Value Stdlib::mat4_inverse(VM& vm, Value* args, u32 argc) {
    (void)argc;
    if (args[0].type == ValueType::Mat4 && args[0].mat4Val) {
        // TODO: implement full 4x4 inverse
        return Value(Mat4::identity());
    }
    return Value(Mat4::identity());
}

Value Stdlib::mat4_lookat(VM& vm, Value* args, u32 argc) {
    if (argc < 3) { vm.runtimeError("mat4_lookat requires 3 arguments"); return Value(); }
    if (args[0].type == ValueType::Vec3 && args[1].type == ValueType::Vec3 && args[2].type == ValueType::Vec3) {
        return Value(Mat4::lookAt(*args[0].vec3Val, *args[1].vec3Val, *args[2].vec3Val));
    }
    return Value(Mat4::identity());
}

Value Stdlib::mat4_perspective(VM& vm, Value* args, u32 argc) {
    if (argc < 4) { vm.runtimeError("mat4_perspective requires 4 arguments"); return Value(); }
    f32 fov = (args[0].type == ValueType::Float) ? (f32)args[0].floatVal : 60.0f;
    f32 aspect = (args[1].type == ValueType::Float) ? (f32)args[1].floatVal : 1.0f;
    f32 near = (args[2].type == ValueType::Float) ? (f32)args[2].floatVal : 0.1f;
    f32 far = (args[3].type == ValueType::Float) ? (f32)args[3].floatVal : 1000.0f;
    return Value(Mat4::perspective(fov * 0.017453f, aspect, near, far));
}

// ---- Quaternions ----
Value Stdlib::quat_identity(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(Quat::identity());
}

Value Stdlib::quat_from_euler(VM& vm, Value* args, u32 argc) {
    (void)vm;
    if (argc > 0 && args[0].type == ValueType::Vec3) {
        return Value(Quat::fromEuler(Vec3(args[0].vec3Val->x, args[0].vec3Val->y, args[0].vec3Val->z)));
    }
    return Value(Quat::identity());
}

Value Stdlib::quat_mul(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("quat_mul requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::Quat && args[1].type == ValueType::Quat) {
        return Value(*args[0].quatVal * *args[1].quatVal);
    }
    return Value(Quat::identity());
}

Value Stdlib::quat_slerp(VM& vm, Value* args, u32 argc) {
    if (argc < 3) { vm.runtimeError("quat_slerp requires 3 arguments"); return Value(); }
    if (args[0].type == ValueType::Quat && args[1].type == ValueType::Quat && args[2].type == ValueType::Float) {
        return Value(Quat::slerp(*args[0].quatVal, *args[1].quatVal, (f32)args[2].floatVal));
    }
    return Value(Quat::identity());
}

Value Stdlib::quat_look_rotation(VM& vm, Value* args, u32 argc) {
    (void)vm;
    if (argc > 0 && args[0].type == ValueType::Vec3) {
        return Value(Quat::lookRotation(*args[0].vec3Val, Vec3(0, 1, 0)));
    }
    return Value(Quat::identity());
}

// ---- Colors ----
Value Stdlib::color_rgb(VM& vm, Value* args, u32 argc) {
    (void)vm;
    f32 r = (argc > 0) ? ((args[0].type == ValueType::Float) ? (f32)args[0].floatVal : (f32)args[0].intVal / 255.0f) : 0.0f;
    f32 g = (argc > 1) ? ((args[1].type == ValueType::Float) ? (f32)args[1].floatVal : (f32)args[1].intVal / 255.0f) : 0.0f;
    f32 b = (argc > 2) ? ((args[2].type == ValueType::Float) ? (f32)args[2].floatVal : (f32)args[2].intVal / 255.0f) : 0.0f;
    return Value(Color(r, g, b, 1.0f));
}

Value Stdlib::color_rgba(VM& vm, Value* args, u32 argc) {
    (void)vm;
    f32 r = (argc > 0) ? ((args[0].type == ValueType::Float) ? (f32)args[0].floatVal : (f32)args[0].intVal / 255.0f) : 0.0f;
    f32 g = (argc > 1) ? ((args[1].type == ValueType::Float) ? (f32)args[1].floatVal : (f32)args[1].intVal / 255.0f) : 0.0f;
    f32 b = (argc > 2) ? ((args[2].type == ValueType::Float) ? (f32)args[2].floatVal : (f32)args[2].intVal / 255.0f) : 0.0f;
    f32 a = (argc > 3) ? ((args[3].type == ValueType::Float) ? (f32)args[3].floatVal : (f32)args[3].intVal / 255.0f) : 1.0f;
    return Value(Color(r, g, b, a));
}

Value Stdlib::color_lerp(VM& vm, Value* args, u32 argc) {
    if (argc < 3) { vm.runtimeError("color_lerp requires 3 arguments"); return Value(); }
    if (args[0].type == ValueType::Color && args[1].type == ValueType::Color && args[2].type == ValueType::Float) {
        Color& a = *args[0].colorVal;
        Color& b = *args[1].colorVal;
        f32 t = (f32)args[2].floatVal;
        return Value(Color(
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t
        ));
    }
    return Value();
}

Value Stdlib::color_hsv_to_rgb(VM& vm, Value* args, u32 argc) {
    if (argc < 3) { vm.runtimeError("color_hsv_to_rgb requires 3 arguments"); return Value(); }
    f32 h = (args[0].type == ValueType::Float) ? (f32)args[0].floatVal : 0.0f;
    f32 s = (args[1].type == ValueType::Float) ? (f32)args[1].floatVal : 0.0f;
    f32 v = (args[2].type == ValueType::Float) ? (f32)args[2].floatVal : 0.0f;
    f32 c = v * s;
    f32 x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    f32 m = v - c;
    f32 r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    return Value(Color(r + m, g + m, b + m, 1.0f));
}

// ---- Collections ----
Value Stdlib::array_new(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    Value v;
    v.type = ValueType::Array;
    v.arrayVal = new Vector<Value>();
    return v;
}

Value Stdlib::array_push(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("array_push requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::Array && args[0].arrayVal) {
        args[0].arrayVal->pushBack(args[1]);
    }
    return Value();
}

Value Stdlib::array_pop(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("array_pop requires 1 argument"); return Value(); }
    if (args[0].type == ValueType::Array && args[0].arrayVal && args[0].arrayVal->size() > 0) {
        Value back = args[0].arrayVal->back();
        args[0].arrayVal->popBack();
        return back;
    }
    return Value();
}

Value Stdlib::array_len(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("array_len requires 1 argument"); return Value(); }
    if (args[0].type == ValueType::Array && args[0].arrayVal) {
        return Value((i64)args[0].arrayVal->size());
    }
    return Value((i64)0);
}

Value Stdlib::array_get(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("array_get requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::Array && args[0].arrayVal) {
        u32 idx = (u32)((args[1].type == ValueType::Int) ? args[1].intVal : 0);
        if (idx < args[0].arrayVal->size()) return (*args[0].arrayVal)[idx];
    }
    return Value();
}

Value Stdlib::array_set(VM& vm, Value* args, u32 argc) {
    if (argc < 3) { vm.runtimeError("array_set requires 3 arguments"); return Value(); }
    if (args[0].type == ValueType::Array && args[0].arrayVal) {
        u32 idx = (u32)((args[1].type == ValueType::Int) ? args[1].intVal : 0);
        if (idx < args[0].arrayVal->size()) (*args[0].arrayVal)[idx] = args[2];
    }
    return Value();
}

Value Stdlib::array_clear(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("array_clear requires 1 argument"); return Value(); }
    if (args[0].type == ValueType::Array && args[0].arrayVal) {
        args[0].arrayVal->clear();
    }
    return Value();
}

Value Stdlib::map_new(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    Value v;
    v.type = ValueType::Map;
    v.mapVal = nullptr;
    return v;
}

Value Stdlib::map_set(VM& vm, Value* args, u32 argc) {
    if (argc < 3) { vm.runtimeError("map_set requires 3 arguments"); return Value(); }
    return Value();
}

Value Stdlib::map_get(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("map_get requires 2 arguments"); return Value(); }
    return Value();
}

Value Stdlib::map_has(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("map_has requires 2 arguments"); return Value(); }
    return Value(false);
}

Value Stdlib::map_remove(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("map_remove requires 2 arguments"); return Value(); }
    return Value();
}

Value Stdlib::map_keys(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("map_keys requires 1 argument"); return Value(); }
    return array_new(vm, args, 0);
}

// ---- Strings ----
Value Stdlib::string_new(VM& vm, Value* args, u32 argc) {
    (void)vm;
    if (argc > 0 && args[0].type == ValueType::String) return Value(*args[0].stringVal);
    return Value(String(""));
}

Value Stdlib::string_len(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("string_len requires 1 argument"); return Value(); }
    if (args[0].type == ValueType::String && args[0].stringVal) {
        return Value((i64)args[0].stringVal->length());
    }
    return Value((i64)0);
}

Value Stdlib::string_sub(VM& vm, Value* args, u32 argc) {
    if (argc < 3) { vm.runtimeError("string_sub requires 3 arguments"); return Value(); }
    if (args[0].type == ValueType::String && args[0].stringVal) {
        u32 start = (u32)((args[1].type == ValueType::Int) ? args[1].intVal : 0);
        u32 len = (u32)((args[2].type == ValueType::Int) ? args[2].intVal : 0);
        return Value(args[0].stringVal->substr(start, len));
    }
    return Value(String(""));
}

Value Stdlib::string_find(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("string_find requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::String && args[0].stringVal &&
        args[1].type == ValueType::String && args[1].stringVal) {
        auto pos = args[0].stringVal->find(*args[1].stringVal);
        return Value((i64)(pos != String::npos ? pos : -1));
    }
    return Value((i64)-1);
}

Value Stdlib::string_split(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("string_split requires 2 arguments"); return Value(); }
    return array_new(vm, args, 0);
}

Value Stdlib::string_join(VM& vm, Value* args, u32 argc) {
    if (argc < 2) { vm.runtimeError("string_join requires 2 arguments"); return Value(); }
    if (args[0].type == ValueType::String) return Value(*args[0].stringVal);
    return Value(String(""));
}

Value Stdlib::string_format(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("string_format requires at least 1 argument"); return Value(); }
    if (args[0].type == ValueType::String) return Value(*args[0].stringVal);
    return Value(String(""));
}

Value Stdlib::string_upper(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("string_upper requires 1 argument"); return Value(); }
    if (args[0].type == ValueType::String && args[0].stringVal) {
        String result = *args[0].stringVal;
        for (u32 i = 0; i < result.length(); i++) {
            char& c = result.data()[i];
            if (c >= 'a' && c <= 'z') c -= 32;
        }
        return Value(result);
    }
    return Value(String(""));
}

Value Stdlib::string_lower(VM& vm, Value* args, u32 argc) {
    if (argc < 1) { vm.runtimeError("string_lower requires 1 argument"); return Value(); }
    if (args[0].type == ValueType::String && args[0].stringVal) {
        String result = *args[0].stringVal;
        for (u32 i = 0; i < result.length(); i++) {
            char& c = result.data()[i];
            if (c >= 'A' && c <= 'Z') c += 32;
        }
        return Value(result);
    }
    return Value(String(""));
}

// ---- Engine API (stubs) ----
Value Stdlib::entity_spawn(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value((i64)0);
}

Value Stdlib::entity_despawn(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::entity_find(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value((i64)0xFFFFFFFF);
}

Value Stdlib::entity_query(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return array_new(vm, args, 0);
}

Value Stdlib::component_get(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::component_set(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::component_add(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::component_remove(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::transform_get(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(Vec3(0, 0, 0));
}

Value Stdlib::transform_set(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::transform_translate(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::transform_rotate(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::transform_scale(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::mesh_set(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::material_set(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::light_set(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::camera_set(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::physics_force(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::physics_impulse(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::input_key_down(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(false);
}

Value Stdlib::input_key_pressed(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(false);
}

Value Stdlib::input_mouse_pos(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value(Vec2(0, 0));
}

Value Stdlib::time_delta(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value((f64)0.016);
}

Value Stdlib::time_now(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value((f64)0.0);
}

Value Stdlib::debug_log(VM& vm, Value* args, u32 argc) {
    (void)vm;
    if (argc > 0 && args[0].type == ValueType::String && args[0].stringVal) {
        // In a real implementation, this would log to the console
    }
    return Value();
}

Value Stdlib::debug_draw_line(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::debug_draw_box(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

// ---- Coroutines ----
Value Stdlib::coroutine_spawn(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value((i64)0);
}

Value Stdlib::coroutine_yield(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::coroutine_wait(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::coroutine_wait_seconds(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

// ---- Channels ----
Value Stdlib::channel_new(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    Value v;
    v.type = ValueType::Channel;
    v.handleVal = 0;
    return v;
}

Value Stdlib::channel_send(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::channel_receive(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

Value Stdlib::channel_close(VM& vm, Value* args, u32 argc) {
    (void)vm; (void)args; (void)argc;
    return Value();
}

}
}
