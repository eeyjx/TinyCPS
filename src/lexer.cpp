// TinyCPS lexer: split source characters into keywords, identifiers,
// unsigned decimal integer literals, punctuation, and arithmetic operators.
// Source locations are preserved so later stages can report precise errors.

#include "compiler.hpp"

#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace tinycps {

// Human-readable token names used by --tokens and syntax diagnostics.
std::string_view kind_name(TokenKind kind) {
    switch (kind) {
        case TokenKind::End: return "EOF";
        case TokenKind::Identifier: return "IDENT";
        case TokenKind::Number: return "NUMBER";
        case TokenKind::Let: return "LET";
        case TokenKind::Set: return "SET";
        case TokenKind::Print: return "PRINT";
        case TokenKind::Repeat: return "REPEAT";
        case TokenKind::LParen: return "LPAREN";
        case TokenKind::RParen: return "RPAREN";
        case TokenKind::LBrace: return "LBRACE";
        case TokenKind::RBrace: return "RBRACE";
        case TokenKind::Equal: return "EQUAL";
        case TokenKind::Semicolon: return "SEMICOLON";
        case TokenKind::Plus: return "PLUS";
        case TokenKind::Minus: return "MINUS";
        case TokenKind::Star: return "STAR";
        case TokenKind::Slash: return "SLASH";
    }
    return "UNKNOWN";
}

// Cast to unsigned char before calling <cctype> to avoid undefined behavior
// for negative signed-char values. An underscore is valid in identifiers.
static bool letter(char ch) {
    const auto c = static_cast<unsigned char>(ch);
    return std::isalpha(c) != 0 || ch == '_';
}
static bool digit(char ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
}

// A single linear scan turns the source into tokens, ending with EOF.
std::vector<Token> tokenize(std::string_view source) {
    std::vector<Token> tokens;
    std::size_t pos = 0;
    Location loc{};
    // Consume one character and update its 1-based line/column coordinate.
    auto advance = [&]() -> char {
        const char ch = source[pos++];
        if (ch == '\n') { ++loc.line; loc.column = 1; }
        else { ++loc.column; }
        return ch;
    };
    while (pos < source.size()) {
        const char ch = source[pos];
        // Whitespace separates tokens but is not itself represented in the AST.
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            advance();
            continue;
        }
        // Recognize // line comments before interpreting '/' as division.
        if (ch == '/' && pos + 1 < source.size() && source[pos + 1] == '/') {
            while (pos < source.size() && source[pos] != '\n') advance();
            continue;
        }
        const Location start = loc;
        const std::size_t begin = pos;
        // Scan a full identifier, then check whether it is a reserved word.
        if (letter(ch)) {
            advance();
            while (pos < source.size() && (letter(source[pos]) || digit(source[pos]))) advance();
            std::string word(source.substr(begin, pos - begin));
            TokenKind kind = TokenKind::Identifier;
            if (word == "let") kind = TokenKind::Let;
            else if (word == "set") kind = TokenKind::Set;
            else if (word == "print") kind = TokenKind::Print;
            else if (word == "repeat") kind = TokenKind::Repeat;
            tokens.push_back({kind, std::move(word), start});
            continue;
        }
        // Integer literals contain digits only; a leading '-' is a separate
        // unary operator interpreted later by the parser.
        if (digit(ch)) {
            advance();
            while (pos < source.size() && digit(source[pos])) advance();
            tokens.push_back({TokenKind::Number, std::string(source.substr(begin, pos - begin)), start});
            continue;
        }
        // Any remaining single-character symbol must be in this whitelist;
        // unknown characters fail immediately with their source coordinates.
        TokenKind kind;
        switch (ch) {
            case '(': kind = TokenKind::LParen; break;
            case ')': kind = TokenKind::RParen; break;
            case '{': kind = TokenKind::LBrace; break;
            case '}': kind = TokenKind::RBrace; break;
            case '=': kind = TokenKind::Equal; break;
            case ';': kind = TokenKind::Semicolon; break;
            case '+': kind = TokenKind::Plus; break;
            case '-': kind = TokenKind::Minus; break;
            case '*': kind = TokenKind::Star; break;
            case '/': kind = TokenKind::Slash; break;
            default:
                throw std::runtime_error("line " + std::to_string(start.line) +
                    ":" + std::to_string(start.column) + " (lexer): unexpected character '" + ch + "'");
        }
        tokens.push_back({kind, std::string(1, advance()), start});
    }
    // An explicit EOF token lets the parser detect missing delimiters.
    tokens.push_back({TokenKind::End, "", loc});
    return tokens;
}

}  // namespace tinycps
