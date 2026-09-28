#!/usr/bin/env python3
"""Compile the real IR receiver sources against a small simulated host device."""

from pathlib import Path
import subprocess
import tempfile


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
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
