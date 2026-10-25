"""End-to-end tests of direct LLVM IR and C source backends.

The native mode is NOT a C re-parser: .tcps -> AST -> LLVM IR -> object -> exe.
The external clang tool is used for machine-code generation and linking.
"""

import os
import subprocess
import sys
import tempfile
from pathlib import Path


def run(*args, expected=0):
    p = subprocess.run([str(x) for x in args], text=True, capture_output=True)
    assert p.returncode == expected, (
        f"expected {expected}, got {p.returncode}\nargs={args}\n"
        f"stdout={p.stdout}\nstderr={p.stderr}"
    )
    return p


def main():
    compiler = Path(sys.argv[1]).resolve()
    project = Path(sys.argv[2]).resolve()
    clang = os.environ.get("TINYCPS_CLANG", "clang")

    examples = {
        "hello": "30\n",
        "general": "3\n6\n9\n12\n22\n12\n",
        "energy": "7\n14\n21\n28\n34\n",
        "nested": "6\n",
    }
    # Full source-to-executable tests on both independently generated forms.
    with tempfile.TemporaryDirectory(prefix="tinycps_native_test_") as temp:
        root = Path(temp)
        for case, expected in examples.items():
            source = project / "examples" / (case + ".tcps")
            for backend in ("llvm", "c"):
                exe = root / f"{case}_{backend}"
                run(compiler, source, "-o", exe, "--backend", backend,
                    "--clang", clang)
                assert run(exe).stdout == expected
                print("PASS:", case, backend, "native")

        custom = {
            "precedence": ("print(2 + 3 * 4); print((2 + 3) * 4);", "14\n20\n"),
            "negative_division": ("let x = -11; set x = x / 2; print(x);", "-5\n"),
            "zero_and_negative_repeat": (
                "repeat(0){print(3);} repeat(-1){print(5);} print(7);", "7\n"),
            "count_captured_once": (
                "let n = 3; repeat(n){set n = n-1;print(n);} print(n);",
                "2\n1\n0\n0\n"),
            "scope_shadow": (
                "let x = 4; repeat(2){let x = 9;print(x);set x=x+1;print(x);}print(x);",
                "9\n10\n9\n10\n4\n"),
            "repeat_count_uses_outer": (
                "let x=3;repeat(x){let x=2;repeat(x){print(8);}}",
                "8\n8\n8\n8\n8\n8\n"),
            "nested_and_zero_loop": (
                "let s=0;repeat(3){repeat(2){set s=s+2;}repeat(0){print(999);}}print(s);",
                "12\n"),
            "empty_program": ("// Nothing to execute\n", ""),
            "empty_loop": ("repeat(4){}print(8);", "8\n"),
            "scoped_self_reference": (
                "let x=3;repeat(1){let x=x+8;print(x);}print(x);", "11\n3\n"),
        }
        for label, (text, expected) in custom.items():
            input_path = root / f"{label}.tcps"
            input_path.write_text(text, encoding="utf-8")
            outputs = []
            for backend in ("llvm", "c"):
                exe = root / f"{label}_{backend}"
                run(compiler, input_path, "-o", exe, "--backend", backend)
                outputs.append(run(exe).stdout)
            assert outputs == [expected, expected], (label, outputs, expected)
            print("PASS:", label, "backends equivalent")

        source = project / "examples" / "general.tcps"
        ir = root / "general.ll"
        native_o = root / "general.o"
        exe = root / "my general program"
        run(compiler, source, "--emit-llvm", ir)
        ir_text = ir.read_text()
        for substring in ("define i32 @main()", "alloca i64", "load i64",
                          "store i64", "icmp slt i64", "@printf", "repeat.0.header"):
            assert substring in ir_text, ("LLVM content missing", substring)
        run(compiler, source, "--emit-obj", native_o)
        run(clang, native_o, "-o", exe)
        assert run(exe).stdout == examples["general"]
        run(compiler, source, "-o", exe, "--keep-temps", "-O2")
        assert run(exe).stdout == examples["general"]
        assert Path(str(exe) + ".ll").is_file()
        assert Path(str(exe) + ".o").is_file()
        run(compiler, source, "--emit-c", root / "legacy.c", "--ast")
        assert "Program" in run(compiler, source, "--emit-c", root / "legacy2.c", "--ast").stdout
        run(compiler, source, root / "old_style.c", "--tokens")
        print("PASS: LLVM source/object emission, -O2, keep temps, spaces in paths, legacy CLI")

        failures = [
            ("print(unknown);", "undeclared variable"),
            ("set unknown = 3;", "assignment to undeclared variable"),
            ("let a=2;let a=3;", "duplicate declaration"),
            ("print(3 @ 4);", "unexpected character"),
            ("repeat(1){let a=3;}print(a);", "undeclared variable"),
        ]
        inp = root / "bad.tcps"
        for text, diagnostic in failures:
            inp.write_text(text)
            fail_output = root / "do_not_create"
            if fail_output.exists(): fail_output.unlink()
            result = run(compiler, inp, "-o", fail_output, expected=1)
            assert diagnostic in result.stderr, (text, result.stderr)
            assert not fail_output.exists()
        missing_clang = run(compiler, source, "-o", root / "no-compiler",
                            "--clang", "/nonexistent/tinycps-clang", expected=1)
        assert "object-code generation failed" in missing_clang.stderr
        assert not (root / "no-compiler").exists()
        print("PASS: semantic/syntax errors and external compiler failure")

    print("ALL NATIVE BACKEND TESTS PASSED")


if __name__ == "__main__":
    main()
