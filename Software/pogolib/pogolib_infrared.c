/**
 * POGOBOT
 *
 * Copyright © 2022 Sorbonne Université ISIR
 * This file is licensed under the Expat License, sometimes known as the MIT License.
 * Please refer to file LICENCE for details.
**/


#include <alloca.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h> // memcpy()

#include "pogobot.h"

/* Infrared communication */

/* clang-format-ok */

#ifdef CSR_IR_RX0_BASE

uint8_t _selected_power = pogobot_infrared_emitter_power_oneThird;

/* ******************************** ******************************** */

/* slip */
#define SLIP_BUF sizeof( message_t ) + 8
void on_complete_valid_slip_packet_received( uint8_t *data, uint32_t size,
                                             void *tag );
uint8_t write_byte_via_a_four_byte_word_channel( uint8_t byte );

static uint8_t buf[IR_RX_COUNT][SLIP_BUF];

static slip_receive_state_s slip_receive_state[IR_RX_COUNT];

const slip_send_descriptor_s slip_send_descriptor = {
    .crc_seed = 0xFFFFFFFFul,
    .write_byte = write_byte_via_a_four_byte_word_channel,
};

/* ******************************** ******************************** */

static message_t bufmem_mes[NUMEL];
Messagefifo my_mes_fifo;
Messagefifo *my_mes_fifo_p = &my_mes_fifo;

/* ******************************** ******************************** */

void
pogobot_infrared_ll_init( void )
{
    ir_init();
    FifoBuffer_init( my_mes_fifo_p, NUMEL, message_t, bufmem_mes );

    slip_send_init( &slip_send_descriptor );

    slip_receive_state_s slip_receive_state_prototype = {
        .recv_message = on_complete_valid_slip_packet_received,
        .crc_seed = 0xFFFFFFFFul };

    for ( int i = 0; i < IR_RX_COUNT; i++ )
    {
        slip_receive_state[i] = slip_receive_state_prototype;
        slip_receive_state[i].buf = buf[i];
        slip_receive_state[i].buf_size = sizeof( buf[i] );
        slip_receive_state[i].tag = (void *)i;
        slip_receive_init( &( slip_receive_state[i] ) );
    }
}

/**
 * Checks if a message is available
 */
int
pogobot_infrared_message_available( void )
{
    return ( !FifoBuffer_is_empty( my_mes_fifo_p ) );
}

/**
 * Recovers the next message received by Infrared
 */
void
pogobot_infrared_recover_next_message( message_t *mes )
{
    FifoBuffer_read( my_mes_fifo_p, *mes );
}

/**
 * Clear the IR message queue
 */
void
pogobot_infrared_clear_message_queue( void )
{
    FifoBuffer_flush( my_mes_fifo_p );
}

/**
 * Set the power used to send the IR message
 */
void 
pogobot_infrared_set_power( uint8_t power )
{
    uint8_t tmp_power = power;
    if (power > pogobot_infrared_emitter_power_max)
        tmp_power = pogobot_infrared_emitter_power_max;
    if (power < pogobot_infrared_emitter_power_null)
        tmp_power = pogobot_infrared_emitter_power_null;

    _selected_power = tmp_power;
}

typedef union multi_width_pointer_t
{
    uint8_t *w8b;
    uint16_t *w16b;
    uint32_t *w32b;
} multi_width_pointer_t;

uint32_t
pogobot_infrared_sendRawLongMessage( message_t *const message )
{
    if ( message->header.payload_length > MAX_PAYLOAD_SIZE_BYTES )
    {
        return 1;
    }

    message->header._packet_type = ir_t_user; // User packets have type 16.

    message->header._sender_id = pogobot_helper_getid();
    message->header._receiver_ir_index = 0;

    // TODO: this currently emits packet via several IR at the same time (or
    // does it?). What this should to is emit packet several times, via one IR
    // at a time, with field _emitting_power_list reflecting which TX is used.
    // This will be necessary later to have robots detect each other and their
    // relative orientations.

    unsigned int mask = 0;

    for ( int index = 0; index < IR_RX_COUNT; index++ )
    {
        uint8_t power = ( message->header._emitting_power_list >>
                          ( pogobot_infrared_emitter_width_bits * index ) ) &
                        ( ( 1 << pogobot_infrared_emitter_width_bits ) - 1 );
        IRn_conf_tx_power_write( index, power );

        if ( power > 0 )
        {
            mask |= ( 1 << index );
        }
    }

    ir_tx_conf_tx_mask_write( mask );
    slip_send_message( &( slip_send_descriptor ), (uint8_t *)message,
                       sizeof( message_header_t ) +
                           message->header.payload_length, message->header.payload_length );

    return 0;
}

