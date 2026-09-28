/**
 * POGOBOT
 *
 * Copyright © 2022 Sorbonne Université ISIR
 * This file is licensed under the Expat License, sometimes known as the MIT License.
 * Please refer to file LICENCE for details.
**/


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <system.h>
#include <string.h>
#include <irq.h>

#include <generated/mem.h>
#include <generated/csr.h>
#include <generated/soc.h>

#include <sfl.h>
#include <boot.h>
#include <ir_boot.h>
#include <ir_upload_v2.h>
#include <ir_uart.h>
#include <libbase/crc.h>
#include <libbase/spiflash.h>
#include <pogobot.h>

#define NB_MISSING_ADDR 10
// IR uploads occupy the current gateware and firmware slots, ending before the validity marker.
#define IR_FLASH_START 0x40000u
#define IR_FLASH_END 0x80000u

static uint32_t next_addr;
const char * ir_magic_req = IR_MAGIC_REQ;
static uint32_t missing_addr[NB_MISSING_ADDR];
static uint32_t missing_total;
static uint8_t missing_count;
static uint8_t chunk_size;
// Track each erase sector in the upload window using the generated sector size.
static uint8_t erased_sectors[((IR_FLASH_END - IR_FLASH_START) /
                               SPIFLASH_SECTOR_SIZE + 7) / 8];
static uint8_t flash_state_partial;
static uint8_t partial_known;
static uint8_t started;
static uint8_t transfer_error;

static struct {
    uint32_t frames;
    uint32_t accepted;
    uint32_t duplicates;
    uint32_t malformed;
    uint32_t crc_errors;
    uint32_t gaps;
    uint32_t erases;
    uint32_t erase_us;
    uint32_t write_us;
} upload_stats;

/* One versioned image fits either 128 KiB user slot and needs at most 256 bitmap bytes. */
static struct {
    uint32_t id;
    uint32_t offset;
    uint32_t length;
    uint32_t expected_crc;
    uint16_t chunks;
    uint16_t received;
    uint8_t active;
    uint8_t failed;
    uint8_t complete;
    uint8_t bitmap[(IR_V2_MAX_CHUNKS + 7) / 8];
} v2_upload;

static int missing_index(uint32_t addr)
{
    for (uint8_t i = 0; i < missing_count; i++) {
        if (missing_addr[i] == addr)
            return i;
    }
    return -1;
}

static void record_gap(uint32_t from, uint32_t to, uint32_t chunk_size)
{
    // Keep a bounded repair list; an overflow makes this transfer incomplete.
    uint32_t count = (to - from) / chunk_size;
    uint32_t available = NB_MISSING_ADDR - missing_count;
    uint32_t retained = count < available ? count : available;

    for (uint32_t i = 0; i < retained; i++)
        missing_addr[missing_count++] = from + i * chunk_size;

    missing_total += count;
    upload_stats.gaps += count;
    if (retained != count)
        transfer_error = 1;
}

static void print_missing_list(void)
{
    printf("Missing addresses:");
    for (uint8_t i = 0; i < missing_count; i++)
        printf(" %lx", (unsigned long)missing_addr[i]);
    printf("\n");
}

static void time_erase_marker(void)
{
    time_reference_t timer;
    pogobot_stopwatch_reset(&timer);
    spiBeginErase4(FLASH_OK_OFFSET);
    upload_stats.erase_us += pogobot_stopwatch_get_elapsed_microseconds(&timer);
    upload_stats.erases++;
}

static void time_erase_sector(uint32_t addr)
{
    time_reference_t timer;
    pogobot_stopwatch_reset(&timer);
    erase_flash_sector(addr);
    upload_stats.erase_us += pogobot_stopwatch_get_elapsed_microseconds(&timer);
    upload_stats.erases++;
}

static void time_write(uint32_t addr, unsigned char *data, uint32_t length)
{
    time_reference_t timer;
    pogobot_stopwatch_reset(&timer);
    write_to_flash(addr, data, length);
    upload_stats.write_us += pogobot_stopwatch_get_elapsed_microseconds(&timer);
}

#ifndef IR_UPLOAD_FLASH_READ
// SPI flash is memory-mapped on the robot; tests can substitute a simulated NOR byte.
#define IR_UPLOAD_FLASH_READ(offset) \
    (*(volatile const uint8_t *)(uintptr_t)(SPIFLASH_BASE + (offset)))
#endif

