#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bl_usart.h"
#include "ringbuffer.h"
#include "crc16.h"
#include "crc32.h"
#include "tim_delay.h"
#include "stm32flash.h"
#include "borad.h"
#include "key.h"
#include "key_desc.h"
#include "led.h"
#include "led_desc.h"
#include "magic_header.h"
#define PACKET_SIZE_MAX (4 + PAYLOAD_SIZE_MAX + 2) // header(1) + opcode(1) + length(2) + payload + crc16(2)
#define RX_BUFFER_SIZE (5 * 1024)
#define RX_TIMEOUT_MS 20
#define BL_VERSION "0.0.1"
#define PAYLOAD_SIZE_MAX (4096 + 8) // 4096 program data + 8 bytes for address and size
#define APP_BASE_ADDRESS 0x08010000
#define BL_ADDRESS 0x08000000
#define BL_SIZE (48 * 1024) // 48KB bootloader size
#define BOOT_DELAY 3000     // 3S boot delay
typedef enum
{
    PACKET_STATE_HEADER,
    PACKET_STATE_OPCODE,
    PACKET_STATE_LENGTH,
    PACKET_STATE_PAYLOAD,
    PACKET_STATE_CRC16,
} packet_state_machine_t;

typedef enum
{
    PACKET_OPCODE_INQUERY = 0x01,
    PACKET_OPCODE_ERASE = 0x81,
    PACKET_OPCODE_PROGRAM = 0x82,
    PACKET_OPCODE_VERIFY = 0x33,
    PACKET_OPCODE_BOOT = 0x22,
    PACKET_OPCODE_RESET = 0x23,
} packet_opcode_t;

typedef enum
{
    INQUERY_SUBCODE_VERSION = 0x00,
    INQUERY_SUBCODE_MTU = 0x01,
} packet_inquery_subcode_t;

typedef enum
{
    RESPONSE_ERRORCODE_OK = 0,
    RESPONSE_ERRORCODE_OPCODE,
    RESPONSE_ERRORCODE_OVERFLOW,
    RESPONSE_ERRORCODE_TIMEOUT,
    RESPONSE_ERRORCODE_FORMAT,
    RESPONSE_ERRORCODE_VERIFY,
    RESPONSE_ERRORCODE_PARAM,
    RESPONSE_ERRORCODE_UNKNOWN = 0xff,
} packet_errcode_t;

static uint8_t rb_buffer[RX_BUFFER_SIZE];
static rb_t rxrb;
static uint8_t packet_buffer[PACKET_SIZE_MAX];
static uint32_t packet_index;
static packet_opcode_t packet_opcode;
static uint16_t packet_payload_length;
static packet_state_machine_t packet_state = PACKET_STATE_HEADER;
static bool application_validate(void)
{
    if (!magic_header_validate())
    {
        printf("Magic header invalid\n");
        return false;
    }

    uint32_t addr = magic_header_get_address();
    uint32_t size = magic_header_get_length();
    uint32_t crc  = magic_header_get_crc32();
    uint32_t ccrc = crc32((const uint8_t *)addr, size);
    if (crc != ccrc)
    {
        printf("Application CRC32 mismatch: expected %08X, got %08X\n", crc, ccrc);
        return false;
    }

    printf("Application validated OK\n");
    return true;
}
static void boot_application(void)
{
    if(!application_validate())
    {
        printf("Application validate failed,catnot boot\n");
        return;
    }
    printf("Booting  application...\n");
    tim_delay_ms(2);
    led_off(led1);
    TIM_DeInit(TIM6);
    USART_DeInit(USART1);
    USART_DeInit(USART3);
    NVIC_DisableIRQ(TIM6_DAC_IRQn);
    NVIC_DisableIRQ(USART3_IRQn);
    SCB->VTOR = APP_BASE_ADDRESS;
    extern void JumpApp(uint32_t base);
    JumpApp(APP_BASE_ADDRESS);
}
static void bl_response(packet_opcode_t opcode, packet_errcode_t errcode, const uint8_t *data, uint16_t length)
{
    uint8_t *response = packet_buffer;
    response[0] = 0x55; // Response header
    response[1] = opcode;
    response[2] = errcode;
    response[3] = length & 0xFF;
    response[4] = (length >> 8) & 0xFF;
    if (length > 0)
        memcpy(&response[5], data, length);
    uint16_t crc = crc16(response, 5 + length);
    response[5 + length] = crc & 0xFF;
    response[6 + length] = (crc >> 8) & 0xFF;
    bl_usart_write(response, 7 + length);
}
static void bl_opcode_inquery_handler(void)
{
    printf("INQUERY handler\n");
    if (packet_payload_length != 1)
    {
        printf("INQUERY should have no payload, but got %u bytes\n", packet_payload_length);
        return;
    }
    uint8_t subcode = packet_buffer[4];
    switch (subcode)
    {
    case INQUERY_SUBCODE_VERSION:
        bl_response(PACKET_OPCODE_INQUERY, RESPONSE_ERRORCODE_OK, (const uint8_t *)BL_VERSION, strlen(BL_VERSION));
        break;
    case INQUERY_SUBCODE_MTU:
    {
        uint8_t bmtu[2] = {PAYLOAD_SIZE_MAX & 0xFF, (PAYLOAD_SIZE_MAX >> 8) & 0xFF};
        bl_response(PACKET_OPCODE_INQUERY, RESPONSE_ERRORCODE_OK, (const uint8_t *)bmtu, sizeof(bmtu));
        break;
    }
    default:
        printf("Unknown INQUERY subcode: %02X\n", subcode);
        break;
    }
}

