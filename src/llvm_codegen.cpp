// TinyCPS LLVM IR backend (educational, LLVM 17+ opaque-pointer syntax).
//
// The frontend produces a validated AST. This backend walks that AST directly
// and serializes LLVM IR. It never parses generated C and does not depend on
// the LLVM C++ libraries. A separate LLVM-compatible tool (Clang) lowers the
// emitted .ll file to native code and links the executable.
//
// Every source variable is assigned a unique i64 stack slot. This keeps
// shadowing and repeated assignments straightforward. mem2reg can promote
// eligible slots to SSA values after IR generation.

#include "compiler.hpp"

#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tinycps {
namespace {

class LLVMGenerator {
public:
    LLVMGenerator() { scopes_.emplace_back(); }

    std::string generate(const Program& program) {
        for (const auto& stmt : program) emit_stmt(*stmt);
        line("ret i32 0");

        std::ostringstream output;
        output << "; TinyCPS LLVM IR backend - direct lowering from validated AST\n"
               << "; i64 integers, printf output, lexical scopes, counted loops\n"
               << "source_filename = \"tinycps\"\n\n"
               << "@.tinycps.print_format = private unnamed_addr constant [6 x i8] c\"%lld\\0A\\00\", align 1\n\n"
               << "declare i32 @printf(ptr, ...)\n\n"
               << "define i32 @main() {\n"
               << "entry:\n";

        // Entry-block allocations dominate all later loads/stores. Keeping
        // them together also makes LLVM's mem2reg optimization effective.
        output << allocations_.str();
        output << instructions_.str();
        output << "}\n";
        return output.str();
    }

private:
    std::ostringstream allocations_;
    std::ostringstream instructions_;
    std::vector<std::unordered_map<std::string, std::string>> scopes_;
    std::size_t next_temp_{0};
    std::size_t next_slot_{0};
    std::size_t next_loop_{0};

    static std::string annotation(const Location& loc) {
        return "line " + std::to_string(loc.line) + ":" + std::to_string(loc.column);
    }

    void line(const std::string& instruction) {
        instructions_ << "  " << instruction << "\n";
    }

    void label(const std::string& block_name) {
        instructions_ << block_name << ":\n";
    }

    std::string temp() { return "%t" + std::to_string(next_temp_++); }

    std::string allocate(const std::string& label) {
        const auto slot = "%var" + std::to_string(next_slot_++);
        allocations_ << "  " << slot << " = alloca i64 ; " << label << "\n";
        return slot;
    }

    // Map an identifier to its nearest declaration. Different declarations
    // of the same source name get different slots (lexical shadowing).
    std::string lookup(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        throw std::logic_error("missing binding after semantic analysis: " + name);
    }

    std::string emit_expr(const Expr& expr) {
        if (const auto* n = dynamic_cast<const NumberExpr*>(&expr))
            return n->value; // Literal i64 constant: does not emit instructions.

        if (const auto* n = dynamic_cast<const NameExpr*>(&expr)) {
            const auto result = temp();
            line(result + " = load i64, ptr " + lookup(n->name));
            return result;
        }

        if (const auto* n = dynamic_cast<const UnaryExpr*>(&expr)) {
            const auto input = emit_expr(*n->operand);
            const auto result = temp();
            if (n->op != TokenKind::Minus) throw std::logic_error("invalid unary operation");
            line(result + " = sub i64 0, " + input);
            return result;
        }

        if (const auto* n = dynamic_cast<const BinaryExpr*>(&expr)) {
            // Preserve the source language's left-to-right evaluation order.
            // Every expression is currently side-effect-free, but ordering
            // explicitly keeps the backend easy to extend later.
            const auto lhs = emit_expr(*n->left);
            const auto rhs = emit_expr(*n->right);
            std::string instruction;
            switch (n->op) {
                case TokenKind::Plus: instruction = "add"; break;
                case TokenKind::Minus: instruction = "sub"; break;
                case TokenKind::Star: instruction = "mul"; break;
                case TokenKind::Slash: instruction = "sdiv"; break;
                default: throw std::logic_error("invalid binary operation");
            }
            const auto result = temp();
            line(result + " = " + instruction + " i64 " + lhs + ", " + rhs);
            return result;
        }

        throw std::logic_error("unsupported expression at " + annotation(expr.location));
    }

    void emit_stmt(const Stmt& stmt) {
        if (const auto* n = dynamic_cast<const LetStmt*>(&stmt)) {
            // Evaluate the initializer before binding the name. Shadowing
            // thus cannot accidentally read the as-yet-uninitialized slot.
            const auto value = emit_expr(*n->value);
            const auto slot = allocate("let " + n->name);
            line("store i64 " + value + ", ptr " + slot);
            scopes_.back()[n->name] = slot;
            return;
        }
        if (const auto* n = dynamic_cast<const SetStmt*>(&stmt)) {
            const auto value = emit_expr(*n->value);
            line("store i64 " + value + ", ptr " + lookup(n->name));
            return;
        }
        if (const auto* n = dynamic_cast<const PrintStmt*>(&stmt)) {
            const auto value = emit_expr(*n->value);
            const auto format = temp();
            line(format + " = getelementptr inbounds [6 x i8], ptr @.tinycps.print_format, i64 0, i64 0");
            line("call i32 (ptr, ...) @printf(ptr " + format + ", i64 " + value + ")");
            return;
        }
        if (const auto* n = dynamic_cast<const RepeatStmt*>(&stmt)) {
            // Evaluate count exactly once before entering the loop. Its SSA
            // value dominates the header and is unaffected by body changes.
            const auto count = emit_expr(*n->count);
            const auto idx = next_loop_++;
            const auto prefix = "repeat." + std::to_string(idx);
            const auto header = prefix + ".header";
            const auto body = prefix + ".body";
            const auto latch = prefix + ".latch";
            const auto after = prefix + ".end";
            const auto counter_slot = allocate("repeat counter " + std::to_string(idx));

            line("store i64 0, ptr " + counter_slot);
            line("br label %" + header);

            label(header);
            const auto iteration = temp();
            const auto continue_loop = temp();
            line(iteration + " = load i64, ptr " + counter_slot);
            line(continue_loop + " = icmp slt i64 " + iteration + ", " + count);
            line("br i1 " + continue_loop + ", label %" + body + ", label %" + after);

            label(body);
            scopes_.emplace_back();
            for (const auto& nested : n->body) emit_stmt(*nested);
            scopes_.pop_back();
            line("br label %" + latch);

            label(latch);
            const auto current = temp();
            const auto next = temp();
            line(current + " = load i64, ptr " + counter_slot);
            line(next + " = add i64 " + current + ", 1");
            line("store i64 " + next + ", ptr " + counter_slot);
            line("br label %" + header);

            label(after);
            return;
        }
        throw std::logic_error("unsupported statement at " + annotation(stmt.location));
    }
};

} // namespace

std::string generate_llvm_ir(const Program& program) {
    return LLVMGenerator{}.generate(program);
}

} // namespace tinycps
