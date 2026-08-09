#include "FrostEngine/Scripting/KitrisDebugger.h"

namespace Frost {
namespace Kitris {

u32 Debugger::addBreakpoint(const String& file, u32 line, const String& condition) {
    u32 id = (u32)breakpoints_.size();
    Breakpoint bp;
    bp.id = id;
    bp.file = file;
    bp.line = line;
    bp.condition = condition;
    bp.enabled = true;
    bp.hitCount = 0;
    breakpoints_.pushBack(bp);
    return id;
}

bool Debugger::removeBreakpoint(u32 id) {
    for (u32 i = 0; i < breakpoints_.size(); i++) {
        if (breakpoints_[i].id == id) {
            breakpoints_.erase(i);
            return true;
        }
    }
    return false;
}

bool Debugger::enableBreakpoint(u32 id, bool enabled) {
    for (u32 i = 0; i < breakpoints_.size(); i++) {
        if (breakpoints_[i].id == id) {
            breakpoints_[i].enabled = enabled;
            return true;
        }
    }
    return false;
}

void Debugger::pause() {
    paused_ = true;
    stepping_ = false;

    if (callback_) {
        DebugEventData event;
        event.type = DebugEvent::BreakpointHit;
        event.frame = callStack_.size() > 0 ? &callStack_.back() : nullptr;
        event.message = "Paused";
        callback_(event, callbackUserData_);
    }
}

void Debugger::resume() {
    paused_ = false;
    stepping_ = false;
}

void Debugger::stepIn() {
    paused_ = false;
    stepping_ = true;
    stepFrameDepth_ = (u32)callStack_.size();
}

void Debugger::stepOver() {
    paused_ = false;
    stepping_ = true;
    stepFrameDepth_ = (u32)callStack_.size() - 1;
}

void Debugger::stepOut() {
    paused_ = false;
    stepping_ = true;
    stepFrameDepth_ = (u32)callStack_.size() - 2;
    if (stepFrameDepth_ < 0) stepFrameDepth_ = 0;
}

Value Debugger::evaluate(const String& expr, u32 frameIndex) {
    (void)frameIndex;
    // Simplified: just return null for now
    // In a real implementation, this would parse and evaluate the expression
    // using the current VM state and call frame locals
    (void)expr;
    return Value();
}

Vector<String> Debugger::getLocals(u32 frameIndex) {
    Vector<String> locals;
    if (frameIndex < callStack_.size()) {
        StackFrame& frame = callStack_[frameIndex];
        (void)frame;
        // In a real implementation, iterate over local variables
    }
    return locals;
}

Vector<String> Debugger::getGlobals() {
    Vector<String> globals;
    if (vm_) {
        // In a real implementation, iterate over VM globals
        (void)vm_;
    }
    return globals;
}

Value Debugger::getVariable(const String& name, u32 frameIndex) {
    (void)frameIndex;
    (void)name;
    // In a real implementation, look up the variable in locals/globals
    return Value();
}

u32 Debugger::addWatch(const String& expr) {
    u32 id = (u32)watches_.size();
    WatchExpression watch;
    watch.expression = expr;
    watch.changed = false;
    watches_.pushBack(watch);
    return id;
}

void Debugger::removeWatch(u32 id) {
    for (u32 i = 0; i < watches_.size(); i++) {
        if (i == id) {
            watches_.erase(i);
            return;
        }
    }
}

String Debugger::repl(const String& input) {
    // Simplified REPL: evaluate input and return result as string
    Value result = evaluate(input);
    switch (result.type) {
        case ValueType::Null: return "null";
        case ValueType::Bool: return result.boolVal ? "true" : "false";
        case ValueType::Int: return String((i32)result.intVal);
        case ValueType::Float: return String((f32)result.floatVal);
        case ValueType::String: return result.stringVal ? *result.stringVal : "";
        default: return "<value>";
    }
}

void Debugger::hotReload(const String& source) {
    // In a real implementation, this would:
    // 1. Re-parse the source
    // 2. Re-compile to bytecode
    // 3. Replace the function in the VM
    // 4. Update breakpoints for new line numbers
    (void)source;
}

void Debugger::checkBreakpoints(u32 line, const String& file) {
    for (u32 i = 0; i < breakpoints_.size(); i++) {
        Breakpoint& bp = breakpoints_[i];
        if (!bp.enabled) continue;
        if (bp.line != line) continue;
        if (bp.file.length() > 0 && bp.file != file) continue;

        bp.hitCount++;

        // Check condition if present
        if (bp.condition.length() > 0) {
            Value condResult = evaluate(bp.condition);
            if (!condResult.isTruthy()) continue;
        }

        // Hit a breakpoint
        paused_ = true;

        if (callback_) {
            DebugEventData event;
            event.type = DebugEvent::BreakpointHit;
            event.frame = callStack_.size() > 0 ? &callStack_.back() : nullptr;
            event.message = "Breakpoint hit";
            callback_(event, callbackUserData_);
        }
        return;
    }

    // Check stepping
    if (stepping_) {
        u32 currentDepth = (u32)callStack_.size();
        if (currentDepth <= stepFrameDepth_) {
            paused_ = true;
            stepping_ = false;

            if (callback_) {
                DebugEventData event;
                event.type = DebugEvent::StepComplete;
                event.frame = callStack_.size() > 0 ? &callStack_.back() : nullptr;
                event.message = "Step complete";
                callback_(event, callbackUserData_);
            }
        }
    }
}

}
}
