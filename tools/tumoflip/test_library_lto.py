"""Resident library optimization and native FAP fallback; synthetic arithmetic only."""
import ast
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Environment(dict):
    def AppendUnique(self, **values):
        for key, flags in values.items():
            items = self.setdefault(key, [])
            items.extend(flag for flag in flags if flag not in items)

    def Replace(self, **values):
        self.update(values)


def build_helper(path, name):
    tree = ast.parse((ROOT / path).read_text())
    node = next((node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == name), None)
    if node is None:
        raise AssertionError(f"Missing {name}: library/FAP optimization boundary is not implemented")
    scope = {}
    exec(compile(ast.Module(body=[node], type_ignores=[]), path, "exec"), scope)
    return scope[name]


class LibraryLtoTests(unittest.TestCase):
    def test_freertos_exception_port_preserves_native_assembly_abi(self):
        configure = build_helper("lib/freertos.scons", "configure_freertos_port_native")
        native_source = build_helper("lib/freertos.scons", "native_freertos_port_source")
        original = ["-Os", "-flto", "-ffat-lto-objects"]
        env = Environment(CCFLAGS=original)
        configure(env)
        configure(env)
        self.assertEqual(original, ["-Os", "-flto", "-ffat-lto-objects"])
        self.assertEqual(env["CCFLAGS"], ["-Os", "-fno-lto"])
        self.assertTrue(native_source("FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c"))
        self.assertTrue(native_source("lib/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c"))
        self.assertTrue(native_source("lib\\FreeRTOS-Kernel\\portable\\GCC\\ARM_CM4F\\port.c"))
        for source in ("FreeRTOS-Kernel/tasks.c", "drivers/port.c", "FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c"):
            self.assertFalse(native_source(source))

    def test_startup_vector_table_is_native_and_preserved(self):
        configure = build_helper("targets/SConscript", "configure_startup_native")
        native_source = build_helper("targets/SConscript", "native_startup_source")
        env=Environment(CCFLAGS=["-Os","-flto","-ffat-lto-objects"])
        configure(env)
        self.assertEqual(env["CCFLAGS"],["-Os","-fno-lto"])
        self.assertTrue(native_source("targets/f7/src/stm32wb55_startup.c"))
        self.assertFalse(native_source("targets/f7/furi_hal/furi_hal.c"))

    def test_crash_register_capture_stays_native_and_out_of_lto(self):
        configure = build_helper("furi/SConscript", "configure_crash_capture_native")
        original = ["-Os", "-flto", "-ffat-lto-objects"]
        env = Environment(CCFLAGS=original)
        configure(env)
        self.assertEqual(original, ["-Os", "-flto", "-ffat-lto-objects"])
        self.assertEqual(env["CCFLAGS"], ["-Os", "-fno-lto"])
        native_source = build_helper("furi/SConscript", "native_core_abi_source")
        self.assertTrue(native_source("check.c"))
        self.assertTrue(native_source("memmgr.c"))
        self.assertFalse(native_source("system_policy.c"))

    def test_resident_libraries_keep_native_fallback_and_updater_is_unchanged(self):
        configure = build_helper("firmware.scons", "configure_resident_library_lto")
        for resident, enabled in ((True, True), (False, True), (True, False)):
            env = Environment(IS_BASE_FIRMWARE=resident, LTO=enabled, CCFLAGS=["-Os"])
            configure(env); configure(env)
            self.assertEqual(env["CCFLAGS"], ["-Os", "-flto", "-ffat-lto-objects"] if resident and enabled else ["-Os"])

    def test_fap_forces_native_symbols_without_changing_source_environment(self):
        configure = build_helper("site_scons/extapps.scons", "configure_native_fap_lto")
        original = ["-Os", "-flto", "-ffat-lto-objects"]
        env = Environment(CCFLAGS=original, CXXFLAGS=["-flto"], LINKFLAGS=["-Ur", "-flto"])
        configure(env); configure(env)
        self.assertEqual(original, ["-Os", "-flto", "-ffat-lto-objects"])
        self.assertEqual(env["CCFLAGS"], ["-Os", "-fno-lto"])
        self.assertEqual(env["CXXFLAGS"], ["-fno-lto"])
        self.assertEqual(env["LINKFLAGS"], ["-Ur", "-fno-lto"])

    def test_actual_arm_archive_links_as_both_resident_lto_and_native_plugin(self):
        configure = build_helper("site_scons/extapps.scons", "configure_native_fap_lto")
        candidates = [ROOT / "toolchain/current/bin", ROOT / "toolchain/x86_64-linux/bin", ROOT / "toolchain/arm64-darwin/bin"]
        bindir = next((path for path in candidates if (path / "arm-none-eabi-gcc").exists()), None)
        self.assertIsNotNone(bindir, "The bundled ARM toolchain must be installed before this build test")
        with tempfile.TemporaryDirectory(prefix="library-lto-") as directory:
            work = Path(directory)
            (work / "library.c").write_text("int safe_configuration_value(int n){return n+3;}\n")
            (work / "probe.c").write_text("extern int safe_configuration_value(int);\nint configuration_probe(void){return safe_configuration_value(7);}\n")
            gcc, ar, nm, objdump = [str(bindir / f"arm-none-eabi-{name}") for name in ("gcc", "ar", "nm", "objdump")]
            common = ["-mcpu=cortex-m4", "-mthumb", "-Os", "-ffunction-sections"]
            subprocess.run([gcc, *common, "-flto", "-ffat-lto-objects", "-c", str(work / "library.c"), "-o", str(work / "library.o")], check=True, capture_output=True)
            sections = subprocess.check_output([objdump, "-h", str(work / "library.o")], text=True)
            self.assertIn(".gnu.lto_", sections)
            self.assertIn(".text.safe_configuration_value", sections)
            subprocess.run([ar, "rcs", str(work / "library.a"), str(work / "library.o")], check=True, capture_output=True)
            subprocess.run([gcc, *common, "-flto", "-nostdlib", "-Wl,-e,configuration_probe", str(work / "probe.c"), str(work / "library.a"), "-o", str(work / "resident.elf")], check=True, capture_output=True)
            env = Environment(CCFLAGS=["-flto", "-ffat-lto-objects"], CXXFLAGS=[], LINKFLAGS=["-r"])
            configure(env)
            subprocess.run([gcc, *common, *env["CCFLAGS"], "-c", str(work / "probe.c"), "-o", str(work / "probe.o")], check=True, capture_output=True)
            subprocess.run([gcc, *common, *env["LINKFLAGS"], "-nostdlib", str(work / "probe.o"), str(work / "library.a"), "-o", str(work / "native.elf")], check=True, capture_output=True)
            symbols = subprocess.check_output([nm, "--defined-only", str(work / "native.elf")], text=True)
            self.assertIn(" T safe_configuration_value", symbols)
            self.assertIn(" T configuration_probe", symbols)


if __name__ == "__main__":
    unittest.main()
