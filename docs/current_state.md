# Current repository state

Updated: 2026-09-28. Branch: `optimized_remote_transfer`; this is a working-tree
snapshot, including local changes.

## Inspected

- The root, software, and hardware READMEs; the SDK and remote upload guides;
  the project layout; and the earlier [software architecture notes](software_architecture.md).
- The IR upload path in `Software/litex_term.py`, `pogobios/boot.c`,
  `pogobios/ir_boot.c`, `pogolib/pogolib_infrared.c`, `pogolib/ir_uart.c`, and
  `pogolib/slip.c`, along with relevant headers and generated flash addresses.
- The user-provided log of a failed Pogowall or Pogoshower firmware upload.
- Host fault injection of the production IR upload, UART ring, and SLIP decoder
  sources in `Software/tests/ir_upload_fault_test.c`.
- The example build rules, v3 linker flash bounds, and startup copy path for
  a firmware self-integrity test.

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
- Which exact robot and remote binaries were deployed for that upload, and
  whether they match this checkout.
- Completion rate and upload duration across distances, orientations, lighting,
  and robot counts. Return-link reliability is also unmeasured.
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
v3 build is 57,544 bytes (56.2 KiB); changing `PAYLOAD_BYTES` reseals without
recompilation. Cross-build, ELF/raw boundary check, and host CRC corruption
check pass. A damaged image may fail before the checker can run, and hardware
execution has not yet been tested.

Other local changes include `Software/pogosoc.py` and an untracked hardware
history directory. Their contents are outside this verification work.

## Current decisions

- Measure coded and uncoded timing and completion on hardware before changing
  pacing or selecting return-link feedback parameters.
- Use fixed 16+4 Cauchy Reed–Solomon coding with four-group sender
  interleaving. One coded pass is the initial default; hardware results may
  justify more passes or different pacing.
- Keep firmware changes small and preserve the existing application API where
  possible. New diagnostics must have bounded RAM and execution costs.
- Treat the reported upload as an engineering failure case, not a measured
  channel loss rate. The selected 16+4 parity rate is provisional, not a
  measured optimum.

## Data limitations

The current evidence is one failure log, source inspection, a simulated host
device, and a cross-build. A gap does not locate the loss within the optical,
interrupt, buffering, or flash path. The host harness uses mocked transport,
flash, and timing; no instrumented hardware upload or image readback has been
performed.

## Next concrete tasks

1. Validate Phase 2 flash readback, CRC, incomplete marker state, and the
   provisional erase pause on suitable hardware before production use. Update
   the SDK terminal and helper before testing `make connect TTY=...`.
2. Record per-receiver errors, queue and ring drops, flash timings, upload time,
   and final image integrity for both remote types.
3. Compare one coded pass, repeated coded passes, and uncoded passes under
   matched conditions. Use completion, decoder timing, and loss counters to
   tune pacing and decide whether return-link feedback is useful.
4. Upload `Software/example/firmware_integrity` as a 50–60 KiB image and record
   its on-robot PASS/FAIL result alongside the receiver diagnostics.
