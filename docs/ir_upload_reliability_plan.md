# Plan: reliable firmware uploads over infrared

Date: 2026-09-28

Status: Phases 1–4 software and host verification complete; hardware validation
and pacing measurements pending.

## Phase commit record

Record the final implementation commit after host verification for each phase.
Robot validation is tracked separately, so a software checkpoint does not imply
that the feature has been tested on hardware.

| Phase | Final software commit | Host verification | Robot validation |
| --- | --- | --- | --- |
| 1 | `6afb76846663270bf2411c1b27728e18ab1f6b65` | Passed fault injection and v3 cross-build | Pending |
| 2 | `b04b557044e78d07a14879d926f911764f60bd59` | Passed versioned fault injection, serial auto-selection tests, and robot/remote v3 cross-builds | Pending |
| 3 | `95c0d8a09ccb4a9d57df140567b1af4c8d5d78d4` | Passed repeated-pass sender tests, receiver fault injection, and robot/remote v3 cross-builds | Pending; pacing measurements needed |
| 4 | `21a7120f12d08c4b37553f22bba0775284a0afda` | Passed GF(256) matrix checks, 64 KiB cross-language recovery fixtures, malformed/over-capacity/CRC fault tests, and robot/remote v3 cross-builds | Pending; decoder timing and completion measurements needed |

## Objective

Make uploads of 50–60 KiB firmware reliable through a Pogowall or Pogoshower,
with bounded RAM use and support for broadcasting to several robots. A robot
must mark an image valid only after receiving and verifying the complete image.
Correct chunks should survive retransmission rounds within the same transfer.

Use receiver correctness, image verification, configurable repetition, and
packet-level forward error correction (FEC) for one-way broadcasts. Measure
reliability and upload time on robots before tuning redundancy and pacing.

## Evidence from the current implementation

The reported log contains 31 missing chunks, each indicated by an address gap of
`0x40` (64 bytes), but prints only nine stored missing addresses.

| Finding | Relevant source | Consequence |
| --- | --- | --- |
| `NB_MISSING_ADDR` is 10, and insertion stops at `NB_MISSING_ADDR - 1`. | [`ir_boot.c`](../Software/pogobios/ir_boot.c), `add_to_missing_list()` | Only nine missing addresses can be retained for recovery. |
| Completion handles counts below ten and above ten, leaving exactly ten outside both failure branches. | `ir_boot.c`, `exec_frame_cmd()` | An incomplete image can be marked valid. |
| Recovered addresses remain in the missing list; duplicate suppression remembers only the last accepted address. | `ir_boot.c`, `in_missing_list()` and `exec_frame_cmd()` | Repeated passes and delayed duplicates can corrupt recovery accounting. |
| Missing data is inferred from gaps between received addresses. | `ir_boot.c`, `exec_frame_cmd()` | Lost trailing chunks are not reliably detected. |
| Erasure is triggered by receipt of a chunk at a sector boundary. | `ir_boot.c`, `exec_frame_cmd()` | Loss or retransmission of that chunk can interfere with correct erase/write sequencing. |
| Each data frame is sent once, followed by a 200 ms delay and a serial acknowledgement. | [`boot.c`](../Software/pogobios/boot.c), `flash_robot()` | PC acknowledgement confirms remote processing, not robot reception. |
| The complete-message queue silently discards messages when full. | [`pogolib_infrared.c`](../Software/pogolib/pogolib_infrared.c), `on_complete_valid_slip_packet_received()` | Buffer losses are difficult to distinguish from optical losses. |
| The receive ISR can loop without consuming the hardware FIFO when its software ring is full. | [`ir_uart.c`](../Software/pogolib/ir_uart.c), `ir_uart_rx_isr()` | Reception can prevent the main loop from making progress and draining the ring. |

The transport already has an outer IR CRC-32 and an inner SFL CRC-16. These detect
corruption but do not reconstruct discarded packets. The log alone does not
identify whether losses arise from optical interference, buffering, or flash
operation timing. Verify that deployed robot and remote firmware match the
inspected source before interpreting measurements.

## Phase 1: correct existing failure handling and expose losses

- [x] Fix the missing-count boundary conditions so every detected incomplete transfer
      remains invalid, including exactly ten missing chunks.
- [x] Ensure recovered chunks are counted only once. Reset transfer accounting
      consistently on new transfers, aborts, and timeouts.
- [x] Validate message lengths before parsing SFL fields or checking their CRC.
      Reject unsupported commands and invalid destinations without writing flash.
- [x] Make receive-buffer overflow handling bounded so the main loop can resume.
      Define how decoding resynchronizes after bytes are dropped.
- [x] Count hardware/software receive overruns where observable, full message
      queues, malformed frames, CRC failures, duplicates, and accepted chunks.
      Retain per-receiver counters where useful.