static uint32_t v2_flash_crc32(uint32_t offset, uint32_t length)
{
    // Reflected CRC-32/ISO-HDLC, matching Python's zlib.crc32 over exact image bytes.
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < length; i++) {
        crc ^= IR_UPLOAD_FLASH_READ(offset + i);
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static uint8_t v2_command(uint8_t cmd)
{
    return cmd >= IR_V2_CMD_START && cmd <= IR_V2_CMD_ABORT;
}

static uint8_t exec_v2_frame(struct sfl_frame *frame)
{
    const uint8_t *p = frame->payload;
    uint32_t id;

    if (frame->cmd == IR_V2_CMD_START) {
        uint32_t address, offset, length, expected_crc;
        if (frame->payload_length != IR_V2_START_LENGTH ||
            p[0] != IR_V2_VERSION || p[1] != 0 ||
            ir_v2_read_u16(p + 14) != IR_V2_CHUNK_SIZE) {
            upload_stats.malformed++;
            return 0;
        }
        id = ir_v2_read_u32(p + 2);
        address = ir_v2_read_u32(p + 6);
        length = ir_v2_read_u32(p + 10);
        expected_crc = ir_v2_read_u32(p + 16);
        if (address < SPIFLASH_BASE || length == 0 || length > IR_V2_SLOT_SIZE) {
            upload_stats.malformed++;
            return 0;
        }
        offset = address - SPIFLASH_BASE;
        // Only whole user slots are valid destinations; subtraction avoids wraparound.
        if ((offset != IR_FLASH_START && offset != 0x60000u) ||
            offset >= SPIFLASH_SIZE || length > SPIFLASH_SIZE - offset ||
            length > IR_V2_SLOT_SIZE) {
            upload_stats.malformed++;
            return 0;
        }
        if (v2_upload.active && v2_upload.id == id &&
            v2_upload.offset == offset && v2_upload.length == length &&
            v2_upload.expected_crc == expected_crc)
            return 0;  // A repeated START preserves the completed bitmap.

        memset(&v2_upload, 0, sizeof(v2_upload));
        time_erase_marker();
        flash_state_partial = 0;
        partial_known = 0;
        // Erase all image sectors before accepting DATA, including a lost first chunk.
        uint32_t first = offset / SPIFLASH_SECTOR_SIZE;
        uint32_t last = (offset + length - 1) / SPIFLASH_SECTOR_SIZE;
        for (uint32_t sector = first; sector <= last; sector++)
            time_erase_sector(sector * SPIFLASH_SECTOR_SIZE);
        v2_upload.id = id;
        v2_upload.offset = offset;
        v2_upload.length = length;
        v2_upload.expected_crc = expected_crc;
        v2_upload.chunks = (length + IR_V2_CHUNK_SIZE - 1) / IR_V2_CHUNK_SIZE;
        v2_upload.active = 1;
        printf("IR v2 START: id=%08lx bytes=%lu chunks=%u\n",
               (unsigned long)id, (unsigned long)length,
               (unsigned int)v2_upload.chunks);
        return 0;
    }

    if (frame->payload_length < IR_V2_ID_LENGTH) {
        upload_stats.malformed++;
        return 0;
    }
    id = ir_v2_read_u32(p);
    if (!v2_upload.active || id != v2_upload.id)
        return 0;  // Stale frames must not affect an active image.

    if (frame->cmd == IR_V2_CMD_DATA) {
        uint16_t index;
        uint32_t relative, length, offset;
        uint8_t mask;
        if (frame->payload_length < IR_V2_DATA_HEADER_LENGTH + 1 ||
            frame->payload_length > IR_V2_DATA_HEADER_LENGTH + IR_V2_CHUNK_SIZE) {
            upload_stats.malformed++;
            return 0;
        }
        index = ir_v2_read_u16(p + 4);
        if (index >= v2_upload.chunks) {
            upload_stats.malformed++;
            return 0;
        }
        relative = (uint32_t)index * IR_V2_CHUNK_SIZE;
        length = v2_upload.length - relative;
        if (length > IR_V2_CHUNK_SIZE)
            length = IR_V2_CHUNK_SIZE;
        if (frame->payload_length != IR_V2_DATA_HEADER_LENGTH + length) {
            upload_stats.malformed++;
            return 0;
        }
        mask = 1u << (index & 7u);
        if (v2_upload.bitmap[index >> 3] & mask) {
            upload_stats.duplicates++;
            return 0;
        }
        if (v2_upload.failed || v2_upload.complete)
            return 0;
        offset = v2_upload.offset + relative;
        time_write(offset, &frame->payload[IR_V2_DATA_HEADER_LENGTH], length);
        // A failed NOR write cannot be repaired by programming more zero bits.
        for (uint32_t i = 0; i < length; i++) {
            if (IR_UPLOAD_FLASH_READ(offset + i) != p[IR_V2_DATA_HEADER_LENGTH + i]) {
                v2_upload.failed = 1;
                printf("IR v2 flash readback failed at %08lx; restart required\n",
                       (unsigned long)(offset + i));
                return 0;
            }
        }
        v2_upload.bitmap[index >> 3] |= mask;
        v2_upload.received++;
        upload_stats.accepted++;
        return 0;
    }

    if (frame->payload_length != IR_V2_ID_LENGTH) {
        upload_stats.malformed++;
        return 0;
    }
    if (frame->cmd == IR_V2_CMD_ABORT) {
        time_erase_marker();
        memset(&v2_upload, 0, sizeof(v2_upload));
        printf("IR v2 transfer aborted\n");
        return 1;
    }
    if (v2_upload.complete)
        return 1;  // Repeated END has no flash side effects.
    if (v2_upload.failed) {
        printf("IR v2 transfer failed; send ABORT then START to restart\n");
        return 1;
    }
    if (v2_upload.received != v2_upload.chunks) {
        printf("IR v2 incomplete: %u of %u chunks\n",
               (unsigned int)v2_upload.received, (unsigned int)v2_upload.chunks);
        return 0;
    }
    uint32_t actual_crc = v2_flash_crc32(v2_upload.offset, v2_upload.length);
    if (actual_crc != v2_upload.expected_crc) {
        v2_upload.failed = 1;
        printf("IR v2 image CRC mismatch: %08lx != %08lx; restart required\n",
               (unsigned long)actual_crc, (unsigned long)v2_upload.expected_crc);
        return 1;
    }
    time_write(FLASH_OK_OFFSET, (unsigned char *)FLASH_IS_OK, strlen(FLASH_IS_OK));
    v2_upload.complete = 1;
    printf("IR v2 image verified and complete\n");
    return 1;
}

uint8_t check_crc(struct sfl_frame* frame) {
	/* Check Frame CRC */
    int computed_crc;
    int received_crc;
	received_crc = ((int)frame->crc[0] << 8)|(int)frame->crc[1];
	computed_crc = crc16(&frame->cmd, frame->payload_length+1);
	return(computed_crc == received_crc);
}

void print_frame(struct sfl_frame* frame) {
    int i;
    uint32_t addr;
    printf("size: 0x%02x\n", frame->payload_length);
    printf("crc: 0x%04x\n", ((int)frame->crc[0] << 8)|(int)frame->crc[1]);
    printf("cmd: 0x%02x\ndata:", frame->cmd);
    addr = frame->payload_length >= 4 ? get_uint32(&frame->payload[0]) : 0;
    for( i=4; i<frame->payload_length; i++ ) {
        if( (i-4)%16 == 0 ) {
            printf("\n%08lx ", addr+i-4);
        }
        else {
            if( (i-4)%8 == 0 ) {
                printf(" ");
            }
        }
        printf(" %02x", frame->payload[i]);
    }
    printf("\n");
}

static uint8_t exec_frame_cmd(struct sfl_frame *frame)
{
    uint32_t addr;
    uint32_t offset;
    uint32_t length;
    int recovered;

    switch (frame->cmd) {
    case SFL_CMD_ABORT:
        time_erase_marker();
        missing_count = 0;
        missing_total = 0;
        partial_known = 0;
        started = 0;
        printf("IR transfer aborted\n");
        return 1;

    case SFL_CMD_LOAD:
        if (frame->payload_length <= 4) {
            upload_stats.malformed++;
            transfer_error = 1;
            break;
        }
        addr = get_uint32(&frame->payload[0]);
        length = frame->payload_length - 4;
        if (addr < SPIFLASH_BASE) {
            upload_stats.malformed++;
            transfer_error = 1;
            break;
        }
        offset = addr - SPIFLASH_BASE;
        if (offset < IR_FLASH_START || offset >= IR_FLASH_END ||
            offset >= SPIFLASH_SIZE || length > IR_FLASH_END - offset ||
            length > SPIFLASH_SIZE - offset) {
            upload_stats.malformed++;
            transfer_error = 1;
            break;
        }

        if (flash_state_partial) {
            recovered = missing_index(offset);
            if (recovered < 0) {
                upload_stats.duplicates++;
                break;
            }
            if (length != chunk_size) {
                upload_stats.malformed++;
                transfer_error = 1;
                break;
            }
        } else if (!started) {
            // Without an image manifest, a fresh transfer must start at a known slot base.
            if (offset != IR_FLASH_START && offset != 0x60000u) {
                upload_stats.malformed++;
                transfer_error = 1;
                break;
            }
            started = 1;
            next_addr = offset;
            chunk_size = length;
            time_erase_marker();
            recovered = -1;
        } else if (offset < next_addr) {
            recovered = missing_index(offset);
            if (recovered < 0) {
                upload_stats.duplicates++;
                break;
            }
            if (length != chunk_size) {
                upload_stats.malformed++;
                transfer_error = 1;
                break;
            }
        } else {
            recovered = -1;
        }

        if (recovered < 0 && offset > next_addr) {
            uint32_t gap = offset - next_addr;
            // The legacy stream has no length metadata for missing frames.
            // Its first data frame determines the only recoverable gap stride.
            if (gap % chunk_size != 0) {
                upload_stats.malformed++;
                transfer_error = 1;
                break;
            }
            record_gap(next_addr, offset, chunk_size);
        }

        if (!flash_state_partial) {
            // Erase each touched sector once, even when its boundary packet was lost.
            uint32_t first = (offset - IR_FLASH_START) / SPIFLASH_SECTOR_SIZE;
            uint32_t last = (offset + length - 1 - IR_FLASH_START) /
                            SPIFLASH_SECTOR_SIZE;
            for (uint32_t sector = first; sector <= last; sector++) {
                uint8_t bit = 1u << (sector & 7u);
                if (!(erased_sectors[sector >> 3] & bit)) {
                    time_erase_sector(IR_FLASH_START + sector * SPIFLASH_SECTOR_SIZE);
                    erased_sectors[sector >> 3] |= bit;
                }
            }
        }

        time_write(offset, &frame->payload[4], length);
        upload_stats.accepted++;
        if (recovered >= 0) {
            missing_addr[recovered] = missing_addr[--missing_count];
            missing_total--;
        } else {
            next_addr = offset + length;
        }
        break;

    case SFL_CMD_JUMP:
        time_erase_marker();
        if (!started || transfer_error || missing_total != 0) {
            printf("IR upload incomplete: %lu missing chunks%s\n",
                   (unsigned long)missing_total,
                   transfer_error ? ", transfer error" : "");
            if (missing_count)
                print_missing_list();
            if (started && !transfer_error && missing_total == missing_count &&
                missing_total <= NB_MISSING_ADDR) {
                time_write(FLASH_OK_OFFSET, (unsigned char *)FLASH_IS_PARTIAL,
                           strlen(FLASH_IS_PARTIAL));
                flash_state_partial = 1;
                partial_known = 1;
            } else {
                partial_known = 0;
            }
            return 1;
        }
        time_write(FLASH_OK_OFFSET, (unsigned char *)FLASH_IS_OK,
                   strlen(FLASH_IS_OK));
        missing_count = 0;
        missing_total = 0;
        partial_known = 0;
        printf("IR upload complete\n");
        return 1;

    default:
        upload_stats.malformed++;
        transfer_error = 1;
        break;
    }
    return 0;
}

void ir_boot_loop(void) {
    time_reference_t mytimer;
    uint32_t timeout = 2000000;  // The versioned mode allows longer erase preparation.
    struct sfl_frame * frame;
    message_t msg;
    slip_error_counter_s slip_start[IR_RX_COUNT];
    uint32_t ring_drop_start[IR_RX_COUNT];
    uint32_t queue_drop_start;
    uint32_t malformed_start;
    uint8_t stopped = 0;
    uint8_t legacy_blocked;

    memset(&upload_stats, 0, sizeof(upload_stats));
    next_addr = 0;
    memset(erased_sectors, 0, sizeof(erased_sectors));
    started = 0;
    transfer_error = 0;
    rgb_blink_set_time(5, 95);                      // Flash 5 ms every 100ms
    rgb_blink_set_color(0, 0, 50);                  // Tel the user data is being received

    flash_state_partial = check_flash_state(FLASH_IS_PARTIAL, FLASH_OK_OFFSET);
    legacy_blocked = check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET) ||
                     v2_upload.active;
    if (v2_upload.active)
        timeout = 8000000;
    pogobot_timer_init(&mytimer, timeout);

    if (flash_state_partial) {
        // The address list is RAM-only; a reboot cannot safely resume a partial image.
        if (!partial_known) {
            printf("Partial image has no recoverable address list; erase and restart upload\n");
            legacy_blocked = 1;
        } else {
            started = 1;
            missing_total = missing_count;
        }
    } else {
        partial_known = 0;
        missing_count = 0;
        missing_total = 0;
        chunk_size = 0;
    }

    queue_drop_start = pogobot_infrared_get_queue_drop_count();
    malformed_start = pogobot_infrared_get_malformed_count();
    for (uint8_t i = 0; i < IR_RX_COUNT; i++) {
        pogobot_infrared_get_receiver_error_counter(&slip_start[i], i);
        ring_drop_start[i] = ir_uart_rx_drop_count(i);
    }

    while(!pogobot_timer_has_expired(&mytimer)) {
        pogobot_infrared_update();
        rgb_blink();
        /* Get one Frame */
        if( pogobot_infrared_message_available() ) {
            pogobot_infrared_recover_next_message( &msg );
            if (msg.header._packet_type != ir_t_flash)
                continue;

            pogobot_timer_init(&mytimer, timeout);
            upload_stats.frames++;
            if (msg.header.payload_length < 4 ||
                msg.header.payload_length > sizeof(msg.payload)) {
                upload_stats.malformed++;
                transfer_error = 1;
                continue;
            }

            frame = (struct sfl_frame *)msg.payload;
            if (frame->payload_length != msg.header.payload_length - 4 ||
                (frame->cmd == SFL_CMD_JUMP && frame->payload_length != 4) ||
                (frame->cmd == SFL_CMD_ABORT && frame->payload_length != 0)) {
                upload_stats.malformed++;
                transfer_error = 1;
                continue;
            }
            if (!check_crc(frame)) {
                upload_stats.crc_errors++;
                continue;
            }
            if (v2_command(frame->cmd)) {
                legacy_blocked = 1;
                timeout = 8000000;
                if (exec_v2_frame(frame)) {
                    stopped = 1;
                    break;
                }
                // Start/erase may take time; wait for the host's preparation pause.
                pogobot_timer_init(&mytimer, timeout);
            } else if (!legacy_blocked && exec_frame_cmd(frame)) {
                stopped = 1;
                break;
            }
        }
	}
    if (!stopped)
        printf("IR upload timed out\n");

    printf("IR upload: frames=%lu written=%lu duplicates=%lu malformed=%lu "
           "inner_crc=%lu gaps=%lu erases=%lu erase_us=%lu write_us=%lu "
           "queue_drops=%lu outer_malformed=%lu\n",
           (unsigned long)upload_stats.frames, (unsigned long)upload_stats.accepted,
           (unsigned long)upload_stats.duplicates, (unsigned long)upload_stats.malformed,
           (unsigned long)upload_stats.crc_errors, (unsigned long)upload_stats.gaps,
           (unsigned long)upload_stats.erases, (unsigned long)upload_stats.erase_us,
           (unsigned long)upload_stats.write_us,
           (unsigned long)(pogobot_infrared_get_queue_drop_count() - queue_drop_start),
           (unsigned long)(pogobot_infrared_get_malformed_count() - malformed_start));
    for (uint8_t i = 0; i < IR_RX_COUNT; i++) {
        slip_error_counter_s current;
        pogobot_infrared_get_receiver_error_counter(&current, i);
        printf("IR RX%u: slip_crc=%lu slip_overflow=%lu slip_escape=%lu ring_drops=%lu\n",
               (unsigned int)i,
               (unsigned long)(current.crc_mismatch_counter - slip_start[i].crc_mismatch_counter),
               (unsigned long)(current.overflow_counter - slip_start[i].overflow_counter),
               (unsigned long)(current.unknown_escaped_byte_counter -
                               slip_start[i].unknown_escaped_byte_counter),
               (unsigned long)(ir_uart_rx_drop_count(i) - ring_drop_start[i]));
    }

    if (!flash_state_partial) {
        missing_count = 0;
        missing_total = 0;
    }
    rgb_blink_set_time(5, 995);
    update_led_status();
}
