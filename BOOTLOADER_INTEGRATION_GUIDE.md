# Bootloader Integration Guide

## Quick Start

This guide walks through integrating the new CAN-based bootloader into your existing dsPIC33EP512GP504 project.

## Files Added

The following new files have been created:

1. **bootloader.h** - Header file with bootloader API and protocol definitions
2. **bootloader.c** - Core bootloader implementation
3. **bootloader_config.h** - Memory configuration and constants
4. **bootloader_linker.gld** - Reference linker script configuration
5. **BOOTLOADER_DOCUMENTATION.md** - Full protocol documentation
6. **BOOTLOADER_INTEGRATION_GUIDE.md** - This file

## Step 1: Add Files to MPLAB X Project

1. Copy the following files to `BoostedBattery.X/`:
   - `bootloader.h`
   - `bootloader.c`
   - `bootloader_config.h`

2. In MPLAB X:
   - Right-click on project → "Add Existing Item"
   - Select the three files above
   - Verify they appear under "Source Files" in project tree

3. Add existing CANBus.c modifications:
   - The file has been updated with new functions
   - New functions: `CanProcessBootloaderMessage()`, `CanSendBootloaderStatus()`

## Step 2: Update Compiler Options

### Include Paths

Ensure your project includes the bootloader headers. In MPLAB X:
- Project Properties → XC16 (Global Options) → Preprocessing
- Add include path: `.` (current directory)

### Linker Script

**Option A: Unified Single Project (Simplest)**

Use the existing default linker script but with this modification:
- Keep current settings as-is
- The bootloader code will be included in the build
- Both bootloader and application ship together

**Option B: Separate Bootloader Project (Recommended)**

1. Create new MPLAB X project "BoostedBattery_Bootloader"
2. Add only:
   - bootloader.h
   - bootloader.c  
   - bootloader_config.h
   - Necessary MCC generated files (CAN1, etc.)

3. Modify linker script for bootloader:
   - Edit `p33EP512GP504.gld`: 
   ```
   MEMORY
   {
       program (rx)  : ORIGIN = 0x000000, LENGTH = 0x002000
       data (rwx)    : ORIGIN = 0x0800,   LENGTH = 0x7E00
   }
   ```

**Option C: Dual-Application Images**

1. Keep current main code (becomes Image 1 at 0x002000)
2. Build variant for Image 2 at 0x0B0000
3. Linker script entry point: change based on build configuration

## Step 3: Update main.c

Add bootloader initialization and routing:

```c
// At top of main.c, add include
#include "bootloader.h"

int main(void) {
    // System initialization
    SYSTEM_Initialize();
    
    // NEW: Initialize bootloader
    BL_Init();
    
    // NEW: Check if should run bootloader
    // (If using dual-image: check entry conditions)
    // For now, always run main app but keep bootloader active
    
    // Existing initialization code...
    
    // Main loop
    while(1) {
        // Existing code...
        
        // NEW: Add CAN message handling for bootloader
        CAN_MSG_OBJ recMsg;
        if (CanReceive(&recMsg)) {
            // Check if bootloader message
            BL_STATUS_t bl_status = CanProcessBootloaderMessage(&recMsg);
            
            // Send response
            if (bl_status != BL_ERR_INVALID_CMD) {
                CanSendBootloaderStatus(bl_status);
            }
        }
        
        // Bootloader housekeeping
        BL_Task();
        
        // ... rest of main loop ...
    }
    
    return 0;
}
```

## Step 4: Configure CAN Message Filtering (Optional)

To efficiently route bootloader messages:

```c
// In MCC or CAN configuration
void ConfigureCAN_Bootloader_Filters(void) {
    // Configure CAN filters to accept bootloader command IDs
    // Message IDs: 0x100-0x105 (START, WRITE, COMMIT, VERIFY, GET_STATUS, ABORT)
    
    // Example using MCC-generated functions:
    CAN1_FilterMaskConfigure(/* Your filter config */);
}
```

## Step 5: Testing the Bootloader

### Test 1: Bootloader Initialization

```c
void test_bootloader_init(void) {
    BL_STATUS_t status = BL_Init();
    Serial_printf("BL_Init: %d (should be 0)\r\n", status);
    
    BL_STATE_t state = BL_GetState();
    Serial_printf("BL_State: %d (should be 0=IDLE)\r\n", state);
}
```

### Test 2: CAN Message Processing

Send a START_DOWNLOAD frame via CAN and verify response:

```
CAN ID: 0x100
Data: [0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01]

Expected response on ID 0x200:
Data[0] = 0x00 (BL_OK)
Data[1] = 0x01 (BL_STATE_DOWNLOAD_ACTIVE)
```

### Test 3: Image Validation

Verify image validity flags:

```c
void test_image_status(void) {
    BL_STATUS_t img1_status = BL_ValidateImage(BL_IMAGE_1);
    BL_STATUS_t img2_status = BL_ValidateImage(BL_IMAGE_2);
    
    Serial_printf("Image 1 valid: %s\r\n", 
                  img1_status == BL_OK ? "Yes" : "No");
    Serial_printf("Image 2 valid: %s\r\n",
                  img2_status == BL_OK ? "Yes" : "No");
}
```

## Memory Layout Configuration

Verify your hex file memory regions match the bootloader expectations:

```
Bootloader:      0x000000 - 0x001FFF (compile separate project)
Application 1:   0x002000 - 0x0AFFFF (current project)
Application 2:   0x0B0000 - 0x15AFFF (optional future image)
Metadata:        0x15B000 - 0x15BFFE (pre-programmed or dynamic)
```

