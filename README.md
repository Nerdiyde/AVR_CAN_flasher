# AVR CAN flasher

This is a port for the ESP32 of the "MCP-CAN-Boot Flash-App" from Peter Müller.
More information about this is available here: 
https://github.com/crycode-de/mcp-can-boot-flash-app
 

## The bootloader

More information about the bootloader are available in the official repository: [https://github.com/crycode-de/mcp-can-boot](https://github.com/crycode-de/mcp-can-boot)

## Requirements
* ESP32 (Tested on an ESP32 Wrover-B)
* Connected CAN transceiver. (Tested with an MCP2515.)
* Enough free RAM on the ESP32 to load and parse the target hex file.

## Usage
Please see the example "flash_hex_via_can.ino" in the example folder.
The hex file needs to be stored in the SPIFFS of your ESP32. Please see https://github.com/me-no-dev/arduino-esp32fs-plugin for further information about how you can upload data to the SPIFFS.


## Known issues and testing state
### Tested and known to be working:
* Flashing
* Verification
### Not tested:
* Reading only of hex files
* Different hex file sizes
* Other AVR MCUs than Atmega 328P
* Extended Frame Format

### Test environment
* ESP32 incl. its integrated ESP32SJA1000 and an externally connected MCP2551 CAN tranceiver
* Atmega 328P incl. MCP2515 and MCP2551
* Hex file size: 70.569 Bytes
* Standard CAN frame format
* Target MCU reset via reset pin ()

##TODO
* Test Extended frame format
* Integrate alternative "progress bar" for status report via serial

## License

**CC BY-NC-SA 4.0**

[Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International](https://creativecommons.org/licenses/by-nc-sa/4.0/)