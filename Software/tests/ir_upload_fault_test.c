/* Host fault injection for the production IR upload, UART ring, and SLIP code. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../pogolib/slip.h"

#define CSR_IR_TX_BASE 1
#define CSR_IR_RX0_BASE 1
#define IR_NUMBER 1
#define IR_RX_COUNT 1
#define IR_RX0_INTERRUPT 0
#define IR_RX1_INTERRUPT 0
#define IR_RX2_INTERRUPT 0
#define IR_RX3_INTERRUPT 0
#define SPIFLASH_BASE 0x200000u
#define SPIFLASH_SIZE 0x200000u
#define SPIFLASH_SECTOR_SIZE 65536u
#define SFL_CMD_LOAD 1
#define SFL_CMD_JUMP 2
#define SFL_CMD_ABORT 3
#define ir_t_flash 2

typedef struct { uint32_t hardware_value_at_time_origin; } time_reference_t;
typedef struct {
    uint8_t _packet_type;
    uint8_t _emitting_power_list;
    uint16_t _sender_id;
    uint8_t _sender_ir_index;
    uint8_t _receiver_ir_index;
    uint16_t payload_length;
} message_header_t;
typedef struct { message_header_t header; uint8_t payload[382]; } message_t;
struct sfl_frame {
    uint8_t payload_length;
    uint8_t crc[2];
    uint8_t cmd;
    uint8_t payload[255];
};

/* Minimal device interface called by the included production sources. */
uint16_t crc16(const unsigned char *data, unsigned int length);
uint32_t get_uint32(unsigned char *data);
void spiBeginErase4(uint32_t address);
void erase_flash_sector(uint32_t address);
void write_to_flash(uint32_t address, unsigned char *data, uint32_t length);
uint8_t check_flash_state(const char *marker, uint32_t address);
void pogobot_stopwatch_reset(time_reference_t *timer);
uint32_t pogobot_stopwatch_get_elapsed_microseconds(time_reference_t *timer);
void pogobot_timer_init(time_reference_t *timer, uint32_t timeout);
int pogobot_timer_has_expired(time_reference_t *timer);
void rgb_blink_set_time(int on, int off);
void rgb_blink_set_color(int r, int g, int b);
void rgb_blink(void);
void update_led_status(void);
void pogobot_infrared_update(void);
int pogobot_infrared_message_available(void);
void pogobot_infrared_recover_next_message(message_t *message);
uint32_t pogobot_infrared_get_queue_drop_count(void);
uint32_t pogobot_infrared_get_malformed_count(void);
void pogobot_infrared_get_receiver_error_counter(slip_error_counter_s *out, uint8_t channel);
unsigned int ir_rx0_ev_pending_read(void);
unsigned int ir_rx0_rxempty_read(void);
unsigned int ir_rx0_rx_read(void);
void ir_rx0_ev_pending_write(unsigned int value);
void ir_rx0_ev_enable_write(unsigned int value);
unsigned int irq_getie(void);
unsigned int irq_getmask(void);
void irq_setmask(unsigned int value);

#include "../pogolib/ir_uart.c"
#include "../pogolib/slip.c"
#include "../pogobios/ir_boot.c"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#define MAX_MESSAGES 1200
static message_t messages[MAX_MESSAGES];
static unsigned int message_count, message_read;
static uint8_t flash_bytes[SPIFLASH_SIZE];
static unsigned int sector_erases[SPIFLASH_SIZE / SPIFLASH_SECTOR_SIZE];
static unsigned int chunk_writes[SPIFLASH_SIZE / 64];
static uint32_t clock_us;
static uint8_t hardware_fifo[700];
static unsigned int hardware_count, hardware_read;
static uint8_t encoded[128];
static unsigned int encoded_count, decoded_count;

