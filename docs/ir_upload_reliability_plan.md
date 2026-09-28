# Plan: reliable firmware uploads over infrared

Date: 2026-09-28

Status: Phases 1–2 software and host verification complete; robot validation
and Phases 3–5 pending.

## Phase commit record

Record the final implementation commit after host verification for each phase.
Robot validation is tracked separately, so a software checkpoint does not imply
that the feature has been tested on hardware.

| Phase | Final software commit | Host verification | Robot validation |
| --- | --- | --- | --- |
| 1 | `6afb76846663270bf2411c1b27728e18ab1f6b65` | Passed fault injection and v3 cross-build | Pending |
| 2 | `b04b557044e78d07a14879d926f911764f60bd59` | Passed versioned fault injection, serial auto-selection tests, and robot/remote v3 cross-builds | Pending |

## Objective

Make uploads of 50–60 KiB firmware reliable through a Pogowall or Pogoshower,
with bounded RAM use and support for broadcasting to several robots. A robot
must mark an image valid only after receiving and verifying the complete image.
Correct chunks should survive retransmission rounds within the same transfer.

Start with receiver correctness, image verification, and configurable repetition.
Add packet-level forward error correction (FEC) and optional feedback after
measuring the resulting reliability and upload time.

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
serial ACK still does not confirm reception by a robot; Phase 5 feedback is
needed for per-robot completion status.

## Phase 3: add configurable repetition and measured pacing

- [ ] Support configurable copy counts and repeated image passes. A receiver
      accepts only chunks it still needs during subsequent passes.
- [ ] Repeat metadata and completion messages as well as data.
- [ ] Separate copies in time; support spacing across small groups or passes once
      the receiver safely accepts reordered chunks.
- [ ] Measure transmission completion, receiver processing, and flash write time
      before tuning the current fixed 200 ms delay. Preserve preparation time for
      erases and adequate receive servicing between packets.
- [ ] Make uploader status distinguish remote transmission from confirmed robot
      completion. Broadcast mode without feedback can report transmission progress
      and expose completion through robot indicators.

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

Start evaluation with systematic Reed–Solomon using 16 data chunks and four
parity chunks, retaining 64-byte payloads. Keep parity counts configurable.
XOR parity is a simpler alternative with less recovery capacity.

- [ ] Specify coding-group IDs, symbol indices, finite-field parameters, and
      final-group padding. Whole-image verification excludes padding bytes.
- [ ] Use bounded static buffers and integer arithmetic over GF(256). Choose an
      implementation compatible with firmware size and portability constraints.
- [ ] Budget decoder working memory and firmware size explicitly. Twenty 64-byte
      payloads require 1,280 bytes before matrices, metadata, and other scratch space.
- [ ] Evaluate interleaving a small number of coding groups to spread burst losses.
      Four fully buffered groups require 5,120 payload bytes before decoder state.
- [ ] Schedule decoding and flash writes so reception continues to make progress.
- [ ] Keep groups that exceed their recovery capacity incomplete and recover them
      in subsequent rounds. FEC must not bypass final image verification.

Acceptance: each supported erasure pattern within the code's capacity reconstructs
the original data; losses beyond capacity cannot produce a valid incomplete image.
Measure decoding time, RAM, stack use, firmware size, and reception losses during
decoding before choosing production parameters.

## Phase 5: optional selective retransmission through the return link

First measure whether robots can reliably reach the remote. The wall/shower's
strong outgoing signal does not establish reliable reception in the reverse
direction.

- [ ] For one robot, request missing-chunk bitmaps and retransmit only required
      chunks or additional parity.
- [ ] For multiple robots, reserve quiet feedback periods and use polling or
      randomized response slots to avoid simultaneous replies.
- [ ] Aggregate repair requests so one broadcast repair can serve several robots.
- [ ] Retry lost feedback with bounded timeouts. Require explicit final status
      from each expected robot when claiming group completion; silence is not success.
- [ ] Retain repetition and FEC modes for deployments with unreliable feedback.

Acceptance: feedback remains usable as robot count increases, lost responses do
not produce false completion, and repairs reduce total traffic or upload time.

## Implementation locations and compatibility

| Location | Planned responsibility |
| --- | --- |
| [`Software/pogobios/ir_boot.c`](../Software/pogobios/ir_boot.c) | Receiver state machine, bounds checks, bitmap, erase lifecycle, reconstruction integration, and final verification. |
| [`Software/pogobios/boot.c`](../Software/pogobios/boot.c) | Remote forwarding, repetition, pacing, and optional feedback scheduling. |
| [`Software/litex_term.py`](../Software/litex_term.py) | Image metadata, transfer configuration, accurate progress reporting, and possibly parity generation. |
| [`Software/pogolib/ir_uart.c`](../Software/pogolib/ir_uart.c) | Bounded overflow handling and counters. |
| [`Software/pogolib/pogolib_infrared.c`](../Software/pogolib/pogolib_infrared.c) | Queue-drop accounting and reception servicing. |
| [`Software/pogobios/cmds/cmd_ir.c`](../Software/pogobios/cmds/cmd_ir.c) | Upload-mode configuration and diagnostic commands as needed. |

Deploy matching uploader, remote Pogobios, and robot Pogobios for the new protocol.
Use explicit version selection or capability negotiation where feedback is
available. Keep legacy transfers identifiable and preserve existing application
APIs. Determine whether the active receiver runs in the bootloader or current
Pogobios image when documenting the upgrade procedure.

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
- [RFC 5401: multicast negative-acknowledgement building blocks](https://www.rfc-editor.org/rfc/rfc5401.html).
