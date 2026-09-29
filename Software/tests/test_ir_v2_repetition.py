"""Check the actual terminal frame schedule for repeated IR broadcasts."""

from contextlib import redirect_stdout
from pathlib import Path
import io
import os
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import litex_term
from ir_upload_v2 import (CMD_START, CMD_DATA, CMD_END, CMD_ABORT, CMD_PARITY,
                          DEFAULT_COPIES)


class RepeatedUploadTest(unittest.TestCase):
    def test_default_needs_no_makefile_option(self):
        with patch.object(litex_term, "Console", lambda: object()), \
             patch.dict(os.environ, {}, clear=True):
            term = litex_term.LiteXTerm(True, "firmware.bin", "0x260000",
                                         None, True, False, 0, False)
        self.assertEqual(term.ir_v2_copies, DEFAULT_COPIES)

    def make_term(self, copies):
        # Construction needs a console only for interactive operation; the
        # upload itself uses the recorded SFL sender below.
        with patch.object(litex_term, "Console", lambda: object()), \
             patch.dict(os.environ, {"POGOBOT_IR_COPIES": str(copies)}):
            term = litex_term.LiteXTerm(True, "firmware.bin", "0x260000",
                                        None, True, False, 0, False)
        term.ir_fec = False  # Direct upload tests bypass the serial announcement.
        return term

    def upload(self, term, image, sender):
        with tempfile.TemporaryDirectory() as directory:
            firmware = Path(directory) / "firmware.bin"
            firmware.write_bytes(image)
            term.send_frame = sender
            with redirect_stdout(io.StringIO()) as output:
                result = term.upload_v2(str(firmware), 0x260000)
            return result, output.getvalue()

    def test_three_passes_repeat_metadata_data_and_completion(self):
        term = self.make_term(3)
        sent = []

        def record(frame):
            sent.append((frame.cmd, frame.payload))
            return True

        with patch.object(litex_term.secrets, "randbits", return_value=0x12345678):
            length, output = self.upload(term, bytes(range(129)), record)
        self.assertEqual(length, 129)
        expected = [CMD_START, CMD_DATA, CMD_DATA, CMD_DATA, CMD_END, CMD_END]
        for offset in (0, 6, 12):
            self.assertEqual([command for command, _ in sent[offset:offset + 6]], expected)
            self.assertEqual([struct.unpack(">H", payload[4:6])[0]
                              for command, payload in sent[offset:offset + 6]
                              if command == CMD_DATA], [0, 1, 2])
        self.assertEqual(sent[0][1], sent[6][1])
        self.assertEqual(sent[6][1], sent[12][1])
        for position in range(1, 6):
            self.assertEqual(sent[position][1], sent[position + 6][1])
            self.assertEqual(sent[position + 6][1], sent[position + 12][1])
        self.assertEqual(sent[4][1], sent[5][1])
        self.assertIn("Remote acknowledged pass 3/3", output)
        self.assertIn("robot completion unconfirmed", output)
        self.assertIn("Remote pass 1/3", output)
        self.assertIn("Remote pass 3/3", output)
        self.assertEqual(output.count("| 100%"), 3)

    def test_one_pass_and_failure_abort(self):
        term = self.make_term(1)
        sent = []

        def record(frame):
            sent.append(frame.cmd)
            return len(sent) != 2

        with self.assertRaises(IOError):
            self.upload(term, bytes(range(65)), record)
        self.assertEqual(sent, [CMD_START, CMD_DATA, CMD_ABORT])

    def test_parity_is_sent_in_every_pass(self):
        term = self.make_term(2)
        term.ir_fec = True
        sent = []

        def record(frame):
            sent.append((frame.cmd, frame.payload))
            return True

        self.upload(term, bytes(range(256)) * 4 + b"x", record)
        commands = [command for command, _ in sent]
        self.assertEqual(commands.count(CMD_PARITY), 16)
        self.assertEqual(commands.count(CMD_START), 2)
        self.assertEqual(commands.count(CMD_END), 4)
        self.assertTrue(all(payload[1] == 1 for command, payload in sent
                            if command == CMD_START))

    def test_copy_count_is_bounded(self):
        for invalid in ("0", "6", "two"):
            with self.assertRaisesRegex(ValueError, "POGOBOT_IR_COPIES"):
                self.make_term(invalid)


if __name__ == "__main__":
    unittest.main()
