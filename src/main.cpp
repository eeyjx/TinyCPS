// TinyCPS command-line driver: .tcps -> validated AST -> C11 or LLVM IR.
//
// Native executable mode uses the LLVM-IR backend by default. TinyCPS owns
// lexing, parsing, semantic analysis, and IR generation. Clang is invoked as
// an external LLVM machine-code backend and linker; this is NOT a standalone
// assembler, linker, or C compiler.

#include "compiler.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace {

enum class Mode { EmitC, EmitLLVM, EmitObject, Executable };

struct Options {
    std::string input;
    std::string output;
    std::string clang_path{"clang"};
    Mode mode{Mode::Executable};
    std::string backend{"llvm"};
    std::string optimization{"-O0"};
    bool keep_temps{false};
    bool show_tokens{false};
    bool show_ast{false};
};

void usage() {
    std::cerr
        << "Usage:\n"
        << "  tinycps <file.tcps> -o <program> [--backend llvm|c] [--keep-temps]\n"
        << "  tinycps <file.tcps> --emit-llvm <file.ll>\n"
        << "  tinycps <file.tcps> --emit-c <file.c>\n"
        << "  tinycps <file.tcps> --emit-obj <file.o>\n"
        << "  tinycps <file.tcps> <file.c> [--tokens|--ast]   (legacy)\n"
        << "Options: --tokens --ast --clang <path> -O0 -O2 --keep-temps\n";
}

Options parse_options(int argc, char** argv) {
    Options opts;
    if (auto* env = std::getenv("TINYCPS_CLANG"); env && *env)
        opts.clang_path = env;
    bool has_explicit_mode = false;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" || a == "--emit-c" || a == "--emit-llvm" || a == "--emit-obj") {
            if (has_explicit_mode) throw std::runtime_error("specify only one output mode");
            if (++i >= argc) throw std::runtime_error("missing filename after " + a);
            opts.output = argv[i];
            opts.mode = a == "-o" ? Mode::Executable :
                        a == "--emit-c" ? Mode::EmitC :
                        a == "--emit-llvm" ? Mode::EmitLLVM : Mode::EmitObject;
            has_explicit_mode = true;
        } else if (a == "--backend" || a == "--clang") {
            if (++i >= argc) throw std::runtime_error("missing value after " + a);
            if (a == "--backend") opts.backend = argv[i];
            else opts.clang_path = argv[i];
        } else if (a == "--tokens") opts.show_tokens = true;
        else if (a == "--ast") opts.show_ast = true;
        else if (a == "--keep-temps") opts.keep_temps = true;
        else if (a == "-O0" || a == "-O2") opts.optimization = a;
        else if (!a.empty() && a.front() == '-') throw std::runtime_error("unknown option: " + a);
        else positional.push_back(a);
    }

    if (positional.size() != (has_explicit_mode ? 1U : 2U))
        throw std::runtime_error("expected input.tcps and an output path");
    opts.input = positional[0];
    if (!has_explicit_mode) { // Backward-compatible source-to-C invocation.
        opts.output = positional[1];
        opts.mode = Mode::EmitC;
    }
    if (opts.backend != "llvm" && opts.backend != "c")
        throw std::runtime_error("--backend must be 'llvm' or 'c'");
    if (opts.mode == Mode::EmitLLVM && opts.backend == "c")
        throw std::runtime_error("--emit-llvm cannot be combined with --backend c");

    if (fs::absolute(opts.input).lexically_normal() == fs::absolute(opts.output).lexically_normal())
        throw std::runtime_error("input and output paths must differ");
    return opts;
}

std::string read_all(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("could not read input: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_all(const fs::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("could not write: " + path.string());
    stream << text;
    if (!stream) throw std::runtime_error("write failed: " + path.string());
}

// An argv-based process launch is safer than system("..."), because user
// filenames are never interpreted by a shell (no command injection).
void run_tool(const std::vector<std::string>& args, const std::string& phase) {
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
    if (pid == 0) {
        execvp(argv[0], argv.data());
        std::cerr << "TinyCPS: cannot start " << args[0] << ": " << std::strerror(errno) << '\n';
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) throw std::runtime_error("waitpid failed");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error(phase + " failed (" +
            (WIFEXITED(status) ? "exit " + std::to_string(WEXITSTATUS(status)) : "signal") + ")");
}

// mkdtemp creates a private temporary directory that is automatically
// removed even when compilation or linking throws an exception.
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        auto pattern = (fs::temp_directory_path() / "tinycps-XXXXXX").string();
        std::vector<char> mutable_pattern(pattern.begin(), pattern.end());
        mutable_pattern.push_back('\0');
        char* created = mkdtemp(mutable_pattern.data());
        if (!created) throw std::runtime_error("mkdtemp failed");
        path_ = created;
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    const fs::path& path() const { return path_; }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
private:
    fs::path path_;
};

void build_native(const Options& opts, const tinycps::Program& program) {
    const bool llvm = opts.backend == "llvm";
    const auto text = llvm ? tinycps::generate_llvm_ir(program) : tinycps::generate_c(program);
    TemporaryDirectory temp;
    const auto source = temp.path() / (llvm ? "generated.ll" : "generated.c");
    const auto object = temp.path() / "generated.o";
    write_all(source, text);

    if (opts.keep_temps) {
        const fs::path permanent = opts.output + (llvm ? ".ll" : ".c");
        write_all(permanent, text);
    }

    std::vector<std::string> compile_args{opts.clang_path};
    if (llvm) compile_args.insert(compile_args.end(), {"-x", "ir", "-Wno-override-module"});
    else compile_args.insert(compile_args.end(), {"-x", "c", "-std=c11"});
    compile_args.insert(compile_args.end(), {opts.optimization, "-c", source.string(), "-o",
        opts.mode == Mode::EmitObject ? opts.output : object.string()});
    run_tool(compile_args, "object-code generation");

    if (opts.mode == Mode::EmitObject) {
        std::cout << "Generated object file: " << opts.output << '\n';
        return;
    }
    if (opts.keep_temps) fs::copy_file(object, opts.output + ".o", fs::copy_options::overwrite_existing);
    run_tool({opts.clang_path, object.string(), "-o", opts.output}, "linking");
    std::cout << "Generated executable: " << opts.output
              << " (" << (llvm ? "direct LLVM IR" : "C11") << " backend)\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { usage(); return 2; }
    try {
        const Options opts = parse_options(argc, argv);
        const auto source = read_all(opts.input);
        const auto tokens = tinycps::tokenize(source);
        const auto program = tinycps::parse(tokens);
        tinycps::check_semantics(program);

        if (opts.show_tokens) {
            for (const auto& t : tokens)
                std::cout << t.location.line << ':' << t.location.column << ' '
                          << tinycps::kind_name(t.kind) << ' ' << t.lexeme << '\n';
        }
        if (opts.show_ast) std::cout << tinycps::dump_ast(program);

        switch (opts.mode) {
            case Mode::EmitC:
                write_all(opts.output, tinycps::generate_c(program));
                std::cout << "Generated C code: " << opts.output << '\n';
                break;
            case Mode::EmitLLVM:
                write_all(opts.output, tinycps::generate_llvm_ir(program));
                std::cout << "Generated LLVM IR: " << opts.output << '\n';
                break;
            case Mode::EmitObject:
            case Mode::Executable:
                build_native(opts, program);
                break;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "TinyCPS error: " << e.what() << '\n';
        return 1;
    }
}
