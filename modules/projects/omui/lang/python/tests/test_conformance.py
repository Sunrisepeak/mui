"""CTS level S (M3 acceptance ③): every scenario of spec/conformance/S, run
by the Python runner (cts.py), records the trace beside it, as the Lua and
JavaScript runners must. OMUI_CTS_WRITE=1 writes the traces instead."""

import glob
import json
import os
import unittest

import cts
import openmui as omui


def scenarios():
    return sorted(glob.glob(os.path.join(cts.SCENARIOS, "*.scenario.omui.json")))


class Conformance(unittest.TestCase):
    def test_the_scenarios_record_their_traces(self):
        files = scenarios()
        self.assertGreaterEqual(len(files), 3, cts.SCENARIOS)
        omui.run(None, size=(320, 480), headless=True, argv=[])
        for file in files:
            name = os.path.basename(file).split(".")[0]
            with self.subTest(scenario=name):
                with open(file, encoding="utf-8") as f:
                    scenario = json.load(f)
                trace = cts.canonical(cts.Runner(scenario).trace())
                self.assertEqual(cts.expectations(scenario, trace), [])
                trace_file = os.path.join(cts.SCENARIOS, name + ".trace.omui.json")
                if os.environ.get("OMUI_CTS_WRITE") == "1":
                    with open(trace_file, "w", encoding="utf-8", newline="\n") as f:
                        json.dump({"omui": "0.1", "trace": trace}, f, ensure_ascii=False, indent=1)
                        f.write("\n")
                    continue
                with open(trace_file, encoding="utf-8") as f:
                    pinned = cts.canonical(json.load(f)["trace"])
                for got, want in zip(trace, pinned):
                    self.assertEqual(got, want, f"step {want.get('step')}")
                self.assertEqual(len(trace), len(pinned))


if __name__ == "__main__":
    unittest.main()
