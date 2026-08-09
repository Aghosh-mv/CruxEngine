#include "FrostEngine/Scripting/KitrisParser.h"

namespace Frost {
namespace Kitris {

ASTNode* Parser::parse() {
    ASTNode* root = new ASTNode(ASTNodeType::Block, current());
    while (!check(TokenType::EndOfFile)) {
        ASTNode* stmt = parseStatement();
        if (stmt) {
            stmt->parent = root;
            root->children.pushBack(stmt);
        }
    }
    return root;
}

Token Parser::consume(TokenType type, const char* msg) {
    if (check(type)) {
        return current();
    }
    error(msg);
    return current();
}

void Parser::error(const char* msg) {
    const Token& tok = current();
    char lineBuf[32];
    snprintf(lineBuf, sizeof(lineBuf), "%u", tok.line);
    String errMsg = String("[line ") + lineBuf + String("] Error: ") + msg;
    if (tok.text.length() > 0) {
        errMsg = errMsg + String(" near '") + tok.text + String("'");
    }
    errors_.pushBack(errMsg);
}

void Parser::synchronize() {
    while (!check(TokenType::EndOfFile)) {
        if (check(TokenType::Semicolon)) {
            pos_++;
            return;
        }
        switch (current().type) {
            case TokenType::Function:
            case TokenType::Fn:
            case TokenType::Class:
            case TokenType::Struct:
            case TokenType::Trait:
            case TokenType::Impl:
            case TokenType::For:
            case TokenType::While:
            case TokenType::If:
            case TokenType::Match:
            case TokenType::Try:
            case TokenType::Return:
                return;
            default:
                break;
        }
        pos_++;
    }
}

ASTNode* Parser::parseExpression() {
    return parsePrecedence(PrecNone);
}

ASTNode* Parser::parsePrecedence(Precedence minPrec) {
    ASTNode* left = parsePrimary();

    while (true) {
        TokenType op = current().type;
        Precedence prec = PrecNone;

        switch (op) {
            case TokenType::Plus:
            case TokenType::Minus:
                prec = PrecAddSub;
                break;
            case TokenType::Star:
            case TokenType::Slash:
            case TokenType::Percent:
                prec = PrecMulDiv;
                break;
            case TokenType::EqualEqual:
            case TokenType::BangEqual:
                prec = PrecEquality;
                break;
            case TokenType::Less:
            case TokenType::Greater:
            case TokenType::LessEqual:
            case TokenType::GreaterEqual:
                prec = PrecComparison;
                break;
            case TokenType::Ampersand:
                prec = PrecBitwiseAnd;
                break;
            case TokenType::Pipe:
                prec = PrecBitwiseOr;
                break;
            case TokenType::Caret:
                prec = PrecBitwiseXor;
                break;
            case TokenType::AmpersandAmpersand:
                prec = PrecAnd;
                break;
            case TokenType::PipePipe:
                prec = PrecOr;
                break;
            case TokenType::DotDot:
            case TokenType::DotDotDot:
                prec = PrecRange;
                break;
            case TokenType::Equal:
            case TokenType::PlusEqual:
            case TokenType::MinusEqual:
            case TokenType::StarEqual:
            case TokenType::SlashEqual:
            case TokenType::PercentEqual:
                prec = PrecAssignment;
                break;
            case TokenType::Question:
                prec = PrecTernary;
                break;
            default:
                prec = PrecNone;
                break;
        }

        if (prec == PrecNone || prec < minPrec) break;

        if (prec == PrecAssignment) {
            ASTNode* node = new ASTNode(ASTNodeType::Assign, current());
            node->children.pushBack(left);
            pos_++;
            ASTNode* rhs = parsePrecedence(PrecAssignment);
            node->children.pushBack(rhs);
            left = node;
            continue;
        }

        if (op == TokenType::Question) {
            ASTNode* node = new ASTNode(ASTNodeType::BinaryOp, current());
            node->children.pushBack(left);
            pos_++;
            ASTNode* thenExpr = parseExpression();
            node->children.pushBack(thenExpr);
            consume(TokenType::Colon, "Expected ':' in ternary");
            ASTNode* elseExpr = parsePrecedence(PrecTernary);
            node->children.pushBack(elseExpr);
            left = node;
            continue;
        }

        ASTNode* node = new ASTNode(ASTNodeType::BinaryOp, current());
        node->children.pushBack(left);
        pos_++;

        ASTNode* right = parsePrecedence((Precedence)(prec + 1));
        node->children.pushBack(right);
        left = node;
    }

    return left;
}

ASTNode* Parser::parsePrimary() {
    const Token& tok = current();

    // Literals
    if (tok.type == TokenType::Number) {
        ASTNode* node = new ASTNode(ASTNodeType::Literal, tok);
        node->numberValue = tok.numberValue;
        node->floatValue = tok.floatValue;
        pos_++;
        return node;
    }

    if (tok.type == TokenType::String) {
        ASTNode* node = new ASTNode(ASTNodeType::Literal, tok);
        node->stringValue = tok.stringValue;
        pos_++;
        return node;
    }

    if (tok.type == TokenType::True || tok.type == TokenType::False) {
        ASTNode* node = new ASTNode(ASTNodeType::Literal, tok);
        node->numberValue = (tok.type == TokenType::True) ? 1 : 0;
        pos_++;
        return node;
    }

    if (tok.type == TokenType::Null) {
        ASTNode* node = new ASTNode(ASTNodeType::Literal, tok);
        pos_++;
        return node;
    }

    // Identifier
    if (tok.type == TokenType::Identifier) {
        ASTNode* node = new ASTNode(ASTNodeType::Identifier, tok);
        node->stringValue = tok.text;
        pos_++;
        return node;
    }

    // Unary operators
    if (tok.type == TokenType::Minus || tok.type == TokenType::BangEqual || tok.type == TokenType::Tilde) {
        ASTNode* node = new ASTNode(ASTNodeType::UnaryOp, tok);
        pos_++;
        ASTNode* operand = parsePrecedence(PrecUnary);
        node->children.pushBack(operand);
        return node;
    }

    // Parenthesized expression
    if (tok.type == TokenType::LeftParen) {
        pos_++;
        ASTNode* expr = parseExpression();
        consume(TokenType::RightParen, "Expected ')' after expression");
        return expr;
    }

    // Array literal
    if (tok.type == TokenType::LeftBracket) {
        ASTNode* node = new ASTNode(ASTNodeType::ArrayLit, tok);
        pos_++;
        if (!check(TokenType::RightBracket)) {
            do {
                ASTNode* elem = parseExpression();
                node->children.pushBack(elem);
            } while (match(TokenType::Comma));
        }
        consume(TokenType::RightBracket, "Expected ']' after array");
        return node;
    }

    // Map literal
    if (tok.type == TokenType::LeftBrace) {
        ASTNode* node = new ASTNode(ASTNodeType::MapLit, tok);
        pos_++;
        if (!check(TokenType::RightBrace)) {
            do {
                ASTNode* key = parseExpression();
                consume(TokenType::Colon, "Expected ':' in map literal");
                ASTNode* val = parseExpression();
                node->children.pushBack(key);
                node->children.pushBack(val);
            } while (match(TokenType::Comma));
        }
        consume(TokenType::RightBrace, "Expected '}' after map");
        return node;
    }

    // Block / lambda
    if (tok.type == TokenType::LeftBrace) {
        return parseBlock();
    }

    // Keywords
    if (tok.type == TokenType::If) return parseIf();
    if (tok.type == TokenType::While) return parseWhile();
    if (tok.type == TokenType::For) return parseFor();
    if (tok.type == TokenType::Function || tok.type == TokenType::Fn) return parseFunction();
    if (tok.type == TokenType::Lambda) return parseLambda();
    if (tok.type == TokenType::Class) return parseClass();
    if (tok.type == TokenType::Struct) return parseStruct();
    if (tok.type == TokenType::Match) return parseMatch();
    if (tok.type == TokenType::Try) return parseTry();

    // Let / Const / Var
    if (tok.type == TokenType::Let || tok.type == TokenType::Const || tok.type == TokenType::Var) {
        ASTNode* node = new ASTNode(ASTNodeType::Let, tok);
        pos_++;
        ASTNode* name = parsePrimary();
        node->children.pushBack(name);
        if (match(TokenType::Equal)) {
            ASTNode* init = parseExpression();
            node->children.pushBack(init);
        }
        match(TokenType::Semicolon);
        return node;
    }

    // Return / Break / Continue
    if (tok.type == TokenType::Return) {
        ASTNode* node = new ASTNode(ASTNodeType::Return, tok);
        pos_++;
        if (!check(TokenType::Semicolon) && !check(TokenType::RightBrace) && !check(TokenType::EndOfFile)) {
            node->children.pushBack(parseExpression());
        }
        match(TokenType::Semicolon);
        return node;
    }

    if (tok.type == TokenType::Break) {
        ASTNode* node = new ASTNode(ASTNodeType::Break, tok);
        pos_++;
        match(TokenType::Semicolon);
        return node;
    }

    if (tok.type == TokenType::Continue) {
        ASTNode* node = new ASTNode(ASTNodeType::Continue, tok);
        pos_++;
        match(TokenType::Semicolon);
        return node;
    }

    error("Unexpected token");
    pos_++;
    return new ASTNode(ASTNodeType::Literal, tok);
}

ASTNode* Parser::parseStatement() {
    ASTNode* stmt = parseExpression();
    if (stmt) {
        match(TokenType::Semicolon);
    }
    return stmt;
}

ASTNode* Parser::parseBlock() {
    ASTNode* node = new ASTNode(ASTNodeType::Block, current());
    consume(TokenType::LeftBrace, "Expected '{'");
    while (!check(TokenType::RightBrace) && !check(TokenType::EndOfFile)) {
        ASTNode* stmt = parseStatement();
        if (stmt) {
            stmt->parent = node;
            node->children.pushBack(stmt);
        }
    }
    consume(TokenType::RightBrace, "Expected '}'");
    return node;
}

ASTNode* Parser::parseIf() {
    ASTNode* node = new ASTNode(ASTNodeType::If, current());
    pos_++; // consume 'if'
    ASTNode* cond = parseExpression();
    node->children.pushBack(cond);
    ASTNode* then = parseBlock();
    node->children.pushBack(then);
    while (match(TokenType::Elif)) {
        ASTNode* elifCond = parseExpression();
        node->children.pushBack(elifCond);
        ASTNode* elifBody = parseBlock();
        node->children.pushBack(elifBody);
    }
    if (match(TokenType::Else)) {
        ASTNode* elseBody = parseBlock();
        node->children.pushBack(elseBody);
    }
    return node;
}

ASTNode* Parser::parseWhile() {
    ASTNode* node = new ASTNode(ASTNodeType::Block, current());
    pos_++; // consume 'while'
    ASTNode* cond = parseExpression();
    node->children.pushBack(cond);
    ASTNode* body = parseBlock();
    node->children.pushBack(body);
    return node;
}

ASTNode* Parser::parseFor() {
    ASTNode* node = new ASTNode(ASTNodeType::Block, current());
    pos_++; // consume 'for'
    // for (init; cond; step) body
    match(TokenType::LeftParen);
    ASTNode* init = nullptr;
    if (!check(TokenType::Semicolon)) {
        init = parseExpression();
    }
    node->children.pushBack(init ? init : new ASTNode(ASTNodeType::Literal, current()));
    match(TokenType::Semicolon);
    ASTNode* cond = nullptr;
    if (!check(TokenType::Semicolon)) {
        cond = parseExpression();
    }
    node->children.pushBack(cond ? cond : new ASTNode(ASTNodeType::Literal, current()));
    match(TokenType::Semicolon);
    ASTNode* step = nullptr;
    if (!check(TokenType::RightParen)) {
        step = parseExpression();
    }
    node->children.pushBack(step ? step : new ASTNode(ASTNodeType::Literal, current()));
    match(TokenType::RightParen);
    ASTNode* body = parseBlock();
    node->children.pushBack(body);
    return node;
}

ASTNode* Parser::parseFunction() {
    ASTNode* node = new ASTNode(ASTNodeType::Function, current());
    pos_++; // consume 'function' or 'fn'
    ASTNode* name = new ASTNode(ASTNodeType::Identifier, current());
    name->stringValue = current().text;
    pos_++;
    node->children.pushBack(name);
    consume(TokenType::LeftParen, "Expected '(' after function name");
    while (!check(TokenType::RightParen) && !check(TokenType::EndOfFile)) {
        ASTNode* param = new ASTNode(ASTNodeType::Identifier, current());
        param->stringValue = current().text;
        pos_++;
        node->children.pushBack(param);
        if (!match(TokenType::Comma)) break;
    }
    consume(TokenType::RightParen, "Expected ')'");
    if (match(TokenType::Arrow)) {
        ASTNode* retType = parseType();
        node->children.pushBack(retType);
    }
    ASTNode* body = parseBlock();
    node->children.pushBack(body);
    return node;
}

ASTNode* Parser::parseLambda() {
    ASTNode* node = new ASTNode(ASTNodeType::Lambda, current());
    pos_++; // consume 'lambda'
    if (match(TokenType::LeftParen)) {
        while (!check(TokenType::RightParen) && !check(TokenType::EndOfFile)) {
            ASTNode* param = new ASTNode(ASTNodeType::Identifier, current());
            param->stringValue = current().text;
            pos_++;
            node->children.pushBack(param);
            if (!match(TokenType::Comma)) break;
        }
        consume(TokenType::RightParen, "Expected ')'");
    }
    if (match(TokenType::Arrow)) {
        ASTNode* body = parseExpression();
        node->children.pushBack(body);
    } else {
        ASTNode* body = parseBlock();
        node->children.pushBack(body);
    }
    return node;
}

ASTNode* Parser::parseClass() {
    ASTNode* node = new ASTNode(ASTNodeType::Class, current());
    pos_++; // consume 'class'
    ASTNode* name = new ASTNode(ASTNodeType::Identifier, current());
    name->stringValue = current().text;
    pos_++;
    node->children.pushBack(name);
    if (match(TokenType::Colon)) {
        ASTNode* parent = parseType();
        node->children.pushBack(parent);
    }
    ASTNode* body = parseBlock();
    node->children.pushBack(body);
    return node;
}

ASTNode* Parser::parseStruct() {
    ASTNode* node = new ASTNode(ASTNodeType::Struct, current());
    pos_++; // consume 'struct'
    ASTNode* name = new ASTNode(ASTNodeType::Identifier, current());
    name->stringValue = current().text;
    pos_++;
    node->children.pushBack(name);
    ASTNode* body = parseBlock();
    node->children.pushBack(body);
    return node;
}

ASTNode* Parser::parseMatch() {
    ASTNode* node = new ASTNode(ASTNodeType::Match, current());
    pos_++; // consume 'match'
    ASTNode* value = parseExpression();
    node->children.pushBack(value);
    consume(TokenType::LeftBrace, "Expected '{'");
    while (!check(TokenType::RightBrace) && !check(TokenType::EndOfFile)) {
        ASTNode* pattern = parsePattern();
        node->children.pushBack(pattern);
        consume(TokenType::Arrow, "Expected '=>'");
        ASTNode* body = parseExpression();
        node->children.pushBack(body);
        match(TokenType::Comma);
    }
    consume(TokenType::RightBrace, "Expected '}'");
    return node;
}

ASTNode* Parser::parseTry() {
    ASTNode* node = new ASTNode(ASTNodeType::Try, current());
    pos_++; // consume 'try'
    ASTNode* body = parseBlock();
    node->children.pushBack(body);
    if (match(TokenType::Catch)) {
        ASTNode* catchBlock = parseBlock();
        node->children.pushBack(catchBlock);
    }
    if (match(TokenType::Finally)) {
        ASTNode* finallyBlock = parseBlock();
        node->children.pushBack(finallyBlock);
    }
    return node;
}

ASTNode* Parser::parseType() {
    ASTNode* node = new ASTNode(ASTNodeType::Identifier, current());
    node->stringValue = current().text;
    pos_++;
    return node;
}

ASTNode* Parser::parsePattern() {
    const Token& tok = current();
    if (tok.type == TokenType::Identifier && tok.text == "_") {
        ASTNode* node = new ASTNode(ASTNodeType::PatternWildcard, tok);
        pos_++;
        return node;
    }
    if (tok.type == TokenType::Number) {
        ASTNode* node = new ASTNode(ASTNodeType::PatternLiteral, tok);
        node->numberValue = tok.numberValue;
        pos_++;
        return node;
    }
    ASTNode* node = new ASTNode(ASTNodeType::PatternBinding, tok);
    node->stringValue = tok.text;
    pos_++;
    return node;
}

}
}
