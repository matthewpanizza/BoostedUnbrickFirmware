# Bootloader Quick Reference

## Files Created/Modified

| File | Type | Purpose |
|------|------|---------|
| bootloader.h | NEW | API definitions and types |
| bootloader.c | NEW | Core implementation (1500 LOC) |
| bootloader_config.h | NEW | Memory map and constants |
| CANBus.c | MODIFIED | Added bootloader handlers |
| CANBus.h | MODIFIED | Added function declarations |
| bootloader_linker.gld | NEW | Linker script reference |
| bootloader_update_tool.py | NEW | Host update tool |

## Memory Layout (Fixed)

```
Bootloader:   0x000000-0x001FFF (8 KB)
Image 1:      0x002000-0x0AFFFF (688 KB)
Image 2:      0x0B0000-0x15AFFF (688 KB)
Metadata:     0x15B000-0x15BFFE (4 KB)
```

## CAN Command Reference

| ID | Name | Payload | Response |
|----|------|---------|----------|
| 0x100 | START_DOWNLOAD | [Image: 0\|1] | Status |
| 0x101 | WRITE_BLOCK | [Data: 1-6 bytes] | (async) |
| 0x102 | COMMIT_IMAGE | [CRC32: 4 bytes] | Status |
| 0x103 | VERIFY_IMAGE | [Image: 0\|1] | Status |
| 0x105 | ABORT | (none) | Status |

**Response ID:** 0x200 with [Status, State, ...] data

## Frame Format (All CAN Messages)

```
Byte 0:   Payload length (0-6)
Byte 1-6: Payload data
Byte 7:   XOR Checksum of bytes 0-6
```

## Integration Checklist

- [ ] Copy bootloader.h, bootloader.c, bootloader_config.h to project
- [ ] Add bootloader.c to source files in MPLAB X
- [ ] Add `#include "bootloader.h"` to main.c
- [ ] Call `BL_Init()` after `SYSTEM_Initialize()`
- [ ] Add CAN receiver loop with `CanProcessBootloaderMessage()`
- [ ] Build and verify no linker conflicts
- [ ] Program device
- [ ] Test with bootloader_update_tool.py

## Basic Usage in main.c

```c
#include "bootloader.h"

int main(void) {
    SYSTEM_Initialize();
    BL_Init();
    
    while(1) {
        CAN_MSG_OBJ msg;
        if (CanReceive(&msg)) {
            BL_STATUS_t status = CanProcessBootloaderMessage(&msg);
            CanSendBootloaderStatus(status);
        }
        BL_Task();
    }
    return 0;
}
```

## Function Summary

**Initialization**
- `BL_Init()` - Initialize bootloader
- `BL_ShouldEnterBootloader()` - Check entry conditions

**CAN Communication**
- `BL_HandleCANFrame()` - Process received frame
- `CanProcessBootloaderMessage()` - Helper for main loop
- `CanSendBootloaderStatus()` - Send status response

**Control**
- `BL_Abort()` - Cancel current operation
- `BL_GetState()` - Get current state
- `BL_GetLastError()` - Get last error code

**Image Management**
- `BL_ValidateImage()` - Check image validity
- `BL_SetActiveImage()` - Set active image
- `BL_GetActiveImage()` - Get active image
- `BL_CalculateImageCRC()` - Compute CRC32
- `BL_EraseImage()` - Erase image space
- `BL_JumpToImage()` - Execute image

**Support**
- `BL_Task()` - Periodic housekeeping

## State Machine

```
        START_DOWNLOAD
             ↓
        [DOWNLOAD_ACTIVE]
             ↓
        WRITE_BLOCK (repeat)
             ↓
        COMMIT_IMAGE
             ↓
        [PROGRAMMING]
             (verify CRC & metadata)
             ↓
        Return to [IDLE]
```

## Update Sequence (Host)

```python
# Python example
updater = BootloaderUpdater(channel='COM1', bitrate=250000)
updater.connect()
updater.update('firmware.hex', target_image=0)
updater.disconnect()
```

Command sequence:
1. START_DOWNLOAD(0) → Erase Image 1
2. WRITE_BLOCK (repeat) → Send firmware 6 bytes at a time
3. COMMIT_IMAGE(crc32) → Verify and mark valid
4. Monitor status via 0x200 responses

## Status Codes

| Code | Meaning |
|------|---------|
| 0x00 | Success (BL_OK) |
| 0x01 | Invalid command |
| 0x02 | Invalid state |
| 0x03 | Checksum failed |
| 0x04 | Flash error |
| 0x07 | CRC mismatch |
| 0x08 | Image full |

