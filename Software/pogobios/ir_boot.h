/**
 * POGOBOT
 *
 * Copyright © 2022 Sorbonne Université ISIR
 * This file is licensed under the Expat License, sometimes known as the MIT License.
 * Please refer to file LICENCE for details.
**/

#include <sfl.h>
#include <pogobot.h>

#define IR_MAGIC_REQ    "ir2PoG042ISIR"
#define FLASH_IS_OK     "FlashIsOK"
#define FLASH_IS_PARTIAL     "FlashIsPar"

void ir_boot_loop(void);
/* Enter the upload loop with an already received, CRC-checked v2 START. */
uint8_t ir_boot_try_v2_start(const message_t *message);
uint8_t check_crc(struct sfl_frame* frame);
void print_frame(struct sfl_frame* frame);
