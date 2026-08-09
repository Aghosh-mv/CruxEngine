#include "Scripting/KitrisLexer.h"
#include "Core/Log.h"

namespace Frost {
namespace Kitris {

static const struct { const char* text; TokenType type; } keywords[] = {
    {"let", TokenType::Let}, {"const", TokenType::Const}, {"var", TokenType::Var},
    {"if", TokenType::If}, {"else", TokenType::Else}, {"elif", TokenType::Elif},
    {"for", TokenType::For}, {"while", TokenType::While}, {"loop", TokenType::Loop}, {"in", TokenType::In},
    {"break", TokenType::Break}, {"continue", TokenType::Continue}, {"return", TokenType::Return},
    {"function", TokenType::Function}, {"fn", TokenType::Fn}, {"lambda", TokenType::Lambda},
    {"class", TokenType::Class}, {"struct", TokenType::Struct}, {"trait", TokenType::Trait}, {"impl", TokenType::Impl},
    {"match", TokenType::Match}, {"case", TokenType::Case}, {"default", TokenType::Default},
    {"try", TokenType::Try}, {"catch", TokenType::Catch}, {"finally", TokenType::Finally}, {"throw", TokenType::Throw},
    {"async", TokenType::Async}, {"await", TokenType::Await}, {"spawn", TokenType::Spawn}, {"yield", TokenType::Yield},
    {"import", TokenType::Import}, {"export", TokenType::Export}, {"from", TokenType::From}, {"as", TokenType::As},
    {"typeof", TokenType::Typeof}, {"sizeof", TokenType::Sizeof}, {"alignof", TokenType::Alignof},
    {"static", TokenType::Static}, {"inline", TokenType::Inline}, {"mut", TokenType::Mut}, {"ref", TokenType::Ref}, {"move", TokenType::Move},
    {"self", TokenType::Self}, {"super", TokenType::Super},
    {"where", TokenType::Where}, {"when", TokenType::When}, {"then", TokenType::Then}, {"do", TokenType::Do}, {"end", TokenType::End},
    {"true", TokenType::True}, {"false", TokenType::False}, {"null", TokenType::Null},
    {"int8", TokenType::Int8}, {"int16", TokenType::Int16}, {"int32", TokenType::Int32}, {"int64", TokenType::Int64},
    {"uint8", TokenType::UInt8}, {"uint16", TokenType::UInt16}, {"uint32", TokenType::UInt32}, {"uint64", TokenType::UInt64},
    {"float32", TokenType::Float32}, {"float64", TokenType::Float64},
    {"bool", TokenType::Bool}, {"char", TokenType::Char8}, {"string", TokenType::String8},
    {"vec2", TokenType::Vec2}, {"vec3", TokenType::Vec3}, {"vec4", TokenType::Vec4},
    {"mat2", TokenType::Mat2}, {"mat3", TokenType::Mat3}, {"mat4", TokenType::Mat4},
    {"quat", TokenType::Quat}, {"color", TokenType::Color},
    {"array", TokenType::Array}, {"map", TokenType::Map}, {"set", TokenType::Set}, {"option", TokenType::Option}, {"result", TokenType::Result},
    {"entity", TokenType::Entity}, {"component", TokenType::Component}, {"handle", TokenType::Handle},
    {"coroutine", TokenType::Coroutine}, {"future", TokenType::Future}, {"channel", TokenType::Channel},
    // Engine intrinsics
    {"entity_query", TokenType::EntityQuery}, {"component_query", TokenType::ComponentQuery},
    {"spawn_entity", TokenType::SpawnEntity}, {"despawn_entity", TokenType::DespawnEntity},
    {"get_component", TokenType::GetComponent}, {"set_component", TokenType::SetComponent},
    {"add_component", TokenType::AddComponent}, {"remove_component", TokenType::RemoveComponent},
    {"transform", TokenType::Transform}, {"mesh", TokenType::Mesh}, {"material", TokenType::Material},
    {"light", TokenType::Light}, {"camera", TokenType::Camera},
    {"physics", TokenType::Physics}, {"collider", TokenType::Collider}, {"rigidbody", TokenType::RigidBody},
    {"input", TokenType::Input}, {"time", TokenType::Time}, {"random", TokenType::Random}, {"math", TokenType::Math},
    {"debug", TokenType::Debug}, {"log", TokenType::Log}, {"assert", TokenType::Assert}, {"profile", TokenType::Profile},
    {nullptr, TokenType::Error}
};

Vector<Token> Lexer::tokenize() {
    Vector<Token> tokens;
    while (pos_ < source_.length()) {
        skipWhitespace();
        if (pos_ >= source_.length()) break;

        u32 startLine = line_, startCol = column_;
        char c = peek();

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            tokens.pushBack(scanIdentifier());
        } else if (c >= '0' && c <= '9') {
            tokens.pushBack(scanNumber());
        } else if (c == '"') {
            tokens.pushBack(scanString());
        } else if (c == '\'') {
            tokens.pushBack(scanChar());
        } else {
            tokens.pushBack(scanOperator());
        }
    }
    tokens.pushBack(makeToken(TokenType::EndOfFile));
    return tokens;
}

Token Lexer::makeToken(TokenType type, const String& text) {
    Token t;
    t.type = type;
    t.text = text.length() ? text : source_.substr(pos_ - text.length(), text.length());
    t.line = line_;
    t.column = column_;
    return t;
}

Token Lexer::makeError(const char* msg) {
    Token t;
    t.type = TokenType::Error;
    t.text = msg;
    t.line = line_;
    t.column = column_;
    return t;
}

char Lexer::peek(u32 offset) const {
    return (pos_ + offset < source_.length()) ? source_[pos_ + offset] : '\0';
}

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') { line_++; column_ = 1; }
    else { column_++; }
    return c;
}

