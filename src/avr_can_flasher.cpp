/*                                 _   _                 _  _               _
                                 | \ | |               | |(_)             | |
  __      ____      ____      __ |  \| |  ___  _ __  __| | _  _   _     __| |  ___
  \ \ /\ / /\ \ /\ / /\ \ /\ / / | . ` | / _ \| '__|/ _` || || | | |   / _` | / _ \
   \ V  V /  \ V  V /  \ V  V /_ | |\  ||  __/| |  | (_| || || |_| | _| (_| ||  __/
    \_/\_/    \_/\_/    \_/\_/(_)|_| \_| \___||_|   \__,_||_| \__, |(_)\__,_| \___|
                                                               __/ |
                                                              |___/
     avr_can_flasher.cpp by Fabian Steppat
     Infos on www.nerdiy.de

     The underlying library is mostly based on the awesome work of Peter Müller <peter@crycode.de> (https://crycode.de)
     It is ported from his "Flash application for MCP-CAN-Boot".
     More info is available here:
     - Bootloader: https://github.com/crycode-de/mcp-can-boot
     - Flash application: https://github.com/crycode-de/mcp-can-boot-flash-app

     Huge thanks to Peter Müller for making this available!

     License: CC BY-NC-SA 4.0
*/

#include <Arduino.h>
#include "avr_can_flasher.h"

ACF::ACF(void (*cs_function_pointer)(uint32_t, uint8_t *, uint8_t))
{
    this->can_send_function_pointer = cs_function_pointer;
}