uint32_t 
pogobot_infrared_sendRawShortMessage( ir_direction dir, short_message_t *const message )
{
    if ( message->header.payload_length > MAX_PAYLOAD_SIZE_BYTES )
    {
        return 1;
    }

    message->header._packet_type = ir_t_short; // User packets have type 3.

    unsigned int mask = 0;
    uint8_t emitting_power_list = 0;


    if (dir < ir_all)
    {
        emitting_power_list = _selected_power << (dir * pogobot_infrared_emitter_width_bits );
    } else {
        emitting_power_list = pogobot_infrared_emitting_power_list(_selected_power, _selected_power, _selected_power, _selected_power);
    }
    
    for ( int index = 0; index < IR_RX_COUNT; index++ )
    {
        uint8_t power = ( emitting_power_list >>
                          ( pogobot_infrared_emitter_width_bits * index ) ) &
                        ( ( 1 << pogobot_infrared_emitter_width_bits ) - 1 );
        IRn_conf_tx_power_write( index, power );

        if ( power > 0 )
        {
            mask |= ( 1 << index );
        }
    }

    ir_tx_conf_tx_mask_write( mask );
    slip_send_message( &( slip_send_descriptor ), (uint8_t *)message,
                       sizeof( message_short_header_t ) +
                           message->header.payload_length, message->header.payload_length );

    return 0;
}

uint32_t
pogobot_infrared_sendLongMessage_uniSpe( ir_direction dir,
                                         uint8_t *message,
                                         uint16_t message_size )
{

    message_t m;
    m.header._emitting_power_list =
        _selected_power << ( pogobot_infrared_emitter_width_bits * dir );
    m.header.payload_length = message_size;
    m.header._sender_ir_index = dir;
    memcpy( m.payload, message, message_size );

    return pogobot_infrared_sendRawLongMessage( &m );
}

uint32_t
pogobot_infrared_sendLongMessage_omniGen( uint8_t *message,
                                          uint16_t message_size )
{

    message_t m;
    m.header._emitting_power_list =
        pogobot_infrared_emitting_power_list(_selected_power, _selected_power, _selected_power, _selected_power);
    m.header.payload_length = message_size;
    m.header._sender_ir_index = ir_all;
    memcpy( m.payload, message, message_size );

    return pogobot_infrared_sendRawLongMessage( &m );
}

uint32_t
pogobot_infrared_sendLongMessage_omniSpe( uint8_t *message,
                                          uint16_t message_size )
{

    int i = 0;
    int error = 0;
    message_t m;
    m.header.payload_length = message_size;
    memcpy( m.payload, message, message_size );

    for ( i = 0; i < IR_RX_COUNT; i++ )
    {
        m.header._emitting_power_list =
            _selected_power << ( pogobot_infrared_emitter_width_bits * i );
        m.header._sender_ir_index = i;
        error += pogobot_infrared_sendRawLongMessage( &m );
    }

    return error;
}

uint32_t
pogobot_infrared_sendShortMessage_uni( ir_direction dir,
                                       uint8_t *message,
                                       uint16_t message_size )
{

    short_message_t m;
    m.header.payload_length = message_size;
    memcpy( m.payload, message, message_size );

    return pogobot_infrared_sendRawShortMessage( dir, &m );
}

uint32_t
pogobot_infrared_sendShortMessage_omni( uint8_t *message,
                                        uint16_t message_size )
{

    short_message_t m;
    m.header.payload_length = message_size;
    memcpy( m.payload, message, message_size );

    return pogobot_infrared_sendRawShortMessage( ir_all, &m );
}

uint8_t
write_byte_via_a_four_byte_word_channel( uint8_t byte )
{
    static uint8_t counter = 0;
    static uint32_t send = 0x0;

    // printf("TX: %02X\n", byte);

    /* writes a byte inside the word */
    ( (uint8_t *)&send )[counter] = byte;

    // end of word or end of message
    if ( counter == 3 || byte == SLIP_SPECIAL_BYTE_END )
    {
        if ( byte == SLIP_SPECIAL_BYTE_END )
        {
            // printf(" SLIP_SPECIAL_BYTE_END; counter %d \n", counter);
            // recopy SLIP_SPECIAL_BYTE_END in the whole word
            for ( int i = counter; i < 4; i++ )
            {
                ( (uint8_t *)&send )[i] = SLIP_SPECIAL_BYTE_END;
            }
        }
        //printf (" ready to send %x \n", send);

        // IRn_tx_tx_write is lower level than
        // IRn_tx_write_word_buffer, does not set emitting power each
        // time, less access to wishbone, better performance.
        //ir_tx_tx_write( send );
        
        while( ir_tx_txfull_read() == 1 );  // Wait until FIFO is not full
        for(int i=0; i<sizeof(uint32_t); i++)
        {
            ir_tx_tx_write(((uint8_t *)&send)[i]);     // Direct
        }
        ir_tx_conf_tx_trig_write(1);

        counter = 0;
        send = 0x0;
    }
    else
    {
        counter++;
    }

    // Returning zero would mean overflow.  The IR buffer never overflows so we
    // always return 1.
    return 1;
}