bool Lexer::match(char expected) {
    if (peek() == expected) { advance(); return true; }
    return false;
}

void Lexer::skipWhitespace() {
    while (pos_ < source_.length()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r') { advance(); }
        else if (c == '\n') { advance(); }
        else if (c == '/' && peek(1) == '/') { while (peek() && peek() != '\n') advance(); }
        else if (c == '/' && peek(1) == '*') { advance(); advance(); while (peek() && !(peek() == '*' && peek(1) == '/')) advance(); if (peek()) { advance(); advance(); } }
        else break;
    }
}

Token Lexer::scanIdentifier() {
    u64 start = pos_;
    while (pos_ < source_.length()) {
        char c = peek();
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') advance();
        else break;
    }
    String text = source_.substr(start, pos_ - start);
    for (u32 i = 0; keywords[i].text; i++) {
        if (text == keywords[i].text) return makeToken(keywords[i].type, text);
    }
    return makeToken(TokenType::Identifier, text);
}

Token Lexer::scanNumber() {
    u64 start = pos_;
    bool isFloat = false;
    while (pos_ < source_.length()) {
        char c = peek();
        if (c >= '0' && c <= '9') advance();
        else if (c == '.' && peek(1) >= '0' && peek(1) <= '9') { isFloat = true; advance(); }
        else if (c == '_') advance(); // digit separator
        else break;
    }
    if (peek() == 'f' || peek() == 'F') { isFloat = true; advance(); }
    if (peek() == 'u' || peek() == 'U') { advance(); if (peek() == '8' || peek() == '1' || peek() == '3' || peek() == '6') advance(); }
    String text = source_.substr(start, pos_ - start);
    Token t = makeToken(isFloat ? TokenType::Number : TokenType::Number, text);
    if (isFloat) t.floatValue = atof(text.data()); else t.numberValue = atoll(text.data());
    return t;
}

