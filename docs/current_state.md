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
a partial transfer; and repair cannot resume after reboot. Image length,
transfer identity, and full-image verification require Phase 2 metadata.

Other local changes include `Software/pogosoc.py` and an untracked hardware
history directory. Their contents are outside this verification work.

## Current decisions

- Complete and validate the receiver correctness work before adding repetition,
  FEC, or return-link feedback.
- Keep firmware changes small and preserve the existing application API where
  possible. New diagnostics must have bounded RAM and execution costs.
- Treat the reported upload as an engineering failure case, not a measured
  channel loss rate. No scientific parameter or parity rate has been selected.

## Data limitations

The current evidence is one failure log, source inspection, a simulated host
device, and a cross-build. A gap does not locate the loss within the optical,
interrupt, buffering, or flash path. The host harness uses mocked transport,
flash, and timing; no instrumented hardware upload or image readback has been
performed.

## Next concrete tasks

1. Validate failure, partial recovery, duplicates, and all four 64 KiB erase
   sectors on suitable hardware before flashing production robots.
2. Record per-receiver errors, queue and ring drops, flash timings, upload time,
   and final image integrity for both remote types.
3. Use those measurements to choose Phase 2 bitmap behavior and later repetition
   or FEC parameters.