boolean ACF::acf_start_flash_process(String file_string,
                                     uint32_t mcuId,
                                     String partno,
                                     uint32_t reset_can_id,
                                     String reset_can_message,
                                     boolean doErase,
                                     uint16_t doRead,
                                     boolean doReset,
                                     boolean doVerify,
                                     boolean forceFlashing,
                                     uint32_t canIdRemote,
                                     uint32_t canIdMcu,
                                     boolean printSimpleProgress)
{
    // This is done to clear the (possible) loaded variable values.
    // This avoids crashes on the ESP32 in case a flash start process is started while another one was already prepared.
    this->acf_stop_flash_process();

    this->mcuId = mcuId;
    this->doErase = doErase;
    this->doRead = doRead;
    this->doVerify = this->doRead ? false : doVerify; // if we are just reading, we cannot verify
    this->state = ACF_STATE_INIT;
    this->deviceSignature = this->acf_get_device_signature(partno);
    this->partno = partno;
    this->can_id_remote_to_mcu = canIdRemote;
    this->can_id_mcu_to_remote = canIdMcu;
    this->forceFlashing = forceFlashing;
    this->file_string = file_string;
    this->printSimpleProgress = printSimpleProgress;

    Serial.println("Flash process started with the following settings:");
    Serial.print("\tmcuId: ");
    Serial.println(mcuId, HEX);
    Serial.print("\tdoErase: ");
    Serial.println(doErase);
    Serial.print("\tdoRead: ");
    Serial.println(doRead);
    Serial.print("\tdoVerify: ");
    Serial.println(doVerify);
    Serial.print("\tstate: ");
    Serial.println(state);
    Serial.print("\tdeviceSignature: ");
    Serial.println(deviceSignature, HEX);
    Serial.print("\tpartno: ");
    Serial.println(partno);
    Serial.print("\tcan_id_remote_to_mcu: ");
    Serial.println(can_id_remote_to_mcu, HEX);
    Serial.print("\tcan_id_mcu_to_remote: ");
    Serial.println(can_id_mcu_to_remote, HEX);
    Serial.print("\tforceFlashing: ");
    Serial.println(forceFlashing);
    Serial.print("\tfile_string: ");
    Serial.println(file_string);

    if (!SPIFFS.begin(true))
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
    }

    if (!this->doRead)
    {
        // load from file if we are not only reading the flash
        fs::File file = SPIFFS.open(this->file_string.c_str(), "r");
        if (!file || file.isDirectory())
        {
            Serial.print("Input file ");
            Serial.print(file_string);
            Serial.println(" does not exist!");
            Serial.println("The following content was found:");

            File root = SPIFFS.open("/");
            File file = root.openNextFile();
            while (file)
            {
                if (file.isDirectory())
                {
                    Serial.print("  DIR : ");
                    Serial.println(file.name());
                }
                else
                {
                    Serial.print("  FILE: ");
                    Serial.print(file.name());
                    Serial.print("\tSIZE: ");
                    Serial.println(file.size());
                }
                file = root.openNextFile();
            }
            return false;
        }

        uint32_t fileSize = file.size();
        Serial.print("The file \"");
        Serial.print(this->file_string);
        Serial.print("\" was found. It is ");
        Serial.print((float)((float)fileSize / 1000.0), 3);
        Serial.println("kB big.");
        Serial.println("Possible that it will take some time to read this amount of data...");
        uint32_t hex_file_reading_start = millis();

        // prepare hex file strin variable
        std::string intelHexString = "";

        if (fileSize > intelHexString.max_size())
        {
            Serial.println("Caution! The file size of the hex file you provided is bigger than the .max_size() of std::string's on this environment.");
            Serial.println("Most probably the code needs to be reworked to fix this. Sorry. :/");
            return false;
        }

        // prepare buffer and more for reading the data from the spiffs to the string variable intelHexString
        char buf[ACF_READ_FILE_CHUNK_SIZE];
        uint32_t numberOfChunks = ((fileSize % ACF_READ_FILE_CHUNK_SIZE) == 0) ? (fileSize / ACF_READ_FILE_CHUNK_SIZE) : (fileSize / ACF_READ_FILE_CHUNK_SIZE) + 1;

#ifdef DETAILED_OUTPUT_HEX_FILE_READING
        Serial.print("Number of chunks: ");
        Serial.println(numberOfChunks);
        Serial.print("Number of chunks(raw): ");
        Serial.println((fileSize / ACF_READ_FILE_CHUNK_SIZE));
#endif

        // read file content in chunks to string variable intelHexString
        uint32_t remainingBytes = fileSize;
        for (uint32_t i = 0; i <= numberOfChunks; i++)
        {
            memset(buf, 0, ACF_READ_FILE_CHUNK_SIZE + 1);
            file.read((uint8_t *)buf, ACF_READ_FILE_CHUNK_SIZE);
            remainingBytes -= ACF_READ_FILE_CHUNK_SIZE;
            intelHexString = intelHexString + std::string(buf);
        }
        file.close();

#ifdef DETAILED_OUTPUT_HEX_FILE_READING
        Serial.println("The following content was read from the file: ");
        Serial.println(intelHexString.c_str());
#endif

        // iterate over the single characters of intelHexString to count the ":" as equivalent of the lines in the .hex file
        for (uint32_t i = 0; i < intelHexString.length(); i++)
        {
            if (intelHexString[i] == ':')
                this->memMaplinesNum++;
        }

#ifdef DETAILED_OUTPUT_HEX_FILE_READING
        Serial.print("The file has ");
        Serial.print(this->memMaplinesNum);
        Serial.println(" lines.");
#endif

        // prepare the intel_hex_map_line struct. This struct will later hold all the sorted data from the hex file. Please check the definition for further details about the structure.
        this->hexMapLines = new intel_hex_map_line[this->memMaplinesNum];

        // lets parse the unformatted hex file string and sort its content to the "intel_hex_map_line" struct.
        uint32_t next_line_start_found = 0;
        for (uint32_t line = 0; line < this->memMaplinesNum; line++)
        {
            uint8_t line_length = intelHexString.find("\n", next_line_start_found) - intelHexString.find(":", next_line_start_found); // estimate the length of the current line.

            // get the current line
            std::string current_line = intelHexString.substr(next_line_start_found, line_length).c_str();
#ifdef DETAILED_OUTPUT_HEX_FILE_READING
            Serial.print(" current_line: ");
            Serial.println(current_line.c_str());
#endif

            // get the byte_count value of the current line. See https://en.wikipedia.org/wiki/Intel_HEX for mor information about the structure of an intel hex file.
            this->hexMapLines[line].byte_count = this->acf_convert_hex_string_to_int(current_line.substr(1, 2).c_str());
#ifdef DETAILED_OUTPUT_HEX_FILE_READING
            Serial.print("\thexMapLines[line].byte_count: 0x");
            Serial.println(this->hexMapLines[line].byte_count, HEX);
#endif

            // get the address value of the current line.
            this->hexMapLines[line].address = this->acf_convert_hex_string_to_int(current_line.substr(3, 4).c_str());
#ifdef DETAILED_OUTPUT_HEX_FILE_READING
            Serial.print("\thexMapLines[line].address: 0x");
            Serial.println(this->hexMapLines[line].address, HEX);
#endif

            // get the record_type value of the current line.
            this->hexMapLines[line].record_type = this->acf_convert_hex_string_to_int(current_line.substr(7, 2).c_str());
#ifdef DETAILED_OUTPUT_HEX_FILE_READING
            Serial.print("\thexMapLines[line].record_type: 0x");
            Serial.println(this->hexMapLines[line].record_type, HEX);
#endif

            // get the payload data values of the current line and insert it into the byte array of "intel_hex_map_line" struct.
            for (uint8_t data_byte = 0; data_byte < this->hexMapLines[line].byte_count; data_byte++)
            {
                this->hexMapLines[line].data[data_byte] = this->acf_convert_hex_string_to_int(current_line.substr(9 + 2 * data_byte, 2).c_str());
            }

            // get the checksum value of the current line and check that it is valid.
            this->hexMapLines[line].checksum = this->acf_convert_hex_string_to_int(current_line.substr(current_line.length() - 3, 2).c_str());
#ifdef DETAILED_OUTPUT_HEX_FILE_READING
            Serial.print("\thexMapLines[line].checksum: 0x");
            Serial.println(this->hexMapLines[line].checksum, HEX);

            Serial.println("\tdata: ");
            for (uint8_t data_byte = 0; data_byte < this->hexMapLines[line].byte_count; data_byte++)
            {
                Serial.print("\t[");
                Serial.print(data_byte);
                Serial.print("]:");
                Serial.println(this->hexMapLines[line].data[data_byte], HEX);
            }
#endif
            if (!this->acf_intel_hex_checksum_is_valid(current_line.c_str()))
            {
                Serial.print("Error during reading of the input file. Checksum of line ");
                Serial.print(line);
                Serial.println(" was not valid.");
                std::string intelHexString = "";
                return false;
            }

            // get the start position of the next line.
            next_line_start_found = intelHexString.find(":", next_line_start_found + 1);
        }

        Serial.print("Reading and parsing finished in ");
        Serial.print((float)(millis() - hex_file_reading_start) / 1000.0, 3);
        Serial.println(" seconds.");
    }
    else
    {
        // we are only reading the flash... init the variables

        Serial.println("Sorry, this is not implemented/tested yet. :/");
        return false; // THIS IS NOT IMPLEMENTED YET

        File file = SPIFFS.open(this->file_string, FILE_WRITE);

        // check if output file exists
        if (this->file_string != "-" &&
            !file &&
            !file.isDirectory())
        {
            Serial.print("Output file ");
            Serial.print(file_string);
            Serial.println(" does not exist !");
            return false;
        }
    }

    this->memMapCurrentLine = 0; // set current key to null to begin new key on flash ready
    this->memMapCurrentDataIdx = 0;
    this->curAddr = 0x0000; // current flash address
    this->readDataArr = {0};

    // send can message to reset the mcu?
    if (doReset)
    {
        // TODO!
        /*
        if (isnan(reset_can_id))
        {
            Serial.println("Reset message format error !\nThe can_id is not valid.A three digits standard frame or eight digits extended frame hex id must be provided.");
            return;
        }

        if (isnan(reset_can_message))
        {
            Serial.println("Reset message format error !\nThe data bytes must be provided as hex numbers.");
            return;
        }*/

        this->acf_can_send_data(reset_can_id, reset_can_message, 8);

        Serial.println("Reset message send to the MCU.");
    }

    Serial.print("Waiting for bootloader start message for MCU ID ");
    Serial.print(this->acf_convert_to_hex_string(this->mcuId, 4));
    Serial.println(" ...");

    this->waitingForBootloaderDuration = millis();

    return true;
}