- [x] Summarize counters after a transfer instead of printing each event during
      reception. Measure time spent erasing and programming flash.

Increasing the missing-address array alone is insufficient: completion and
duplicate handling also need correction. The legacy wire protocol carries no
transfer identity; Phase 2 metadata must supply it. Keep initial changes local
to the affected paths and preserve existing public application APIs.

Acceptance: incomplete transfers cannot reach `FlashIsOK` through the known
counting paths; buffer exhaustion cannot indefinitely block reception processing;
diagnostics distinguish observed CRC failures from observed buffer drops.

Host verification: run `python3 Software/tests/run_ir_upload_fault_test.py` from
the repository root. The fault suite compiles the production receiver, UART
ring, and SLIP decoder with AddressSanitizer and UndefinedBehaviorSanitizer.
It covers a 60 KiB image, ten and 31 missing chunks, duplicates, timeout,
abort, malformed commands and lengths, forbidden destinations, lost first and
sector-boundary chunks, sector-spanning frames, UART ring exhaustion, and SLIP
resynchronization. It found a sector-spanning erase defect, which was fixed.
Mocked flash timings and host IR input do not establish hardware performance.
Known probes show that missing final chunks, wrong-image repairs, and a reboot
during partial recovery remain outside Phase 1's guarantees.

## Phase 2: introduce explicit transfer metadata and a receive bitmap

### Transfer messages

Define a versioned upload mode with the following conceptual messages. Choose
wire identifiers and fixed-width encodings during implementation.

Phase 2 uses SFL commands `0x10` START, `0x11` DATA, `0x12` END, and `0x13`
ABORT. All fields are big-endian. START is 20 bytes: version 1 (1 byte), flags
zero (1), transfer ID (4), CPU-mapped destination (4), exact image length (4),
chunk size 64 (2), and image CRC-32 (4). DATA is transfer ID (4), chunk index
(2), and 1–64 actual image bytes; only the last chunk may be short. END and
ABORT each carry the transfer ID (4). The existing SFL CRC-16 covers command
and payload; outer IR SLIP CRC-32 protects the transported message. The image
CRC uses CRC-32/ISO-HDLC over exact flash bytes, matching Python `zlib.crc32`.
Only one image of 1–131072 bytes at mapped address `0x240000` or `0x260000` is
accepted per transfer. Transfer IDs are random 32-bit values chosen by the PC.

| Message | Required information and purpose |
| --- | --- |
| `START` | Protocol version, transfer/image identity, destination, exact byte length, chunk size, expected whole-image CRC-32, and optional FEC parameters. |
| `DATA` | Transfer identity, chunk index, actual payload length, and data protected by a packet CRC. |
| `END` | Transfer identity; requests completeness and integrity verification. |
| `ABORT` | Transfer identity; stops the transfer while leaving an incomplete image invalid. |
| `STATUS` | Optional response containing transfer identity, robot identity, readiness/completion state, or requested repairs. |

Specify byte order, CRC parameters and covered bytes, maximum lengths, padding
rules, and treatment of malformed or unsupported messages. Validate destinations
and lengths against the allowed image region using arithmetic that cannot wrap.
Protect the bootloader, validity marker, and other reserved flash regions.

Repeated `START` messages for the same active transfer must preserve progress.
Different image metadata must never reuse the previous bitmap. Define an explicit
restart operation for an image whose final flash verification fails.

### Receiver state and flash lifecycle

- [x] Track expected chunks with one bit per successfully programmed chunk.
      Mark a bit after the write succeeds and the selected readback check passes.
- [x] Derive completion from the expected bitmap, including the last short chunk.
      Ignore already accepted chunks regardless of arrival order.
- [x] Invalidate the old image before modifying its contents. Erase all required
      sectors once at transfer start, independently of individual data chunks.
- [x] Establish an erase preparation period before data transmission. The
      remote currently uses a provisional three-second pause; measure and tune
      it on hardware. Repeated starts do not erase an active matching image.
- [x] Preserve incomplete progress across retransmission rounds for the same
      transfer. Make repeated completion messages safe.
- [x] Before writing `FlashIsOK`, require the complete bitmap and compare a CRC-32
      computed from the exact image bytes in flash with the expected CRC.
- [x] On timeout or abort, retain an invalid image. On reset, restart an incomplete
      transfer unless progress metadata is deliberately persisted in a later design.

Bitmap storage is small:

| Image size | 64-byte chunks | Bitmap bytes |
| --- | ---: | ---: |
| 60 KiB | 960 | 120 |
| 64 KiB | 1,024 | 128 |
| 96 KiB | 1,536 | 192 |