static void bl_opcode_reset_handler(void)
{
    printf("reset handler...\n");
    bl_response(PACKET_OPCODE_RESET, RESPONSE_ERRORCODE_OK, NULL, 0);
    printf("System resetting...\n");
    tim_delay_ms(2);
    NVIC_SystemReset();
}

static void bl_opcode_boot_handler(void)
{
    printf("boot handler...\n");
    bl_response(PACKET_OPCODE_BOOT, RESPONSE_ERRORCODE_OK, NULL, 0);
}

static void bl_opcode_erase_handler(void)
{
    printf("erase handler\n");
    uint32_t address = 0, size = 0;

    if (packet_payload_length != 8)
    {
        printf("ERASE should have 8 bytes payload, but got %u bytes\n", packet_payload_length);
        bl_response(PACKET_OPCODE_ERASE, RESPONSE_ERRORCODE_FORMAT, NULL, 0);
        return;
    }
    address = (packet_buffer[7] << 24) | (packet_buffer[6] << 16) | (packet_buffer[5] << 8) | packet_buffer[4];
    size = (packet_buffer[11] << 24) | (packet_buffer[10] << 16) | (packet_buffer[9] << 8) | packet_buffer[8];

    if (address >= BL_ADDRESS && address < BL_ADDRESS + BL_SIZE)
    {
        printf("address %08X is protected\n", address);
        bl_response(PACKET_OPCODE_ERASE, RESPONSE_ERRORCODE_PARAM, NULL, 0);
        return;
    }
    printf("Erase request: address=0x%08X, size=%u\n", address, size);

    stm32_flash_unlock();
    stm32_flash_erase(address, size);
    stm32_flash_lock();
    bl_response(PACKET_OPCODE_ERASE, RESPONSE_ERRORCODE_OK, NULL, 0);
}
static void bl_opcode_program_handler(void)
{
    printf("program handler\n");
    uint32_t address = 0, size = 0;
    if (packet_payload_length < 8)
    {
        printf("PROGRAM should have at least 8 bytes payload, but got %u bytes\n", packet_payload_length);
        bl_response(PACKET_OPCODE_PROGRAM, RESPONSE_ERRORCODE_FORMAT, NULL, 0);
        return;
    }

    address = (packet_buffer[7] << 24) | (packet_buffer[6] << 16) | (packet_buffer[5] << 8) | packet_buffer[4];
    size = (packet_buffer[11] << 24) | (packet_buffer[10] << 16) | (packet_buffer[9] << 8) | packet_buffer[8];
    uint8_t *data = &packet_buffer[12];
    if (address >= BL_ADDRESS && address < BL_ADDRESS + BL_SIZE)
    {
        printf("address %08X is protected\n", address);
        bl_response(PACKET_OPCODE_PROGRAM, RESPONSE_ERRORCODE_PARAM, NULL, 0);
        return;
    }
    if (size != packet_payload_length - 8)
    {
        printf("PROGRAM size mismatch: expected %u, got %u\n", size, packet_payload_length - 8);
        bl_response(PACKET_OPCODE_PROGRAM, RESPONSE_ERRORCODE_FORMAT, NULL, 0);
        return;
    }
    printf("Program request: address=0x%08X, size=%u\n", address, size);
    stm32_flash_unlock();
    stm32_flash_program(address, data, size);
    stm32_flash_lock();
    bl_response(PACKET_OPCODE_PROGRAM, RESPONSE_ERRORCODE_OK, NULL, 0);
}