void ACF::acf_stop_flash_process()
{
    free(this->hexMapLines);
    this->mcuId = 0;
    this->doErase = false;
    this->doRead = false;
    this->doVerify = false;
    this->forceFlashing = false;
    this->state = ACF_STATE_INIT;
    this->deviceSignature = 0;
    this->curAddr = 0;      
    this->flashStartTs = 0; 
    this->partno = "";
    this->can_id_remote_to_mcu = 0;
    this->can_id_mcu_to_remote = 0;
    this->file_string = "";
    this->memMapCurrentLine = 0;
    this->memMapCurrentDataIdx = 0;
    this->readDataArr = 0;
    this->hexMapLines = 0;
    this->memMaplinesNum = 0;
    this->printSimpleProgress = false;
    this->waitingForBootloaderDuration = 0;
    this->flashingFinished = false;
    this->verificationFinished = false;
}

boolean ACF::acf_handle_can_msg(acf_can_message msg)
{
#ifdef DETAILED_OUTPUT_CAN_MESSAGE_RECEIVE
    Serial.println("Data received in acf_handle_can_msg");
    Serial.print("\tID: ");
    Serial.println(msg.id, HEX);
    Serial.print("\tcount: ");
    Serial.println(msg.data_length);
    Serial.println("\tdata: ");

    for (uint8_t i = 0; i < msg.data_length; i++)
    {
        Serial.print("\t[");
        Serial.print(i);
        Serial.print("]: ");
        Serial.println(msg.data[i], HEX);
    }
#endif

    if (msg.data_length != 8)
        return false;
#ifdef DETAILED_OUTPUT_CAN_MESSAGE_RECEIVE
    Serial.println("CAN message had the correct length.");
#endif

    uint32_t mcuid_recevied = msg.data[ACF_CAN_DATA_BYTE_MCU_ID_LSB] + (msg.data[ACF_CAN_DATA_BYTE_MCU_ID_MSB] << 8);

    if (msg.id != this->can_id_mcu_to_remote)
    {
        Serial.println("CAN message id didn't match can_id_mcu_to_remote.");
        Serial.print("Received ID: ");
        Serial.println(mcuid_recevied, HEX);
        Serial.print("Set ID: ");
        Serial.println(this->can_id_mcu_to_remote, HEX);
        return false;
    }
#ifdef DETAILED_OUTPUT_CAN_MESSAGE_RECEIVE
    Serial.println("CAN message id matched can_id_mcu_to_remote.");
#endif

    if (mcuid_recevied != this->mcuId)
        return false;
#ifdef DETAILED_OUTPUT_CAN_MESSAGE_RECEIVE
    Serial.println("CAN message contained the target mcuid.");
#endif

    // the message is for this bootloader session

    uint8_t byteCount = 0;
    uint8_t addrPart = 0;
    switch (this->state)
    {
    case ACF_STATE_INIT:

        switch (msg.data[ACF_CAN_DATA_BYTE_CMD])
        {
        case ACF_CMD_BOOTLOADER_START:
        {
            // check device signature
            uint8_t devSig1 = (uint8_t)(this->deviceSignature >> 16);
            uint8_t devSig2 = (uint8_t)(this->deviceSignature >> 8);
            uint8_t devSig3 = (uint8_t)this->deviceSignature;

            if (msg.data[4] != devSig1 ||
                msg.data[5] != devSig2 ||
                msg.data[6] != devSig3)
            {
                Serial.println("Error: Got bootloader start message but device signature missmatched!");
                Serial.println("Expected:");
                Serial.println(this->acf_convert_to_hex_string(devSig1));
                Serial.println(this->acf_convert_to_hex_string(devSig2));
                Serial.println(this->acf_convert_to_hex_string(devSig3));
                Serial.print("for ");
                Serial.print(this->partno);
                Serial.println(" got:");
                Serial.println(this->acf_convert_to_hex_string(msg.data[4]));
                Serial.println(this->acf_convert_to_hex_string(msg.data[5]));
                Serial.println(this->acf_convert_to_hex_string(msg.data[6]));
                return false;
            }

            // check bootloader version
            if (msg.data[7] != ACF_BOOTLOADER_CMD_VERSION)
            {
                Serial.print("ERROR: Bootloader command version of MCU ");
                Serial.print(this->acf_convert_to_hex_string(msg.data[7]));
                Serial.print(" does not match the version expected by this flash app ");
                Serial.print(this->acf_convert_to_hex_string(ACF_BOOTLOADER_CMD_VERSION));
                if (this->forceFlashing)
                {
                    Serial.println(". You forced flashing anyways. This may lead to an stupid result...");
                }
                else
                {
                    Serial.println(". You can force flashing by setting the function parameters accordingly.");
                    return false;
                }
            }

            // enter flash mode
            Serial.println("Got bootloader start, entering flash mode ...");
            this->flashStartTs = millis();

            uint8_t can_buffer[8] = {
                (uint8_t)(this->mcuId >> 8),
                (uint8_t)this->mcuId,
                ACF_CMD_FLASH_INIT,
                0x00,
                (uint8_t)(this->deviceSignature >> 16),
                (uint8_t)(this->deviceSignature >> 8),
                (uint8_t)this->deviceSignature,
                0x00};

            this->acf_can_send_data(this->can_id_remote_to_mcu, can_buffer, 8);
        }
        break;

        case ACF_CMD_FLASH_READY:

            // flash is ready for first data, read or erase...
            if (this->doRead)
            {
                Serial.println("Got flash ready message, reading flash ...");
                // Serial.println("Changed state to ACF_STATE_READING");
                this->state = ACF_STATE_READING;

                uint8_t can_buffer[8] = {
                    (uint8_t)(this->mcuId >> 8),
                    (uint8_t)this->mcuId,
                    ACF_CMD_FLASH_READ,
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00};

                this->acf_can_send_data(this->can_id_remote_to_mcu, can_buffer, 8);
            }
            else if (this->doErase)
            {
                Serial.println("Got flash ready message, erasing flash ...");
                uint8_t can_buffer[8] = {
                    (uint8_t)(this->mcuId >> 8),
                    (uint8_t)this->mcuId,
                    ACF_CMD_FLASH_ERASE,
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00};

                this->acf_can_send_data(this->can_id_remote_to_mcu, can_buffer, 8);

                this->doErase = false;
            }
            else
            {
                Serial.println("Got flash ready message, begin flashing ...");
                // Serial.println("Changed state to ACF_STATE_FLASHING");
                this->state = ACF_STATE_FLASHING;
                this->acf_on_flash_ready(msg.data);
            }

            break;

        default:
            // something wrong?
            Serial.print("WARNING: Got unexpected message during ACF_STATE_INIT from MCU: ");
            Serial.println(this->acf_convert_to_hex_string(msg.data[ACF_CAN_DATA_BYTE_CMD]));
        }
        break;

    case ACF_STATE_FLASHING:

        switch (msg.data[ACF_CAN_DATA_BYTE_CMD])
        {
        case ACF_CMD_FLASH_DATA_ERROR:
            Serial.println("Flash data error!");
            Serial.println("Maybe there are some CAN bus issues?");
            break;

        case ACF_CMD_FLASH_ADDRESS_ERROR:
            Serial.println("Flash address error!");
            Serial.println("Maybe the hex file is not for this MCU type or bigger than the available space?");
            break;

        case ACF_CMD_FLASH_READY:
            byteCount = (msg.data[ACF_CAN_DATA_BYTE_LEN_AND_ADDR] >> 5);

            if (!this->printSimpleProgress)
            {
                Serial.print(byteCount);
                Serial.println(" bytes flashed.");
            }

            this->curAddr += byteCount;
            this->memMapCurrentDataIdx += byteCount;

            if (this->printSimpleProgress)
            {
                Serial.print("Flash progress: ");
                Serial.print((((float)this->memMapCurrentLine / (float)this->memMaplinesNum) * 100.0), 2); // print flash progress in percent
                Serial.println("%");
            }

            this->acf_on_flash_ready(msg.data);
            break;

        case ACF_CMD_START_APP:
            Serial.println("Flash done in ");
            Serial.println(millis() - this->flashStartTs);
            Serial.println("MCU is starting the app. :-)");
            return true;
            break;

        default:
            // something wrong?
            Serial.print("WARNING: Got unexpected message during ACF_STATE_FLASHING from MCU: ");
            Serial.println(this->acf_convert_to_hex_string(msg.data[ACF_CAN_DATA_BYTE_CMD]));
        }
        break;

    case ACF_STATE_READING:

        switch (msg.data[ACF_CAN_DATA_BYTE_CMD])
        {
        case ACF_CMD_FLASH_DONE_VERIFY:

            // start reading flash to verify
            if (!this->printSimpleProgress)
            {
                Serial.println("Start reading flash to verify ...");
            }
            this->memMapCurrentLine = 0; // set current key to null to begin new key on flash read
            this->memMapCurrentDataIdx = 0;

            this->acf_read_for_verify();

            break;

        case ACF_CMD_FLASH_READ_DATA:

            byteCount = (msg.data[ACF_CAN_DATA_BYTE_LEN_AND_ADDR] >> 5);
            addrPart = msg.data[ACF_CAN_DATA_BYTE_LEN_AND_ADDR] & 0b00011111;

            if ((this->curAddr & 0b00011111) != addrPart)
            {
                Serial.println("Got an unexpected address of read data from MCU!");
                Serial.println("Will now abort and exit the bootloader ...");
                this->acf_send_start_app();
                return false;
            }

            if (!this->printSimpleProgress)
            {
                Serial.print("Got flash data for ");
                Serial.print(this->acf_convert_to_hex_string(this->curAddr, 4));
                Serial.println(" ...");
            }

            if (this->doVerify)
            {
                // verify flash
                for (uint8_t i = 0; i < byteCount; i++)
                {
#ifdef DETAILED_OUTPUT_VERIFICATION
                    Serial.print("this->memMapCurrentLine: ");
                    Serial.println(this->memMapCurrentLine);
                    Serial.print("this->memMapCurrentDataIdx: ");
                    Serial.println(this->memMapCurrentDataIdx);
                    Serial.print("this->hexMapLines[this->memMapCurrentLine].data[this->memMapCurrentDataIdx]: ");
                    Serial.println(this->hexMapLines[this->memMapCurrentLine].data[this->memMapCurrentDataIdx]);
                    Serial.print("msg.data[");
                    Serial.print(4 + i);
                    Serial.print("]: ");
                    Serial.println(msg.data[4 + i]);
#endif
                    if (this->hexMapLines[this->memMapCurrentLine].data[this->memMapCurrentDataIdx] != msg.data[4 + i])
                    {
                        Serial.print("ERROR: Verify failed at ");
                        Serial.print(this->acf_convert_to_hex_string(this->curAddr));
                        Serial.println("! Trying to start the app nevertheless ...");
                        this->acf_send_start_app();
                        return false;
                    }
                    this->curAddr++;
                    this->memMapCurrentDataIdx++;

                    // in some rare cases it can happen that a line in the hex file has less than the usual 16 payload bytes.
                    // in this case we need to reset the data pointer and increment the line pointer to point to the correct payload byte in the next line.
                    if (this->memMapCurrentDataIdx >= this->hexMapLines[this->memMapCurrentLine].byte_count)
                    {
                        this->memMapCurrentDataIdx = 0;
                        this->memMapCurrentLine++;
                    }

                    // in case we reached the last line of the hex file the verification is finished and we must stop here
                    if (this->hexMapLines[this->memMapCurrentLine].record_type == ACF_HEX_FILE_RECORD_TYPE_END_OF_LINE)
                    {
#ifdef DETAILED_OUTPUT_VERIFICATION
                        Serial.println("End of line reached. Verification succeeded. :)");
#endif
                        break;
                    }
                }

                this->acf_read_for_verify();
            }
            else
            {
                // read whole flash
                // cache the data
                for (uint8_t i = 0; i < byteCount; i++)
                {
                    this->readDataArr[this->curAddr] = msg.data[4 + i];
                    this->curAddr++;
                }

                if (this->doRead > 0 &&
                    this->curAddr > this->doRead)
                {
                    // reached max read address...
                    this->acf_read_done();
                    return true;
                }
                // request next address
                uint8_t can_buffer[8] = {
                    (uint8_t)(this->mcuId >> 8),
                    (uint8_t)this->mcuId,
                    ACF_CMD_FLASH_READ,
                    0x00,
                    (uint8_t)((this->curAddr >> 24) & 0xFF),
                    (uint8_t)((this->curAddr >> 16) & 0xFF),
                    (uint8_t)((this->curAddr >> 8) & 0xFF),
                    (uint8_t)(this->curAddr & 0xFF)};

                this->acf_can_send_data(this->can_id_remote_to_mcu, can_buffer, 8);
            }

            break;

        case ACF_CMD_FLASH_READ_ADDRESS_ERROR:
        {
            // we hit the end of the flash
            if (this->doVerify)
            {
                // hitting the end at verify must be an error...
                Serial.println("ERROR: Reading flash failed during verify!");
                this->acf_send_start_app();
                return false;
            }
            else
            {
                // when reading whole flash this is expected
                this->acf_read_done();
            }
        }
        break;

        case ACF_CMD_START_APP:
        {
            Serial.println("MCU is starting the app. :-)");
        }
        break;

        default:
        {
            // something wrong?
            Serial.print("WARNING: Got unexpected message during ACF_STATE_READING from MCU: ");
            Serial.println(this->acf_convert_to_hex_string(msg.data[ACF_CAN_DATA_BYTE_CMD]));
        }
        }
        break;
    }
    return true;
}

