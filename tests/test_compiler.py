"""Integration tests for the general-purpose TinyCPS learning compiler.

Each successful case checks behavior across all stages: TinyCPS source ->
compiler-generated C -> host C compiler -> native executable -> stdout.
Negative cases check that invalid programs produce useful diagnostics.
"""

import os
import subprocess
import sys
import tempfile
from pathlib import Path


def check(command, *, expected_return=0):
    """Run a process and fail with its stderr/stdout if the status differs."""
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != expected_return:
        raise AssertionError(
            f"Command {command!r} returned {result.returncode}, expected {expected_return}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return result


def run():
    compiler = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve()
    gcc = os.environ.get("CC", "cc")
    # Original example programs are regression fixtures, not performance tests.
    cases = {
        "hello": "30\n",
        "general": "3\n6\n9\n12\n22\n12\n",
        "energy": "7\n14\n21\n28\n34\n",
        "nested": "6\n",
    }
    with tempfile.TemporaryDirectory(prefix="tinycps_test_") as work:
        work = Path(work)
        for sample, output in cases.items():
            source = root / "examples" / (sample + ".tcps")
            generated = work / (sample + ".c")
            exe = work / sample
            check([compiler, source, generated])
            check([gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(generated), "-o", str(exe)])
            actual = check([exe]).stdout
            assert actual == output, f"{sample}: {actual!r} != {output!r}"
            print(f"PASS: {sample} -> C -> executable: {actual.strip()!r}")

        def case(label, source, output=None, error=None):
            """Check either successful code generation or expected rejection."""
            input_file = work / "test.tcps"
            output_file = work / "test.c"
            native_file = work / "test"
            input_file.write_text(source, encoding="utf8")
            if output_file.exists():
                output_file.unlink()
            compile_result = check([compiler, input_file, output_file], expected_return=1 if error else 0)
            if error:
                assert error in compile_result.stderr, (label, compile_result.stderr)
                assert not output_file.exists(), (label, "output created despite compiler error")
            else:
                check([gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(output_file), "-o", str(native_file)])
                got = check([native_file]).stdout
                assert got == output, (label, got, output)
            print("PASS:", label)

        case("operator precedence", "print(2 + 3 * 4); print((2 + 3) * 4);\n", "14\n20\n")
        case("negative, division and assignment", "let x = -10; set x = x / 2; print(x);\n", "-5\n")
        case("zero and negative repeat", "repeat(0) { print(100); } repeat(-2) { print(200); } print(9);", "9\n")
        case("count evaluated once", "let x = 3; repeat(x) { set x = x - 1; } print(x);", "0\n")
        case("shadowing and lexical scopes", "let x = 1; repeat(1) { let x = 9; print(x); } print(x);", "9\n1\n")
        case("undefined name", "print(missing);", error="undeclared variable")
        case("use before declaration", "let x = y; let y = 1;", error="undeclared variable")
        case("duplicate declaration", "let x = 1; let x = 2;", error="duplicate declaration")
        case("scope escape", "repeat(1) { let temp = 42; } print(temp);", error="undeclared variable")
        case("undeclared assignment", "set y = 3;", error="assignment to undeclared variable")
        case("missing semicolon", "let x = 1", error="expected ';'")
        case("unexpected character", "print(2 @ 3);", error="unexpected character")
        case("integer literal overflow", "print(9999999999999999999999999999);", error="outside signed 64-bit")
        case("unclosed repeat block", "repeat(2) { print(3);", error="expected '}'")
        print("ALL TESTS PASSED")


if __name__ == "__main__":
    run()
