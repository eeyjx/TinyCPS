// TinyCPS - General Code Generation Study
// Shared syntax-tree definitions and public interfaces for the compiler stages.
// Pipeline: source -> tokens -> AST -> semantic checks -> C11 or LLVM IR.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tinycps {

// A source coordinate retained by tokens and AST nodes for diagnostics.
struct Location {
    std::size_t line{1};
    std::size_t column{1};
};

// Token categories recognized by the handwritten lexer. Keywords are
// different from user-defined identifiers, even though both start as letters.
enum class TokenKind {
    End, Identifier, Number,
    Let, Set, Print, Repeat,
    LParen, RParen, LBrace, RBrace,
    Equal, Semicolon, Plus, Minus, Star, Slash
};

struct Token {
    TokenKind kind;
    std::string lexeme;
    Location location;
};

std::string_view kind_name(TokenKind kind);
// Convert characters into a flat stream of classified tokens.
std::vector<Token> tokenize(std::string_view source);

// The AST represents *structure*, not source-text formatting.
// Each parent owns its child expressions through std::unique_ptr, so tree
// destruction requires no manual delete calls.
struct Expr {
    Location location;
    explicit Expr(Location loc) : location(loc) {}
    virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

// A decimal integer literal; the original spelling is preserved for emission.
struct NumberExpr : Expr {
    std::string value;
    NumberExpr(Location loc, std::string num) : Expr(loc), value(std::move(num)) {}
};
// A variable reference. Its declaration is verified in the semantic pass.
struct NameExpr : Expr {
    std::string name;
    NameExpr(Location loc, std::string id) : Expr(loc), name(std::move(id)) {}
};
// A prefix operator such as -x.
struct UnaryExpr : Expr {
    TokenKind op;
    ExprPtr operand;
    UnaryExpr(Location loc, TokenKind kind, ExprPtr expr)
        : Expr(loc), op(kind), operand(std::move(expr)) {}
};
// A binary arithmetic operator. Nesting encodes operator precedence.
struct BinaryExpr : Expr {
    TokenKind op;
    ExprPtr left;
    ExprPtr right;
    BinaryExpr(Location loc, TokenKind kind, ExprPtr lhs, ExprPtr rhs)
        : Expr(loc), op(kind), left(std::move(lhs)), right(std::move(rhs)) {}
};

// Statements describe actions and state changes; expressions compute values.
struct Stmt {
    Location location;
    explicit Stmt(Location loc) : location(loc) {}
    virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

// let introduces a variable inside the current lexical scope.
struct LetStmt : Stmt {
    std::string name;
    ExprPtr value;
    LetStmt(Location loc, std::string id, ExprPtr expr)
        : Stmt(loc), name(std::move(id)), value(std::move(expr)) {}
};
// set assigns to an already-visible variable (unlike let).
struct SetStmt : Stmt {
    std::string name;
    ExprPtr value;
    SetStmt(Location loc, std::string id, ExprPtr expr)
        : Stmt(loc), name(std::move(id)), value(std::move(expr)) {}
};
// print evaluates an expression and writes one integer line.
struct PrintStmt : Stmt {
    ExprPtr value;
    PrintStmt(Location loc, ExprPtr expr) : Stmt(loc), value(std::move(expr)) {}
};
// repeat evaluates its count once and introduces a nested lexical scope.
struct RepeatStmt : Stmt {
    ExprPtr count;
    std::vector<StmtPtr> body;
    RepeatStmt(Location loc, ExprPtr expr, std::vector<StmtPtr> statements)
        : Stmt(loc), count(std::move(expr)), body(std::move(statements)) {}
};

using Program = std::vector<StmtPtr>;
// Compiler passes deliberately use separate entry points so each stage
// can be inspected, tested, or replaced without rewriting the others.
Program parse(const std::vector<Token>& tokens);
void check_semantics(const Program& program);
// Selectable backends share the same validated AST. The LLVM backend emits
// textual LLVM IR directly without reparsing generated C.
std::string generate_c(const Program& program);
std::string generate_llvm_ir(const Program& program);
std::string dump_ast(const Program& program);

}  // namespace tinycps