Token Lexer::scanString() {
    advance(); // skip opening "
    u64 start = pos_;
    String result;
    while (pos_ < source_.length()) {
        char c = peek();
        if (c == '"') break;
        if (c == '\\') {
            advance();
            char esc = peek();
            if (esc == 'n') result.append('\n');
            else if (esc == 't') result.append('\t');
            else if (esc == 'r') result.append('\r');
            else if (esc == '"') result.append('"');
            else if (esc == '\\') result.append('\\');
            else if (esc == '0') result.append('\0');
            else if (esc == 'x') { advance(); u8 val = 0; for (int i = 0; i < 2; i++) { char h = peek(); advance(); val = (val << 4) | ((h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : 0); } result.append((char)val); }
            else result.append(esc);
            advance();
        } else {
            result.append(c);
            advance();
        }
    }
    if (peek() == '"') advance();
    return makeToken(TokenType::String, result);
}

Token Lexer::scanChar() {
    advance(); // skip '
    char c = advance();
    if (c == '\\') {
        char esc = advance();
        if (esc == 'n') c = '\n';
        else if (esc == 't') c = '\t';
        else if (esc == 'r') c = '\r';
        else if (esc == '\\') c = '\\';
        else if (esc == '\'') c = '\'';
    }
    if (peek() == '\'') advance();
    Token t = makeToken(TokenType::Char, String(&c, 1));
    t.numberValue = (u8)c;
    return t;
}

Token Lexer::scanOperator() {
    u64 start = pos_;
    char c = advance();
    char c2 = peek();

    // 3-char operators
    if (c == '.' && c2 == '.' && peek(1) == '.') { advance(); advance(); return makeToken(TokenType::DotDotDot); }
    if (c == '.' && c2 == '.') { advance(); return makeToken(TokenType::DotDot); }
    if (c == '+' && c2 == '+') { advance(); return makeToken(TokenType::PlusPlus); }
    if (c == '-' && c2 == '-') { advance(); return makeToken(TokenType::MinusMinus); }
    if (c == '+' && c2 == '=') { advance(); return makeToken(TokenType::PlusEqual); }
    if (c == '-' && c2 == '=') { advance(); return makeToken(TokenType::MinusEqual); }
    if (c == '*' && c2 == '=') { advance(); return makeToken(TokenType::StarEqual); }
    if (c == '/' && c2 == '=') { advance(); return makeToken(TokenType::SlashEqual); }
    if (c == '%' && c2 == '=') { advance(); return makeToken(TokenType::PercentEqual); }
    if (c == '=' && c2 == '=') { advance(); return makeToken(TokenType::EqualEqual); }
    if (c == '!' && c2 == '=') { advance(); return makeToken(TokenType::BangEqual); }
    if (c == '<' && c2 == '=') { advance(); return makeToken(TokenType::LessEqual); }
    if (c == '>' && c2 == '=') { advance(); return makeToken(TokenType::GreaterEqual); }
    if (c == '&' && c2 == '&') { advance(); return makeToken(TokenType::AmpersandAmpersand); }
    if (c == '|' && c2 == '|') { advance(); return makeToken(TokenType::PipePipe); }
    if (c == '-' && c2 == '>') { advance(); return makeToken(TokenType::Arrow); }
    if (c == '=' && c2 == '>') { advance(); return makeToken(TokenType::FatArrow); }

    // 1-char operators
    TokenType type = TokenType::Error;
    switch (c) {
        case '+': type = TokenType::Plus; break;
        case '-': type = TokenType::Minus; break;
        case '*': type = TokenType::Star; break;
        case '/': type = TokenType::Slash; break;
        case '%': type = TokenType::Percent; break;
        case '=': type = TokenType::Equal; break;
        case '<': type = TokenType::Less; break;
        case '>': type = TokenType::Greater; break;
        case '&': type = TokenType::Ampersand; break;
        case '|': type = TokenType::Pipe; break;
        case '^': type = TokenType::Caret; break;
        case '~': type = TokenType::Tilde; break;
        case '?': type = TokenType::Question; break;
        case ':': type = TokenType::Colon; break;
        case ';': type = TokenType::Semicolon; break;
        case ',': type = TokenType::Comma; break;
        case '@': type = TokenType::At; break;
        case '$': type = TokenType::Dollar; break;
        case '#': type = TokenType::Hash; break;
        case '.': type = TokenType::Dot; break;
        case '(': type = TokenType::LeftParen; break;
        case ')': type = TokenType::RightParen; break;
        case '{': type = TokenType::LeftBrace; break;
        case '}': type = TokenType::RightBrace; break;
        case '[': type = TokenType::LeftBracket; break;
        case ']': type = TokenType::RightBracket; break;
        default: type = TokenType::Error; break;
    }
    String text = source_.substr(start, pos_ - start);
    return makeToken(type, text);
}

}
}