void ACF::acf_read_for_verify()
{
    // check memory map and get next map key if we reached the end
    if (this->memMapCurrentDataIdx >= this->hexMapLines[this->memMapCurrentLine].byte_count) // all data of the current line was read
    {
        // no more data... goto next memory map key...
        if (this->memMapCurrentLine >= this->memMaplinesNum ||
            this->hexMapLines[this->memMapCurrentLine].record_type == ACF_HEX_FILE_RECORD_TYPE_END_OF_LINE)
        {
            // all keys done... verify complete
            Serial.print("Flash and verify done in ");
            Serial.print((float)((millis() - this->flashStartTs) / 1000.0), 1);
            Serial.println(" seconds.");
            this->acf_send_start_app();
            this->verificationFinished = true;
            return;
        }

        // apply new current address and set data index to 0
        this->memMapCurrentLine++;
        this->memMapCurrentDataIdx = 0;
        this->curAddr = this->hexMapLines[this->memMapCurrentLine].address;
    }

    if (this->printSimpleProgress)
    {
        Serial.print("Verify progress: ");
        Serial.print(((float)this->memMapCurrentLine / (float)this->memMaplinesNum) * 100.0, 2); // print verification progress in percent
        Serial.println("%");
    }

    // request next address
    uint8_t can_buffer[8] = {
        (uint8_t)(this->mcuId >> 8),
        (uint8_t)this->mcuId,
        ACF_CMD_FLASH_READ,
        0x00,
        (uint8_t)((this->curAddr >> 24) & 0xFF),
        (uint8_t)((this->curAddr >> 16) & 0xFF),
        (uint8_t)((this->curAddr >> 8) & 0xFF),
        (uint8_t)(this->curAddr & 0xFF)};

    this->acf_can_send_data(this->can_id_remote_to_mcu, can_buffer, 8);
}

