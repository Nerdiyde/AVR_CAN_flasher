/*                                 _   _                 _  _               _
                                 | \ | |               | |(_)             | |
  __      ____      ____      __ |  \| |  ___  _ __  __| | _  _   _     __| |  ___
  \ \ /\ / /\ \ /\ / /\ \ /\ / / | . ` | / _ \| '__|/ _` || || | | |   / _` | / _ \
   \ V  V /  \ V  V /  \ V  V /_ | |\  ||  __/| |  | (_| || || |_| | _| (_| ||  __/
    \_/\_/    \_/\_/    \_/\_/(_)|_| \_| \___||_|   \__,_||_| \__, |(_)\__,_| \___|
                                                               __/ |
                                                              |___/
     avr_can_flasher.h by Fabian Steppat
     Infos on www.nerdiy.de

     The underlying library is mostly based on the awesome work of Peter Müller <peter@crycode.de> (https://crycode.de)
     It is ported from his "Flash application for MCP-CAN-Boot".
     More info is available here:
     - Bootloader: https://github.com/crycode-de/mcp-can-boot
     - Flash application: https://github.com/crycode-de/mcp-can-boot-flash-app

     Huge thanks to Peter Müller for making this available!

     License: CC BY-NC-SA 4.0
*/


#ifndef AVR_CAN_FLASHER_H
#define AVR_CAN_FLASHER_H

#include <Arduino.h>
#include "SPIFFS.h"
#include "FS.h"

#define ACF_BOOTLOADER_CMD_VERSION 0x01

#define ACF_CAN_DATA_BYTE_MCU_ID_MSB 0
#define ACF_CAN_DATA_BYTE_MCU_ID_LSB 1
#define ACF_CAN_DATA_BYTE_CMD 2
#define ACF_CAN_DATA_BYTE_LEN_AND_ADDR 3

#define ACF_CAN_ID_MCU_TO_REMOTE_DEFAULT 0x1FFFFF01
#define ACF_CAN_ID_REMOTE_TO_MCU_DEFAULT 0x1FFFFF02

#define ACF_CMD_BOOTLOADER_START 0b00000010
#define ACF_CMD_FLASH_INIT 0b00000110               // remote -> mcu
#define ACF_CMD_FLASH_READY 0b00000100              // mcu -> remote
#define ACF_CMD_FLASH_SET_ADDRESS 0b00001010        // remote -> mcu
#define ACF_CMD_FLASH_ADDRESS_ERROR 0b00001011      // mcu -> remote
#define ACF_CMD_FLASH_DATA 0b00001000               // remote -> mcu
#define ACF_CMD_FLASH_DATA_ERROR 0b00001101         // mcu -> remote
#define ACF_CMD_FLASH_DONE 0b00010000               // remote -> mcu
#define ACF_CMD_FLASH_DONE_VERIFY 0b01010000        // remote <-> mcu
#define ACF_CMD_FLASH_ERASE 0b00100000              // remote -> mcu
#define ACF_CMD_FLASH_READ 0b01000000               // remote -> mcu
#define ACF_CMD_FLASH_READ_DATA 0b01001000          // mcu -> remote
#define ACF_CMD_FLASH_READ_ADDRESS_ERROR 0b01001011 // mcu -> remote
#define ACF_CMD_START_APP 0b10000000                // mcu <-> remote

#define ACF_STATE_INIT 0
#define ACF_STATE_FLASHING 1
#define ACF_STATE_READING 2

#define ACF_HEX_FILE_RECORD_TYPE_END_OF_LINE 0x01
#define ACF_READ_FILE_CHUNK_SIZE 179 // read four "standard" lines of a intel hex file are 179 characters long (including whitespaces)

//#define DETAILED_OUTPUT_HEX_FILE_READING // uncomment this to get (very) detailed debug output during hex file reading and parsing. (This increases parsing time a lot.)
//#define DETAILED_OUTPUT_CAN_MESSAGE_RECEIVE // uncomment this to get (very) detailed debug output of the received can messages. (This increases flashing time a lot.)
//#define DETAILED_OUTPUT_VERIFICATION        // uncomment this to get (very) detailed debug output during hex file verifcation. (This increases verification time a lot.)
//#define DETAILED_OUTPUT_FLASHING            // uncomment this to get (very) detailed debug output during hex file flashing. (This increases flashing time a lot.)

#ifndef ARDUINO_ARCH_ESP32
#error This library requires to be run on the ESP32 architecture
#endif

extern "C"
{
    typedef struct
    {
        uint32_t id = 0;
        uint8_t data[8] = {0};
        uint8_t data_length = 0;
    } acf_can_message;
}

class ACF
{
public:
    ACF(void (*cs_function_pointer)(uint32_t, uint8_t *, uint8_t));

    void acf_handle_can_msg(acf_can_message msg);

    boolean acf_start_flash_process(String file_string,
                                    uint32_t mcuId,
                                    String partno,
                                    uint32_t reset_can_id = 0,
                                    String reset_can_message = "null",
                                    boolean doErase = false,
                                    uint16_t doRead = 0,
                                    boolean doReset = true,
                                    boolean doVerify = true,
                                    boolean forceFlashing = false,
                                    uint32_t canIdRemote = ACF_CAN_ID_REMOTE_TO_MCU_DEFAULT,
                                    uint32_t canIdMcu = ACF_CAN_ID_MCU_TO_REMOTE_DEFAULT);

private:
    typedef struct
    {
        uint8_t byte_count = 0;
        uint16_t address = 0;
        uint8_t data[16] = {0};
        uint8_t record_type = 0;
        uint8_t checksum = 0;
    } intel_hex_map_line;

    void (*can_send_function_pointer)(uint32_t, uint8_t *, uint8_t);
    uint32_t mcuId = 0;
    uint8_t doErase = false;
    uint8_t doRead = false;
    uint8_t doVerify = false;
    uint8_t forceFlashing = false;
    uint8_t state = ACF_STATE_INIT;
    uint32_t deviceSignature = 0;
    uint32_t curAddr = 0;      // current flash address
    uint32_t flashStartTs = 0; // timestamp of the flash start
    String partno;
    uint32_t can_id_remote_to_mcu = 0;
    uint32_t can_id_mcu_to_remote = 0;
    String file_string;
    uint32_t memMapCurrentLine = 0;
    uint16_t memMapCurrentDataIdx = 0;
    uint8_t *readDataArr;
    uint8_t *hexFileBytes;
    intel_hex_map_line *hexMapLines;
    uint16_t memMaplinesNum = 0;

    void acf_read_for_verify();
    void acf_read_done();
    void acf_send_start_app();
    void acf_on_flash_ready(uint8_t msgData[]);
    String acf_convert_to_hex_string(uint32_t num, uint8_t minLength);
    String acf_convert_to_hex_string(uint32_t num);
    uint32_t acf_get_device_signature(String partno);
    void acf_can_send_data(uint32_t can_id, uint8_t reset_acf_can_message[], uint8_t data_count);
    void acf_can_send_data(uint32_t can_id, String can_data_string, uint8_t data_count);
    boolean acf_intel_hex_checksum_is_valid(String line);
    void acf_parse_intel_hex_file_string(intel_hex_map_line *hexMap);
    uint32_t acf_convert_hex_string_to_int(String hex_string);
    String acf_convert_data_array_to_intel_hex_string(uint8_t *readDataArr);
};

#endif