void
on_complete_valid_slip_packet_received( uint8_t *data, uint32_t size,
                                        void *tag )
{
    // double verification of payload size
    // the double is placed inside the 2 last bytes of the received data 
    uint16_t d_payload_size = 0;
    d_payload_size |= data[size-2] << 8;
    d_payload_size |= data[size-1];
    //printf("d_payload_size = %d\n", d_payload_size);

    //if the message type is "ir_t_short", we reconstruct a long message to avoid pb after
    //we choose extrem false information 
    if (data[0] == ir_t_short) {
        message_t m; 
        short_message_t *ms = (short_message_t*)( data );
        m.header._packet_type = ms->header._packet_type;
        m.header._emitting_power_list = 0; // power all to 0 shouldn't emit something
        m.header._sender_id = 0xFF; 
        m.header._sender_ir_index = 0xF; // index not possible
        m.header._receiver_ir_index = (int)tag;
        m.header.payload_length = ms->header.payload_length;

        //printf("payload_size = %d\n", m.header.payload_length);

        if (ms->header.payload_length <= MAX_PAYLOAD_SIZE_BYTES && ms->header.payload_length == d_payload_size)
        {
            //printf("double verif ok \n");
            memcpy( m.payload, ms->payload, m.header.payload_length);

            /* when a message arrives, it is put into the FIFO */
            if ( !FifoBuffer_is_full( my_mes_fifo_p ) )
            {
                FifoBuffer_write( my_mes_fifo_p, m );
            }
        }

    } else {
        message_t *m = (message_t *)( data );
        m->header._receiver_ir_index = (int)tag;
        // printf("new message from %d \n", m->header._receiver_ir_index);

        // filter mesage from type ir_t_cmd (command message)
        if ( m->header._packet_type == ir_t_cmd)
        {
            //if stop message reboot on pogobios
            int ret = strncmp("DEADBEEF", (char*)(m->payload), 8);
            if (ret == 0)
            {
                reboot_ctrl_write(0xac);
            }
        }

        //printf("payload_size = %d\n", m->header.payload_length);

        if (m->header.payload_length == d_payload_size)
        {
            //printf("double verif ok \n");
            /* when a message arrives, it is put into the FIFO */
            if ( !FifoBuffer_is_full( my_mes_fifo_p ) )
            {
                FifoBuffer_write( my_mes_fifo_p, *m );
            }
        }
        
    }

}

void
pogobot_infrared_update( void )
{
    for ( int ir_i = 0; ir_i < IR_RX_COUNT; ir_i++ )
    {
        int counter = 0;
        //printf( "infra %d, counter %d\n", ir_i, counter);

        //while ( IRn_rx_rxempty_read( ir_i ) != 1 && counter < MAX_NUMBER_OF_WORD )
        while ( ir_uart_read_nonblock(ir_i) != 0 && counter < (MAX_NUMBER_OF_WORD*sizeof(uint32_t)) )
        {
            counter ++;
            //uint32_t recd = IRn_rx_read( ir_i );
            char recd = ir_uart_read(ir_i);
            //printf( "R= %s \n", (char*)(&recd) );

            int i = 0;
            //for ( i = 0; i < sizeof( uint32_t ); i++ )
            {
                slip_error_t error =
                    slip_decode_received_byte( &( slip_receive_state[ir_i] ),
                                               ( (uint8_t *)( &recd ) )[i] );
                if ( error > 0 )
                {
#ifdef SLIP_DEBUG
                    printf( "( from %d ) slip_decode_received_byte error : ",
                            ir_i );
                    switch ( error )
                    {
                    case SLIP_ERROR_BUFFER_OVERFLOW:
                        printf( "SLIP_ERROR_BUFFER_OVERFLOW \n" );
                        break;
                    case SLIP_ERROR_UNKNOWN_ESCAPED_BYTE:
                        printf( "SLIP_ERROR_UNKNOWN_ESCAPED_BYTE \n" );
                        break;
                    case SLIP_ERROR_CRC_MISMATCH:
                        printf( "SLIP_ERROR_CRC_MISMATCH \n" );
                        break;

                    default:
                        printf( "unknown error \n" );
                        break;
                    }
#endif
                }
            }
        }
    }
}

void 
pogobot_infrared_get_receiver_error_counter( slip_error_counter_s *error_counter, uint8_t ir_index )
{
    slip_get_error_counter( &(slip_receive_state[ir_index]), error_counter);
}

void 
pogobot_infrared_reset_receiver_error_counter( void )
{
    for (int i = 0; i < IR_RX_COUNT; i++)
    {
        slip_reset_error_counter( &(slip_receive_state[i]) );
    }
    
}
#endif
