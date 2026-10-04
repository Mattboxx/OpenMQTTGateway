"""Verify pinned-source patch idempotence and refusal of unknown source."""

import sys
import re
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from rtl433_patch import PATCHES, apply_patch, digest, original_from_patched, restore_known_source, checked_receiver_v1, compact_decoder_v1


class PatchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="omg-rtl433-test-")
        self.library = Path(self.temp.name)
        self.sources = {}
        cached = ROOT / ".pio/libdeps/esp32dev-multi_receiver-wol-gpio-ble/rtl_433_ESP"
        for relative, expected, transform in PATCHES:
            source = (cached / relative).read_text(encoding="utf-8")
            if digest(source) != expected:
                source = restore_known_source(source, relative, expected, transform)
            self.assertEqual(digest(source), expected)
            self.sources[relative] = source
            path = self.library / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def test_idempotent_and_exactly_reversible(self):
        self.assertEqual(len(apply_patch(self.library)), 4)
        self.assertEqual(apply_patch(self.library), [])
        for relative, _, transform in PATCHES:
            patched = (self.library / relative).read_text(encoding="utf-8")
            self.assertEqual(original_from_patched(patched, transform), self.sources[relative])

    def test_validate_all_before_any_write(self):
        last = self.library / PATCHES[-1][0]
        last.write_text(last.read_text(encoding="utf-8") + "\n// local change\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            apply_patch(self.library)
        first = self.library / PATCHES[0][0]
        self.assertEqual(first.read_text(encoding="utf-8"), self.sources[PATCHES[0][0]])

    def test_modified_patched_file_is_not_overwritten(self):
        apply_patch(self.library)
        path = self.library / PATCHES[0][0]
        source = path.read_text(encoding="utf-8").replace("int signalRssi;", "short signalRssi;")
        path.write_text(source, encoding="utf-8")
        with self.assertRaises(ValueError):
            apply_patch(self.library)
        self.assertEqual(path.read_text(encoding="utf-8"), source)

    def test_known_previous_patch_upgrades_without_unknown_overwrite(self):
        relative = "src/rtl_433_ESP.cpp"
        (self.library / relative).write_text(checked_receiver_v1(self.sources[relative]), encoding="utf-8")
        self.assertIn(relative, apply_patch(self.library))
        self.assertEqual(apply_patch(self.library), [])
        self.assertIn("rfSignalCopyHasHeadroom", (self.library / relative).read_text(encoding="utf-8"))

    def test_previous_decoder_patch_migrates_without_losing_metadata(self):
        relative = "src/signalDecoder.cpp"
        (self.library / relative).write_text(compact_decoder_v1(self.sources[relative]), encoding="utf-8")
        self.assertIn(relative, apply_patch(self.library))
        result = (self.library / relative).read_text(encoding="utf-8")
        self.assertIn("omg_rtl433_set_metadata", result)
        self.assertIn("releaseRFTemplateTable", result)
        self.assertLess(result.index("register_protocol(cfg"), result.index("releaseRFTemplateTable"))
        self.assertEqual(apply_patch(self.library), [])

    def test_all_standard_protocol_templates_are_preserved(self):
        relative = "src/signalDecoder.cpp"
        original = self.sources[relative]
        apply_patch(self.library)
        patched = (self.library / relative).read_text(encoding="utf-8")
        copies = r"memcpy\(&cfg->devices\[(\d+)\], &(\w+), sizeof\(r_device\)\);"
        self.assertEqual(re.findall(copies, original), re.findall(copies, patched))
        standard = patched[:patched.index("\n#else\n    memcpy(&cfg->devices[0], &lacrosse_tx141x")]
        branches = [[]]
        for index, _ in re.findall(copies, standard):
            index = int(index)
            if index == 0 and branches[-1]:
                branches.append([])
            branches[-1].append(index)
        self.assertEqual(branches, [list(range(157)), list(range(80))])

    def test_unknown_generated_helper_is_preserved(self):
        helper = self.library / "include/RFSignalMemoryBudget.h"
        helper.write_text("// unknown user change\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            apply_patch(self.library)
        self.assertEqual(helper.read_text(encoding="utf-8"), "// unknown user change\n")
        self.assertEqual((self.library / PATCHES[0][0]).read_text(encoding="utf-8"), self.sources[PATCHES[0][0]])

    def test_null_copy_guard_precedes_every_use(self):
        apply_patch(self.library)
        receiver = (self.library / "src/rtl_433_ESP.cpp").read_text(encoding="utf-8")
        copy = receiver[receiver.index("const uint32_t pulseMemoryCaps ="):]
        guard = copy.index("if (!rtl_pulses)")
        guard_end = copy.index("memcpy(rtl_pulses")
        self.assertLess(guard, guard_end)
        self.assertIn("num_pulses = 0;", copy[guard:guard_end])
        self.assertIn("ignoredSignals++;", copy[guard:guard_end])
        self.assertIn("return;", copy[guard:guard_end])
        self.assertIn("MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT", copy[:guard])
        for function in ("initReceiver", "enableReceiver", "resetReceiver"):
            body = receiver[receiver.index("void rtl_433_ESP::" + function):]
            self.assertIn("if (!_pulseTrains)", body[:230])


if __name__ == "__main__":
    unittest.main()
