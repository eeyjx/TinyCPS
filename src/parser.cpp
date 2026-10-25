// TinyCPS recursive-descent parser.
// Each grammar level becomes a function: expression -> term -> unary ->
// primary. This structure enforces operator precedence and left associativity.

#include "compiler.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tinycps {
namespace {

class Parser {
public:
    explicit Parser(const std::vector<Token>& t) : tokens_(t) {}

    // A program is a sequence of statements until the explicit EOF token.
    Program program() {
        Program result;
        while (!at(TokenKind::End)) result.push_back(statement());
        return result;
    }

private:
    const std::vector<Token>& tokens_;
    std::size_t index_{0};

    const Token& current() const { return tokens_.at(index_); }
    bool at(TokenKind kind) const { return current().kind == kind; }
    // match() consumes an optional token; expect() demands a required one.
    bool match(TokenKind kind) {
        if (!at(kind)) return false;
        ++index_;
        return true;
    }
    const Token& expect(TokenKind kind, const char* message) {
        if (!at(kind)) {
            const auto& loc = current().location;
            throw std::runtime_error("line " + std::to_string(loc.line) + ":" +
                std::to_string(loc.column) + " (parser): " + message +
                ", got " + std::string(kind_name(current().kind)));
        }
        return tokens_.at(index_++);
    }

    // Dispatch based on the leading keyword and construct the AST node.
    // Syntax errors are detected here, before any C source is generated.
    StmtPtr statement() {
        const Token start = current();
        if (match(TokenKind::Let)) {
            std::string name = expect(TokenKind::Identifier, "expected variable after 'let'").lexeme;
            expect(TokenKind::Equal, "expected '=' in variable declaration");
            ExprPtr value = expression();
            expect(TokenKind::Semicolon, "expected ';' after variable declaration");
            return std::make_unique<LetStmt>(start.location, std::move(name), std::move(value));
        }
        if (match(TokenKind::Set)) {
            std::string name = expect(TokenKind::Identifier, "expected variable after 'set'").lexeme;
            expect(TokenKind::Equal, "expected '=' in assignment");
            ExprPtr value = expression();
            expect(TokenKind::Semicolon, "expected ';' after assignment");
            return std::make_unique<SetStmt>(start.location, std::move(name), std::move(value));
        }
        if (match(TokenKind::Print)) {
            expect(TokenKind::LParen, "expected '(' after 'print'");
            ExprPtr value = expression();
            expect(TokenKind::RParen, "expected ')' after print expression");
            expect(TokenKind::Semicolon, "expected ';' after print statement");
            return std::make_unique<PrintStmt>(start.location, std::move(value));
        }
        // repeat(count) { ... } contains nested statements. Its body is
        // represented as child nodes, not executed during parsing.
        if (match(TokenKind::Repeat)) {
            expect(TokenKind::LParen, "expected '(' after 'repeat'");
            ExprPtr count = expression();
            expect(TokenKind::RParen, "expected ')' after repeat count");
            expect(TokenKind::LBrace, "expected '{' before repeat body");
            std::vector<StmtPtr> body;
            while (!at(TokenKind::RBrace)) {
                if (at(TokenKind::End)) {
                    expect(TokenKind::RBrace, "expected '}' at end of repeat body");
                }
                body.push_back(statement());
            }
            expect(TokenKind::RBrace, "expected '}' at end of repeat body");
            return std::make_unique<RepeatStmt>(start.location, std::move(count), std::move(body));
        }
        const auto& loc = current().location;
        throw std::runtime_error("line " + std::to_string(loc.line) + ":" +
            std::to_string(loc.column) + " (parser): expected statement (let, set, print, repeat)");
    }

    // '+' and '-' form the lowest arithmetic precedence level.
    // The loop makes repeated operators left-associative: a-b-c = (a-b)-c.
    ExprPtr expression() {
        ExprPtr left = term();
        while (at(TokenKind::Plus) || at(TokenKind::Minus)) {
            const Token op = tokens_[index_++];
            left = std::make_unique<BinaryExpr>(op.location, op.kind, std::move(left), term());
        }
        return left;
    }
    // '*' and '/' bind more tightly because a term is parsed as an operand
    // of addition/subtraction before those outer operators are combined.
    ExprPtr term() {
        ExprPtr left = unary();
        while (at(TokenKind::Star) || at(TokenKind::Slash)) {
            const Token op = tokens_[index_++];
            left = std::make_unique<BinaryExpr>(op.location, op.kind, std::move(left), unary());
        }
        return left;
    }
    // Recursive unary parsing supports chains such as --x.
    ExprPtr unary() {
        if (at(TokenKind::Minus)) {
            const Token op = tokens_[index_++];
            return std::make_unique<UnaryExpr>(op.location, op.kind, unary());
        }
        return primary();
    }
    // Leaves of the expression tree: integer, variable, or parenthesized
    // expression. Parentheses affect tree shape but are not AST nodes.
    ExprPtr primary() {
        if (at(TokenKind::Number)) {
            const Token token = tokens_[index_++];
            return std::make_unique<NumberExpr>(token.location, token.lexeme);
        }
        if (at(TokenKind::Identifier)) {
            const Token token = tokens_[index_++];
            return std::make_unique<NameExpr>(token.location, token.lexeme);
        }
        if (match(TokenKind::LParen)) {
            ExprPtr value = expression();
            expect(TokenKind::RParen, "expected ')' after grouped expression");
            return value;
        }
        const auto& loc = current().location;
        throw std::runtime_error("line " + std::to_string(loc.line) + ":" +
            std::to_string(loc.column) + " (parser): expected a number, variable, or '('");
    }
};

} // anonymous namespace

Program parse(const std::vector<Token>& tokens) {
    return Parser(tokens).program();
}

}  // namespace tinycps
