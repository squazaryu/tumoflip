"""Hermetic LLVM tool discovery; never installs or invokes a compiler."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.tumoflip import measure_upstream_1435_tests as coverage


class LlvmCoverageToolTests(unittest.TestCase):
    def test_unversioned_path_tool_remains_supported(self):
        with patch.object(coverage.shutil, "which", return_value="/bin/llvm-cov"), \
                patch.object(coverage.subprocess, "check_output") as execute:
            self.assertEqual(coverage.llvm_tool("llvm-cov"), "/bin/llvm-cov")
            execute.assert_not_called()

    def test_linux_resolves_tools_beside_the_actual_versioned_compiler(self):
        with tempfile.TemporaryDirectory(prefix="tumoflip-llvm-discovery-") as directory:
            root = Path(directory).resolve()
            compiler_bin = root / "lib/llvm-22/bin"
            compiler_bin.mkdir(parents=True)
            for name in ("clang", "llvm-profdata", "llvm-cov"):
                tool = compiler_bin / name
                tool.touch()
                tool.chmod(0o700)
            alias = root / "bin"
            alias.mkdir()
            (alias / "clang").symlink_to(compiler_bin / "clang")
            with patch.object(coverage.shutil, "which", side_effect=lambda name:
                              str(alias / "clang") if name == "clang" else None), \
                    patch("sys.platform", "linux"), \
                    patch.object(coverage.subprocess, "check_output",
                                 side_effect=AssertionError("Linux must not invoke xcrun")):
                for name in ("llvm-profdata", "llvm-cov"):
                    self.assertEqual(coverage.llvm_tool(name), str(compiler_bin / name))
                # A non-executable sibling is not an available tool.
                (compiler_bin / "llvm-cov").chmod(0o600)
                with self.assertRaisesRegex(FileNotFoundError, "LLVM.*clang"):
                    coverage.llvm_tool("llvm-cov")

    def test_missing_linux_tools_fail_closed_without_xcode_fallback(self):
        with patch.object(coverage.shutil, "which", return_value=None), \
                patch("sys.platform", "linux"), \
                patch.object(coverage.subprocess, "check_output",
                             side_effect=AssertionError("Linux must not invoke xcrun")):
            with self.assertRaisesRegex(FileNotFoundError, "LLVM.*clang"):
                coverage.llvm_tool("llvm-profdata")

    def test_macos_keeps_xcode_selection(self):
        selected = "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/llvm-cov"
        with patch.object(coverage.shutil, "which", return_value=None), \
                patch("sys.platform", "darwin"), \
                patch.object(coverage.subprocess, "check_output", return_value=selected + "\n") as execute:
            self.assertEqual(coverage.llvm_tool("llvm-cov"), selected)
            execute.assert_called_once_with(["xcrun", "--find", "llvm-cov"], text=True)


if __name__ == "__main__":
    unittest.main()
