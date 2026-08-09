#pragma once

// ============================================================================
// FrostEngine Kitris Standard Library — Built-in functions and types
// ============================================================================
// Provides: math, collections, strings, I/O, engine API, concurrency
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Math.h"
#include "Scripting/KitrisVM.h"

namespace Frost {
namespace Kitris {

class Stdlib {
public:
    static void registerAll(VM& vm);

private:
    // Math
    static Value math_sin(VM&, Value*, u32);
    static Value math_cos(VM&, Value*, u32);
    static Value math_tan(VM&, Value*, u32);
    static Value math_asin(VM&, Value*, u32);
    static Value math_acos(VM&, Value*, u32);
    static Value math_atan2(VM&, Value*, u32);
    static Value math_sqrt(VM&, Value*, u32);
    static Value math_log(VM&, Value*, u32);
    static Value math_exp(VM&, Value*, u32);
    static Value math_pow(VM&, Value*, u32);
    static Value math_floor(VM&, Value*, u32);
    static Value math_ceil(VM&, Value*, u32);
    static Value math_round(VM&, Value*, u32);
    static Value math_abs(VM&, Value*, u32);
    static Value math_min(VM&, Value*, u32);
    static Value math_max(VM&, Value*, u32);
    static Value math_clamp(VM&, Value*, u32);
    static Value math_lerp(VM&, Value*, u32);
    static Value math_smoothstep(VM&, Value*, u32);

    // Random
    static Value random_int(VM&, Value*, u32);
    static Value random_float(VM&, Value*, u32);
    static Value random_vec3(VM&, Value*, u32);
    static Value random_in_circle(VM&, Value*, u32);
    static Value random_in_sphere(VM&, Value*, u32);

    // Vectors
    static Value vec2_new(VM&, Value*, u32);
    static Value vec3_new(VM&, Value*, u32);
    static Value vec4_new(VM&, Value*, u32);
    static Value vec_add(VM&, Value*, u32);
    static Value vec_sub(VM&, Value*, u32);
    static Value vec_mul(VM&, Value*, u32);
    static Value vec_div(VM&, Value*, u32);
    static Value vec_dot(VM&, Value*, u32);
    static Value vec_cross(VM&, Value*, u32);
    static Value vec_len(VM&, Value*, u32);
    static Value vec_normalize(VM&, Value*, u32);
    static Value vec_lerp(VM&, Value*, u32);
    static Value vec_distance(VM&, Value*, u32);
    static Value vec_reflect(VM&, Value*, u32);

    // Matrices
    static Value mat4_identity(VM&, Value*, u32);
    static Value mat4_translation(VM&, Value*, u32);
    static Value mat4_rotation(VM&, Value*, u32);
    static Value mat4_scaling(VM&, Value*, u32);
    static Value mat4_mul(VM&, Value*, u32);
    static Value mat4_inverse(VM&, Value*, u32);
    static Value mat4_lookat(VM&, Value*, u32);
    static Value mat4_perspective(VM&, Value*, u32);

    // Quaternions
    static Value quat_identity(VM&, Value*, u32);
    static Value quat_from_euler(VM&, Value*, u32);
    static Value quat_mul(VM&, Value*, u32);
    static Value quat_slerp(VM&, Value*, u32);
    static Value quat_look_rotation(VM&, Value*, u32);

    // Colors
    static Value color_rgb(VM&, Value*, u32);
    static Value color_rgba(VM&, Value*, u32);
    static Value color_lerp(VM&, Value*, u32);
    static Value color_hsv_to_rgb(VM&, Value*, u32);

    // Collections
    static Value array_new(VM&, Value*, u32);
    static Value array_push(VM&, Value*, u32);
    static Value array_pop(VM&, Value*, u32);
    static Value array_len(VM&, Value*, u32);
    static Value array_get(VM&, Value*, u32);
    static Value array_set(VM&, Value*, u32);
    static Value array_clear(VM&, Value*, u32);
    static Value map_new(VM&, Value*, u32);
    static Value map_set(VM&, Value*, u32);
    static Value map_get(VM&, Value*, u32);
    static Value map_has(VM&, Value*, u32);
    static Value map_remove(VM&, Value*, u32);
    static Value map_keys(VM&, Value*, u32);

    // Strings
    static Value string_new(VM&, Value*, u32);
    static Value string_len(VM&, Value*, u32);
    static Value string_sub(VM&, Value*, u32);
    static Value string_find(VM&, Value*, u32);
    static Value string_split(VM&, Value*, u32);
    static Value string_join(VM&, Value*, u32);
    static Value string_format(VM&, Value*, u32);
    static Value string_upper(VM&, Value*, u32);
    static Value string_lower(VM&, Value*, u32);

    // Engine API
    static Value entity_spawn(VM&, Value*, u32);
    static Value entity_despawn(VM&, Value*, u32);
    static Value entity_find(VM&, Value*, u32);
    static Value entity_query(VM&, Value*, u32);
    static Value component_get(VM&, Value*, u32);
    static Value component_set(VM&, Value*, u32);
    static Value component_add(VM&, Value*, u32);
    static Value component_remove(VM&, Value*, u32);
    static Value transform_get(VM&, Value*, u32);
    static Value transform_set(VM&, Value*, u32);
    static Value transform_translate(VM&, Value*, u32);
    static Value transform_rotate(VM&, Value*, u32);
    static Value transform_scale(VM&, Value*, u32);
    static Value mesh_set(VM&, Value*, u32);
    static Value material_set(VM&, Value*, u32);
    static Value light_set(VM&, Value*, u32);
    static Value camera_set(VM&, Value*, u32);
    static Value physics_force(VM&, Value*, u32);
    static Value physics_impulse(VM&, Value*, u32);
    static Value input_key_down(VM&, Value*, u32);
    static Value input_key_pressed(VM&, Value*, u32);
    static Value input_mouse_pos(VM&, Value*, u32);
    static Value time_delta(VM&, Value*, u32);
    static Value time_now(VM&, Value*, u32);
    static Value debug_log(VM&, Value*, u32);
    static Value debug_draw_line(VM&, Value*, u32);
    static Value debug_draw_box(VM&, Value*, u32);

    // Coroutines
    static Value coroutine_spawn(VM&, Value*, u32);
    static Value coroutine_yield(VM&, Value*, u32);
    static Value coroutine_wait(VM&, Value*, u32);
    static Value coroutine_wait_seconds(VM&, Value*, u32);

    // Channels (for async communication)
    static Value channel_new(VM&, Value*, u32);
    static Value channel_send(VM&, Value*, u32);
    static Value channel_receive(VM&, Value*, u32);
    static Value channel_close(VM&, Value*, u32);
};

}
}