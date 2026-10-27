# Current repository state

Updated: 2026-10-27. Branch: `optimized_remote_transfer`; this is a working-tree
snapshot, including local changes.

## Inspected

- The root, software, and hardware READMEs; the SDK and remote upload guides;
  the project layout; and the earlier [software architecture notes](software_architecture.md).
- The IR upload path in `Software/litex_term.py`, `pogobios/boot.c`,
  `pogobios/ir_boot.c`, `pogolib/pogolib_infrared.c`, `pogolib/ir_uart.c`, and
  `pogolib/slip.c`, along with relevant headers and generated flash addresses.
- The user-provided log of a failed Pogowall or Pogoshower firmware upload.
- Hardware Pogoshower attempts where the new remote advertised v2, including
  one with an old PC terminal and later attempts where `Pogoboot>` entered
  `ir_flash` but timed out before START arrived.
- Host fault injection of the production IR upload, UART ring, and SLIP decoder
  sources in `Software/tests/ir_upload_fault_test.c`.
- The example build rules, v3 linker flash bounds, and startup copy path for
  a firmware self-integrity test.
- The user flash page API, raw SPI addressing, v2/v3 flash capacities, and
  boot-image and validity-marker allocations.
- Cable loading and IR retry-state interactions, marker write/erase failures,
  IR send bounds and FIFO backpressure, security-register writes, and timers.
- Pogobios size attribution, LTO and section collection, command retention,
  and application linking against the full fat-object library archive.
- PFFS v4 sources and UART shell from `pogo-utils` revision `ac6d558`,
  hardware erase selection, console limits, session isolation and NOR fault tests.

## Understood

Pogobot builds a LiteX SoC for the iCE40UP5K FPGA. Pogobios runs on a VexRiscv
soft core; `pogolib` provides the robot API. The remote receives SFL frames over
serial and broadcasts them over IR. The robot validates received frames and
writes their contents to flash. See the [architecture notes](software_architecture.md)
for the detailed build flow, memory map, and directory survey.

The reported log shows 31 inferred gaps of 64 bytes. The original receiver
retained only nine missing addresses, and its completion logic mishandled
exactly ten missing packets. The remote's serial acknowledgement reports that
the remote processed a frame; it does not confirm reception by a robot.
The [IR upload reliability plan](ir_upload_reliability_plan.md) records the
proposed repair sequence and its trade-offs.

## Unknown

- Which combination of optical errors, software queue drops, ring overruns, and
  flash-operation delays caused the reported losses. No corresponding counters
  or timing measurements have been supplied.
- Which exact binaries were deployed for the original 31-gap upload. The latest
  attempt used a robot installed from this checkout's previous v2.7.1 package;
  the remote image still needs independent confirmation.
- Completion rate and upload duration for larger images across distances,
  orientations, lighting, and robot counts.
- Whether Phase 1 behavior is correct on hardware under real optical loss.

## Working analyses and implementation

Phase 1 receiver changes in `ir_boot.c`, `ir_uart.c/.h`,
`pogolib_infrared.c`, `pogobot.h`, and `slip.c` add frame and flash-range checks,
bounded missing-packet tracking, overflow counters, and diagnostics. Host fault
injection passes for a 60 KiB image, ten recoverable gaps, 31 gaps, duplicates,
timeout, abort, malformed frames, sector-boundary loss, a full UART ring, and
SLIP resynchronization. That testing found and fixed an erase bug when one
frame spans two sectors. The Pogobios v3 cross-build succeeds with RISC-V GCC
10.1.0. Hardware validation remains outstanding.

The legacy IR receiver has been removed. Its missing-final-chunk sequence and
legacy LOAD/JUMP/ABORT frames now leave flash unchanged, including existing
partial or valid images. Mixed legacy frames cannot complete or abort a v2
upload. The remote rejects legacy programming frames and uses serial JUMP
only to close the PC transfer.

Phase 2 now adds versioned START/DATA/END/ABORT SFL frames, a 256-byte maximum
receive bitmap, flash readback after each chunk, and exact-image CRC-32 before
`FlashIsOK`. The remote announces support before the existing serial request,
so the SDK terminal selects this mode automatically for the example Makefiles'
single-image `make connect TTY=...` command. The optional `--ir-v2` flag remains
for manual use. Direct cable uploads use the existing serial protocol;
IR programming requires the current remote and SDK terminal. The remote
forwards new commands
with a provisional three-second erase pause. Host fault injection passes for
reordered chunks, missing first and last chunks, 31 losses, the 128 KiB size
limit, metadata changes, readback failure, CRC mismatch, and a Python-generated
wire fixture. Automatic mode selection has host serial-stream tests. Robot and
remote Pogobios v3 builds succeed.
Robot delivery, timing, and per-robot completion remain unverified on hardware.

