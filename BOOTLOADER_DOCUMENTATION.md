# CAN Bus Bootloader for dsPIC33EP512GP504

## Overview

This is a custom dual-image bootloader for the Boosted Battery dsPIC33EP512GP504 microcontroller. It allows firmware updates via CAN Bus using a 6-byte payload protocol with checksums. The bootloader supports two independent application images that can be swapped, enabling safe firmware rollback if issues occur.

## Memory Map

The 512KB program flash is divided into four regions:

```
Address Range       | Size    | Purpose
--------------------|---------|----------------------------------
0x000000 - 0x001FFF | 8 KB    | Bootloader (fixed, always runs)
0x002000 - 0x0AFFFF | ~688 KB | Application Image 1
0x0B0000 - 0x15AFFF | ~688 KB | Application Image 2
0x15B000 - 0x15BFFE | 4 KB    | Metadata/Configuration
```

### Metadata Region Layout

The metadata region stores image validity flags, active image selector, and CRC checksums:

```
Offset    | Field              | Size | Description
-----------|--------------------|----|-------------------------------------
0x000     | Image1_Valid       | 2   | 0xA5A5 = valid, 0xFFFF = invalid
0x002     | Image2_Valid       | 2   | 0xA5A5 = valid, 0xFFFF = invalid
0x004     | ActiveImage        | 2   | 0 = Image1, 1 = Image2
0x006     | Image1_CRC32       | 4   | CRC32 checksum of Image 1
0x00A     | Image2_CRC32       | 4   | CRC32 checksum of Image 2
0x00E     | BootCount          | 2   | Number of bootloader activations
```

## CAN Protocol Specification

### Frame Format

All bootloader frames use 8-byte CAN data format:

```
Byte  | Name     | Description
------|----------|------------------------------------------
0     | Length   | Payload length (1-6 bytes)
1-6   | Payload  | Firmware data or command parameters
7     | Checksum | XOR of all previous bytes (0-6)
```

### Checksum Algorithm

```
Checksum = (Byte[0] ^ Byte[1] ^ Byte[2] ^ Byte[3] ^ Byte[4] ^ Byte[5] ^ Byte[6])
```

### Command Messages

Commands are identified by the CAN message ID (not extended frames):

#### 1. START_DOWNLOAD (ID: 0x100)

Initiates firmware download sequence and erases target image space.

**Payload Format:**
```
Byte[0] = Target Image (0=Image1, 1=Image2)
```

**Response:**
- On success: Returns to IDLE state
- On error: Sets ERROR state with error code

**Example:**
```
Start download to Image 2:
  ID: 0x100
  Data: [0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01]
         ↑     ↑     ↑    ↑    ↑    ↑    ↑    ↑
         Len   Img2   0    0    0    0    0    Checksum (0x01)
```

#### 2. WRITE_BLOCK (ID: 0x101)

Writes up to 6 bytes of firmware data to the target image.

**Payload Format:**
```
Byte[0] = Data Byte 1
Byte[1] = Data Byte 2
...
Byte[n] = Data Byte n (where n <= 5)
```

**Notes:**
- Must have active download session (START_DOWNLOAD sent first)
- Data is written sequentially to flash
- CRC32 is calculated for all received data
- Blocks can be 1-6 bytes in length

**Example:**
```
Write 6 bytes (0x01, 0x02, 0x03, 0x04, 0x05, 0x06):
  ID: 0x101
  Data: [0x06, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x15]
         ↑    ↑    ↑    ↑    ↑    ↑    ↑    ↑
         Len  D1   D2   D3   D4   D5   D6   Checksum
```

#### 3. COMMIT_IMAGE (ID: 0x102)

Finalizes the firmware download and validates the received data.

**Payload Format:**
```
Byte[0-3] = Expected CRC32 (little-endian)
            Byte[0] = CRC bits 7-0
            Byte[1] = CRC bits 15-8
            Byte[2] = CRC bits 23-16
            Byte[3] = CRC bits 31-24
```

**Process:**
1. Calculates CRC32 of all received data
2. Compares with provided CRC32
3. If match: marks image as valid in metadata
4. Returns to IDLE state
5. If mismatch: error state with BL_ERR_CRC

**Example:**
```
Commit with CRC 0x12345678:
  ID: 0x102
  Data: [0x04, 0x78, 0x56, 0x34, 0x12, 0x00, 0x00, 0x5E]
         ↑    ↑    ↑    ↑    ↑    ↑    ↑    ↑
         Len  CRC0 CRC1 CRC2 CRC3  0    0    Checksum
```

