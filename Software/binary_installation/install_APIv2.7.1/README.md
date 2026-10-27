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

IR programming requires versioned uploads. The robot rejects legacy
LOAD/JUMP/ABORT broadcasts, and the remote no longer forwards legacy
programming frames. Use the current SDK terminal with `rc_flash_robot`.

The current robot, bootloader, and remote software images were rebuilt from
source commit `7e176a3826ee4f3c2f50c1c2c9d19d7b28a086a8` using the existing
gateware. LTO, function/data sections, and linker garbage collection keep
robot and bootloader Pogobios at 86,732 bytes and remote Pogobios at 75,572
bytes, including PFFS v4. All previous commands remain, plus 18 filesystem and
UART protocol handlers (74 robot and 75 remote commands in total).

This build fixes the eight release-review findings while preserving the public
C API. Serial frames and IR payloads are checked before copying. Application
updates invalidate the marker and cached IR state before changing code, and
marker writes are verified before success is reported. IR finalization can be
retried after a marker failure while retaining image data. The library also
fixes FIFO backpressure, full security-register writes, and signed timer
conversions. Rebuild user applications with the refreshed SDK library.

PFFS commands are available directly at the BIOS prompt. Type `pffs` for help
or `ls` to inspect files without changing flash. File creation refuses absent
or corrupt catalogs; `format YES` is the explicit operation that deletes all
PFFS files. The existing v4 layout and user-flash region are unchanged.
See [PFFS file commands](../../readme.md#pffs-file-commands) and the
[UART transfer tool](../../pffs_transfer.py). These commands act on the
UART-connected device; a Pogoshower connection manages the controller's files,
not files on robots over IR. During a file transfer or defrag, other commands
and IR upload entry are blocked. Inactive UART sessions time out after 30
seconds; defrag runs until completion or a reported error.

## Contents

- `bootloader/`: v3 bootloader image shared by robots and remotes.
- `pogobios/`: normal robot gateware and Pogobios.
- `remocon/`: remote gateware and Pogobios.
- `program_robot.sh` and `program_remote.sh`: direct `iceprog` installers.
- `SHA256SUMS`: checksums for the two scripts and five binaries.

The matching SDK is packaged separately as `../sdk_APIv2.7.1.tar.gz`, with its
own `../sdk_APIv2.7.1.tar.gz.sha256` checksum file. The refreshed SDK includes
PFFS v4 file/log declarations, the complete implementation in `libpogobot.a`,
portable `src/pffs` sources for native builds, and the UART transfer client.
The matching standalone SDK commit is `cdaf6b0` (`sdk_2_7_1`); pogo-utils
uses it from commit `1cc1d26` (`dev`).

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

The initial gateware builds used `--build`. This refresh rebuilds software
and combines it with that existing gateware. From `Software/`:

```sh
apptainer exec --cleanenv ../pogobot.sif ./pogosoc.py --cpu-variant=lite --target=pogobotv3 --bootloader
apptainer exec --cleanenv ../pogobot.sif ./pogosoc.py --cpu-variant=lite --target=pogobotv3
apptainer exec --cleanenv ../pogobot.sif ./pogosoc.py --cpu-variant=lite --target=pogobotv3 --remocon
```

All five binaries fit their flash slots, and all seven package checksums pass.
The combined bootloader's header, gateware, embedded firmware, and erased
padding match the build outputs byte for byte. A host test with a fake
`iceprog` checked both installers from another working directory, including
file paths, offsets, marker contents, cleanup, and programmer-failure handling.

The normal robot Pogobios was rebuilt with `make software`; the bootloader
image was rebuilt from the existing gateware with `--bootloader` so `Pogoboot>`
uses the same retry and LED behavior. Its combined image contains a
86,732-byte Pogobios at offset
`0x20000` and fits the `0x00000–0x3ffff` bootloader slot. After `rc_erase`,
the LED is blue. The first v2 data write stores a partial marker at `0x88010`,
so an incomplete upload blinks orange; a verified image blinks green. During
every active upload, including a retry, the LED blinks quickly in blue. The
blue, orange, or green idle status blink is slow.
Q preserves verified chunks for a later same-image retry. Reboot loses the RAM
bitmap, and changing images forces a fresh erase. Host fault injection and
the robot, bootloader, and remote cross-builds pass. FEC decoding drains the IR
receiver between symbols and uses 512 bytes of GF lookup tables to reduce
compute time. The linker copies those tables from flash into SRAM, so they
occupy 512 bytes in each memory. The separate coefficient table uses another
64 bytes in each. The decoder timing, LED colors, and rejection of legacy
uploads still need a hardware trial.

The production C receiver, cable loader, remote forwarder, and library API
tests pass with address and undefined-behavior sanitizers configured to stop
on errors. All 18 Python upload tests pass. A clean SDK package compiled all
32 examples; its library retains all 163 prior exported symbols. The standalone
SDK's integrity and flash-page examples also cross-build. The current release
fixes await hardware validation.

The LTO build also passes the native sanitizer suites with LTO enabled. An
application using flash-page APIs omitted from Pogobios links against the full
library archive, both normally and with `-fno-lto`. The integrity example builds
and seals with that archive. Booting, SPI/sensor access, and IR upload timing
with these optimized installer images still need hardware validation.

The PFFS NOR file/log tests, full and minimal BIOS console integration tests,
and all 19 host-client transfer tests pass with LTO and fatal sanitizers. They
cover CRC faults, retries, interrupted writes, malformed/overlong commands,
partial-input timeouts and file-session isolation. The 163 prior library exports
are preserved; the archive now has 206 symbols, including all 35 public PFFS
file/log functions. Pogobios and applications link the same maintained pogolib
core. Existing pogo-utils headers forward to the SDK declarations, and native
pogo-utils builds compile the SDK sources with its explicit simulator backend.
All PFFS entry points link in SDK-only applications both normally and with
`-fno-lto`. The v4 storage layout and existing function signatures are unchanged.
The earlier LTO installer worked on one user-tested robot. PFFS hardware operations and
persistence across reboot and firmware upload still need robot validation.
