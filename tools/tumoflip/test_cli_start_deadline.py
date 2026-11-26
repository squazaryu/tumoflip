"""CLI-start regressions with fake time/ports only. Never enumerate hardware."""

import ast
from collections import deque
import importlib.util
import logging
from pathlib import Path
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]


def deny_port(*args, **kwargs):
    raise AssertionError("A real serial port must never be used by this test")


def storage_module():
    serial = ModuleType("serial")
    serial.Serial = deny_port
    spec = importlib.util.spec_from_file_location(
        "deadline_storage_fixture", ROOT / "scripts/flipper/storage.py"
    )
    module = importlib.util.module_from_spec(spec)
    with patch.dict("sys.modules", {"serial": serial}):
        spec.loader.exec_module(module)
    return module


class Clock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        assert 0 <= seconds <= 10
        self.now += seconds


class Port:
    def __init__(self, clock, chunks=()):
        self.clock = clock
        self.chunks = deque((at, bytearray(data)) for at, data in chunks)
        self.timeout = 2.0
        self.write_timeout = 60.0
        self.opened = False
        self.writes = []
        self.write_timeouts = []
        self.read_count = 0
        self.resets = 0

    @property
    def in_waiting(self):
        return len(self.chunks[0][1]) if self.chunks and self.chunks[0][0] <= self.clock.now else 0

    def open(self):
        self.opened = True

    def close(self):
        self.opened = False

    def reset_input_buffer(self):
        self.resets += 1

    def read(self, count):
        self.read_count += 1
        if self.read_count > 100:
            raise AssertionError("CLI-start performed an unbounded retry/read loop")
        wait = self.chunks[0][0] - self.clock.now if self.chunks else float("inf")
        if wait > 0:
            if self.timeout is None:
                raise AssertionError("Test read became unbounded")
            self.clock.sleep(min(wait, self.timeout))
        if not self.chunks or self.chunks[0][0] > self.clock.now:
            return b""
        _, data = self.chunks[0]
        result = bytes(data[:count])
        del data[:count]
        if not data:
            self.chunks.popleft()
        return result

    def write(self, data):
        self.writes.append(data)
        self.write_timeouts.append(self.write_timeout)
        return len(data)


def testops_main(clock, factory, resolve):
    tree = ast.parse((ROOT / "scripts/testops.py").read_text())
    original = next(item for item in tree.body if isinstance(item, ast.ClassDef) and item.name == "Main")
    members = [
        item for item in original.body
        if (isinstance(item, ast.FunctionDef) and item.name in {"_get_flipper", "await_flipper"})
        or (isinstance(item, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == "START_TIMEOUT"
            for target in item.targets))
    ]
    harness = ast.ClassDef(name="Harness", bases=[], keywords=[], body=members, decorator_list=[])
    code = ast.fix_missing_locations(ast.Module(body=[harness], type_ignores=[]))
    namespace = {
        "time": clock, "FlipperStorage": factory, "resolve_port": resolve,
        "FlipperStorageException": storage_module().FlipperStorageException,
        "Optional": __import__("typing").Optional,
    }
    exec(compile(code, "isolated_testops_wait", "exec"), namespace)
    app = namespace["Harness"]()
    app.args = SimpleNamespace(port="fixture-port", timeout=3)
    app.logger = logging.getLogger("deadline-tests")
    return app, namespace["FlipperStorageException"]


