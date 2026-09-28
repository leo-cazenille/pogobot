"""Exercise serial protocol selection used by unchanged example connect targets."""

from pathlib import Path
import io
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ir_upload_v2 import CAPABILITY_BANNER
from litex_term import LiteXTerm, sfl_magic_ack, sfl_magic_req


class InputPort:
    def __init__(self, term, incoming):
        self.term = term
        self.incoming = iter(incoming)
        self.writes = []

    def read(self):
        try:
            return bytes((next(self.incoming),))
        except StopIteration:
            self.term.reader_alive = False
            return b""

    def write(self, data):
        self.writes.append(data)


class OutputSink:
    def __init__(self):
        self.buffer = io.BytesIO()

    def flush(self):
        pass

    def write(self, data):
        return len(data)


class VersionedSelectionTest(unittest.TestCase):
    def run_stream(self, incoming, region_count=1, explicit=False):
        # Use the actual reader and SFL handshake, with only serial I/O and
        # flash upload actions replaced by recording stubs.
        term = LiteXTerm.__new__(LiteXTerm)
        term.mem_regions = {"firmware.bin": "0x260000"}
        if region_count == 2:
            term.mem_regions["other.bin"] = "0x240000"
        term.serial_boot = False
        term.ir_v2_requested = explicit
        term.ir_v2 = explicit
        term.ir_v2_advertised = False
        term.magic_detect_buffer = bytes(len(sfl_magic_req))
        term.ir_v2_detect_buffer = bytes(len(CAPABILITY_BANNER))
        term.reader_alive = True
        term.port = InputPort(term, incoming)
        choices = []
        term.upload = lambda filename, address: choices.append("legacy")
        term.upload_v2 = lambda filename, address: choices.append("v2")
        term.boot = lambda: None
        with patch.object(sys, "stdout", OutputSink()):
            term.reader()
        return choices, term.port.writes

    def test_remote_advertisement_uses_v2_for_make_connect(self):
        choices, writes = self.run_stream(CAPABILITY_BANNER + sfl_magic_req)
        self.assertEqual(choices, ["v2"])
        self.assertEqual(writes, [sfl_magic_ack])

    def test_direct_cable_and_old_remote_stay_legacy(self):
        choices, _ = self.run_stream(sfl_magic_req)
        self.assertEqual(choices, ["legacy"])

    def test_advertisement_is_scoped_to_one_request(self):
        choices, _ = self.run_stream(CAPABILITY_BANNER + sfl_magic_req + sfl_magic_req)
        self.assertEqual(choices, ["v2", "legacy"])

    def test_multi_image_request_stays_legacy(self):
        choices, _ = self.run_stream(CAPABILITY_BANNER + sfl_magic_req, region_count=2)
        self.assertEqual(choices, ["legacy", "legacy"])

    def test_explicit_selection_remains_available(self):
        choices, _ = self.run_stream(sfl_magic_req, explicit=True)
        self.assertEqual(choices, ["v2"])


if __name__ == "__main__":
    unittest.main()