Phase 3 supports three uncoded image passes by default with unchanged example
Makefiles. `POGOBOT_IR_COPIES=1` through `5` selects the count. Repeated START
and END messages use one transfer ID, and accepted chunks are not rewritten.
The terminal reports remote-acknowledged pass time; the robot now reports frame
processing time alongside flash erase/write timing. Host tests cover the
three-pass serial schedule and repair of disjoint losses without re-erase.
The 200 ms data gap and three-second erase pause remain provisional.

Phase 4 adds systematic Reed–Solomon 16+4 parity over 64-byte chunks. The PC
interleaves four groups; the robot holds only one 256-byte parity group and
uses accepted flash data for decoding. One coded pass is now the default for
`make connect TTY=...`; `POGOBOT_IR_FEC=0` selects uncoded comparison. Host
tests cover all coding coefficients, four erasures, missing parity, an
over-capacity group, short final chunks, later-pass repair, CRC mismatch, and
a 64 KiB image. Robot and remote v3 cross-builds succeed. Hardware decoder
timing, receive drops during decoding, and completion rate remain unknown.

The `firmware_integrity` example seals a linked application with a
deterministic flash-only payload and a CRC-32 trailer. The robot scans the
complete installed image and reports PASS/FAIL by serial and LED. Its default
v3 build is 57,752 bytes (56.4 KiB); changing `PAYLOAD_BYTES` reseals without
recompilation. Cross-build, ELF/raw boundary check, and host CRC corruption
check pass. A damaged image may fail before the checker can run. The user
reports the examples work on hardware; their test logs are not archived here.

The v3 user flash page API accepts 16-bit page IDs and allocates physical
offsets `0x90000–0x1fffff`: 5888 pages, the contiguous 1472 KiB after the
reserved sectors on a 2 MiB v3 chip. Erasure covers 23 64 KiB sectors;
out-of-range page IDs are rejected. The raw SPI address is corrected from the
CPU-mapped `0x290000` to physical `0x90000`. The v3 normal Pogobios, SDK, and
updated flash example cross-builds pass. The user reports the flash example
works on a robot; an independent full-region readback has not been recorded.

API v2.7.1 source commit `7f05f1e` rebuilt v3 bootloader, robot, remocon, and
SDK artifacts. The new installer includes five binaries, corrected physical
`iceprog` offsets, scripts with the original `check_return` status reporting,
and checksums. A separate SDK archive was generated. Host checks cover script
success and failure paths. A robot was programmed and verified with the previous
package image; the latest image and remote still need testing.

The first hardware attempt reported 148 legacy gaps because the PC used an old
terminal despite the remote's v2 banner. Later attempts reported `frames=0` in
`ir_flash`, then printed START/DATA/parity as ordinary messages. Source commit
`c525851` added a CRC-checked START entry path; `869c86c` extended the initial
wait from two to eight seconds and added Q cancellation. SDK commits `7ae21f8`
and `80f69eb` provide the remote-acknowledged progress bar and cancellation;
the user now sees the bar. The robot was reflashed with the new normal
Pogobios, but its prompt was `Pogoboot>`, whose separate combined bootloader
image still contained the older Pogobios. That explains the repeated short
blue blink, timeout, and absent START fallback. The bootloader has now been
rebuilt with the same source, and its embedded Pogobios was verified against
the build output. A subsequent 25,824-byte coded upload succeeded with 16 FEC
recoveries. A 42,920-byte attempt reached only 285 of 671 chunks; both active
IR receivers reported many CRC errors, while queue and ring drops stayed zero.

Pogobot commit `a60bb69` and SDK commit `d8f9536` derive v2 IDs from SHA-256
of the image. Another `rc_flash_robot` command can retain verified chunks after
timeout or Q while the robot remains powered. A changed image or failed flash
check starts with an erase; `rc_erase` clears RAM progress. A partial marker at
`0x88010` records the first data write, so the LED is blue before data, orange
for incomplete data, and green after verification. Host fault injection,
terminal tests, and both robot cross-builds pass. On hardware, a 42,920-byte
image reached 16, 351, 544, 639, 666, and finally all 671 chunks across
same-image retry commands; the final whole-image CRC passed. Some four-erasure
FEC calls took 80–165 ms, close to the 200 ms packet interval. The final
verified pass also spent 692 ms processing END, mostly on whole-image CRC.
END was received on each incomplete pass, so the eight-second timeout followed
missing chunks rather than a stalled decoder. The three active receivers each
reported hundreds of SLIP CRC failures per pass, with no queue or ring drops.