void ACF::acf_read_done()
{
    // create memory map
    /*const memMap = new MemoryMap();
    memMap.set(0x0000, Uint8Array.from(this->readDataArr));
    String intelHexString = memMap.asHexString();
*/

    String intelHexString = this->acf_convert_data_array_to_intel_hex_string(this->readDataArr);

    File file = SPIFFS.open(this->file_string, FILE_WRITE);
    if (!file)
    {
        Serial.println("Failed to open file for writing.");
        return;
    }
    file.print(intelHexString);

    Serial.print("Hex file written to ");
    Serial.println(this->file_string);

    Serial.print("Reading flash done in ");
    Serial.print((float)(millis() - this->flashStartTs) / 1000.0, 3);
    Serial.println(" seconds.");

    // start the main application at the MCU
    this->acf_send_start_app();
}

void ACF::acf_send_start_app()
{
    Serial.println("Starting the app on the MCU ...");

    uint8_t can_buffer[8] = {
        (uint8_t)(this->mcuId >> 8),
        (uint8_t)this->mcuId,
        ACF_CMD_START_APP,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00};

    this->acf_can_send_data(this->can_id_remote_to_mcu, can_buffer, 8);

    Serial.println("... done.");
}

void ACF::acf_on_flash_ready(uint8_t msgData[])
{
    uint32_t curAddrRemote = msgData[7] + (msgData[6] << 8) + (msgData[5] << 16) + (msgData[4] << 24);

    if (!this->printSimpleProgress)
    {
        Serial.print("Remote flash address is: ");
        Serial.println(acf_convert_to_hex_string(curAddrRemote));
    }

    uint8_t dataBytes = 0;
#ifdef DETAILED_OUTPUT_FLASHING
    Serial.print("memMapCurrentDataIdx: ");
    Serial.println(acf_convert_to_hex_string(this->memMapCurrentDataIdx));
    Serial.print("memMapCurrentLine: ");
    Serial.println(acf_convert_to_hex_string(this->memMapCurrentLine));
    Serial.print("memMaplinesNum: ");
    Serial.println(acf_convert_to_hex_string(this->memMaplinesNum));
    Serial.print("this->hexMapLines[this->memMapCurrentLine].byte_count: ");
    Serial.println(acf_convert_to_hex_string(this->hexMapLines[this->memMapCurrentLine].byte_count));
#endif
    if (this->memMapCurrentDataIdx >= this->hexMapLines[this->memMapCurrentLine].byte_count)
    {
        // no more data... goto next memory map key...
        if (this->memMapCurrentLine >= this->memMaplinesNum ||
            this->hexMapLines[this->memMapCurrentLine].record_type == ACF_HEX_FILE_RECORD_TYPE_END_OF_LINE)
        {
            // all keys done... flash complete
            if (!this->printSimpleProgress)
            {
                Serial.println("All data transmitted. Finalizing ...");
            }
            else
            {
                Serial.println("Flash progress: 100%");
            }

            this->flashingFinished = true;

            if (this->doVerify)
            {
                // we want to verify... send flash done verify and set own state to read
                this->state = ACF_STATE_READING;
                uint8_t can_buffer[8] = {
                    (uint8_t)(this->mcuId >> 8),
                    (uint8_t)this->mcuId,
                    ACF_CMD_FLASH_DONE_VERIFY,
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00};

                this->acf_can_send_data(this->can_id_remote_to_mcu, can_buffer, 8);
            }
            else
            {
                // we don"t want to verify... send flash done to start the app
                uint8_t can_buffer[8] = {
                    (uint8_t)(this->mcuId >> 8),
                    (uint8_t)this->mcuId,
                    ACF_CMD_FLASH_DONE,
                    0x00,
                    0x00,
                    0x00,
                    0x00,
                    0x00};

                this->acf_can_send_data(this->can_id_remote_to_mcu, can_buffer, 8);
            }
            return;
        }

        // apply new current address and set data index to 0
        this->memMapCurrentLine++;
        this->memMapCurrentDataIdx = 0;
        this->curAddr = this->hexMapLines[this->memMapCurrentLine].address;
    }

    if (this->curAddr != curAddrRemote)
    {
        // need to set the address to flash...

        if (!this->printSimpleProgress)
        {
            Serial.print("Setting flash address to ");
            Serial.print(acf_convert_to_hex_string(this->curAddr, 4));
            Serial.println("...");
        }

        uint8_t can_buffer[8] = {
            (uint8_t)(this->mcuId >> 8),
            (uint8_t)this->mcuId,
            ACF_CMD_FLASH_SET_ADDRESS,
            0x00,
            (uint8_t)((this->curAddr >> 24) & 0xFF),
            (uint8_t)((this->curAddr >> 16) & 0xFF),
            (uint8_t)((this->curAddr >> 8) & 0xFF),
            (uint8_t)(this->curAddr & 0xFF)};

        this->acf_can_send_data(this->can_id_remote_to_mcu, can_buffer, 8);

        return;
    }

    // send data to flash...
    uint8_t data_var[8] = {
        (uint8_t)(this->mcuId >> 8),
        (uint8_t)this->mcuId,
        ACF_CMD_FLASH_DATA,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00};

    // add the 4 data bytes if available
    for (uint8_t i = 0; i < 4; i++)
    {
        uint8_t byte_val = this->hexMapLines[this->memMapCurrentLine].data[this->memMapCurrentDataIdx + i];

        // lets consider the amount of data in this line and abort as soon as the complete line is read
        if (this->memMapCurrentDataIdx + i >= this->hexMapLines[this->memMapCurrentLine].byte_count)
        {
            break;
        }
        data_var[4 + i] = byte_val;
        dataBytes++;
    }

    // set the number of bytes and address
    data_var[ACF_CAN_DATA_BYTE_LEN_AND_ADDR] = (dataBytes << 5) | (this->curAddr & 0b00011111);

    // send data
    if (!this->printSimpleProgress)
    {
        Serial.print("Sending flash data of address ");
        Serial.print({acf_convert_to_hex_string(this->curAddr, 4)});
        Serial.println("...");
    }

    this->acf_can_send_data(this->can_id_remote_to_mcu, data_var, 8);
}

