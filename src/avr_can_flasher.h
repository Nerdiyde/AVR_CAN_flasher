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
#define ACF_READ_FILE_CHUNK_SIZE 179 // read four "standard" lines of an intel hex file. Four lines are 179 characters long (including whitespaces).

//#define DETAILED_OUTPUT_HEX_FILE_READING // uncomment this to get (very) detailed debug output during hex file reading and parsing. (This increases parsing time a lot.)
//#define DETAILED_OUTPUT_CAN_MESSAGE_RECEIVE // uncomment this to get (very) detailed debug output of the received can messages. (This increases flashing time a lot.)
//#define DETAILED_OUTPUT_VERIFICATION        // uncomment this to get (very) detailed debug output during hex file verifcation. (This increases verification time a lot.)
//#define DETAILED_OUTPUT_FLASHING            // uncomment this to get (very) detailed debug output during hex file flashing. (This increases flashing time a lot.)

#ifndef ARDUINO_ARCH_ESP32
#error This library requires to be run on the ESP32 architecture!
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

    boolean acf_handle_can_msg(acf_can_message msg);
    String acf_convert_to_hex_string(uint32_t num, uint8_t minLength);
    String acf_convert_to_hex_string(uint32_t num);
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
                                    uint32_t canIdMcu = ACF_CAN_ID_MCU_TO_REMOTE_DEFAULT,
                                    boolean printSimpleProgress = false);
    void acf_stop_flash_process();
    uint32_t acf_wait_for_bootloader_response_duration();
    boolean acf_bootloader_responded();
    boolean acf_flash_process_finished();    
    boolean acf_verification_finished();

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

    void acf_read_for_verify();
    void acf_read_done();
    void acf_send_start_app();
    void acf_on_flash_ready(uint8_t msgData[]);
    uint32_t acf_get_device_signature(String partno);
    void acf_can_send_data(uint32_t can_id, uint8_t reset_acf_can_message[], uint8_t data_count);
    void acf_can_send_data(uint32_t can_id, String can_data_string, uint8_t data_count);
    boolean acf_intel_hex_checksum_is_valid(String line);
    void acf_parse_intel_hex_file_string(intel_hex_map_line *hexMap);
    uint32_t acf_convert_hex_string_to_int(String hex_string);
    String acf_convert_data_array_to_intel_hex_string(uint8_t *readDataArr);

    uint32_t mcuId = 0;                         // ID of the target device/MCU.
    uint8_t doErase = false;                    // Erase the flash before writing.
    uint8_t doRead = false;                     // Do not flash the HEX file. Just read it.
    uint8_t doVerify = false;                   // Execute verification after the flash process was finished.
    uint8_t forceFlashing = false;              // Force flashing in case the expected bootloader version is unequal to the returned bootloader version. 
    uint8_t state = ACF_STATE_INIT;             // Variable for the flashing state machine.
    uint32_t deviceSignature = 0;               // Device signature of the target device/MCU.
    uint32_t curAddr = 0;                       // Current flash address.
    uint32_t flashStartTs = 0;                  // Timestamp of the flash start.
    String partno = "";                         // Specified part number of the target device/MCU.
    uint32_t can_id_remote_to_mcu = 0;          // This holds the CAN ID that is used to identify CAN messages that are sent from the flash app to the target device/MCU.
    uint32_t can_id_mcu_to_remote = 0;          // This holds the CAN ID that is used to identify CAN messages that are sent from the the target device/MCU to the flash app.
    String file_string = "";                    // Variable that holds the filename of the HEX file saved in the SPIFFs.
    uint32_t memMapCurrentLine = 0;             // Pointer variable for the current line in the parsed intel HEX file.
    uint16_t memMapCurrentDataIdx = 0;          // Pointer variable for the current byte in the parsed intel HEX file.
    uint8_t *readDataArr;                   
    intel_hex_map_line *hexMapLines;            // Pointer to the intel_hex_map_line struct that holds the contents of the parsed HEX file.
    uint16_t memMaplinesNum = 0;                // Number of lines in the parsed HEX file.
    boolean printSimpleProgress = false;        // If this is set to true the process debug output is simplified.
    uint32_t waitingForBootloaderDuration = 0;  // Holds the timestamp of the moment when the reset request was sent to the target device/MCU.
    boolean flashingFinished = false;           // This is true as soon as the flash process was finished.
    boolean verificationFinished = false;       // This is true as soon as the verification process was finished.
};

#endif