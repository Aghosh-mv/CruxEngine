#pragma once

// ============================================================================
// CruxEngine Kitris Parser — AST and parsing
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Scripting/KitrisLexer.h"

namespace Crux {
namespace Kitris {

// ---- AST Node Types ----
enum class ASTNodeType : u16 {
    // Expressions
    Literal, Identifier, BinaryOp, UnaryOp, Call, Index, Member,
    Lambda, Block, If, Match, Try, Throw,
    ArrayLit, MapLit, StructLit,
    Await, Spawn, Yield,
    Range, Spread,

    // Statements
    Let, Const, Var, Assign, AssignOp,
    Return, Break, Continue,
    ExpressionStmt, BlockStmt,

    // Declarations
    Function, Class, Struct, Trait, Impl,
    Import, Export, TypeAlias,

    // Patterns
    PatternWildcard, PatternLiteral, PatternBinding, PatternTuple,
    PatternStruct, PatternEnum, PatternOr, PatternGuard,
};

// ---- AST Node ----
struct ASTNode {
    ASTNodeType type;
    Token token;
    Vector<ASTNode*> children;
    String stringValue;
    u64 numberValue;
    f64 floatValue;
    ASTNode* parent = nullptr;

    ASTNode(ASTNodeType t, const Token& tok) : type(t), token(tok) {}
    ~ASTNode() { for (auto* c : children) delete c; }
};

// ---- Parser ----
class Parser {
public:
    explicit Parser(const Vector<Token>& tokens) : tokens_(tokens), pos_(0) {}

    ASTNode* parse();

    Vector<String> errors() const { return errors_; }

private:
    // Precedence levels
    enum Precedence : u8 {
        PrecNone = 0,
        PrecAssignment = 1,  // = += -= *= /= %=
        PrecTernary = 2,     // ? :
        PrecOr = 3,          // ||
        PrecAnd = 4,         // &&
        PrecBitwiseOr = 5,   // |
        PrecBitwiseXor = 6,  // ^
        PrecBitwiseAnd = 7,  // &
        PrecEquality = 8,    // == !=
        PrecComparison = 9,  // < > <= >=
        PrecRange = 10,      // .. ...
        PrecAddSub = 11,     // + -
        PrecMulDiv = 12,     // * / %
        PrecUnary = 13,      // - ! ~ ++ --
        PrecCall = 14,       // () [] .
        PrecPrimary = 15,
    };

    ASTNode* parsePrecedence(Precedence prec);
    ASTNode* parsePrimary();
    ASTNode* parseExpression();
    ASTNode* parseStatement();
    ASTNode* parseBlock();
    ASTNode* parseIf();
    ASTNode* parseWhile();
    ASTNode* parseFor();
    ASTNode* parseFunction();
    ASTNode* parseLambda();
    ASTNode* parseClass();
    ASTNode* parseStruct();
    ASTNode* parseMatch();
    ASTNode* parseTry();
    ASTNode* parseType();
    ASTNode* parsePattern();

    Token& current() { return tokens_[pos_]; }
    Token& peek(i32 offset = 1) { return tokens_[(u32)Mathf::min((i64)tokens_.size() - 1, (i64)pos_ + offset)]; }
    Token& previous() { return tokens_[(u32)Mathf::max(0, (i32)pos_ - 1)]; }
    bool check(TokenType type) { return current().type == type; }
    bool match(TokenType type) { if (check(type)) { pos_++; return true; } return false; }
    Token consume(TokenType type, const char* msg);
    void error(const char* msg);
    void synchronize();

    const Vector<Token>& tokens_;
    u32 pos_;
    Vector<String> errors_;
};

// ---- AST Visitor for codegen ----
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;
    virtual void visit(ASTNode* node) = 0;
};

}
}