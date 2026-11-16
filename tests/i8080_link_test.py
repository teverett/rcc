from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
RCC = Path(sys.argv.pop(1) if len(sys.argv) > 1 else ROOT / "build/bin/rcc").resolve()
BUILD = ROOT / "build/i8080-link-tests"


# Regression test for a bug where `--target=8080`'s object-mode linking failed with
# "undefined symbol 'fp'" for any function with a parameter or local variable -
# I8080LinkerFactory.cpp's synthesized entry object declared the ac/bc/cx/dx/ptr scratch
# cells every 8080 object defers to it, but left out `fp`, the software frame pointer every
# function's prologue/epilogue also defers the same way (see docs/known-bugs.md's own entry,
# removed once this fix landed) - undetected until examples/8080-altair's own main.c, calling a
# function with a parameter, needed a real `--link` step for the first time on this target.
class I8080LinkTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        BUILD.mkdir(parents=True, exist_ok=True)

    def compile(self, stem, source):
        source_path = BUILD / f"{stem}.c"
        object_path = BUILD / f"{stem}.o"
        source_path.write_text(source)
        result = subprocess.run(
            [str(RCC), "--target=8080", "--format=coff", f"--out={object_path}", str(source_path)],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return object_path

    def test_cross_object_call_to_function_with_a_parameter_links_successfully(self):
        # The bug's symptom, directly: `helper`'s own parameter needs `fp` for its frame-
        # relative address, and that frame pointer is shared across every linked object.
        main_obj = self.compile("main", "int helper(int x);\nint main() {\n    return helper(1);\n}\n")
        lib_obj = self.compile("lib", "int helper(int x) {\n    return x + 41;\n}\n")
        output = BUILD / "out.raw"
        result = subprocess.run(
            [str(RCC), "--link", "--target=8080", "--format=raw", f"--out={output}", str(main_obj), str(lib_obj)],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertGreater(output.stat().st_size, 0)

    def test_cross_object_call_to_function_with_a_local_variable_links_successfully(self):
        # Same root cause, via a Local slot instead of a Param - genFunction()'s prologue
        # reads/writes `fp` unconditionally whenever a function needs frame space at all.
        main_obj = self.compile("main_local", "int helper();\nint main() {\n    return helper();\n}\n")
        lib_obj = self.compile(
            "lib_local", "int helper() {\n    int total;\n    total = 41;\n    total = total + 1;\n    return total;\n}\n"
        )
        output = BUILD / "out_local.raw"
        result = subprocess.run(
            [str(RCC), "--link", "--target=8080", "--format=raw", f"--out={output}", str(main_obj), str(lib_obj)],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertGreater(output.stat().st_size, 0)


if __name__ == "__main__":
    unittest.main()