String ACF::acf_convert_to_hex_string(uint32_t num)
{
    return this->acf_convert_to_hex_string(num, 0);
}

String ACF::acf_convert_to_hex_string(uint32_t num, uint8_t minLength)
{
    String hex = String(num, HEX);
    if ((hex.length() % 2) != 0)
    {
        hex = "0" + hex;
    }

    if (minLength)
    {
        while (hex.length() < minLength)
        {
            hex = "0" + hex;
        }
    }
    hex.toUpperCase();
    return "0x" + hex;
}

uint32_t ACF::acf_get_device_signature(String partno)
{
    partno.toLowerCase();
    if (partno == "m32" ||
        partno == "mega32" ||
        partno == "atmega32")
        return (((uint32_t)0x1E << 16) | ((uint32_t)0x95 << 8) | 0x02);

    if (partno == "m328" ||
        partno == "mega328" ||
        partno == "atmega328")
        return (((uint32_t)0x1E << 16) | ((uint32_t)0x95 << 8) | 0x14);

    if (partno == "m328p" ||
        partno == "mega328p" ||
        partno == "atmega328p")
        return (((uint32_t)0x1E << 16) | ((uint32_t)0x95 << 8) | 0x0F);

    if (partno == "m64" ||
        partno == "mega64" ||
        partno == "atmega64")
        return (((uint32_t)0x1E << 16) | ((uint32_t)0x96 << 8) | 0x02);

    if (partno == "m644p" ||
        partno == "mega644p" ||
        partno == "atmega644p")
        return (((uint32_t)0x1E << 16) | ((uint32_t)0x96 << 8) | 0x0A);

    if (partno == "m128" ||
        partno == "mega128" ||
        partno == "atmega128")
        return (((uint32_t)0x1E << 16) | ((uint32_t)0x97 << 8) | 0x02);

    if (partno == "m1284p" ||
        partno == "mega1284p" ||
        partno == "atmega1284p")
        return (((uint32_t)0x1E << 16) | ((uint32_t)0x97 << 8) | 0x05);

    if (partno == "m2560" ||
        partno == "mega2560" ||
        partno == "atmega2560")
        return (((uint32_t)0x1E << 16) | ((uint32_t)0x98 << 8) | 0x01);

    return 0;
}