static void bl_opcode_verify_handler(void)
{
    printf("verify handler\n");
    uint32_t address = 0, size = 0;
    if (packet_payload_length != 12)
    {
        printf("VERIFY should have 12 bytes payload, but got %u bytes\n", packet_payload_length);
        bl_response(PACKET_OPCODE_VERIFY, RESPONSE_ERRORCODE_PARAM, NULL, 0);
        return;
    }
    address = (packet_buffer[7] << 24) | (packet_buffer[6] << 16) | (packet_buffer[5] << 8) | packet_buffer[4];
    size = (packet_buffer[11] << 24) | (packet_buffer[10] << 16) | (packet_buffer[9] << 8) | packet_buffer[8];
    uint32_t crc = (packet_buffer[15] << 24) | (packet_buffer[14] << 16) | (packet_buffer[13] << 8) | packet_buffer[12];
    if (address < STM32_FLASH_BASE || address + size > STM32_FLASH_BASE + STM32_FLASH_SIZE)
    {
        printf("address %08X is protected\n", address);
        bl_response(PACKET_OPCODE_VERIFY, RESPONSE_ERRORCODE_PARAM, NULL, 0);
        return;
    }
    printf("Verify request: address=0x%08X, size=%u, crc32=%08X\n", address, size, crc);
    uint32_t ccrc = crc32((const uint8_t *)address, size);
    if (ccrc != crc)
    {
        printf("Verify failed: expected %08X, got %08X\n", crc, ccrc);
        bl_response(PACKET_OPCODE_VERIFY, RESPONSE_ERRORCODE_VERIFY, NULL, 0);
    }
    else
    {
        printf("Verify OK\n");
        bl_response(PACKET_OPCODE_VERIFY, RESPONSE_ERRORCODE_OK, NULL, 0);
    }
}
static void bl_packet_handler(void)
{
    switch (packet_opcode)
    {
    case PACKET_OPCODE_INQUERY:
        bl_opcode_inquery_handler();
        printf("Inquery received\n");
        break;

    case PACKET_OPCODE_ERASE:
        bl_opcode_erase_handler();
        printf("Erase received\n");
        break;

    case PACKET_OPCODE_PROGRAM:
        bl_opcode_program_handler();
        printf("Program received\n");
        break;

    case PACKET_OPCODE_VERIFY:
        bl_opcode_verify_handler();
        printf("Verify received\n");
        break;
    case PACKET_OPCODE_BOOT:
        bl_opcode_boot_handler();
        printf("Boot received\n");
        break;
    case PACKET_OPCODE_RESET:
        bl_opcode_reset_handler();
        printf("Reset received\n");
        break;

    default:
        printf("Unknown opcode received\n");
        break;
    }
}