uint16_t crc16(const unsigned char *data, unsigned int length)
{
    uint16_t crc = 0xffff;
    for (unsigned int i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}

uint32_t get_uint32(unsigned char *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

void spiBeginErase4(uint32_t address)
{
    CHECK(address + 4096 <= SPIFLASH_SIZE);
    memset(&flash_bytes[address], 0xff, 4096);
}

void erase_flash_sector(uint32_t address)
{
    CHECK(address % SPIFLASH_SECTOR_SIZE == 0);
    CHECK(address < SPIFLASH_SIZE);
    memset(&flash_bytes[address], 0xff, SPIFLASH_SECTOR_SIZE);
    sector_erases[address / SPIFLASH_SECTOR_SIZE]++;
}

void write_to_flash(uint32_t address, unsigned char *data, uint32_t length)
{
    CHECK(address + length <= SPIFLASH_SIZE);
    for (uint32_t i = 0; i < length; i++)
        flash_bytes[address + i] &= data[i];  /* NOR programming cannot set a zero bit. */
    if (address >= IR_FLASH_START && address < IR_FLASH_END)
        chunk_writes[address / 64]++;
}

uint8_t check_flash_state(const char *marker, uint32_t address)
{
    return memcmp(&flash_bytes[address], marker, strlen(marker)) == 0;
}

void pogobot_stopwatch_reset(time_reference_t *timer) { timer->hardware_value_at_time_origin = clock_us; }
uint32_t pogobot_stopwatch_get_elapsed_microseconds(time_reference_t *timer)
{ return clock_us - timer->hardware_value_at_time_origin; }
void pogobot_timer_init(time_reference_t *timer, uint32_t timeout)
{ timer->hardware_value_at_time_origin = clock_us + timeout; }
int pogobot_timer_has_expired(time_reference_t *timer)
{ clock_us += 1000; return clock_us >= timer->hardware_value_at_time_origin; }
void rgb_blink_set_time(int on, int off) { (void)on; (void)off; }
void rgb_blink_set_color(int r, int g, int b) { (void)r; (void)g; (void)b; }
void rgb_blink(void) {}
void update_led_status(void) {}
void pogobot_infrared_update(void) {}
int pogobot_infrared_message_available(void) { return message_read < message_count; }
void pogobot_infrared_recover_next_message(message_t *message)
{ *message = messages[message_read++]; }
uint32_t pogobot_infrared_get_queue_drop_count(void) { return 0; }
uint32_t pogobot_infrared_get_malformed_count(void) { return 0; }
void pogobot_infrared_get_receiver_error_counter(slip_error_counter_s *out, uint8_t channel)
{ (void)channel; memset(out, 0, sizeof(*out)); }
unsigned int ir_rx0_ev_pending_read(void) { return hardware_read < hardware_count; }
unsigned int ir_rx0_rxempty_read(void) { return hardware_read == hardware_count; }
unsigned int ir_rx0_rx_read(void) { return hardware_fifo[hardware_read++]; }
void ir_rx0_ev_pending_write(unsigned int value) { (void)value; }
void ir_rx0_ev_enable_write(unsigned int value) { (void)value; }
unsigned int irq_getie(void) { return 0; }
unsigned int irq_getmask(void) { return 0; }
void irq_setmask(unsigned int value) { (void)value; }

static uint8_t pattern(unsigned int index, unsigned int byte)
{ return (uint8_t)(index * 17u + byte * 3u + 1u); }

static void append_frame(uint8_t command, uint32_t offset, unsigned int length)
{
    CHECK(message_count < MAX_MESSAGES && length <= 251);
    message_t *message = &messages[message_count++];
    struct sfl_frame *frame = (struct sfl_frame *)message->payload;
    memset(message, 0, sizeof(*message));
    message->header._packet_type = ir_t_flash;
    frame->cmd = command;
    frame->payload_length = command == SFL_CMD_LOAD ? length + 4 :
                            command == SFL_CMD_JUMP ? 4 : 0;
    if (command != SFL_CMD_ABORT) {
        uint32_t address = SPIFLASH_BASE + offset;
        frame->payload[0] = address >> 24;
        frame->payload[1] = address >> 16;
        frame->payload[2] = address >> 8;
        frame->payload[3] = address;
    }
    if (command == SFL_CMD_LOAD)
        for (unsigned int i = 0; i < length; i++)
            frame->payload[i + 4] = pattern((offset - 0x60000u) / 64, i);
    uint16_t crc = crc16(&frame->cmd, frame->payload_length + 1);
    frame->crc[0] = crc >> 8;
    frame->crc[1] = crc;
    message->header.payload_length = frame->payload_length + 4;
}

static void append_chunk(unsigned int index)
{ append_frame(SFL_CMD_LOAD, 0x60000u + index * 64u, 64); }

static void append_jump(void) { append_frame(SFL_CMD_JUMP, 0x60000u, 0); }

static void reset_messages(void) { message_count = message_read = 0; }

static void reset_device(void)
{
    memset(flash_bytes, 0xff, sizeof(flash_bytes));
    memset(sector_erases, 0, sizeof(sector_erases));
    memset(chunk_writes, 0, sizeof(chunk_writes));
    reset_messages();
    clock_us = 0;
    partial_known = 0;  /* A fresh test starts with a fresh robot boot. */
    ir_uart_init();
}

static void assert_chunks(unsigned int first, unsigned int end)
{
    for (unsigned int chunk = first; chunk < end; chunk++)
        for (unsigned int byte = 0; byte < 64; byte++)
            CHECK(flash_bytes[0x60000u + chunk * 64u + byte] == pattern(chunk, byte));
}

static void test_complete_image(void)
{
    reset_device();
    for (unsigned int i = 0; i < 960; i++) append_chunk(i);  /* 60 KiB */
    append_jump();
    ir_boot_loop();
    CHECK(check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));
    CHECK(upload_stats.accepted == 960);
    CHECK(sector_erases[0x60000u / SPIFLASH_SECTOR_SIZE] == 1);
    assert_chunks(0, 960);
}

static void test_ten_missing_and_duplicates(void)
{
    reset_device();
    for (unsigned int i = 0; i < 30; i++)
        if (i < 2 || i >= 12) append_chunk(i);
    append_chunk(12);  /* Repeated complete chunk must not be rewritten. */
    append_jump();
    ir_boot_loop();
    CHECK(!check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));
    CHECK(check_flash_state(FLASH_IS_PARTIAL, FLASH_OK_OFFSET));
    CHECK(missing_count == 10 && missing_total == 10);
    CHECK(chunk_writes[(0x60000u + 12 * 64u) / 64] == 1);

    reset_messages();
    for (unsigned int i = 2; i < 12; i++) append_chunk(i);
    append_chunk(2);  /* Repeated recovered chunk must not be counted twice. */
    append_jump();
    ir_boot_loop();
    CHECK(check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));
    CHECK(upload_stats.accepted == 10 && upload_stats.duplicates == 1);
    CHECK(chunk_writes[(0x60000u + 2 * 64u) / 64] == 1);
    assert_chunks(0, 30);
}

