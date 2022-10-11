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
#include <ir_uart.h>
#include <libbase/crc.h>
#include <libbase/spiflash.h>
#include <pogobot.h>

static unsigned int success_addr, ptr;
const char * ir_magic_req = IR_MAGIC_REQ;
static unsigned int missing_packet = 0;

//extern uint32_t get_uint32(unsigned char* data); // Defined in boot.c

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
    addr = get_uint32(&frame->payload[0]);
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

static unsigned int exec_frame_cmd(struct sfl_frame* frame)
{
    unsigned int addr=0;
    char * flash_ok = FLASH_IS_OK;
	/* Execute Frame CMD and return flashed address (or reboot) */
	switch(frame->cmd) {
		case SFL_CMD_LOAD:
			addr = get_uint32(&frame->payload[0]);
			// Check if we try to write to flash, and not overwrite the bootloader
			if( (addr >= SPIFLASH_BASE+0x40000) & (addr < SPIFLASH_BASE+SPIFLASH_SIZE) ) {
                addr -= SPIFLASH_BASE; // addr is now just the offset in flash
                // Flash only if not already flashed before (duplicated messages)
                if(addr != success_addr) {
                    //rgb_set(10,0,10);
                    success_addr = addr;
                    // Only erase if address is on a sector boundary.
                    if ((addr & ~(SPIFLASH_SECTOR_SIZE - 1) ) == addr) {
                        printf("Erasing sector 0x%" PRIxPTR "\n", addr);
                        erase_flash_sector(addr);
                        ptr = addr;
                        if((addr == 0x40000) | ( addr == 0x60000 )) {
                            printf("Erasing flashOK flag\n");
                            spiBeginErase4(FLASH_OK_OFFSET);
                        }
                    }
                    if( ptr != addr) {
                        printf("Error : Non contiguous flashing, expected: %" PRIxPTR ", actual address: %" PRIxPTR "\n", ptr, addr);
                        missing_packet++;
                    }
                    write_to_flash(addr, (unsigned char *)&frame->payload[4], frame->payload_length - 4);
                    ptr=addr+frame->payload_length - 4;  // Should be the next address to flash
                    //printf("Flashed address %08x\n", addr);
                    //rgb_set(0,0,0);
                }
			}
            break;
			
		case SFL_CMD_JUMP: 
            if (missing_packet > 0)
            {
                printf("missing %d packets", missing_packet);
                return 1;
            }
            /* Flash successful, write magic value to enable autorun */
            write_to_flash(FLASH_OK_OFFSET, (unsigned char *)flash_ok, strlen(flash_ok));
            printf("Rebooting to user image\n");
            msleep(100);
            rgb_set(64,0,0);    // If program is bad, LED will stay RED
            reboot_ctrl_write(0xac | 1);
			break;

		default:
			printf("CMD not recognized: %d\n", frame->cmd);
			break;
	}
    return 0;
}

void ir_boot_loop(void) {
    time_reference_t mytimer;
    uint32_t timeout = 10000000;  // in microseconds
    struct sfl_frame * frame;
    message_t msg;
    missing_packet = 0;
    success_addr=0;
    rgb_blink_set_time(5, 95);                      // Flash 5 ms every 100ms
    rgb_blink_set_color(0, 0, 50);                  // Tel the user data is being received

    pogobot_timer_init(&mytimer, timeout);          // Set a timeout

    while(!pogobot_timer_has_expired(&mytimer)) {
        pogobot_infrared_update();
        rgb_blink();
        /* Get one Frame */
        if( pogobot_infrared_message_available() ) {
            pogobot_timer_init(&mytimer, timeout);         // Reset timer each time we receive data
            pogobot_infrared_recover_next_message( &msg );
            if (msg.header._packet_type == ir_t_flash)
            {
            
                //printf("Received length : %d, [%s]\n", msg.header.payload_length, msg.payload);
                frame = (struct sfl_frame*) msg.payload;
                if(check_crc(frame)) {
                    if(exec_frame_cmd(frame) != 0) {
                        printf("exec_frame_cmd failed\n");
                        break;
                    }
                }
                else {
                    printf("CRC Error after address 0x%08x\n", success_addr);
                    print_frame(frame);
                }
                  
            }
        }
	}
    rgb_blink_set_time(5, 995);
    rgb_blink_set_color(0, 50, 0);
}