Use the permitted flash layout to set the supported image limit. The local
`ROM_LINKER_SIZE = 96*kB` change does not by itself define that limit.

Acceptance: missing first or last chunks, more than nine losses, arbitrary
duplicates, and reordered chunks are handled correctly. Only a complete image
whose contents match the expected CRC is marked valid.

Host verification runs through `Software/tests/run_ir_upload_fault_test.py`.
The harness exercises production receiver code using Python-generated wire
payloads, reordered and repeated chunks, short final chunks, 31 missing chunks,
the 128 KiB size limit, metadata changes, reboot restart, malformed frames,
readback failure, and CRC
mismatch. Both robot and remote Pogobios targets cross-build. The remote's
serial ACK does not confirm reception by a robot; inspect each robot's final
integrity result to establish completion.

## Phase 3: add configurable repetition and measured pacing

- [x] Support configurable copy counts and repeated image passes. A receiver
      accepts only chunks it still needs during subsequent passes.
- [x] Repeat metadata and completion messages as well as data.
- [x] Separate copies in time; support spacing across small groups or passes once
      the receiver safely accepts reordered chunks.
- [x] Report host elapsed time per remote-acknowledged pass and instrument robot
      frame processing alongside existing erase/write timing and drop counters.
- [ ] Measure on hardware before tuning the current fixed 200 ms delay and
      three-second erase preparation. Check receiver servicing between packets.
- [x] Make uploader status distinguish remote transmission from confirmed robot
      completion. One-way broadcast reports transmission progress and exposes
      completion through robot indicators.

In uncoded mode, the unchanged example `make connect TTY=...` command defaults
to three full passes for the observed high-loss case. `POGOBOT_IR_COPIES=1` or
`2` selects shorter comparisons; accepted values are 1–5. Every pass repeats
START, all DATA frames, and two END frames using the same transfer ID. The
remote already broadcasts each START three times. Robot bitmaps suppress
duplicate flash writes and retain progress if a pass ends incomplete. These
counts and the existing pacing are provisional until robot measurements.

Two copies add 100% packet traffic; three add 200%. With independent loss
probability `p`, two copies leave residual loss probability `p^2` per chunk.
Burst interference and receiver stalls violate the independence assumption, so
copy spacing must be evaluated on hardware.

At 64-byte chunks, a 60 KiB upload contains 960 chunks. The existing 200 ms pause
alone contributes about 192 seconds per pass, excluding transmission and other
work. Repeating that schedule has a substantial time cost.

Acceptance: repeated broadcasts accumulate progress without erasing or rewriting
accepted chunks. Compare upload time and completion rate at one, two, and three
copies under the same conditions.

## Phase 4: add packet-level erasure coding

Retain packet CRCs. Treat failed CRC checks as missing packets and reconstruct
their data from valid parity packets.

| Candidate | Added packet traffic | Recovery capacity per group |
| --- | ---: | --- |
| 8 data + 1 XOR parity | 12.5% | Any one missing packet out of nine |
| 16 data + 4 Reed–Solomon parity | 25% | Any four missing packets out of twenty |
| 16 data + 8 Reed–Solomon parity | 50% | Any eight missing packets out of twenty-four |

The selected fixed format is systematic 16 data + 4 Reed–Solomon parity with
64-byte symbols. Other parity counts require a new protocol definition.

- [x] Specify coding-group IDs, symbol indices, finite-field parameters, and
      final-group padding. Whole-image verification excludes padding bytes.
- [x] Use bounded static buffers and integer arithmetic over GF(256).
- [x] Budget decoder working memory and firmware size explicitly.
- [x] Interleave four coding groups in the PC sender to spread adjacent losses.
      The robot holds parity for only one group at a time and reads accepted
      data from flash when solving, so interleaving adds no robot group buffers.
- [x] Decode as sufficient parity arrives, then read back each reconstructed
      flash write. The remote retains the provisional 200 ms frame gap.
- [x] Keep groups that exceed their recovery capacity incomplete and recover
      them in subsequent rounds. FEC cannot bypass final image verification.
- [ ] Measure decoder latency, queue/ring drops, and completion rate on robots
      before changing pacing or treating the host results as hardware validation.

### Implemented 16+4 wire format

START retains its 20-byte layout and sets flags byte bit 0 for FEC. The new
SFL command `0x14` PARITY carries a 4-byte transfer ID, 2-byte group index,
1-byte parity row (0–3), and 64 parity bytes; all multibyte fields are
big-endian. Group `g` covers DATA indices `16g` through `16g+15`. The final
group pads absent symbols and a short final DATA symbol with zero bytes for
parity generation; only actual image bytes are programmed or included in the
final CRC-32. Inner SFL CRC-16 and outer IR SLIP CRC-32 still reject corrupt
packets before decoding.

