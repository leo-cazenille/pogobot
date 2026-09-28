#!/usr/bin/env python3
"""Compile the real IR receiver sources against a small simulated host device."""

from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ir_upload_v2 import image_frames, CMD_START, CMD_DATA, CMD_END


TEST_DIR = Path(__file__).resolve().parent
STUB_HEADERS = (
    "system.h",
    "irq.h",
    "generated/mem.h",
    "generated/csr.h",
    "generated/soc.h",
    "sfl.h",
    "pogobot.h",
    "pogobot_ir.h",
    "libbase/crc.h",
    "libbase/spiflash.h",
)


def main():
    with tempfile.TemporaryDirectory(prefix="pogobot-ir-test-") as temp:
        temp_dir = Path(temp)
        # The C harness supplies hardware symbols; empty headers keep the
        # production source unchanged while excluding target-only headers.
        for name in STUB_HEADERS:
            header = temp_dir / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text("#pragma once\n")

        executable = temp_dir / "ir_upload_fault_test"
        # Feed actual Python uploader payloads through the C receiver harness.
        image = bytes((i // 64 * 17 + i % 64 * 3 + 1) & 0xff for i in range(65))
        frames = list(image_frames(image, 0x260000, 0x12345678))
        assert [command for command, _ in frames] == [CMD_START, CMD_DATA, CMD_DATA, CMD_END]
        assert len(frames[2][1]) == 7  # ID, index, and one-byte final chunk.
        for invalid_image, invalid_address in ((b"", 0x260000),
                                               (image, 0x220000),
                                               (bytes(0x20001), 0x260000)):
            try:
                list(image_frames(invalid_image, invalid_address, 1))
            except ValueError:
                pass
            else:
                raise AssertionError("uploader accepted an invalid image or destination")
        fixture = temp_dir / "python_wire_fixture.bin"
        with fixture.open("wb") as output:
            for command, payload in frames:
                output.write(command + bytes([len(payload)]) + payload)
        subprocess.run(
            [
                "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-I", str(temp_dir),
                "-I", str(TEST_DIR.parent / "pogobios"),
                "-I", str(TEST_DIR.parent / "pogolib"),
                str(TEST_DIR / "ir_upload_fault_test.c"),
                "-o", str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable), str(fixture)], check=True)


if __name__ == "__main__":
    main()
