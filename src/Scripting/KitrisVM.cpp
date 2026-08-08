#include "Scripting/KitrisVM.h"
#include "Core/Log.h"
#include "Core/Math.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

namespace Crux {
namespace Kitris {

VM::VM() {
    // Initialize globals
    globals_.resize(256);
    for (auto& g : globals_) g = Value();

    // Pre-allocate some strings
    strings_.reserve(1024);
}

VM::~VM() {
    collectGarbage();
    for (auto* fn : functions_) delete fn;
}

VM::InterpretResult VM::interpret(const String& source) {
    // This would be implemented with lexer + parser + compiler
    // For now return Ok
    return Ok;
}

VM::InterpretResult VM::run() {
    if (frameCount_ == 0) return Ok;
    return runFrame(frames_[frameCount_ - 1]);
}

void VM::ensureRegisters(u32 count) {
    if (count > MAX_REGISTERS) {
        runtimeError("Register count %u exceeds maximum %u", count, MAX_REGISTERS);
    }
}

void VM::collectGarbage() {
    // Mark phase
    grayStack_.clear();

    // Mark globals
    for (auto& g : globals_) markValue(g);

    // Mark stack
    for (u32 i = 0; i < stackTop_; i++) markValue(stack_[i]);

    // Mark registers in all frames
    for (u32 i = 0; i < frameCount_; i++) {
        CallFrame& frame = frames_[i];
        for (u32 r = 0; r < frame.function->registerCount; r++) {
            markValue(frame.registers[r]);
        }
    }

    // Mark gray stack
    while (!grayStack_.empty()) {
        void* obj = grayStack_.back();
        grayStack_.popBack();
        // Mark object fields
    }

    // Sweep phase (simplified)
    // In real implementation, would sweep through all allocated objects
}

void VM::markValue(Value& v) {
    switch (v.type) {
        case ValueType::String: if (v.stringVal) markObject(v.stringVal); break;
        case ValueType::Vec2: case ValueType::Vec3: case ValueType::Vec4:
        case ValueType::Mat4: case ValueType::Quat: case ValueType::Color:
            if (v.objVal) markObject(v.objVal); break;
        case ValueType::Array: if (v.arrayVal) markObject(v.arrayVal); break;
        case ValueType::Object: if (v.objVal) markObject(v.objVal); break;
        case ValueType::Function: if (v.fnVal) markObject(v.fnVal); break;
        case ValueType::Coroutine: if (v.coroVal) markObject(v.coroVal); break;
        default: break;
    }
}

void VM::markObject(void* obj) {
    // Simplified - real impl would track GC headers
    // Add to gray stack for traversal
    grayStack_.pushBack(obj);
}

Coroutine* VM::newCoroutine(Function* fn, Value* args, u32 argc) {
    Coroutine* coro = new Coroutine();
    coro->vm = this;
    coro->function = fn;
    coro->registerCount = fn->registerCount;
    coro->registers = new Value[fn->registerCount];
    coro->ip = 0;
    coro->state = Coroutine::New;
    coro->caller = nullptr;

    // Copy args to registers
    for (u32 i = 0; i < argc && i < fn->arity; i++) {
        coro->registers[i] = args[i];
    }
    return coro;
}

void VM::resumeCoroutine(Coroutine* coro, Value arg) {
    if (coro->state == Coroutine::Dead) return;
    coro->state = Coroutine::Running;

    // Push new frame
    if (frameCount_ >= MAX_FRAMES) {
        runtimeError("Coroutine frame stack overflow");
        return;
    }

    CallFrame& frame = frames_[frameCount_++];
    frame.function = coro->function;
    frame.ip = coro->ip;
    frame.baseReg = 0;
    frame.registers = coro->registers;

    if (coro->state == Coroutine::New) {
        coro->state = Coroutine::Running;
    } else {
        // Resume with yielded value
        coro->registers[0] = arg;
    }
}

Value VM::yieldCoroutine(Value val) {
    if (frameCount_ == 0) return Value();

    CallFrame& frame = frames_[frameCount_ - 1];
    Coroutine* coro = nullptr; // Would find coroutine for this frame

    if (coro) {
        coro->state = Coroutine::Suspended;
        coro->ip = frame.ip;
        coro->yieldedValue = val;
    }

    frameCount_--;
    return val;
}

u32 VM::registerNative(const String& name, NativeFn fn) {
    // In real impl, would store in native function table
    return 0;
}

void VM::runtimeError(const char* fmt, ...) {
    hadError_ = true;
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    FROST_LOG_ERROR("[Kitris VM] Runtime error: %s", buffer);
}

VM::InterpretResult VM::runFrame(CallFrame& frame) {
    Function* fn = frame.function;
    u32& ip = frame.ip;
    Value* regs = frame.registers;

    while (ip < fn->code.size()) {
        OpCode op = fn->code[ip++];

        switch (op) {
            case OpCode::LoadConst: {
                u32 dst = fn->code[ip++];
                u32 constIdx = fn->code[ip++];
                regs[dst] = fn->constants[constIdx];
                break;
            }
            case OpCode::LoadNull: {
                u32 dst = fn->code[ip++];
                regs[dst] = Value();
                break;
            }
            case OpCode::LoadTrue: {
                u32 dst = fn->code[ip++];
                regs[dst] = Value(true);
                break;
            }
            case OpCode::LoadFalse: {
                u32 dst = fn->code[ip++];
                regs[dst] = Value(false);
                break;
            }
            case OpCode::Move: {
                u32 dst = fn->code[ip++];
                u32 src = fn->code[ip++];
                regs[dst] = regs[src];
                break;
            }
            case OpCode::Add: {
                u32 dst = fn->code[ip++];
                u32 a = fn->code[ip++];
                u32 b = fn->code[ip++];
                if (regs[a].type == ValueType::Int && regs[b].type == ValueType::Int) {
                    regs[dst] = Value(regs[a].intVal + regs[b].intVal);
                } else if (regs[a].type == ValueType::Float && regs[b].type == ValueType::Float) {
                    regs[dst] = Value(regs[a].floatVal + regs[b].floatVal);
                } else if (regs[a].type == ValueType::Vec3 && regs[b].type == ValueType::Vec3) {
                    regs[dst] = Value(*regs[a].vec3Val + *regs[b].vec3Val);
                }
                break;
            }
            case OpCode::Sub: {
                u32 dst = fn->code[ip++];
                u32 a = fn->code[ip++];
                u32 b = fn->code[ip++];
                if (regs[a].type == ValueType::Int && regs[b].type == ValueType::Int) {
                    regs[dst] = Value(regs[a].intVal - regs[b].intVal);
                } else if (regs[a].type == ValueType::Float && regs[b].type == ValueType::Float) {
                    regs[dst] = Value(regs[a].floatVal - regs[b].floatVal);
                }
                break;
            }
            case OpCode::Mul: {
                u32 dst = fn->code[ip++];
                u32 a = fn->code[ip++];
                u32 b = fn->code[ip++];
                if (regs[a].type == ValueType::Int && regs[b].type == ValueType::Int) {
                    regs[dst] = Value(regs[a].intVal * regs[b].intVal);
                } else if (regs[a].type == ValueType::Float && regs[b].type == ValueType::Float) {
                    regs[dst] = Value(regs[a].floatVal * regs[b].floatVal);
                }
                break;
            }
            case OpCode::Div: {
                u32 dst = fn->code[ip++];
                u32 a = fn->code[ip++];
                u32 b = fn->code[ip++];
                if (regs[a].type == ValueType::Int && regs[b].type == ValueType::Int) {
                    regs[dst] = Value(regs[a].intVal / regs[b].intVal);
                } else if (regs[a].type == ValueType::Float && regs[b].type == ValueType::Float) {
                    regs[dst] = Value(regs[a].floatVal / regs[b].floatVal);
                }
                break;
            }
            case OpCode::Call: {
                u32 funcReg = fn->code[ip++];
                u32 argc = fn->code[ip++];
                Value* args = &regs[fn->code[ip]];
                ip += argc;
                Value result = callValue(regs[funcReg], args, argc);
                u32 dst = fn->code[ip++];
                regs[dst] = result;
                break;
            }
            case OpCode::Return: {
                u32 src = fn->code[ip++];
                Value result = regs[src];
                frameCount_--;
                if (frameCount_ > 0) {
                    CallFrame& caller = frames_[frameCount_ - 1];
                    regs[caller.returnReg] = result;
                }
                return Ok;
            }
            case OpCode::Jump: {
                i32 offset = (i32)fn->code[ip++];
                ip = (u32)((i32)ip + offset);
                break;
            }
            case OpCode::JumpIf: {
                u32 cond = fn->code[ip++];
                i32 offset = (i32)fn->code[ip++];
                if (regs[cond].isTruthy()) ip = (u32)((i32)ip + offset);
                break;
            }
            case OpCode::JumpIfNot: {
                u32 cond = fn->code[ip++];
                i32 offset = (i32)fn->code[ip++];
                if (!regs[cond].isTruthy()) ip = (u32)((i32)ip + offset);
                break;
            }
            case OpCode::GetField: {
                u32 dst = fn->code[ip++];
                u32 obj = fn->code[ip++];
                u32 fieldIdx = fn->code[ip++];
                // Simplified - would lookup field in object
                break;
            }
            case OpCode::SetField: {
                u32 obj = fn->code[ip++];
                u32 fieldIdx = fn->code[ip++];
                u32 value = fn->code[ip++];
                // Simplified
                break;
            }
            case OpCode::Line: {
                u32 line = fn->code[ip++];
                // Debug info
                break;
            }
            default:
                runtimeError("Unimplemented opcode: %d", (int)op);
                return RuntimeError;
        }
    }
    return Ok;
}

Value VM::callValue(Value callee, Value* args, u32 argc) {
    if (callee.type == ValueType::Function && callee.fnVal) {
        Function* fn = callee.fnVal;
        if (frameCount_ >= MAX_FRAMES) {
            runtimeError("Stack overflow");
            return Value();
        }
        CallFrame& frame = frames_[frameCount_++];
        frame.function = fn;
        frame.ip = 0;
        frame.baseReg = 0;
        frame.registers = &registers_[frameCount_ * 16]; // Simplified register allocation
        frame.returnReg = 0;
        // Copy args
        for (u32 i = 0; i < argc && i < fn->arity; i++) {
            frame.registers[i] = args[i];
        }
        return Value(); // Result will be in return register after run
    }
    return Value();
}

Value VM::callNative(u32 nativeId, Value* args, u32 argc) {
    // Would call registered native function
    return Value();
}

}
}