## Metadata Region Layout

| Offset | Field | Size | Notes |
|--------|-------|------|-------|
| 0x000 | Image1_Valid | 2 B | 0xA5A5=valid |
| 0x002 | Image2_Valid | 2 B | 0xA5A5=valid |
| 0x004 | ActiveImage | 2 B | 0=Img1, 1=Img2 |
| 0x006 | Image1_CRC | 4 B | CRC32 value |
| 0x00A | Image2_CRC | 4 B | CRC32 value |
| 0x00E | BootCount | 2 B | 16-bit counter |

## CRC32 Calculation

```python
import zlib
firmware_crc = zlib.crc32(firmware_data) & 0xffffffff
# Return as 4 bytes little-endian in COMMIT_IMAGE
```

## Python Tool Usage

```bash
# Basic
python3 bootloader_update_tool.py firmware.hex

# With options
python3 bootloader_update_tool.py application.hex \
    --image 1 \
    --can-channel COM1 \
    --bitrate 250000 \
    --timeout 2.0
```

## Debugging Commands

```c
// Print bootloader status
Serial_printf("State: %d, Error: %d\n", 
              BL_GetState(), BL_GetLastError());

// Verify images
BL_STATUS_t img1 = BL_ValidateImage(BL_IMAGE_1);
BL_STATUS_t img2 = BL_ValidateImage(BL_IMAGE_2);

// Get active image
uint8_t active = BL_GetActiveImage();

// Calculate CRC of image
uint32_t crc = BL_CalculateImageCRC(BL_IMAGE_1);
```

## Common Issues

| Problem | Solution |
|---------|----------|
| Build error: bootloader.h not found | Check project include paths |
| CAN messages not received | Verify CAN1 initialized, message format 8 bytes |
| Flash write fails | Check VDD, verify FLASH module initialized |
| CRC mismatch | Verify firmware file integrity, no CAN drops |
| Bootloader doesn't respond | Call BL_Init() before main loop |

## Timeout Values (ms)

```c
#define BL_ENTRY_TIMEOUT        5000    // Entry window
#define BL_BLOCK_RX_TIMEOUT     1000    // Per-block timeout
```

## Flash Writing

Two implementation options:

**Option A: Simple (current)**
- Write word-by-word (slower, simpler)
- See `BL_WriteFlashWord()` in bootloader.c

**Option B: Optimized**
- Use FLASH page buffering
- Modify bootloader.c `BL_WriteFlashWord()` implementation

## Building for Dual Images

**Method 1: Single Project** (simplest)
- All code in one project, loads at 0x000000
- Bootloader + App both included

**Method 2: Separate Projects** (production)
- Bootloader project: Links to 0x000000
- App project: Links to 0x002000
- Combine hex files in post-build step

## Verification Checklist

- [ ] No linker warnings/errors
- [ ] Memory layout matches bootloader_config.h
- [ ] CAN messages received and processed
- [ ] Status responses sent on ID 0x200
- [ ] Test WRITE_BLOCK processing
- [ ] Verify metadata updates working
- [ ] Confirm image switching works
- [ ] Full update cycle completes successfully

## Advanced Customization

### Change bootloader size
Edit `bootloader_config.h`:
```c
#define BL_END_ADDR 0x003FFF       // 16 KB instead of 8 KB
#define APP_IMAGE1_START_ADDR 0x004000
```

### Change block size
CAN limitation: max 6 bytes per WRITE_BLOCK
- Smaller blocks: Send multiple blocks
- Larger frames: Requires protocol redesign

### Add boot counter
Already implemented in metadata:
```c
uint16_t boot_count = *(uint16_t*)(METADATA_START_ADDR + 0x00E);
```

## Production Deployment

1. **Bootloader binary** (0x000000-0x001FFF)
2. **App binary** (0x002000-0x0AFFFF)
3. **Combined hex file** for factory programming
4. **Update tool** for field updates

## Next Steps

1. Add files to MPLAB X project
2. Update main.c with BL_Init() and message routing
3. Run `make clean; make build=production`
4. Program device with xxd programmer
5. Test CAN communication
6. Deploy update tool to production

---

**For details:** See BOOTLOADER_DOCUMENTATION.md  
**For integration:** See BOOTLOADER_INTEGRATION_GUIDE.md  
**For API:** See bootloader.h comments  
