#pragma once

// ============================================================================
// FrostEngine Kitris Language — Fast, expressive scripting for games
// ============================================================================
// Design goals:
//   - One line does a LOT (like APL/J but readable)
//   - Native vector/matrix math, entity queries, coroutines
//   - Compiles to bytecode, runs on register-based VM
//   - Interoperates seamlessly with C++ engine
//   - Hot-reloadable scripts
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"

namespace Frost {
namespace Kitris {

// ---- Token types ----
enum class TokenType : u16 {
    // Special
    EndOfFile = 0,
    Error,
    Identifier,

    // Literals
    Number, String, Char, True, False, Null,

    // Operators
    Plus, Minus, Star, Slash, Percent,      // + - * / %
    PlusPlus, MinusMinus,                    // ++ --
    PlusEqual, MinusEqual, StarEqual, SlashEqual, PercentEqual,  // += -= *= /= %=
    Equal, EqualEqual, BangEqual,            // = == !=
    Less, Greater, LessEqual, GreaterEqual,  // < > <= >=
    Ampersand, Pipe, Caret, Tilde,           // & | ^ ~
    AmpersandAmpersand, PipePipe,            // && ||
    Arrow, FatArrow,                         // -> =>
    Dot, DotDot, DotDotDot,                  // . .. ...
    Question, Colon, Semicolon, Comma,       // ? : ; ,
    At, Dollar, Hash,                        // @ $ #

    // Brackets
    LeftParen, RightParen,
    LeftBrace, RightBrace,
    LeftBracket, RightBracket,

    // Keywords
    Let, Const, Var,
    If, Else, Elif,
    For, While, Loop, In,
    Break, Continue, Return,
    Function, Fn, Lambda,
    Class, Struct, Trait, Impl,
    Match, Case, Default,
    Try, Catch, Finally, Throw,
    Async, Await, Spawn, Yield,
    Import, Export, From, As,
    Typeof, Sizeof, Alignof,
    Static, Inline, Mut, Ref, Move,
    Self, Super,
    Where, When, Then, Do,
    End,

    // Types
    Int8, Int16, Int32, Int64,
    UInt8, UInt16, UInt32, UInt64,
    Float32, Float64,
    Bool, Char8, String8,
    Vec2, Vec3, Vec4,
    Mat2, Mat3, Mat4,
    Quat, Color,
    Array, Map, Set, Option, Result,
    Entity, Component, Handle,
    Coroutine, Future, Channel,

    // Engine intrinsics
    EntityQuery, ComponentQuery,
    SpawnEntity, DespawnEntity,
    GetComponent, SetComponent, AddComponent, RemoveComponent,
    Transform, Mesh, Material, Light, Camera,
    Physics, Collider, RigidBody,
    Input, Time, Random, Math,
    Debug, Log, Assert, Profile,
};

struct Token {
    TokenType type;
    String text;
    u32 line;
    u32 column;
    u64 numberValue;
    f64 floatValue;
    String stringValue;
};

class Lexer {
public:
    explicit Lexer(const String& source) : source_(source), pos_(0), line_(1), column_(1) {}

    Vector<Token> tokenize();

private:
    Token makeToken(TokenType type, const String& text = "");
    Token makeError(const char* msg);
    char peek(u32 offset = 0) const;
    char advance();
    bool match(char expected);
    void skipWhitespace();
    Token scanIdentifier();
    Token scanNumber();
    Token scanString();
    Token scanChar();
    Token scanOperator();
    TokenType checkKeyword(const String& text);

    const String& source_;
    u64 pos_;
    u32 line_;
    u32 column_;
};

}
}