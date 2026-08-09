#include "FrostEngine/Scripting/KitrisBytecode.h"

namespace Frost {
namespace Kitris {

BytecodeCompiler::CompileResult BytecodeCompiler::compile(ASTNode* ast, const String& moduleName) {
    CompileResult result;
    Compiler c;
    c.moduleName = moduleName;

    if (!ast) {
        result.errors.pushBack("No AST to compile");
        return result;
    }

    Function* fn = new Function();
    fn->name = moduleName.length() > 0 ? moduleName : "<module>";
    fn->arity = 0;
    fn->registerCount = 16;
    fn->upvalueCount = 0;
    fn->isNative = false;
    fn->nativePtr = nullptr;

    currentFunction_ = fn;

    compileBlock(c, ast);

    fn->code = c.code;
    fn->constants = c.constants;
    fn->registerCount = c.nextReg > 0 ? c.nextReg : 1;

    result.function = fn;
    result.errors = c.errors;
    return result;
}

u32 BytecodeCompiler::compileExpression(Compiler& c, ASTNode* node, u32 targetReg) {
    if (!node) return 0xFFFFFFFF;

    switch (node->type) {
    case ASTNodeType::Literal:
        compileLiteral(c, node, targetReg);
        return targetReg;

    case ASTNodeType::Identifier: {
        u32 reg = (targetReg == 0xFFFFFFFF) ? c.allocReg() : targetReg;
        // Look up variable - simplified: load from globals by name hash
        u32 nameIdx = c.addConstant(Value(node->stringValue));
        c.emit(OpCode::Move);
        c.emitU32(reg);
        c.emitU32(nameIdx);
        return reg;
    }

    case ASTNodeType::BinaryOp:
        compileBinaryOp(c, node, targetReg);
        return targetReg;

    case ASTNodeType::UnaryOp: {
        u32 reg = (targetReg == 0xFFFFFFFF) ? c.allocReg() : targetReg;
        u32 operandReg = compileExpression(c, node->children[0], reg);
        TokenType op = node->token.type;
        if (op == TokenType::Minus) {
            c.emit(OpCode::Neg);
            c.emitU32(reg);
            c.emitU32(operandReg);
        } else if (op == TokenType::BangEqual) {
            c.emit(OpCode::Not);
            c.emitU32(reg);
            c.emitU32(operandReg);
        } else if (op == TokenType::Tilde) {
            c.emit(OpCode::BitNot);
            c.emitU32(reg);
            c.emitU32(operandReg);
        }
        return reg;
    }

    case ASTNodeType::Call:
        compileCall(c, node, targetReg);
        return targetReg;

    case ASTNodeType::Index:
        compileIndex(c, node, targetReg);
        return targetReg;

    case ASTNodeType::Member:
        compileMember(c, node, targetReg);
        return targetReg;

    case ASTNodeType::If: {
        u32 reg = (targetReg == 0xFFFFFFFF) ? c.allocReg() : targetReg;
        compileIf(c, node);
        return reg;
    }

    case ASTNodeType::Block: {
        u32 reg = (targetReg == 0xFFFFFFFF) ? c.allocReg() : targetReg;
        compileBlock(c, node);
        return reg;
    }

    case ASTNodeType::Lambda:
        compileLambda(c, node);
        return targetReg;

    case ASTNodeType::ArrayLit: {
        u32 reg = (targetReg == 0xFFFFFFFF) ? c.allocReg() : targetReg;
        u32 count = (u32)node->children.size();
        c.emit(OpCode::NewArray);
        c.emitU32(reg);
        c.emitU32(count);
        for (u32 i = 0; i < count; i++) {
            u32 elemReg = compileExpression(c, node->children[i], c.allocReg());
            c.emit(OpCode::SetIndex);
            c.emitU32(reg);
            c.emitU32(i);
            c.emitU32(elemReg);
            c.freeReg(elemReg);
        }
        return reg;
    }

    case ASTNodeType::MapLit: {
        u32 reg = (targetReg == 0xFFFFFFFF) ? c.allocReg() : targetReg;
        c.emit(OpCode::NewMap);
        c.emitU32(reg);
        for (u32 i = 0; i + 1 < (u32)node->children.size(); i += 2) {
            u32 keyReg = compileExpression(c, node->children[i], c.allocReg());
            u32 valReg = compileExpression(c, node->children[i + 1], c.allocReg());
            c.emit(OpCode::SetIndex);
            c.emitU32(reg);
            c.emitU32(keyReg);
            c.emitU32(valReg);
            c.freeReg(keyReg);
            c.freeReg(valReg);
        }
        return reg;
    }

    default:
        break;
    }
    return targetReg;
}

void BytecodeCompiler::compileStatement(Compiler& c, ASTNode* node) {
    if (!node) return;

    switch (node->type) {
    case ASTNodeType::ExpressionStmt:
        if (!node->children.empty()) {
            u32 reg = compileExpression(c, node->children[0], 0xFFFFFFFF);
            c.freeReg(reg);
        }
        break;

    case ASTNodeType::Let:
    case ASTNodeType::Const:
    case ASTNodeType::Var: {
        u32 nameReg = c.allocReg();
        if (node->children.size() > 1) {
            u32 valReg = compileExpression(c, node->children[1], c.allocReg());
            c.emit(OpCode::Move);
            c.emitU32(nameReg);
            c.emitU32(valReg);
            c.freeReg(valReg);
        } else {
            c.emit(OpCode::LoadNull);
            c.emitU32(nameReg);
        }
        break;
    }

    case ASTNodeType::Assign: {
        if (node->children.size() >= 2) {
            u32 valReg = compileExpression(c, node->children[1], c.allocReg());
            u32 targetReg = compileExpression(c, node->children[0], c.allocReg());
            c.emit(OpCode::Move);
            c.emitU32(targetReg);
            c.emitU32(valReg);
            c.freeReg(valReg);
            c.freeReg(targetReg);
        }
        break;
    }

    case ASTNodeType::Return: {
        if (!node->children.empty()) {
            u32 reg = compileExpression(c, node->children[0], c.allocReg());
            c.emit(OpCode::Return);
            c.emitU32(reg);
            c.freeReg(reg);
        } else {
            c.emit(OpCode::LoadNull);
            u32 reg = c.allocReg();
            c.emitU32(reg);
            c.emit(OpCode::Return);
            c.emitU32(reg);
            c.freeReg(reg);
        }
        break;
    }

    case ASTNodeType::Break:
        c.emit(OpCode::Jump);
        c.emitU32(0); // placeholder
        break;

    case ASTNodeType::Continue:
        c.emit(OpCode::Jump);
        c.emitU32(0); // placeholder
        break;

    case ASTNodeType::Function:
        compileFunction(c, node, false);
        break;

    case ASTNodeType::Block:
        compileBlock(c, node);
        break;

    case ASTNodeType::If:
        compileIf(c, node);
        break;

    default:
        compileExpression(c, node, 0xFFFFFFFF);
        break;
    }
}

void BytecodeCompiler::compileBlock(Compiler& c, ASTNode* node) {
    if (!node) return;
    for (auto* child : node->children) {
        compileStatement(c, child);
    }
}

void BytecodeCompiler::compileIf(Compiler& c, ASTNode* node) {
    if (node->children.empty()) return;

    u32 condReg = compileExpression(c, node->children[0], c.allocReg());
    c.emit(OpCode::JumpIfNot);
    c.emitU32(condReg);
    u32 jumpOffset = (u32)c.code.size();
    c.emitU32(0); // placeholder
    c.freeReg(condReg);

    if (node->children.size() > 1) {
        compileBlock(c, node->children[1]);
    }

    // else branch
    if (node->children.size() > 2) {
        c.emit(OpCode::Jump);
        u32 elseJump = (u32)c.code.size();
        c.emitU32(0); // placeholder
        c.patchJump(jumpOffset, (u32)c.code.size());
        compileBlock(c, node->children[2]);
        c.patchJump(elseJump, (u32)c.code.size());
    } else {
        c.patchJump(jumpOffset, (u32)c.code.size());
    }
}

void BytecodeCompiler::compileWhile(Compiler& c, ASTNode* node) {
    if (node->children.empty()) return;

    u32 loopStart = (u32)c.code.size();
    u32 condReg = compileExpression(c, node->children[0], c.allocReg());
    c.emit(OpCode::JumpIfNot);
    c.emitU32(condReg);
    u32 exitJump = (u32)c.code.size();
    c.emitU32(0);
    c.freeReg(condReg);

    if (node->children.size() > 1) {
        compileBlock(c, node->children[1]);
    }

    c.emit(OpCode::Jump);
    c.emitU32(loopStart - (u32)c.code.size());
    c.patchJump(exitJump, (u32)c.code.size());
}

void BytecodeCompiler::compileFor(Compiler& c, ASTNode* node) {
    if (node->children.size() < 3) return;

    // init
    if (node->children[0]) {
        compileStatement(c, node->children[0]);
    }

    u32 loopStart = (u32)c.code.size();

    // condition
    if (node->children[1]) {
        u32 condReg = compileExpression(c, node->children[1], c.allocReg());
        c.emit(OpCode::JumpIfNot);
        c.emitU32(condReg);
        u32 exitJump = (u32)c.code.size();
        c.emitU32(0);
        c.freeReg(condReg);

        // body
        if (node->children.size() > 3) {
            compileBlock(c, node->children[3]);
        }

        // step
        if (node->children[2]) {
            compileStatement(c, node->children[2]);
        }

        c.emit(OpCode::Jump);
        c.emitU32(loopStart - (u32)c.code.size());
        c.patchJump(exitJump, (u32)c.code.size());
    } else {
        if (node->children.size() > 3) {
            compileBlock(c, node->children[3]);
        }
        if (node->children[2]) {
            compileStatement(c, node->children[2]);
        }
        c.emit(OpCode::Jump);
        c.emitU32(loopStart - (u32)c.code.size());
    }
}

void BytecodeCompiler::compileFunction(Compiler& c, ASTNode* node, bool isMethod) {
    (void)isMethod;
    if (node->children.size() < 2) return;

    u32 fnReg = c.allocReg();
    String fnName = node->children[0]->stringValue;

    Function* fn = new Function();
    fn->name = fnName;
    fn->arity = (u32)node->children.size() - 2; // minus name and body
    fn->registerCount = 16;
    fn->upvalueCount = 0;
    fn->isNative = false;
    fn->nativePtr = nullptr;

    Compiler innerC;
    innerC.moduleName = fnName;

    // Parameters become local registers
    for (u32 i = 1; i + 1 < (u32)node->children.size(); i++) {
        innerC.allocReg(); // reserve register for param
    }

    // Compile body
    ASTNode* body = node->children.back();
    for (auto* child : body->children) {
        compileStatement(innerC, child);
    }

    innerC.emit(OpCode::LoadNull);
    u32 retReg = innerC.allocReg();
    innerC.emitU32(retReg);
    innerC.emit(OpCode::Return);
    innerC.emitU32(retReg);

    fn->code = innerC.code;
    fn->constants = innerC.constants;
    fn->registerCount = innerC.nextReg > 0 ? innerC.nextReg : 1;

    u32 fnIdx = c.addConstant(Value());
    c.emit(OpCode::LoadConst);
    c.emitU32(fnReg);
    c.emitU32(fnIdx);
}

void BytecodeCompiler::compileLambda(Compiler& c, ASTNode* node) {
    (void)c;
    (void)node;
}

void BytecodeCompiler::compileBinaryOp(Compiler& c, ASTNode* node, u32 targetReg) {
    if (node->children.size() < 2) return;

    u32 reg = (targetReg == 0xFFFFFFFF) ? c.allocReg() : targetReg;

    // Short-circuit for && and ||
    if (node->token.type == TokenType::AmpersandAmpersand) {
        u32 leftReg = compileExpression(c, node->children[0], c.allocReg());
        c.emit(OpCode::JumpIfNot);
        c.emitU32(leftReg);
        u32 patch1 = (u32)c.code.size();
        c.emitU32(0);
        u32 rightReg = compileExpression(c, node->children[1], leftReg);
        c.emit(OpCode::Move);
        c.emitU32(reg);
        c.emitU32(rightReg);
        c.patchJump(patch1, (u32)c.code.size());
        c.emit(OpCode::Move);
        c.emitU32(reg);
        c.emitU32(leftReg);
        return;
    }

    if (node->token.type == TokenType::PipePipe) {
        u32 leftReg = compileExpression(c, node->children[0], c.allocReg());
        c.emit(OpCode::JumpIf);
        c.emitU32(leftReg);
        u32 patch1 = (u32)c.code.size();
        c.emitU32(0);
        u32 rightReg = compileExpression(c, node->children[1], leftReg);
        c.emit(OpCode::Move);
        c.emitU32(reg);
        c.emitU32(rightReg);
        c.patchJump(patch1, (u32)c.code.size());
        c.emit(OpCode::Move);
        c.emitU32(reg);
        c.emitU32(leftReg);
        return;
    }

    u32 leftReg = compileExpression(c, node->children[0], c.allocReg());
    u32 rightReg = compileExpression(c, node->children[1], c.allocReg());

    OpCode op;
    switch (node->token.type) {
        case TokenType::Plus: op = OpCode::Add; break;
        case TokenType::Minus: op = OpCode::Sub; break;
        case TokenType::Star: op = OpCode::Mul; break;
        case TokenType::Slash: op = OpCode::Div; break;
        case TokenType::Percent: op = OpCode::Mod; break;
        case TokenType::EqualEqual: op = OpCode::Eq; break;
        case TokenType::BangEqual: op = OpCode::Ne; break;
        case TokenType::Less: op = OpCode::Lt; break;
        case TokenType::Greater: op = OpCode::Gt; break;
        case TokenType::LessEqual: op = OpCode::Le; break;
        case TokenType::GreaterEqual: op = OpCode::Ge; break;
        case TokenType::Ampersand: op = OpCode::And; break;
        case TokenType::Pipe: op = OpCode::Or; break;
        case TokenType::Caret: op = OpCode::Xor; break;
        default: op = OpCode::Add; break;
    }

    c.emit(op);
    c.emitU32(reg);
    c.emitU32(leftReg);
    c.emitU32(rightReg);

    c.freeReg(leftReg);
    c.freeReg(rightReg);
}

void BytecodeCompiler::compileCall(Compiler& c, ASTNode* node, u32 targetReg) {
    if (node->children.empty()) return;

    u32 reg = (targetReg == 0xFFFFFFFF) ? c.allocReg() : targetReg;
    u32 funcReg = compileExpression(c, node->children[0], c.allocReg());

    u32 argc = (u32)node->children.size() - 1;
    Vector<u32> argRegs;
    for (u32 i = 1; i < (u32)node->children.size(); i++) {
        u32 argReg = compileExpression(c, node->children[i], c.allocReg());
        argRegs.pushBack(argReg);
    }

    c.emit(OpCode::Call);
    c.emitU32(funcReg);
    c.emitU32(argc);
    for (u32 reg : argRegs) {
        c.emitU32(reg);
    }

    c.emit(OpCode::Move);
    c.emitU32(reg);
    c.emitU32(0); // return value in register 0

    for (u32 reg : argRegs) {
        c.freeReg(reg);
    }
    c.freeReg(funcReg);
}

void BytecodeCompiler::compileIndex(Compiler& c, ASTNode* node, u32 targetReg) {
    if (node->children.size() < 2) return;
    u32 reg = (targetReg == 0xFFFFFFFF) ? c.allocReg() : targetReg;
    u32 objReg = compileExpression(c, node->children[0], c.allocReg());
    u32 idxReg = compileExpression(c, node->children[1], c.allocReg());
    c.emit(OpCode::GetIndex);
    c.emitU32(reg);
    c.emitU32(objReg);
    c.emitU32(idxReg);
    c.freeReg(objReg);
    c.freeReg(idxReg);
}

void BytecodeCompiler::compileMember(Compiler& c, ASTNode* node, u32 targetReg) {
    if (node->children.size() < 2) return;
    u32 reg = (targetReg == 0xFFFFFFFF) ? c.allocReg() : targetReg;
    u32 objReg = compileExpression(c, node->children[0], c.allocReg());
    u32 nameIdx = c.addConstant(Value(node->children[1]->stringValue));
    c.emit(OpCode::GetField);
    c.emitU32(reg);
    c.emitU32(objReg);
    c.emitU32(nameIdx);
    c.freeReg(objReg);
}

void BytecodeCompiler::compileLiteral(Compiler& c, ASTNode* node, u32 targetReg) {
    u32 reg = (targetReg == 0xFFFFFFFF) ? c.allocReg() : targetReg;

    switch (node->token.type) {
        case TokenType::Number:
            if (node->floatValue != 0.0 || node->numberValue == 0) {
                u32 constIdx = c.addConstant(Value(node->floatValue));
                c.emit(OpCode::LoadConst);
                c.emitU32(reg);
                c.emitU32(constIdx);
            } else {
                u32 constIdx = c.addConstant(Value((i64)node->numberValue));
                c.emit(OpCode::LoadConst);
                c.emitU32(reg);
                c.emitU32(constIdx);
            }
            break;

        case TokenType::String: {
            u32 constIdx = c.addConstant(Value(node->stringValue));
            c.emit(OpCode::LoadConst);
            c.emitU32(reg);
            c.emitU32(constIdx);
            break;
        }

        case TokenType::True:
            c.emit(OpCode::LoadTrue);
            c.emitU32(reg);
            break;

        case TokenType::False:
            c.emit(OpCode::LoadFalse);
            c.emitU32(reg);
            break;

        case TokenType::Null:
            c.emit(OpCode::LoadNull);
            c.emitU32(reg);
            break;

        default:
            c.emit(OpCode::LoadNull);
            c.emitU32(reg);
            break;
    }
}

u32 BytecodeCompiler::getPrecedence(ASTNodeType type) {
    switch (type) {
        case ASTNodeType::BinaryOp:
            return 1;
        case ASTNodeType::UnaryOp:
            return 2;
        case ASTNodeType::Call:
        case ASTNodeType::Index:
        case ASTNodeType::Member:
            return 3;
        default:
            return 0;
    }
}

}
}
