from __future__ import annotations

from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python"))

from lobx.mesa_realtime_server import parse_run_params  # noqa: E402


class RealtimeCppEndpointParamsTest(unittest.TestCase):
    def test_events_default_engine_is_cpp(self) -> None:
        params = parse_run_params("")

        self.assertEqual(params["engine"], "cpp")
        self.assertEqual(params["seed"], 42)

    def test_events_explicit_cpp_engine_is_allowed(self) -> None:
        params = parse_run_params("engine=cpp&steps=100&sleep_ms=0")

        self.assertEqual(params["engine"], "cpp")
        self.assertEqual(params["steps"], 100)
        self.assertEqual(params["sleep_ms"], 0)

    def test_events_python_engine_is_disabled(self) -> None:
        with self.assertRaisesRegex(ValueError, "only engine=cpp"):
            parse_run_params("engine=python")

    def test_events_invalid_engine_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "only engine=cpp"):
            parse_run_params("engine=fast")

    def test_events_minimums_are_clamped(self) -> None:
        params = parse_run_params("engine=cpp&steps=-1&sleep_ms=-2&reference_price=0&makers=-3")

        self.assertEqual(params["steps"], 0)
        self.assertEqual(params["sleep_ms"], 0)
        self.assertEqual(params["reference_price"], 1)
        self.assertEqual(params["makers"], 0)


if __name__ == "__main__":
    unittest.main()