Arithmetic is over GF(256) with polynomial `0x11d`. Parity row `r` contains
the sum of data symbol `i` multiplied by `inverse(i XOR (16+r))` at every byte
position. The Cauchy matrix makes any available set of `k` parity rows solve
any `k <= 4` missing data symbols. The PC sends DATA for four groups in
interleaved order, followed by each group's four parity frames. The remote
forwards parity without computing it.

The robot's `v2_upload` state occupies 540 bytes of BSS, including its existing
256-byte receive bitmap and one 256-byte parity group. The decoder uses a
384-byte RISC-V stack frame and a 64-byte coefficient table in read-only data;
its compiled function is 1,740 bytes. Current v3 Pogobios ELF sizes are
71,180 bytes text / 14,216 bytes BSS on a robot and 61,268 bytes text /
10,656 bytes BSS on a remote. Measure actual decoder and flash timing on
hardware; host mocks report zero microseconds.

The matching updated remote is assumed, so the existing versioned upload
announcement selects FEC automatically without a second capability banner.
With FEC enabled, the unchanged `make connect TTY=...` command defaults to one
pass; `POGOBOT_IR_COPIES=2` or more adds full repair passes, and
`POGOBOT_IR_FEC=0` disables parity for comparison. For a 60 KiB image, 960
DATA and 240 PARITY frames add 25% packet traffic and at least 240 seconds of
the current 200 ms remote pauses, before serial and erase overhead.

Host verification checks every Cauchy submatrix through four erasures, Python
sender framing, representative mixed data/parity losses, short-tail padding,
malformed parity, an over-capacity group, whole-image CRC rejection of bad
recovery, repair in a later pass, and a 64 KiB cross-language fixture exercising
all 64 coefficient positions. On hardware, compare completion and duration at
one, two, and three passes; inspect FEC recovery, decoder timing, and receive
drop counters before production use.

## Implementation locations and compatibility

| Location | Planned responsibility |
| --- | --- |
| [`Software/pogobios/ir_boot.c`](../Software/pogobios/ir_boot.c) | Receiver state machine, bounds checks, bitmap, erase lifecycle, reconstruction integration, and final verification. |
| [`Software/pogobios/boot.c`](../Software/pogobios/boot.c) | Remote forwarding, repetition, and pacing. |
| [`Software/litex_term.py`](../Software/litex_term.py) | Image metadata, transfer configuration, accurate progress reporting, and possibly parity generation. |
| [`Software/pogolib/ir_uart.c`](../Software/pogolib/ir_uart.c) | Bounded overflow handling and counters. |
| [`Software/pogolib/pogolib_infrared.c`](../Software/pogolib/pogolib_infrared.c) | Queue-drop accounting and reception servicing. |
| [`Software/pogobios/cmds/cmd_ir.c`](../Software/pogobios/cmds/cmd_ir.c) | Upload-mode configuration and diagnostic commands as needed. |

Deploy matching uploader, remote Pogobios, and robot Pogobios for the new protocol.
The remote's existing announcement selects the versioned upload. Preserve
existing application APIs. Determine whether the active receiver runs in the
bootloader or current Pogobios image when documenting the upgrade procedure.

The host may generate parity to reduce work on the remote, or the remote may
generate it from a small buffered group. Select this placement after measuring
memory and serial-protocol costs. Gateware changes should be driven by a measured
need that firmware changes cannot resolve.

## Validation plan for implementation

Use deterministic fault injection for transfer logic before hardware trials:

- Drop the first, middle, final, and sector-boundary chunks; include exactly ten
  missing chunks and the reported pattern of 31 isolated losses.
- Duplicate and reorder chunks and repeat `START` and `END` messages.
- Corrupt data, headers, and metadata; exercise short final chunks, invalid lengths,
  unsupported commands, arithmetic boundaries, and forbidden destinations.
- Interrupt transfers with timeout, abort, or reset and verify image validity state.
- Exercise buffer exhaustion and flash write/readback failures.
- Cover transfers within one erase sector and across sector boundaries, using
  50–60 KiB images and the largest supported image.
- For FEC, cover missing data and parity packets, bursts, final partial groups,
  and losses above the correction capacity.

On hardware, compare both remote types across representative distances, angles,
lighting, and robot counts. Record exact firmware versions, total upload time,
completion rate over repeated attempts, loss counters, and final flash integrity.
Set measurable reliability and duration targets from these baseline trials before
selecting default repetition, parity, and pacing parameters.

## References

- [Current software architecture](current_state.md).
- [Existing remote upload workflow](../readme-irRemote.md).
- [RFC 5510: Reed–Solomon FEC schemes](https://www.rfc-editor.org/rfc/rfc5510.html).
