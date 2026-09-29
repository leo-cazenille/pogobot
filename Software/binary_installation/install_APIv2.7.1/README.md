# Pogobot API v2.7.1 installer

This package targets Pogobot v3 heads with 2 MiB SPI flash and their remotes.
The initial images were built from source commit
`7f05f1edad0328e6ca6eead2ba531d09b5263a33`. Both the bootloader's
embedded Pogobios and the normal robot Pogobios image include the upload-entry
fix from `c52585136636e5bd81ce3be9197524a9c47682ba`
and the longer initial receive wait from `869c86c089f934c4fa578a1a40c845f18c51bc10`.
The SDK terminal shows progress for each remote-acknowledged pass and accepts
Q to cancel an active v2 upload. The current terminal derives transfer IDs
from the image hash, allowing another `rc_flash_robot` command to repair the
same image without erasing verified chunks while the robot remains powered.

## Contents

- `bootloader/`: v3 bootloader image shared by robots and remotes.
- `pogobios/`: normal robot gateware and Pogobios.
- `remocon/`: remote gateware and Pogobios.
- `program_robot.sh` and `program_remote.sh`: direct `iceprog` installers.
- `SHA256SUMS`: checksums for the two scripts and five binaries.

The matching SDK is packaged separately as `../sdk_APIv2.7.1.tar.gz`, with its
own `../sdk_APIv2.7.1.tar.gz.sha256` checksum file.

## Program a device

From this directory, first check the package:

```sh
sha256sum -c SHA256SUMS
```

Connect the FTDI-based iCE programmer to the intended device, then run the
corresponding script:

```sh
./program_robot.sh
# or
./program_remote.sh
```

The scripts can be launched from any working directory. `iceprog -o` uses
physical flash offsets: bootloader `0x00000`, device gateware `0x40000`,
device Pogobios `0x60000`, and upload-validity marker `0x88000`. User flash
pages begin at `0x90000`, after the marker's erase block.

## Build and validation

The v3 bootloader and normal robot were built with `make bootloader` and
`make gateware` from `Software/`. The remote was built with
`./pogosoc.py --cpu-variant=lite --target=pogobotv3 --remocon --build`, and
the SDK with `make -C sdk clean all`, inside `pogobot.sif`. All five binaries
fit their flash slots, and the package checksums pass. The packaged SDK's
`firmware_integrity` and `test_read_write_flash` examples cross-build. A host
test with a fake `iceprog` checked both installers' file paths, offsets,
marker contents, cleanup, and failure behavior.

The normal robot Pogobios was rebuilt with `make software`; the bootloader
image was rebuilt with `make bootloader` so `Pogoboot>` uses the same retry and
LED behavior. Its combined image contains a 71,764-byte Pogobios at offset
`0x20000` and fits the `0x00000–0x3ffff` bootloader slot. After `rc_erase`,
the LED is blue. The first v2 data write stores a partial marker at `0x88010`,
so an incomplete upload blinks orange; a verified image blinks green. Active
uploads blink quickly, while the idle status blink is slow.
Q preserves verified chunks for a later same-image retry. Reboot loses the RAM
bitmap, and changing images forces a fresh erase. Host fault injection and
both robot cross-builds pass. The new retry and LED behavior still needs a
hardware trial.