### Check with Symbolic Debugger

1. Build and load hex file
2. In debugger: Memory window → Enter address 0x002000
3. Should show your application code starting there

## Compilation Issues and Solutions

### Issue: "bootloader.h: No such file or directory"

**Solution:**
- Verify bootloader.h is in project directory
- Check project include paths in Project Properties
- Clean and rebuild project

### Issue: Duplicate symbol definitions

**Solution:**
- Check you're not including bootloader twice
- Verify #ifndef guards in header files
- Use Search → Find Usages to check

### Issue: Flash size exceeded

**Solution:**
- Bootloader is ~5-10KB depending on optimization
- Application gets ~680KB
- If code too large, enable optimization: -O2

## CAN Interface Verification

### Verify CAN Bus is Working

```c
void verify_can_bootloader(void) {
    // Send START_DOWNLOAD to Image 1
    CAN_MSG_OBJ msg;
    msg.msgId = 0x100;  // BL_CMD_START_DOWNLOAD
    msg.field.dlc = CAN_DLC_8;
    msg.field.frameType = CAN_FRAME_DATA;
    msg.field.idType = CAN_FRAME_STD;
    
    uint8_t data[8] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    msg.data = data;
    
    CAN1_Transmit(CAN_PRIORITY_HIGH, &msg);
    
    // Check for response
    Delay_ms(100);
    CAN_MSG_OBJ response;
    if (CanReceive(&response)) {
        Serial_printf("Response received: ID=0x%03X\r\n", response.msgId);
    }
}
```

## Production Deployment Checklist

- [ ] Bootloader code compiles without errors
- [ ] CAN interface tested and working
- [ ] Memory map verified (bootloader @ 0x000000)
- [ ] Sample firmware update tested
- [ ] Image 1 marked as valid in metadata
- [ ] Timeout handling working
- [ ] Error states properly handled
- [ ] Serial console logging for debugging
- [ ] Host firmware update tool tested

## Host Firmware Update Tool

### Requirements

Your host needs to:
1. Calculate CRC32 of firmware
2. Send firmware in 6-byte blocks
3. Handle CAN communication at 250 kbps or configured baud
4. Implement timeout/retry logic
5. Display progress to user

### Python Example (PyCANOpen / python-can)

```python
import can
import struct
import zlib
from time import sleep

def update_firmware_can(fw_file, target_image=0):
    bus = can.interface.Bus(channel='vcan0', bustype='virtual')
    
    # Read firmware
    with open(fw_file, 'rb') as f:
        firmware = f.read()
    
    # Calculate CRC32
    crc = zlib.crc32(firmware) & 0xffffffff
    
    try:
        # START_DOWNLOAD
        checksum = 0x01 ^ target_image
        msg = can.Message(
            arbitration_id=0x100,
            data=[0x01, target_image, 0, 0, 0, 0, 0, checksum],
            is_extended_id=False
        )
        bus.send(msg)
        print(f"Sent START_DOWNLOAD to image {target_image}")
        
        # WRITE_BLOCKs
        for offset in range(0, len(firmware), 6):
            block = firmware[offset:offset+6]
            length = len(block)
            
            checksum = length
            data = [length] + list(block) + [0] * (6 - length)
            checksum ^= checksum.to_bytes(6, 'little')
            checksum = checksum[0]  # Final XOR result
            
            data.append(checksum)
            msg = can.Message(
                arbitration_id=0x101,
                data=data,
                is_extended_id=False
            )
            bus.send(msg)
            sleep(0.02)
            print(f"Block {offset//6}: {len(block)} bytes")
        
        # COMMIT_IMAGE
        crc_bytes = list(struct.pack('<I', crc))  # Little-endian
        checksum = 0x04
        for b in crc_bytes:
            checksum ^= b
        
        msg = can.Message(
            arbitration_id=0x102,
            data=[0x04] + crc_bytes + [0, 0, checksum],
            is_extended_id=False
        )
        bus.send(msg)
        print("Sent COMMIT_IMAGE")
        print(f"Firmware CRC32: 0x{crc:08X}")
        
    finally:
        bus.shutdown()
```

## Troubleshooting

### Bootloader doesn't respond to CAN messages

1. Verify CAN bus initialization in main:
   - Call `SYSTEM_Initialize()` which should init CAN1
   - Check CAN transceiver connected properly

2. Verify CAN message format:
   - Must be 8 bytes exactly
   - Checksum must be present at byte 7
   - ID must be 0x100-0x105

3. Enable debug output:
   ```c
   #define DEBUG_BOOTLOADER 1
   ```

### "Bootloader not initialized" error

Ensure `BL_Init()` called before `BL_HandleCANFrame()`:
```c
void main(void) {
    SYSTEM_Initialize();
    BL_Init();  // Must call this first!
    // ... rest of code
}
```

### Flash write fails

- Verify flash module initialized in MCC
- Check FLASH_Initialize() called
- Verify memory addresses within valid range
- Check for VDD supply issues

## Next Steps

1. Build and program bootloader to device
2. Verify via serial console
3. Test CAN communication
4. Develop host firmware update tool
5. Document your specific CAN IDs and bitrates
6. Add to production deployment process

## Support Resources

- See BOOTLOADER_DOCUMENTATION.md for protocol details
- Check bootloader.h for API documentation
- Review bootloader.c for implementation details
- dsPIC33EP512GP504 datasheet for flash programming

## Version Information

- Bootloader Version: 1.0
- Target Device: dsPIC33EP512GP504
- CAN Protocol Version: 1.0
- Memory Map Version: 1.0

Date: 2024
