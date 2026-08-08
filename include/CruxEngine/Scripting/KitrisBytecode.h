#pragma once

// ============================================================================
// CruxEngine Kitris Bytecode Compiler — AST to bytecode
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Scripting/KitrisParser.h"
#include "Scripting/KitrisVM.h"

namespace Crux {
namespace Kitris {

class BytecodeCompiler {
public:
    struct CompileResult {
        Function* function = nullptr;
        Vector<String> errors;
    };

    CompileResult compile(ASTNode* ast, const String& moduleName = "");

private:
    struct Compiler {
        Vector<OpCode> code;
        Vector<Value> constants;
        u32 registerCount = 0;
        u32 nextReg = 0;
        Vector<String> strings;
        Vector<String> errors;
        String moduleName;

        u32 allocReg() { return nextReg++; }
        void freeReg(u32 reg) { if (reg + 1 == nextReg) nextReg--; }
        u32 addConstant(Value v) { constants.pushBack(v); return (u32)constants.size() - 1; }
        u32 emit(OpCode op) { code.pushBack(op); return (u32)code.size() - 1; }
        void emitU32(u32 v) { code.pushBack((OpCode)v); }
        void patchJump(u32 offset, u32 target) { code[offset] = (OpCode)(target - offset); }
    };

    void compileNode(Compiler& c, ASTNode* node, u32 targetReg = 0xFFFFFFFF);
    u32 compileExpression(Compiler& c, ASTNode* node, u32 targetReg);
    void compileStatement(Compiler& c, ASTNode* node);
    void compileBlock(Compiler& c, ASTNode* node);
    void compileIf(Compiler& c, ASTNode* node);
    void compileWhile(Compiler& c, ASTNode* node);
    void compileFor(Compiler& c, ASTNode* node);
    void compileFunction(Compiler& c, ASTNode* node, bool isMethod);
    void compileLambda(Compiler& c, ASTNode* node);
    void compileBinaryOp(Compiler& c, ASTNode* node, u32 targetReg);
    void compileCall(Compiler& c, ASTNode* node, u32 targetReg);
    void compileIndex(Compiler& c, ASTNode* node, u32 targetReg);
    void compileMember(Compiler& c, ASTNode* node, u32 targetReg);
    void compileLiteral(Compiler& c, ASTNode* node, u32 targetReg);
    u32 getPrecedence(ASTNodeType type);

    Function* currentFunction_ = nullptr;
};

}
}