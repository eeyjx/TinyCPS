// TinyCPS semantic analysis.
// The lexer recognizes names and the parser checks grammar; this pass checks
// whether names are declared, scopes are valid, and integer literals fit.

#include "compiler.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace tinycps {
namespace {

class Checker {
public:
    // The outermost scope exists even if the program contains no blocks.
    Checker() { scopes_.emplace_back(); }
    void run(const Program& program) {
        for (const auto& stmt : program) check_stmt(*stmt);
    }

private:
    // One set of declarations per lexical scope, from outermost to innermost.
    std::vector<std::unordered_set<std::string>> scopes_;

    [[noreturn]] static void fail(Location loc, const std::string& message) {
        throw std::runtime_error("line " + std::to_string(loc.line) + ":" +
            std::to_string(loc.column) + " (semantic): " + message);
    }
    // Search from the innermost block outward to support shadowing.
    bool visible(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
            if (it->contains(name)) return true;
        return false;
    }
    // Recursively validate expression leaves and subexpressions.
    void check_expr(const Expr& expr) {
        if (auto* node = dynamic_cast<const NumberExpr*>(&expr)) {
            // Source number literals must fit in the signed 64-bit demo type.
            try {
                std::size_t used = 0;
                std::stoll(node->value, &used, 10);
                if (used != node->value.size()) fail(expr.location, "invalid integer literal");
            } catch (const std::out_of_range&) {
                fail(expr.location, "integer literal outside signed 64-bit range");
            } catch (const std::invalid_argument&) {
                fail(expr.location, "invalid integer literal");
            }
            return;
        }
        if (auto* node = dynamic_cast<const NameExpr*>(&expr)) {
            if (!visible(node->name)) fail(expr.location, "undeclared variable '" + node->name + "'");
            return;
        }
        if (auto* node = dynamic_cast<const UnaryExpr*>(&expr)) {
            check_expr(*node->operand);
            return;
        }
        if (auto* node = dynamic_cast<const BinaryExpr*>(&expr)) {
            check_expr(*node->left);
            check_expr(*node->right);
            return;
        }
        fail(expr.location, "unsupported expression");
    }
    void check_stmt(const Stmt& stmt) {
        // Check the initializer *before* adding the new variable. This
        // prevents accidentally using the declaration before it exists.
        if (auto* node = dynamic_cast<const LetStmt*>(&stmt)) {
            check_expr(*node->value);
            auto& locals = scopes_.back();
            if (locals.contains(node->name)) fail(stmt.location, "duplicate declaration '" + node->name + "'");
            locals.insert(node->name);
            return;
        }
        // Assignment requires an existing declaration; it does not create one.
        if (auto* node = dynamic_cast<const SetStmt*>(&stmt)) {
            if (!visible(node->name)) fail(stmt.location, "assignment to undeclared variable '" + node->name + "'");
            check_expr(*node->value);
            return;
        }
        if (auto* node = dynamic_cast<const PrintStmt*>(&stmt)) {
            check_expr(*node->value);
            return;
        }
        // A repeat body introduces block scope. Names declared within it are
        // unavailable after this statement, while outer names stay visible.
        if (auto* node = dynamic_cast<const RepeatStmt*>(&stmt)) {
            check_expr(*node->count);
            scopes_.emplace_back();
            for (const auto& inner : node->body) check_stmt(*inner);
            scopes_.pop_back();
            return;
        }
        fail(stmt.location, "unsupported statement");
    }
};

}  // namespace

void check_semantics(const Program& program) {
    Checker{}.run(program);
}

}  // namespace tinycps