class CLIStartDeadlineTests(unittest.TestCase):
    def setUp(self):
        self.storage = storage_module()
        self.clock = Clock()
        self.storage.time = self.clock

    def flipper(self, chunks=()):
        port = Port(self.clock, chunks)
        self.storage.serial.Serial = lambda: port
        return self.storage.FlipperStorage("fixture-port"), port

    def test_default_start_still_accepts_the_existing_three_phase_handshake(self):
        flipper, port = self.flipper([(0, b">: "), (0, b"hardware_model: fixture\r\n>: ")])
        flipper.start()
        self.assertTrue(port.opened)
        self.assertEqual(port.writes, [b"device_info\r"])
        self.assertEqual(port.timeout, 2)
        self.assertEqual(port.write_timeout, 60)

    def test_silent_start_stops_at_the_total_deadline(self):
        flipper, port = self.flipper()
        with self.assertRaises(self.storage.FlipperStorageException):
            flipper.start(timeout=1.25)
        self.assertAlmostEqual(self.clock.now, 1.25)
        self.assertEqual(port.timeout, 2)
        self.assertEqual(port.write_timeout, 60)

    def test_all_three_reads_share_one_budget(self):
        flipper, port = self.flipper([
            (0.6, b">: "), (1.2, b"hardware_model"), (1.7, b"\r\n>: "),
        ])
        with self.assertRaises(self.storage.FlipperStorageException):
            flipper.start(timeout=1.5)
        self.assertAlmostEqual(self.clock.now, 1.5)
        self.assertLessEqual(port.write_timeouts[0], 0.9 + 1e-9)
        self.assertEqual(port.write_timeout, 60)

    def test_continuous_non_prompt_bytes_cannot_extend_the_budget(self):
        flipper, port = self.flipper([(0.55 + i * 0.05, b"x") for i in range(60)])
        with self.assertRaises(self.storage.FlipperStorageException):
            flipper.start(timeout=1.0)
        self.assertAlmostEqual(self.clock.now, 1.0)
        self.assertLess(port.read_count, 25)

    def test_successful_bounded_handshake_restores_transport_timeouts(self):
        flipper, port = self.flipper([(0.6, b">: "), (0.8, b"hardware_model\r\n>: ")])
        flipper.start(timeout=2)
        self.assertTrue(port.opened)
        self.assertLess(self.clock.now, 2)
        self.assertEqual(port.timeout, 2)
        self.assertEqual(port.write_timeout, 60)

    def test_invalid_start_timeout_does_not_open_the_port(self):
        for budget in (0, -1, float("nan"), float("inf")):
            with self.subTest(budget=budget):
                flipper, port = self.flipper()
                with self.assertRaises(ValueError):
                    flipper.start(timeout=budget)
                self.assertFalse(port.opened)

    def test_legacy_idle_timeout_still_extends_on_progress(self):
        port = Port(self.clock, [(0.05, b"a"), (0.10, b"b"), (0.15, b">")])
        port.timeout = 0.05
        reader = self.storage.BufferedRead(port)
        self.assertEqual(reader.until(">", timeout=0.075), b"ab")
        self.assertAlmostEqual(self.clock.now, 0.15)

    def test_missing_port_waits_only_the_requested_seconds(self):
        app, _ = testops_main(self.clock, deny_port, lambda *_: None)
        self.assertIsNone(app._get_flipper(timeout=3))
        self.assertAlmostEqual(self.clock.now, 3)

    def test_retries_start_with_a_fresh_port_and_close_each_failure(self):
        created = []
        app, failure = testops_main(self.clock, lambda *_: None, lambda *_: "fixture-port")

        class Connection:
            def __init__(inner, _):
                inner.closed = False
                inner.budget = None
                created.append(inner)

            def start(inner, timeout):
                inner.budget = timeout
                self.clock.sleep(0.2)
                if len(created) == 1:
                    raise failure("silent fixture")

            def stop(inner):
                inner.closed = True

        app._get_flipper.__globals__["FlipperStorage"] = Connection
        result = app._get_flipper(timeout=3)
        self.assertEqual(len(created), 2)
        self.assertTrue(created[0].closed)
        self.assertIs(result, created[1])
        self.assertFalse(created[1].closed)
        self.assertLessEqual(created[1].budget, 2.8)

    def test_one_silent_attempt_cannot_exceed_the_outer_budget(self):
        closed = []
        budgets = []
        app, failure = testops_main(self.clock, lambda *_: None, lambda *_: "fixture-port")

        class Connection:
            def __init__(inner, _):
                pass

            def start(inner, timeout):
                budgets.append(timeout)
                self.clock.sleep(timeout)
                raise failure("timeout fixture")

            def stop(inner):
                closed.append(True)

        app._get_flipper.__globals__["FlipperStorage"] = Connection
        self.assertIsNone(app._get_flipper(timeout=2))
        self.assertEqual(budgets, [2])
        self.assertEqual(len(closed), 1)
        self.assertAlmostEqual(self.clock.now, 2)

    def test_zero_wait_budget_does_not_resolve_or_open_a_port(self):
        app, _ = testops_main(self.clock, deny_port, deny_port)
        self.assertIsNone(app._get_flipper(timeout=0))
        self.assertEqual(self.clock.now, 0)


if __name__ == "__main__":
    unittest.main()