boolean ACF::acf_intel_hex_checksum_is_valid(String line)
{
    line.replace(":", "");
    uint32_t sum = 0;
    // Iterate over the bytes in the hex string and add each byte to sum
    for (uint8_t i = 0; i < (line.length() / 2); i++)
    {
        String substring = line.substring(i * 2, 2 + (i * 2));
        uint8_t current_byte = this->acf_convert_hex_string_to_int(substring);
        sum += current_byte;
    }
    // Do simplified checksum verification mentioned here: https://en.wikipedia.org/wiki/Intel_HEX
    //... "this process can be reduced to summing all decoded byte values, including the record's checksum, and verifying that the LSB of the sum is zero. ..."
    uint16_t lsb = sum & 0xFF; // getting the lsb
    return (lsb == 0);
}

uint32_t ACF::acf_convert_hex_string_to_int(String hex_string)
{
    // Thanks to: https://stackoverflow.com/questions/23576827/arduino-convert-a-string-hex-ffffff-into-3-int
    return (uint32_t)strtol(&hex_string[0], 0, 16);
}

String ACF::acf_convert_data_array_to_intel_hex_string(uint8_t *readDataArr)
{
    // TODO!
    return "";
}

void ACF::acf_can_send_data(uint32_t can_id, String can_data_string, uint8_t data_count)
{
    can_data_string.replace("0x", ""); // remove any 0x in the data string
    // lets convert the hex string to an byte array first and then forward it to the overloaded function that can handle the byte array
    uint8_t can_data[data_count] = {0};
    for (uint8_t i = 0; i < data_count; i++)
    {
        String substring = can_data_string.substring(i * 2, (i * 2) + 2);
        can_data[i] = acf_convert_hex_string_to_int(substring);
    }
    this->acf_can_send_data(can_id, can_data, data_count);
}

void ACF::acf_can_send_data(uint32_t can_id, uint8_t can_data[], uint8_t data_count)
{
    // Passing the can data to send to the function that was specified in the constructor of the library.
    this->can_send_function_pointer(can_id, can_data, data_count);
}

/*
*  Returns the duration since the reset request was sent to the target device. This can be used to implement an timeout for a not responding target device.
*/
uint32_t ACF::acf_wait_for_bootloader_response_duration()
{
    return millis() - this->waitingForBootloaderDuration;
}

/*
*  This returns true if the bootloader of the target device responded.
*/
boolean ACF::acf_bootloader_responded()
{
    return this->flashStartTs != 0;
}

/*
*  This returns true if the flash process is finished.
*/
boolean ACF::acf_flash_process_finished()
{
    return this->flashingFinished;
}

/*
*  This returns true if the verification process is finished.
*/
boolean ACF::acf_verification_finished()
{
    return this->verificationFinished;
}