The current receiver drains IR input between FEC symbols and uses 512 bytes of
GF lookup tables to shorten multiplication. The linker copies these tables
from flash into SRAM, so their cost is 512 bytes in each memory. The separate
64-byte coefficient table also occupies both flash and SRAM. Fast blinking
remains blue for
every active upload; the slow idle blink reflects the flash markers: blue for
empty, orange for partial, and green for verified. Host fault injection and
both v3 robot cross-builds pass. The rebuilt bootloader embeds the new receiver.
Decoder timing and LED colors for this build still need a robot trial.

The eight release-review findings are implemented without public API changes.
IR send wrappers reject oversized data before copying. Cable and remote
uploads share bounded frame reception, including empty frames. Cable loading
checks destinations, invalidates application markers before writing, resets
cached IR progress, and reads writes back. Incomplete cable sessions cannot
be finalized by JUMP in a later session. Marker erasure and final writes are
verified; an IR END can retry marker finalization without erasing image data.
TX FIFO space is checked per byte. Security-register bounds avoid overflow,
and the write counter supports 256 bytes. Timer conversions use signed integer
arithmetic. The SPI byte sender also preserves hardware reads without its
previous unused, uninitialized shift accumulator.

Native sanitizer tests exercise the production serial loader and IR receiver
together, including cable replacement of a completed IR image, marker failures,
timeouts, and sector-spanning frames. The library API suite covers maximum
IR payloads, FIFO backpressure, full security-register writes, negative timer
values, and counter wraparound. Target builds and hardware validation are
tracked separately in the IR plan and installer notes. Applications must be
rebuilt against the updated static SDK library to receive the library fixes.
Source commit `b07404f` cross-builds for the robot, bootloader, and remote.
All 32 examples compile from a clean SDK package, whose library retains the
same 163 exported symbols. The standalone SDK branch `sdk_2_7_1` is refreshed
at `d61344d`; its integrity example's SDK path is corrected. Native sanitizer
tests and 18 Python upload tests pass. Hardware validation is pending.

The Pogobios and pogolib Makefiles now enable `-flto`, function/data sections,
and the existing linker section collection. Fat library objects preserve native
code for non-LTO applications; the compiler's archiver indexes the LTO symbols.
Robot and bootloader binaries shrink from 71,180 to 62,228 bytes (12.6%);
the remote shrinks from 67,828 to 51,052 bytes (24.7%). Robot static SRAM use
falls from 85,184 to 76,224 bytes, leaving 54,848 bytes for stack/runtime use.
A section-only control (`LTO=0`) is 67,740 bytes. Changing LTO mode requires
a clean rebuild or `make -B`. All 56 robot/57 remote commands and all 163
library exports remain. Sanitizer suites pass with and without LTO; all 18
Python tests pass in Apptainer. Flash-page applications link normally and with
`-fno-lto`; the integrity example builds and seals. These are software checks:
peak stack use and hardware behavior remain unmeasured. The LTO-only API v2.7.1
installer was refreshed from source commit `097dd13`, including the combined
bootloader with the 62,228-byte firmware at offset `0x20000`. All seven package
checksums, slot bounds, embedded bytes, and simulated programmer success/failure
checks pass. SDK packages and the standalone SDK remain unchanged and compatible.

PFFS v4 file and append-log code is now maintained in `Software/pogolib/pffs`
and included in `libpogobot.a`. Pogobios links that library; SDK applications
get the same API from `<pogobot.h>` or the standalone `<pffs/...>` headers.
pogo-utils keeps forwarding headers and consumes the SDK archive on hardware
and its portable sources on the host. Hardware defaults to real 4 KiB erases;
only explicit simulator builds select page-write erase emulation. Storage
layout and existing signatures are unchanged. The BIOS console/transfer policy
still requires `format YES`, bounded input and serialized file sessions.