static void test_thirty_one_missing(void)
{
    reset_device();
    append_chunk(0);
    append_chunk(32);
    append_jump();
    ir_boot_loop();
    CHECK(missing_total == 0);  /* Invalid sessions discard their RAM repair list. */
    CHECK(upload_stats.gaps == 31);
    CHECK(!check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));
    CHECK(!check_flash_state(FLASH_IS_PARTIAL, FLASH_OK_OFFSET));
}

static void test_timeout_and_abort(void)
{
    reset_device();
    append_chunk(0);
    append_chunk(1);
    ir_boot_loop();  /* No JUMP; the simulated timer expires. */
    CHECK(!check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));
    reset_messages();
    append_chunk(0);
    append_frame(SFL_CMD_ABORT, 0, 0);
    ir_boot_loop();
    CHECK(!check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));
    reset_messages();
    for (unsigned int i = 0; i < 3; i++) append_chunk(i);
    append_jump();
    ir_boot_loop();
    CHECK(check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));
    assert_chunks(0, 3);
}

static void test_invalid_frames(void)
{
    reset_device();
    append_chunk(1);  /* Lost first chunk cannot establish a fresh transfer. */
    append_jump();
    ir_boot_loop();
    CHECK(!check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));

    reset_device();
    append_frame(SFL_CMD_LOAD, 0x30000u, 64);  /* Bootloader destination. */
    append_jump();
    ir_boot_loop();
    CHECK(!check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));
    CHECK(sector_erases[0x30000u / SPIFLASH_SECTOR_SIZE] == 0);

    reset_device();
    append_chunk(0);
    ((struct sfl_frame *)messages[0].payload)->crc[0] ^= 1;
    append_jump();
    ir_boot_loop();
    CHECK(upload_stats.crc_errors == 1);
    CHECK(!check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));

    reset_device();
    append_chunk(0);
    messages[0].header.payload_length--;
    append_jump();
    ir_boot_loop();
    CHECK(upload_stats.malformed == 1);
    CHECK(!check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));

    reset_device();
    append_frame(99, 0, 0);  /* Unsupported SFL command. */
    append_jump();
    ir_boot_loop();
    CHECK(upload_stats.malformed == 1);
    CHECK(!check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));
}

static void test_missing_sector_boundary(void)
{
    reset_device();
    for (unsigned int i = 0; i < 1024; i++) append_chunk(i);
    append_chunk(1025);  /* Chunk 1024 at 0x70000 is lost. */
    append_jump();
    ir_boot_loop();
    CHECK(check_flash_state(FLASH_IS_PARTIAL, FLASH_OK_OFFSET));
    CHECK(sector_erases[0x70000u / SPIFLASH_SECTOR_SIZE] == 1);
    reset_messages();
    append_chunk(1024);
    append_jump();
    ir_boot_loop();
    CHECK(check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));
    assert_chunks(1023, 1026);
}

static void test_frame_crossing_sector_boundary(void)
{
    reset_device();
    /* Simulate old contents in the second sector, as on a reused robot. */
    memset(&flash_bytes[0x70000u], 0, SPIFLASH_SECTOR_SIZE);
    for (unsigned int i = 0; i < 1023; i++) append_chunk(i);
    append_frame(SFL_CMD_LOAD, 0x6ffc0u, 128);
    append_jump();
    ir_boot_loop();
    CHECK(check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));
    CHECK(sector_erases[0x70000u / SPIFLASH_SECTOR_SIZE] == 1);
    for (unsigned int byte = 0; byte < 128; byte++)
        CHECK(flash_bytes[0x6ffc0u + byte] == pattern(1023, byte));
}

