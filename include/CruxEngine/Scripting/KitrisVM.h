#pragma once

// ============================================================================
// CruxEngine Kitris VM — Register-based virtual machine
// ============================================================================
// Features:
//   - 256 registers per frame
//   - Stack for call frames
//   - Garbage collector (incremental, generational)
//   - Coroutine support (yield/resume)
//   - Built-in types: vec2/3/4, mat2/3/4, quat, color, entity, component
//   - Direct engine API access (no marshaling overhead)
//   - JIT compilation hot paths (optional)
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Math.h"
#include "Core/ECS.h"

namespace Crux {
namespace Kitris {

// ---- Bytecode Instructions ----
enum class OpCode : u16 {
    // Stack/Register
    LoadConst,      // dst, const_idx
    LoadNull,       // dst
    LoadTrue,       // dst
    LoadFalse,      // dst
    Move,           // dst, src
    Swap,           // a, b

    // Arithmetic
    Add, Sub, Mul, Div, Mod,
    AddI, SubI, MulI, DivI, ModI,  // immediate
    Neg, Not, BitNot,

    // Bitwise
    And, Or, Xor, Shl, Shr,

    // Comparison
    Eq, Ne, Lt, Gt, Le, Ge,

    // Jumps
    Jump,           // offset
    JumpIf,         // cond, offset
    JumpIfNot,      // cond, offset
    JumpTable,      // value, table_offset

    // Calls
    Call,           // func, argc, argv_regs...
    CallNative,     // native_id, argc, argv_regs...
    Return,         // src
    TailCall,       // func, argc, argv_regs...

    // Coroutines
    Yield,          // value
    Resume,         // coroutine, value
    Spawn,          // func, argc, argv_regs... -> coroutine

    // Objects
    NewArray,       // dst, size
    NewMap,         // dst
    NewStruct,      // dst, type_id, field_count
    GetIndex,       // dst, obj, index
    SetIndex,       // obj, index, value
    GetField,       // dst, obj, field_name
    SetField,       // obj, field_name, value
    GetMember,      // dst, obj, member_idx
    SetMember,      // obj, member_idx, value

    // Type ops
    TypeOf,         // dst, value
    Cast,           // dst, value, type_id
    InstanceOf,     // dst, value, type_id

    // Math vectors (intrinsic)
    Vec2New, Vec3New, Vec4New,
    VecAdd, VecSub, VecMul, VecDiv,
    VecDot, VecCross, VecLen, VecNormalize, VecLerp,
    Mat4New, Mat4Mul, Mat4Inverse, Mat4Transpose,
    QuatNew, QuatMul, QuatSlerp, QuatLookAt,
    ColorNew, ColorLerp,

    // Engine intrinsics
    EntityQuery,        // dst, filter...
    ComponentQuery,     // dst, entity, type
    SpawnEntity,        // dst, archetype
    DespawnEntity,      // entity
    GetComponent,       // dst, entity, type
    SetComponent,       // entity, type, value
    AddComponent,       // entity, type, value
    RemoveComponent,    // entity, type
    TransformGet,       // dst, entity
    TransformSet,       // entity, pos, rot, scale
    MeshSet,            // entity, mesh_id
    MaterialSet,        // entity, material_id
    LightSet,           // entity, light_data
    CameraSet,          // entity, camera_data
    PhysicsApplyForce,  // entity, force, point
    InputKeyDown,       // dst, key
    InputMousePos,      // dst
    TimeDelta,          // dst
    TimeNow,            // dst
    RandomRange,        // dst, min, max
    MathSin, MathCos, MathTan, MathSqrt, MathLog, MathExp,
    DebugLog,           // message
    ProfileBegin, ProfileEnd,

    // Exception handling
    TryBegin,       // handler_offset
    TryEnd,
    Throw,          // value

    // Debug
    Line,           // line_number
    ScopeEnter, ScopeExit,

    Count
};

// ---- Value Types ----
enum class ValueType : u8 {
    Null = 0,
    Bool,
    Int,
    Float,
    String,
    Vec2, Vec3, Vec4,
    Mat2, Mat3, Mat4,
    Quat,
    Color,
    Array,
    Map,
    Struct,
    Function,
    NativeFunction,
    Coroutine,
    Entity,
    Component,
    Handle,
    Channel,
    Future,
    Object,  // generic heap object
};

struct Value {
    ValueType type;
    union {
        bool boolVal;
        i64 intVal;
        f64 floatVal;
        String* stringVal;
        Vec2* vec2Val;
        Vec3* vec3Val;
        Vec4* vec4Val;
        Mat4* mat4Val;
        Quat* quatVal;
        Color* colorVal;
        Vector<Value>* arrayVal;
        struct HashMap* mapVal;
        struct HeapObject* objVal;
        struct Function* fnVal;
        struct NativeFn* nativeVal;
        struct Coroutine* coroVal;
        u32 entityVal;
        u32 handleVal;
    };