All 163 prior library exports remain; the archive now has 206 symbols,
including all 35 public PFFS functions. SDK-only applications link every PFFS
entry point both normally and with `-fno-lto`; standalone headers compile as
C++. NOR file/log tests, full/minimal BIOS console tests and all 19 host-client
transfer cases pass under LTO with fatal ASan/UBSan. All 15 pogo-utils Debug
CTests pass against the shared sources, and its flash shell cross-builds for
hardware and the simulator. Existing IR/API fault suites pass. PFFS hardware
behavior and persistence across reboot/firmware upload remain unvalidated.
The earlier LTO installer worked on one user-tested robot. The shared-core
extraction alone kept the robot build at 86,732 bytes, with no application runtime dependency
on the BIOS. The installer and separate SDK archive are refreshed from source
commit `7e176a3826ee4f3c2f50c1c2c9d19d7b28a086a8`. Package checksums, slot bounds,
bootloader embedding and mock programmer success/failure checks pass. Robot static SRAM
remains 103,240 bytes (27,832 bytes left for stack/runtime); remote static SRAM
remains 89,088 bytes. All 32 packaged SDK examples build, and unused PFFS
code is absent from the raw-flash example. The standalone SDK is committed as
`cdaf6b0` on `sdk_2_7_1`; pogo-utils integration is `1cc1d26` on `dev`.
PFFS hardware operations still need validation.

Robot BIOS now adds `load <id|name>` to install a saved raw PFFS controller at
physical `0x60000`, leaving `run` separate as after serialboot. Source catalog
and data CRCs are checked before invalidating the previous image. Page readback
and a final installed CRC gate the completion marker; no bootloader, gateware,
CRC metadata or PFFS sectors are erased. The loader reuses the 256-byte console
page cache, resets resumable IR state on installation, and rejects incompatible
containers and files outside the 4..131072-byte slot bounds. The command remains
UART-only and is excluded on remotes. Full/minimal console tests cover sizes,
CRC rejection, guard regions, transfer isolation, injected erase/program/read
faults and retry. Robot Pogobios is 88,100 bytes (+1,368), with 104,608 bytes
static SRAM and a 26,464-byte stack/runtime gap. No extra page cache is added;
remote firmware stays 75,572 bytes. Source commit `c237ffd` implements the
command. The installer refresh includes normal and embedded bootloader BIOS;
all three release cross-builds, checksums, slot/embedding checks and mock
programmer success/failure checks pass. The standalone SDK/API remains
unchanged. Native PFFS/console, 19 UART-client
cases and the IR/API fault suites pass. Hardware launch and reboot persistence
still need verification.

## Current decisions

- Measure coded and uncoded timing and completion on hardware before changing
  pacing or redundancy.
- Treat uploads as one-way broadcasts; robots are not expected to message remotes.
- Use fixed 16+4 Cauchy Reed–Solomon coding with four-group sender
  interleaving. One coded pass is the initial default; hardware results may
  justify more passes or different pacing.
- Maintain one PFFS core in pogolib; keep BIOS protocol/policy separate and
  distribute native sources through the SDK.
- Keep firmware changes small and preserve the existing application API where
  possible. New diagnostics must have bounded RAM and execution costs.
- Treat the reported upload as an engineering failure case, not a measured
  channel loss rate. The selected 16+4 parity rate is provisional, not a
  measured optimum.
- Target v3 heads with 2 MiB flash only; the user page API may use all
  physical flash from `0x90000` through `0x1fffff`.
- Keep the API v2.7.1 installer limited to programming artifacts; distribute
  the SDK as a separate archive.

## Data limitations

The current evidence is several hardware attempt logs, source inspection, a
simulated host device, and cross-builds. A gap does not locate the loss within
the optical, interrupt, buffering, or flash path. The host harness uses mocked
transport, flash, and timing; it cannot measure target decoder speed. No
instrumented upload dataset or full flash dump has been archived. The robot
installer has programmed a head with `iceprog` verification; the current
decoder and LED changes still need a hardware trial.

## Next concrete tasks

1. Program the current v2.7.1 robot image and bootloader. Retry the
   42,920-byte image without rebooting or `rc_erase`; record FEC decode maxima,
   remaining chunks, and completion time. Check fast blue during upload and
   slow blue/orange/green after erase, partial upload, and completion.
2. Record per-receiver errors, queue and ring drops, flash timings, upload time,
   and final image integrity for both remote types. Compare with the earlier
   five-command retry sequence.
3. Compare one coded pass, repeated coded passes, and uncoded passes under
   matched conditions. Use completion, decoder timing, and loss counters to
   tune pacing and redundancy.
4. Save the on-robot `firmware_integrity` PASS/FAIL log and flash-page test
   results for pages 0, 255, 256, 1791, 1792, and 5887.
5. Program one test robot and each remote type from the API v2.7.1 package,
   then verify the reported version and repeat the upload and flash tests.
6. Validate the refreshed installer images on hardware, including PFFS UART
   operations, persistence across reboot/firmware upload, and IR upload timing.
7. Upload two raw SDK controller binaries to PFFS, select each with `load` and
   `run`, then verify file CRCs and selection again after reboot. Check a failed
   installation remains unbootable until a successful retry.