#### 4. VERIFY_IMAGE (ID: 0x103)

Verifies that an image is valid.

**Payload Format:**
```
Byte[0] = Image to verify (0=Image1, 1=Image2)
```

**Response:**
- BL_OK if image valid flag is set
- BL_ERR_CRC if image invalid or not present

#### 5. ABORT (ID: 0x105)

Cancels current download operation and returns to IDLE state.

**Payload:** Empty (0 bytes)

### Status Response Message (ID: 0x200)

The bootloader can send status responses via ID 0x200:

```
Byte[0] = Status Code (BL_STATUS_t enum)
Byte[1] = State Code (BL_STATE_t enum)
Byte[2-7] = Reserved (0x00)
```

**Status Codes:**
```
0x00 = BL_OK                  - Success
0x01 = BL_ERR_INVALID_CMD     - Invalid command
0x02 = BL_ERR_INVALID_STATE   - Invalid state
0x03 = BL_ERR_CHECKSUM        - Checksum failed
0x04 = BL_ERR_FLASH           - Flash error
0x05 = BL_ERR_MEMORY          - Memory error
0x06 = BL_ERR_TIMEOUT         - Timeout
0x07 = BL_ERR_CRC             - CRC mismatch
0x08 = BL_ERR_FULL            - Image space full
0x09 = BL_ERR_NOT_INITIALIZED - Not initialized
```

**State Codes:**
```
0x00 = BL_STATE_IDLE              - Waiting for command
0x01 = BL_STATE_DOWNLOAD_ACTIVE   - Receiving blocks
0x02 = BL_STATE_PROGRAMMING       - Writing to flash
0x03 = BL_STATE_VERIFYING         - Verifying data
0x04 = BL_STATE_ERROR             - Error occurred
```

## Download Sequence

### Typical Firmware Update Flow

```
Host                           Bootloader
 |                                 |
 |-------- START_DOWNLOAD -------->|  Erase image space
 |                                 |  Set state: DOWNLOAD_ACTIVE
 |                                 |
 |-------- WRITE_BLOCK #1 -------->|  Write 6 bytes
 |                                 |  Update CRC
 |-------- WRITE_BLOCK #2 -------->|  Write 6 bytes
 |                                 |  Update CRC
 |         ... repeat ...           |
 |-------- WRITE_BLOCK #n -------->|  Write final bytes
 |                                 |  Update CRC
 |                                 |
 |-------- COMMIT_IMAGE ---------->|  Verify CRC
 |                                 |  Update metadata
 |<------ STATUS_RESPONSE ---------|  Success (0x00)
 |                                 |
```

### Example Download (100 bytes)

```
1. START_DOWNLOAD (Image 1):
   ID: 0x100, Data: [0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01]

2. WRITE_BLOCK #1 (6 bytes):
   ID: 0x101, Data: [0x06, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x15]

3. WRITE_BLOCK #2 (6 bytes):
   ID: 0x101, Data: [0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x33]

4. ... continue for 16 blocks total (6*16 + 4 = 100 bytes) ...

5. WRITE_BLOCK #17 (4 bytes, final):
   ID: 0x101, Data: [0x04, 0xFD, 0xFE, 0xFF, 0x00, 0x00, 0x00, 0x02]

6. COMMIT_IMAGE with calculated CRC:
   ID: 0x102, Data: [0x04, CRC0, CRC1, CRC2, CRC3, 0x00, 0x00, CHECKSUM]
```

## Integration with Main Application

### In main.c

Add bootloader initialization and CAN message routing:

```c
#include "bootloader.h"
#include "CANBus.h"

int main(void) {
    SYSTEM_Initialize();
    
    // Initialize bootloader module
    BL_Init();
    
    // Check if bootloader should run
    if (!BL_ShouldEnterBootloader()) {
        // Jump to application
        BL_JumpToImage(BL_GetActiveImage());
    }
    
    // Main loop - bootloader mode
    while (1) {
        CAN_MSG_OBJ recMsg;
        if (CanReceive(&recMsg)) {
            // Check if it's a bootloader message
            BL_STATUS_t status = CanProcessBootloaderMessage(&recMsg);
            
            // Send status response
            CanSendBootloaderStatus(status);
        }
        
        // Bootloader housekeeping
        BL_Task();
    }
    
    return 0;
}
```

