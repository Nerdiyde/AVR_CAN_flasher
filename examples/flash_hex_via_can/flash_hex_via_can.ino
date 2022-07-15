/*                                 _   _                 _  _               _
                                 | \ | |               | |(_)             | |
  __      ____      ____      __ |  \| |  ___  _ __  __| | _  _   _     __| |  ___
  \ \ /\ / /\ \ /\ / /\ \ /\ / / | . ` | / _ \| '__|/ _` || || | | |   / _` | / _ \
   \ V  V /  \ V  V /  \ V  V /_ | |\  ||  __/| |  | (_| || || |_| | _| (_| ||  __/
    \_/\_/    \_/\_/    \_/\_/(_)|_| \_| \___||_|   \__,_||_| \__, |(_)\__,_| \___|
                                                               __/ |
                                                              |___/
     avr_can_flasher example by Fabian Steppat
     Infos on www.nerdiy.de

     The underlying library is mostly based on the awesome work of Peter Müller <peter@crycode.de> (https://crycode.de)
     It is ported from his "Flash application for MCP-CAN-Boot". 
     More info is available here: 
     - Bootloader: https://github.com/crycode-de/mcp-can-boot
     - Flash application: https://github.com/crycode-de/mcp-can-boot-flash-app   

     Huge thanks to Peter Müller for making this available!
    
     License: CC BY-NC-SA 4.0
*/


//CAN
#include <CAN.h>

#define CAN_RX_PIN 4 // CAN RX pin to use. 4 is the standard CAN RX pin on the ESP32.
#define CAN_TX_PIN 5 // CAN RX pin to use. 5 is the standard CAN TX pin on the ESP32.
#define CAN_BITRATE 500E3
//#define DETAILED_OUTPUT_CAN_MESSAGE_SEND // uncomment this to get detailed output about the send data that was send via CAN
boolean can_initialized = false;

//UART
#define BAUDRATE 115200 
String serialReceiveBuffer = "";

//AVR can flasher
#include <avr_can_flasher.h>
ACF flasher = ACF(&can_send_data); // Initialize the flasher and pass the function pointer of the function that will handle the sending of can data
acf_can_message msg;
boolean can_new_message = false;

// Here you can define the details about the target that will be flashed
#define HEX_FILE_NAME "/mcs.hex"        // filename of the hexfile that must be available in the spiffs. Make sure that the "/" is included in the filename 
#define MCU_ID 0x7A                   // id of the mcu which should be specified in the config of the bootloader.
#define MCU_PART_NO "m328p"             // device string of the target device
#define CAN_ID_TO_TRIGGER_RESET_OF_MCU 0x012
#define CAN_MESSAGE_TO_TRIGGER_RESET_OF_MCU "0x7A"
#define DO_ERASE_BEFORE_FLASHING false // If this is true the flash contents of the target MCU will be erased before flashing
#define READ_FLASH_CONTENTS_ONLY false // If this is activated the hex contents (of the AVR) will be read and saved to SPIFFS
#define DO_RESET_BEFORE_FLASHING true // If this is true the specified CAN message is sent before flashing to trigger an reset of the target MCU. This would activate the bootloader and the flashing process would start without externally triggered reset of the target mcu.
#define DO_VERIFY_AFTER_FLASHING true // If this is true the flashed contents will be verified after flashing is finished.
#define FORCE_FLASHING false // if this is activated flashing will be executed even if the read hex file is not valid
#define CAN_ID_MCU_TO_REMOTE 0x1F1 // CAN ID that is used to identify CAN messages that are send from the target MCU to the flash app
#define CAN_ID_REMOTE_TO_MCU 0x1F2 // CAN ID that is used to identify CAN messages that are send from the flash app to the target MCU
#define PRINT_SIMPLE_PROGRESS_TO_SERIAL true  // If this is true the serial output during is simplified.

void setup()
{
  Serial.begin(BAUDRATE);
  init_can();
  Serial.println("RAM statistics before hex file parsing:");
  print_ram_statistics();
  
  boolean success = flasher.acf_start_flash_process(
                      HEX_FILE_NAME,
                      MCU_ID,
                      MCU_PART_NO,
                      CAN_ID_TO_TRIGGER_RESET_OF_MCU,
                      CAN_MESSAGE_TO_TRIGGER_RESET_OF_MCU,
                      DO_ERASE_BEFORE_FLASHING,
                      READ_FLASH_CONTENTS_ONLY,
                      DO_RESET_BEFORE_FLASHING,
                      DO_VERIFY_AFTER_FLASHING,
                      FORCE_FLASHING,
                      CAN_ID_REMOTE_TO_MCU,
                      CAN_ID_MCU_TO_REMOTE,
                      PRINT_SIMPLE_PROGRESS_TO_SERIAL);

  Serial.println("RAM statistics after hex file parsing:");
  print_ram_statistics();

  if (success)
    Serial.println("Successfully started flash process. Please perform a reset on the AVR MCU to start the bootloader (if not already done).");
  else
    Serial.println("Starting of flash process failed.");

}

void loop()
{
  serial_messages_handle();
  if (can_new_message)
  {
    flasher.acf_handle_can_msg(msg);
    can_new_message = false;
  }
}


void print_ram_statistics()
{
  Serial.print("Total heap: ");
  Serial.println(ESP.getHeapSize());
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Total PSRAM: ");
  Serial.println(ESP.getPsramSize());
  Serial.print("Total PSRAM: ");
  Serial.println(ESP.getFreePsram());
}


void serial_messages_handle()
{
  while (Serial.available())
  {
    char character = (char)Serial.read();
    if (character == 13) {
      if (serialReceiveBuffer == "res")
      {
        restart_mcu();
      }
      serialReceiveBuffer = "";
    } else
    {
      serialReceiveBuffer += character;
    }
  }
}

void restart_mcu()
{
  ESP.restart();
}

void init_can()
{
  CAN.setPins(CAN_RX_PIN, CAN_TX_PIN);
  can_initialized = CAN.begin(CAN_BITRATE);

  if (!can_initialized)
  {
    Serial.println("Failed to initialize CAN phy.");
  } else
  {
    Serial.println("Successfully initialized CAN phy.");
    CAN.onReceive(can_on_receive);
  }
}

void can_on_receive(int can_packet_size)
{
  // read all received bytes and save it in the buffer
  uint8_t can_buffer[can_packet_size];
  uint8_t counter = 0;
  while (CAN.available())
  {
    can_buffer[counter] = CAN.read();
    counter++;
  }
  // convert the received can data to the acf_can_message struct format
  msg.id = CAN.packetId();
  msg.data_length = can_packet_size;
  for (uint8_t i = 0; i < can_packet_size; i++)
    msg.data[i] = can_buffer[i];

  // pass the prepared acf_can_message struct to the acf handler
  can_new_message = true;
}


void can_send_data(uint32_t can_id, uint8_t can_data[], uint8_t data_count)
{
#ifdef DETAILED_OUTPUT_CAN_MESSAGE_SEND
  Serial.println("Send data via can");
  Serial.print("\tID: ");
  Serial.println(can_id, HEX);
  Serial.print("\tcount: ");
  Serial.println(data_count);
  Serial.println("\tdata: ");

  for (uint8_t i = 0; i < data_count; i++)
  {
    Serial.print("\t[");
    Serial.print(i);
    Serial.print("]: ");
    Serial.println(can_data[i], HEX);
  }
#endif

  CAN.beginPacket(can_id);
  CAN.write(can_data, data_count);
  CAN.endPacket();
}