    Value() : type(ValueType::Null), intVal(0) {}
    Value(bool v) : type(ValueType::Bool), boolVal(v) {}
    Value(i64 v) : type(ValueType::Int), intVal(v) {}
    Value(f64 v) : type(ValueType::Float), floatVal(v) {}
    Value(const String& v) : type(ValueType::String), stringVal(new String(v)) {}
    Value(const Vec2& v) : type(ValueType::Vec2), vec2Val(new Vec2(v)) {}
    Value(const Vec3& v) : type(ValueType::Vec3), vec3Val(new Vec3(v)) {}
    Value(const Vec4& v) : type(ValueType::Vec4), vec4Val(new Vec4(v)) {}
    Value(const Mat4& v) : type(ValueType::Mat4), mat4Val(new Mat4(v)) {}
    Value(const Quat& v) : type(ValueType::Quat), quatVal(new Quat(v)) {}
    Value(const Color& v) : type(ValueType::Color), colorVal(new Color(v)) {}

    ~Value() { destroy(); }

    void destroy() {
        switch (type) {
            case ValueType::String: delete stringVal; break;
            case ValueType::Vec2: delete vec2Val; break;
            case ValueType::Vec3: delete vec3Val; break;
            case ValueType::Vec4: delete vec4Val; break;
            case ValueType::Mat4: delete mat4Val; break;
            case ValueType::Quat: delete quatVal; break;
            case ValueType::Color: delete colorVal; break;
            case ValueType::Array: delete arrayVal; break;
            case ValueType::Map: break;
            case ValueType::Object: break;
            default: break;
        }
        type = ValueType::Null;
    }

    bool isTruthy() const {
        switch (type) {
            case ValueType::Null: return false;
            case ValueType::Bool: return boolVal;
            case ValueType::Int: return intVal != 0;
            case ValueType::Float: return floatVal != 0.0;
            case ValueType::String: return stringVal && stringVal->length() > 0;
            case ValueType::Array: return arrayVal && arrayVal->size() > 0;
            default: return true;
        }
    }
};

// ---- Function ----
struct Function {
    String name;
    Vector<OpCode> code;
    Vector<Value> constants;
    u32 arity;
    u32 registerCount;
    u32 upvalueCount;
    bool isNative;
    void* nativePtr;
};

// ---- Call Frame ----
struct CallFrame {
    Function* function;
    u32 ip;           // instruction pointer
    u32 baseReg;      // base register index
    u32 returnReg;    // where to store return value
    Value* registers; // pointer to register window
};

// ---- VM ----
class VM {
public:
    static constexpr u32 MAX_REGISTERS = 1024;
    static constexpr u32 MAX_FRAMES = 256;
    static constexpr u32 STACK_SIZE = 4096;

    VM();
    ~VM();

    // ---- Execution ----
    enum InterpretResult { Ok, CompileError, RuntimeError };

    InterpretResult interpret(const String& source);
    InterpretResult run();

    // ---- Stack/Register management ----
    Value& reg(u32 idx) { return registers_[idx]; }
    const Value& reg(u32 idx) const { return registers_[idx]; }
    void ensureRegisters(u32 count);

    // ---- GC ----
    void collectGarbage();
    void markValue(Value& v);
    void markObject(void* obj);

    // ---- Engine integration ----
    void setEngine(void* engine) { engine_ = engine; }
    void* engine() const { return engine_; }

    // ---- Coroutines ----
    struct Coroutine* newCoroutine(Function* fn, Value* args, u32 argc);
    void resumeCoroutine(struct Coroutine* coro, Value arg);
    Value yieldCoroutine(Value val);

    // ---- Native function registration ----
    using NativeFn = Value(*)(VM&, Value*, u32);
    u32 registerNative(const String& name, NativeFn fn);

    // ---- Error handling ----
    void runtimeError(const char* fmt, ...);

    Value registers_[MAX_REGISTERS];
    CallFrame frames_[MAX_FRAMES];
    u32 frameCount_ = 0;
    Value stack_[STACK_SIZE];
    u32 stackTop_ = 0;

    Vector<Function*> functions_;
    Vector<String> strings_;
    Vector<Value> globals_;

    void* engine_ = nullptr;
    bool hadError_ = false;

private:
    InterpretResult runFrame(CallFrame& frame);
    Value callValue(Value callee, Value* args, u32 argc);
    Value callNative(u32 nativeId, Value* args, u32 argc);

    // GC
    struct GCHeader { u8 color; u8 marked; u16 type; };
    Vector<void*> grayStack_;
    u64 bytesAllocated_ = 0;
    u64 nextGC_ = 1024 * 1024;
};

// ---- Coroutine ----
struct Coroutine {
    VM* vm;
    Function* function;
    Value* registers;
    u32 registerCount;
    u32 ip;
    enum State { New, Running, Suspended, Dead } state;
    Value yieldedValue;
    Coroutine* caller;
};

}
}