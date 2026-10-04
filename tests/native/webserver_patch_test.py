"""Check isolated WebServer generation against the installed pinned SDK."""

import argparse
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from webserver_patch import generate_webserver, patch_parser

parser = argparse.ArgumentParser()
parser.add_argument("--framework", type=Path, required=True)
args, unittest_args = parser.parse_known_args()


class WebServerPatchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="omg-webserver-test-")
        self.output = Path(self.temp.name) / "OMGWebServer"
        self.library = args.framework / "libraries/WebServer"
        self.helper = ROOT / "main/MultipartBoundary.h"
        self.original = (self.library / "src/Parsing.cpp").read_bytes()

    def tearDown(self):
        self.assertEqual((self.library / "src/Parsing.cpp").read_bytes(), self.original)
        self.temp.cleanup()

    def generate(self):
        return generate_webserver(self.library, self.helper, self.output)

    def test_repeatable_and_sdk_untouched(self):
        self.assertTrue(self.generate())
        self.assertEqual(self.generate(), [])
        generated = (self.output / "src/Parsing.cpp").read_text(encoding="utf-8")
        self.assertNotIn("strstr((const char*)endBuf", generated)
        self.assertNotIn("endBuf[boundary.length()]", generated)
        self.assertLess(generated.index("validMultipartBoundaryLength"),
                        generated.index("endBuf[omg::multipartBoundaryMax]"))
        self.assertIn("matchesMultipartBoundary(endBuf, i,", generated)
        self.assertIn("name=OMGWebServer", (self.output / "library.properties").read_text())

    def test_unknown_parser_is_rejected(self):
        with self.assertRaises(ValueError):
            patch_parser(self.original.decode("utf-8").replace("uint8_t endBuf", "char endBuf"))

    def test_local_edit_preserved(self):
        self.generate()
        header = self.output / "src/WebServer.h"
        edited = header.read_bytes() + b"\n// local edit\n"
        header.write_bytes(edited)
        with self.assertRaises(ValueError):
            self.generate()
        self.assertEqual(header.read_bytes(), edited)

    def test_unrecognized_copy_is_not_overwritten(self):
        self.output.mkdir()
        header = self.output / "keep.txt"
        header.write_text("preserve me", encoding="utf-8")
        with self.assertRaises(ValueError):
            self.generate()
        self.assertEqual(header.read_text(encoding="utf-8"), "preserve me")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]] + unittest_args)
