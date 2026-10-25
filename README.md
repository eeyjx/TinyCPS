# TinyCPS

TinyCPS is a small compiler project I worked on while teaching myself how compilers work. This repository is a record of what I have learned so far, and a place to keep the code and commands so I can come back to them later.

The language is deliberately small: integer arithmetic, variables, printing, and counted loops. My goal is to understand the steps from a source file to a runnable program, one piece at a time. The current version in `CMakeLists.txt` is **0.3.0**.

## What it does so far

- Reads a custom `.tcps` source file.
- Splits it into tokens and builds an abstract syntax tree (AST).
- Checks things like undeclared variables, duplicate declarations, and block scope.
- Generates either LLVM IR or C source.
- Uses an external Clang installation to make object files and executables. LLVM IR is the default backend.

The path I am learning is:

```text
.tcps source -> tokens -> AST -> semantic checks -> LLVM IR -> object file -> executable
                                             \-> C source -> object file -> executable
```

TinyCPS handles the frontend and generates the IR or C. Clang handles machine code and linking. The compiler writes LLVM IR as text, so building TinyCPS itself does not require linking the LLVM C++ libraries.

## Setup notes: Ubuntu / WSL

These are Bash commands for Ubuntu, including Ubuntu inside WSL. The current driver uses POSIX functions such as `fork` and `execvp`, so use a Linux environment for these steps rather than a Windows PowerShell or MSVC build.

Update the package list:

```bash
sudo apt update
```

If I also want to update the installed system packages:

```bash
sudo apt upgrade -y
```

Install the tools from my setup notes:

```bash
sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  clang \
  llvm \
  llvm-dev \
  python3 \
  git \
  curl \
  unzip \
  ca-certificates
```

The build needs a C++20 compiler and CMake 3.16 or newer. Python runs the tests, `cc` compiles the generated C in the tests, and Clang builds native programs. The LLVM backend is written for Clang/LLVM 17 or newer; check `clang --version`, since the default package version depends on the Ubuntu release. `llvm-dev` is part of the setup list above, but the TinyCPS build does not use its C++ libraries. Ninja is optional with the build commands below.

Clone the repository and enter it:

```bash
git clone https://github.com/eeyjx/TinyCPS.git
cd TinyCPS
```

If starting from the original ZIP instead, enter the extracted `TinyCPS_Executable_Compiler` folder. Run the remaining commands from the project root, where `CMakeLists.txt` is.

I use these to check what is in the folder:

```bash
ls -la
find src examples tests generated -maxdepth 2 -type f
```

## Build and check

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
ls -lh build/tinycps
```

Show the command-line usage:

```bash
./build/tinycps --help
```

One small quirk in this version: `--help` shows the usage because there are too few arguments, and returns exit code **2**. There is no separate help-option handler yet. Run it on its own, rather than chaining it to the next command with `&&`.

Run the existing tests:

```bash
ctest --test-dir build --output-on-failure
```

With Python available when CMake configures the project, CTest registers two suites: `compiler_integration` and `native_backend_integration`. They check the C and LLVM paths, example output, scopes, arithmetic, and some invalid input. If CTest reports no tests, check the Python detection during configuration.

## Compile and run the first program

There is a `general.tcps` file in the project root, so these are the same commands as in my original notes:

```bash
./build/tinycps general.tcps -o general
./general
```

Expected program output:

```text
3
6
9
12
22
12
```

The same example also lives in `examples/general.tcps`. It uses `let` to declare variables, `set` to update them, and `repeat` to run a block several times. `print(...)` writes one integer per line. Other examples are `hello.tcps`, `energy.tcps`, and `nested.tcps` in `examples/`.

## Commands I want to keep handy

Look at the generated LLVM IR or C without building an executable:

```bash
./build/tinycps examples/general.tcps --emit-llvm build/general.ll
./build/tinycps examples/general.tcps --emit-c build/general.c
```

Inspect tokens and the AST while generating C:

```bash
./build/tinycps examples/general.tcps --emit-c build/general.c --tokens
./build/tinycps examples/general.tcps --emit-c build/general.c --ast
```

Try the C backend, or keep the intermediate LLVM IR and object file:

```bash
./build/tinycps examples/general.tcps -o build/general_c --backend c
./build/general_c
./build/tinycps examples/general.tcps -o build/general_llvm --keep-temps -O2
./build/general_llvm
```

The second build also leaves `build/general_llvm.ll` and `build/general_llvm.o` for inspection.

Build an object file and link it separately:

```bash
./build/tinycps examples/general.tcps --emit-obj build/general.o
clang build/general.o -o build/general_linked
./build/general_linked
```

If Clang is not on `PATH`, choose an installed copy with `--clang /path/to/clang`. For repeated runs and the test suites, set `TINYCPS_CLANG` to its path before running the commands. The older source-to-C form still works too:

```bash
./build/tinycps examples/general.tcps build/general.c
```

## Where to look when I come back

| File or folder | What I use it for |
| --- | --- |
| `src/compiler.hpp` | Token types, AST nodes, and the interfaces between stages |
| `src/lexer.cpp` | Turning source text into tokens |
| `src/parser.cpp` | Parsing statements and arithmetic with operator precedence |
| `src/semantic.cpp` | Checking names, scopes, and integer literals |
| `src/codegen.cpp` | Generating C and printing the AST |
| `src/llvm_codegen.cpp` | Generating LLVM IR directly from the AST |
| `src/main.cpp` | Command-line options and calls to Clang |
| `examples/` | Small programs to read and change |
| `generated/` | Saved C, LLVM IR, and AST examples to compare with the source |
| `tests/` | Python integration tests for both output paths |

When reviewing the code, I would start with an example, follow it through the lexer and parser, then compare the two code generators.

## Things still to learn

This is a small self-study result, with plenty left to improve. There are no functions, strings, arrays, or conditionals yet. Arithmetic overflow and division by zero do not have friendly runtime checks. The driver also needs more work for native Windows support and a proper `--help` option.

I want to keep experimenting with clearer error messages, more language features, and how LLVM optimizes the generated IR. For now, the examples and commands here are my starting point for picking the project up again.