static bool bl_byte_handler(uint8_t byte)
{
    bool full_packet = false;
    static uint64_t last_byte_ms;
    uint64_t now_ms = tim_get_ms();
    if (now_ms - last_byte_ms > RX_TIMEOUT_MS)
    {
        if (packet_state != PACKET_STATE_HEADER)
        {
            printf("Packet timeout, reset state machine\n");
            packet_index = 0;
            packet_state = PACKET_STATE_HEADER;
        }
    }
    last_byte_ms = now_ms;
    // printf("Recv: %02X\n", byte);
    packet_buffer[packet_index++] = byte;
    switch (packet_state)
    {
    case PACKET_STATE_HEADER:
        if (packet_buffer[0] == 0xAA)
        {
            printf("Header OK\n");
            packet_state = PACKET_STATE_OPCODE;
        }
        else
        {
            packet_index = 0;
            packet_state = PACKET_STATE_HEADER;
        }
        break;
    case PACKET_STATE_OPCODE:
        if (packet_buffer[1] == PACKET_OPCODE_INQUERY ||
            packet_buffer[1] == PACKET_OPCODE_ERASE ||
            packet_buffer[1] == PACKET_OPCODE_PROGRAM ||
            packet_buffer[1] == PACKET_OPCODE_VERIFY ||
            packet_buffer[1] == PACKET_OPCODE_BOOT ||
            packet_buffer[1] == PACKET_OPCODE_RESET)
        {
            printf("Opcode OK:%02X\n", packet_buffer[1]);
            packet_opcode = (packet_opcode_t)packet_buffer[1];
            packet_state = PACKET_STATE_LENGTH;
        }
        else
        {
            packet_index = 0;
            packet_state = PACKET_STATE_HEADER;
        }
        break;
    case PACKET_STATE_LENGTH:
        if (packet_index == 4)
        {
            uint16_t payload_length = (packet_buffer[3] << 8) | packet_buffer[2];
            if (payload_length <= PACKET_SIZE_MAX)
            {
                printf("Length OK:%u\n", payload_length);
                packet_payload_length = payload_length;
                if (payload_length > 0)
                {
                    packet_state = PACKET_STATE_PAYLOAD;
                }
                else
                {
                    packet_state = PACKET_STATE_CRC16;
                }
            }
            else
            {
                packet_index = 0;
                packet_state = PACKET_STATE_HEADER;
            }
        }
        break;
    case PACKET_STATE_PAYLOAD:
        if (packet_index == 4 + packet_payload_length)
        {

            printf("Payload Received OK\n");
            packet_state = PACKET_STATE_CRC16;
        }
        break;
    case PACKET_STATE_CRC16:
        if (packet_index == 4 + packet_payload_length + 2)
        {
            uint16_t crc = (packet_buffer[4 + packet_payload_length + 1] << 8) |
                           packet_buffer[4 + packet_payload_length];
            uint16_t ccrc = crc16(packet_buffer, 4 + packet_payload_length);
            if (crc == ccrc)
            {
                full_packet = true;
                printf("crc16 ok:%04X\n", ccrc);
                printf("Packet complete: opcode=0x%2X, length=%u\n", packet_opcode, packet_payload_length);
                // printf("Payload: ");
                // for (uint16_t i = 0; i < packet_payload_length; i++)
                // {
                //     printf("%02X ", packet_buffer[4 + i]);
                // }
                printf("\n");
            }
            else
            {
                printf("crc16 error: expected %04X, got %04X\n", crc, ccrc);
            }
            packet_index = 0;
            packet_state = PACKET_STATE_HEADER;
        }
        break;

    default:
        break;
    }
    return full_packet;
}

static void bl_usart_rx_handler(const uint8_t *data, uint32_t length)
{
    rb_puts(rxrb, data, length);
}
static bool key_trap_check(void)
{
    printf("key_trap_check: key_read=%d\n", key_read(key1));  // 加这行
    for (uint32_t t = 0; t < BOOT_DELAY; t += 10)
    {
        tim_delay_ms(10);
        if (!key_read(key1))
        {
            printf("key released at t=%lu\n", t);  // 加这行
            return false;
        }
    }
    printf("key pressed, trap into boot\n");
    return true;
}

static void wait_key_release(void)
{
    while (key_read(key1))
        tim_delay_ms(10);
}

static bool key_press_check(void)
{
    if (!key_read(key1))
        return false;

    tim_delay_ms(10);
    if (!key_read(key1))
        return false;

    return true;
}

bool magic_header_trap_boot(void)
{

    if (!magic_header_validate())
    {
        printf("Magic header invalid, skip trap\n");
        return true; // 魔术头不合法，进入bootloader
    }


    if (!application_validate())
    {
        printf("Application invalid, trap into bootloader\n");
        return true; // 应用程序不合法，进入bootloader
    }


    return false;
}

void bootloader_main(void)
{
    printf("Bootloader started.\r\n");
    rxrb = rb_new(rb_buffer, RX_BUFFER_SIZE);
    bl_usart_init();
    bl_usart_register_rx_callback(bl_usart_rx_handler);

    key_init(key1);
    bool trapboot = key_trap_check();
    if(!trapboot)
    {
        trapboot = magic_header_trap_boot();
    }
    if (!trapboot)
    {
        boot_application();;
    }
    led_init(led1);
    led_on(led1);
    wait_key_release();
    while (1)
    {

        if (key_press_check())
        {
            printf("key pressed,rebooting...\n");
            NVIC_SystemReset();
        }
        if (!rb_empty(rxrb))
        {
            uint8_t byte;
            rb_get(rxrb, &byte);
            if (bl_byte_handler(byte))
            {
                bl_packet_handler();
            }
        }
    }
}
