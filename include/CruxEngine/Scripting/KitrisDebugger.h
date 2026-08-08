#pragma once

// ============================================================================
// CruxEngine Kitris Debugger — Source-level debugging
// ============================================================================
// Features:
//   - Breakpoints (line, conditional, function)
//   - Step in/over/out
//   - Watch expressions
//   - Call stack inspection
//   - Variable inspection (locals, globals, upvalues)
//   - REPL at breakpoints
//   - Hot code reload
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Scripting/KitrisVM.h"

namespace Crux {
namespace Kitris {

enum class DebugEvent : u8 {
    BreakpointHit,
    StepComplete,
    FunctionEnter,
    FunctionExit,
    Exception,
    Output,
};

struct Breakpoint {
    u32 id;
    String file;
    u32 line;
    String condition;  // optional expression
    bool enabled = true;
    u32 hitCount = 0;
};

struct WatchExpression {
    String expression;
    Value lastValue;
    bool changed = false;
};

struct StackFrame {
    Function* function;
    u32 line;
    u32 column;
    u32 ip;
    u32 baseReg;
    Vector<Value> locals;
};

struct DebugEventData {
    DebugEvent type;
    StackFrame* frame;
    String message;
    Value value;
};

class Debugger {
public:
    using EventCallback = void(*)(const DebugEventData&, void* userData);

    void init(VM& vm) { vm_ = &vm; }

    // Breakpoints
    u32 addBreakpoint(const String& file, u32 line, const String& condition = "");
    bool removeBreakpoint(u32 id);
    bool enableBreakpoint(u32 id, bool enabled);
    const Vector<Breakpoint>& breakpoints() const { return breakpoints_; }

    // Execution control
    void pause();
    void resume();
    void stepIn();
    void stepOver();
    void stepOut();
    bool isPaused() const { return paused_; }

    // Inspection
    const Vector<StackFrame>& callStack() const { return callStack_; }
    Value evaluate(const String& expr, u32 frameIndex = 0);
    Vector<String> getLocals(u32 frameIndex = 0);
    Vector<String> getGlobals();
    Value getVariable(const String& name, u32 frameIndex = 0);

    // Watch expressions
    u32 addWatch(const String& expr);
    void removeWatch(u32 id);
    const Vector<WatchExpression>& watches() const { return watches_; }

    // REPL
    String repl(const String& input);

    // Hot reload
    void hotReload(const String& source);

    // Event callback
    void setEventCallback(EventCallback cb, void* userData) {
        callback_ = cb; callbackUserData_ = userData;
    }

    // Check for breakpoint hit (called from VM)
    void checkBreakpoints(u32 line, const String& file);

private:
    VM* vm_ = nullptr;
    Vector<Breakpoint> breakpoints_;
    Vector<WatchExpression> watches_;
    Vector<StackFrame> callStack_;
    EventCallback callback_ = nullptr;
    void* callbackUserData_ = nullptr;
    bool paused_ = false;
    bool stepping_ = false;
    u32 stepFrameDepth_ = 0;
};

}
}