static void test_uart_full_ring(void)
{
    ir_uart_init();
    hardware_count = 600;
    hardware_read = 0;
    for (unsigned int i = 0; i < hardware_count; i++) hardware_fifo[i] = i;
    ir_uart_rx_isr();
    CHECK(hardware_read == hardware_count);
    CHECK(ir_uart_rx_drop_count(0) == 89);  /* 512-byte ring holds 511 bytes. */
    for (unsigned int i = 0; i < 511; i++) {
        CHECK(ir_uart_read_nonblock(0));
        CHECK((uint8_t)ir_uart_read(0) == (uint8_t)i);
    }
    CHECK(!ir_uart_read_nonblock(0));
    hardware_count = 1;
    hardware_read = 0;
    hardware_fifo[0] = 42;
    ir_uart_rx_isr();
    CHECK(ir_uart_read_nonblock(0) && ir_uart_read(0) == 42);
}

static uint8_t collect_byte(uint8_t byte)
{
    CHECK(encoded_count < sizeof(encoded));
    encoded[encoded_count++] = byte;
    return 1;
}

static void got_packet(uint8_t *data, uint32_t size, void *tag)
{
    (void)tag;
    CHECK(size == 5 && data[0] == 1 && data[1] == 2 && data[2] == 3);
    decoded_count++;
}

static void test_slip_overflow_resync(void)
{
    uint8_t buffer[12];
    slip_receive_state_s receiver = {
        .buf = buffer, .buf_size = sizeof(buffer), .recv_message = got_packet,
        .crc_seed = 0xffffffffu
    };
    CHECK(slip_receive_init(&receiver) == SLIP_NO_ERROR);
    for (unsigned int i = 0; i < 30; i++)
        slip_decode_received_byte(&receiver, 0x55);
    CHECK(receiver.error_counter.overflow_counter > 0);
    slip_decode_received_byte(&receiver, SLIP_SPECIAL_BYTE_END);

    uint8_t payload[3] = {1, 2, 3};
    slip_send_descriptor_s sender = {.crc_seed = 0xffffffffu, .write_byte = collect_byte};
    encoded_count = decoded_count = 0;
    CHECK(slip_send_message(&sender, payload, sizeof(payload), 3) == SLIP_NO_ERROR);
    for (unsigned int i = 0; i < encoded_count; i++)
        slip_decode_received_byte(&receiver, encoded[i]);
    CHECK(decoded_count == 1);
}

static void demonstrate_legacy_protocol_limits(void)
{
    reset_device();
    for (unsigned int i = 0; i < 3; i++) append_chunk(i);
    append_jump();  /* The intended fourth/final chunk has no on-wire manifest. */
    ir_boot_loop();
    CHECK(check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));
    puts("KNOWN LIMIT: a missing final chunk is not detectable");

    reset_device();
    append_chunk(0);
    append_chunk(2);
    append_jump();
    ir_boot_loop();
    CHECK(check_flash_state(FLASH_IS_PARTIAL, FLASH_OK_OFFSET));
    reset_messages();
    append_chunk(1);
    struct sfl_frame *replacement = (struct sfl_frame *)messages[0].payload;
    replacement->payload[4] ^= 0xff;  /* Different image, valid frame CRC. */
    uint16_t crc = crc16(&replacement->cmd, replacement->payload_length + 1);
    replacement->crc[0] = crc >> 8;
    replacement->crc[1] = crc;
    append_jump();
    ir_boot_loop();
    CHECK(check_flash_state(FLASH_IS_OK, FLASH_OK_OFFSET));
    CHECK(flash_bytes[0x60040u] != pattern(1, 0));
    puts("KNOWN LIMIT: a same-address repair can belong to a different image");

    reset_device();
    append_chunk(0);
    append_chunk(2);
    append_jump();
    ir_boot_loop();
    CHECK(check_flash_state(FLASH_IS_PARTIAL, FLASH_OK_OFFSET));
    partial_known = 0;  /* Simulate a robot reboot losing the RAM address list. */
    reset_messages();
    append_chunk(1);
    append_jump();
    ir_boot_loop();
    CHECK(check_flash_state(FLASH_IS_PARTIAL, FLASH_OK_OFFSET));
    CHECK(message_read == 0);
    puts("KNOWN LIMIT: partial repair cannot resume after robot reboot");
}

int main(void)
{
    test_complete_image();
    test_ten_missing_and_duplicates();
    test_thirty_one_missing();
    test_timeout_and_abort();
    test_invalid_frames();
    test_missing_sector_boundary();
    test_frame_crossing_sector_boundary();
    test_uart_full_ring();
    test_slip_overflow_resync();
    demonstrate_legacy_protocol_limits();
    puts("IR upload host fault injection: PASS");
    return 0;
}
