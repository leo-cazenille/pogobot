# Current repository state

Updated: 2026-09-29. Branch: `optimized_remote_transfer`; this is a working-tree
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

Host probes also confirm legacy protocol limits: a missing final chunk can
still set `FlashIsOK`; a replacement chunk from another image can complete
a partial transfer; and repair cannot resume after reboot. The new Phase 2
mode uses metadata and image CRC to address the first two limits.

Phase 2 now adds versioned START/DATA/END/ABORT SFL frames, a 256-byte maximum
receive bitmap, flash readback after each chunk, and exact-image CRC-32 before
`FlashIsOK`. The remote announces support before the existing serial request,
so the SDK terminal selects this mode automatically for the example Makefiles'
single-image `make connect TTY=...` command. The optional `--ir-v2` flag remains
for manual use. Direct cable uploads and older remotes use the legacy mode;
SDK copies of the terminal must be updated. The remote forwards new commands
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
v3 build is 57,784 bytes (56.4 KiB); changing `PAYLOAD_BYTES` reseals without
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
read-only GF tables to shorten multiplication. Fast blinking remains blue for
every active upload; the slow idle blink reflects the flash markers: blue for
empty, orange for partial, and green for verified. Host fault injection and
both v3 robot cross-builds pass. The rebuilt bootloader embeds the new receiver.
Decoder timing and LED colors for this build still need a robot trial.

## Current decisions

- Measure coded and uncoded timing and completion on hardware before changing
  pacing or redundancy.
- Treat uploads as one-way broadcasts; robots are not expected to message remotes.
- Use fixed 16+4 Cauchy Reed–Solomon coding with four-group sender
  interleaving. One coded pass is the initial default; hardware results may
  justify more passes or different pacing.
- Keep firmware changes small and preserve the existing application API where
  possible. New diagnostics must have bounded RAM and execution costs.
- Treat the reported upload as an engineering failure case, not a measured
  channel loss rate. The selected 16+4 parity rate is provisional, not a
  measured optimum.
- Target v3 heads with 2 MiB flash only; the user page API may use all
  physical flash from `0x90000` through `0x1fffff`.
- Keep the API v2.7.1 installer limited to programming artifacts; distribute
  the 111 MiB uncompressed SDK as a separate archive.

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