### CAN Message Reception Flow

```c
while (1) {
    // Check for incoming CAN messages
    CAN_MSG_OBJ msg;
    if (CanReceive(&msg)) {
        // Message format validation
        if (msg.msgLen == 8) {
            // Try bootloader handler
            BL_STATUS_t bl_status = CanProcessBootloaderMessage(&msg);
            
            if (bl_status != BL_ERR_INVALID_CMD) {
                // Send bootloader response
                CanSendBootloaderStatus(bl_status);
            }
        }
    }
}
```

## Building the Bootloader

### Separate Build Project

For best results, create separate MPLAB X projects:

1. **Bootloader Project** (Default setup with linker script pointing to 0x000000)
2. **Application Project** (Linker script pointing to 0x002000 or 0x0B0000)

### Linker Script Configuration

#### For Bootloader Build

Modify `p33EP512GP504.gld` to restrict memory:

```
MEMORY
{
    /* Only bootloader region */
    program (rx)  : ORIGIN = 0x000000, LENGTH = 0x002000
    data (rwx)    : ORIGIN = 0x0800,   LENGTH = 0x7E00
}
```

#### For Application Build

```
MEMORY
{
    /* Only application region (Image 1) */
    program (rx)  : ORIGIN = 0x002000, LENGTH = 0x0AE000
    data (rwx)    : ORIGIN = 0x0800,   LENGTH = 0x7E00
}
```

## Error Handling

### Timeout Handling

The bootloader should implement a timeout mechanism:
- If no CAN messages received within 5 seconds of START_DOWNLOAD
- Automatically abort and return to IDLE state

### Checksum Failures

If checksum validation fails:
1. Return BL_ERR_CHECKSUM status
2. Remain in current state
3. Host must retransmit block

### Flash Errors

If flash write fails:
1. Set state to ERROR
2. Return BL_ERR_FLASH status
3. Retry start-download required

## Host Software Example

### Python Pseudo-code for Firmware Update

```python
def send_can_frame(can_id, length, payload):
    # Calculate checksum
    checksum = length
    for byte in payload:
        checksum ^= byte
    
    # Pad payload to 6 bytes
    payload_padded = payload + bytes([0] * (6 - len(payload)))
    
    # Build CAN frame
    data = bytes([length]) + payload_padded + bytes([checksum])
    
    # Send via CAN interface
    can_send(can_id, data)

def update_firmware(image_file, target_image=0):
    # Read firmware file
    with open(image_file, 'rb') as f:
        firmware = f.read()
    
    # Calculate CRC32
    crc = crc32(firmware)
    
    # Start download
    send_can_frame(0x100, 1, bytes([target_image]))
    time.sleep(0.1)
    
    # Send blocks
    for offset in range(0, len(firmware), 6):
        block = firmware[offset:offset+6]
        send_can_frame(0x101, len(block), block)
        time.sleep(0.05)
    
    # Commit
    crc_bytes = crc.to_bytes(4, 'little')
    send_can_frame(0x102, 4, crc_bytes)
    
    # Verify
    response = wait_for_status(timeout=1000)
    return response == 0x00  # BL_OK
```

## Safety Features

1. **Dual-Image Support**: Always have a working firmware image available
2. **CRC Verification**: All images validated before marking valid
3. **Checksum Protection**: Every CAN frame validated
4. **Metadata Protection**: Image status stored in dedicated flash region
5. **Atomic Commits**: Image only marked valid after complete CRC verification
6. **Timeout Protection**: Automatic abort if communication lost

## Debugging and Status Monitoring

Monitor bootloader status:

```c
void print_bootloader_status(void) {
    Serial_printf("BL State: %d\r\n", (int)BL_GetState());
    Serial_printf("BL Error: %d\r\n", (int)BL_GetLastError());
    Serial_printf("Active Image: %d\r\n", 
                  (int)BL_GetActiveImage());
}
```

## Future Enhancements

1. **Rollback on Failure**: Automatically switch images if app crashes
2. **Incremental CRC**: Stream CRC verification during download
3. **Boot-to-ROM**: Fallback ROM bootloader mode
4. **Flash Page Cache**: Buffer writes for efficiency
5. **Multi-Image Slots**: Support for 3+ images
6. **Secure Boot**: Image signing/verification

## References

- dsPIC33EP512GP504 Datasheet
- Microchip XC16 Compiler Documentation
- CAN Protocol Specification (ISO 11898)
