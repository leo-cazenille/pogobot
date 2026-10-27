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
source commit `83d0da87c3c85085a5470417e45df140b909a9bb` using the existing
gateware. LTO, function/data sections, and linker garbage collection keep
robot and bootloader Pogobios at 93,036 bytes and remote Pogobios at 77,172
bytes, including PFFS v4 and IR file transfer. Robot BIOS has 77 commands
(including 21 filesystem/transfer handlers); remote BIOS has 76 (18 such
handlers plus the remote controller-start command). The `load`, `load_run` and `ir_file_abort` commands are robot-only.

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
[UART transfer tool](../../pffs_transfer.py). UART file commands act on the connected device; on a Pogoshower they manage
the controller's files. Use `rc_send_file` below to broadcast a saved file to
robots. During a UART file transfer, other commands and IR upload entry are
blocked. Inactive UART sessions time out after 30 seconds.

Defragmentation is available as `defrag YES` in the pogo-utils
`examples/flash_file` maintenance application. Pogobios omits the command and
its service state; the shared pogolib/SDK API is unchanged. Compaction can be
needed after deletions when a new file requires a larger contiguous extent.

The robot-only command `load <id|name>` installs a raw SDK `firmware.bin`
stored in PFFS, just as a completed `serialboot` would. It leaves the prompt
available; execute `run` to start it. The source file remains intact. File CRCs
are checked before the previous image is invalidated, and page readback plus
the whole installed CRC gate the completion marker. Files must be 4..131072
bytes; ELF, PFT1 envelopes and logs are refused. No gateware, bootloader or PFFS
sectors are erased. An interrupted installation can be retried from `Pogoboot>`.
The loader adds 1,368 bytes to robot firmware and reuses the shared page cache.
With IR file support included, robot static SRAM is 112,400 bytes, leaving
a 18,672-byte stack/runtime gap. Peak stack use still needs hardware measurement.
The shared public SDK API is unchanged.

## Start a named saved controller on robots

Use `rc_load_run <name>` at the remote prompt. Like `rc_start`, it repeats
every 200 ms until a key is pressed. The file must already be stored in each
robot's PFFS; no remote copy is required. Receiving robots must be idle at a
BIOS prompt, without an active file session. Names are exact, including numeric
labels, and contain 1..32 printable ASCII bytes without spaces.

The robot boot action `load_run <name>` verifies and installs the raw controller,
then selects user image 1 only on success. Missing/invalid files or failed
installation show a red LED/idle blink and never start an older image as a
fallback. The normal command LED refresh preserves this red indication; another
BIOS command can restore the marker color. Existing `load` remains UART-only
and does not start automatically. No acknowledgements are added. Both remote
and robot images, including the robot's combined bootloader, need this update.
See [named controller start](../../../readme-irRemote.md#start-a-controller-stored-in-pffs).

## Broadcast a saved file over IR

At a robot BIOS prompt, use `ir_send_file <id|name> [passes] [--replace]`.
At a remote prompt, use `rc_send_file <id|name> [passes] [--replace]`.
For example, `rc_send_file controller 3 --replace` broadcasts the named file
three times with 16+4 parity. Q or ESC stops the sender. Both sender and
receivers need this BIOS; receiving robots must be idle at their BIOS prompt.
See [IR file broadcasts](../../readme.md#broadcast-a-saved-file-over-ir).

The source must be a named ordinary file. Receivers choose a free local ID
and retain its name, format and exact byte length. A conflicting name is
refused unless `--replace` is present. Replacement preserves the destination
ID and requires the same format and allocated page count. It happens in place;
interruption can leave the old file damaged. Verified identical files are
skipped. New files are published only after readback and CRC verification.

A matching broadcast can repair partial data while the receiver remains powered.
Pending reception reserves its flash extent and locks filesystem operations
across timeout or cancellation; `ir_file_abort` at that robot's UART prompt
releases it. Reboot loses unpublished progress. Files are never automatically
loaded or run, and firmware slots and validity markers are untouched. There
are no receiver acknowledgements or automatic relaying. Receivers must have a
valid PFFS catalog; IR reception never formats storage.

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
The matching standalone SDK commit is `39c22e88951f03c75eac32622b26838f25e3b850` (`sdk_2_7_1`); pogo-utils
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
93,036-byte Pogobios at offset
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
are preserved; the archive now has 208 symbols, including all 35 public PFFS
file/log functions. Pogobios and applications link the same maintained pogolib
core. Existing pogo-utils headers forward to the SDK declarations, and native
pogo-utils builds compile the SDK sources with its explicit simulator backend.
All PFFS entry points link in SDK-only applications both normally and with
`-fno-lto`. The v4 storage layout and existing function signatures are unchanged.
The earlier LTO installer worked on one user-tested robot. PFFS hardware operations and
persistence across reboot and firmware upload still need robot validation.

Controller-install tests pass in the full/minimal consoles with fatal
ASan/UBSan and LTO. They cover source/catalog CRC failure, bounds up to 128 KiB,
partial pages, UART/transfer isolation, protected flash regions, failed erases,
page/marker writes, source changes, late corruption and successful retry. Launch
and persistence on a real robot remain unvalidated.

IR file sender/receiver tests pass with LTO and fatal ASan/UBSan. They cover
empty files through the full 1,408 KiB data capacity, interleaved 16+4 recovery,
repeated-pass repair, metadata/source validation, reserved-extent isolation,
reboot/abort behavior, default conflicts and explicit replacement. Injected
erase, write, CRC and catalog-publication failures preserve the documented
locking and publication rules. All 16 pogo-utils host tests pass against the
refreshed portable SDK core. File-radio timing, range, replacement behavior and
peak stack use remain unvalidated on hardware.

Defragmentation remains excluded. The current build retains every previous
non-defrag command, the original 1,024-byte CRC-16 table, and the full terminal. Normal and embedded robot BIOS
are 93,036 bytes; the remote is 77,172 bytes. Native file/log compaction tests
and full/minimal BIOS console tests pass, including rejection of the removed
command without flash changes. All 19 UART-client tests pass. The pogo-utils
`flash_file` example cross-build retains `defrag YES` and the linked shared
compaction implementation; its NOR file/log tests pass. The SDK package and
public library API are unchanged. Hardware validation remains pending.

Named controller-start tests pass in the full/minimal BIOS consoles under LTO
and fatal ASan/UBSan. They cover successful IR dispatch and verified reboot,
missing and numeric names, name bounds, corrupt source data, failed programming,
retry, and exclusion during UART or pending IR file reception. Lookup failures
leave the previous flash image unchanged, set red and never request a reboot.
Normal and embedded robot BIOS are 93,036 bytes (+664); remote BIOS is 77,172
bytes (+320). Package checksums, slot bounds, bootloader embedding and mock
programmer success/failure checks pass. The SDK/API is unchanged. IR delivery,
visible LED behavior and actual controller launch still need hardware validation.
