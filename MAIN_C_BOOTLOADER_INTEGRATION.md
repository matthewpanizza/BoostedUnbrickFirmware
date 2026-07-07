# Main.c Bootloader Integration Summary

## Changes Made to Support CAN Bootloader

This document summarizes the modifications made to `main.c` to support the CAN-based bootloader firmware update feature.

## New Global Variables

Added bootloader-related tracking variables:

```c
// Bootloader variables
volatile bool bootloaderDownloadActive;      // Current download status
volatile uint32_t bootloaderBlocksReceived;  // Number of blocks received
volatile uint32_t bootloaderBytesReceived;   // Total bytes received
volatile uint64_t bootloaderLastMessageTime; // Timestamp of last message

#define BOOTLOADER_STATUS_TIMEOUT    5000    // Send status every 5 seconds
#define BOOTLOADER_DOWNLOAD_TIMEOUT  10000   // Exit if no activity for 10 seconds
```

These variables track:
- **bootloaderDownloadActive**: Set true when START_DOWNLOAD (0x100) received
- **bootloaderBlocksReceived**: Incremented on each WRITE_BLOCK (0x101)
- **bootloaderBytesReceived**: Accumulates bytes from all blocks
- **bootloaderLastMessageTime**: Used for timeout detection

## Function Prototypes

Added three new bootloader function prototypes:

```c
void handleBootloaderMessage(CAN_MSG_OBJ *msg);    // Route bootloader commands
void bootloaderSendStatusMessage(void);             // Send periodic progress
void bootloaderExitDownloadMode(void);              // Cleanup and exit
```

## CAN Message Routing

Modified the main loop's CAN message reception to check for bootloader commands first:

**Before:**
```c
while(CanReceive(&recCanMsg)){
    uint32_t maskedID = recCanMsg.msgId & 0xFFFFFFF0;
    switch(maskedID){ ... }
}
```

**After:**
```c
while(CanReceive(&recCanMsg)){
    // Check for bootloader commands first (CAN IDs 0x100-0x105)
    if(recCanMsg.msgId >= 0x100 && recCanMsg.msgId <= 0x105) {
        handleBootloaderMessage(&recCanMsg);
        continue;  // Skip normal message processing
    }
    
    uint32_t maskedID = recCanMsg.msgId & 0xFFFFFFF0;
    switch(maskedID){ ... }
}
```

This ensures bootloader messages are intercepted before normal ESC/battery messages.

## Periodic Status Sending

Added periodic status message transmission in the main loop:

```c
// Bootloader periodic status sending
if(bootloaderDownloadActive) {
    bootloaderSendStatusMessage();
}
```

This is called every main loop iteration (~100ms). The function itself throttles messages to BOOTLOADER_STATUS_TIMEOUT intervals.

## New Function Implementations

### 1. handleBootloaderMessage()

Processes bootloader command CAN messages:

| CAN ID | Command | Action |
|--------|---------|--------|
| 0x100 | START_DOWNLOAD | Set download mode, LED = magenta, reset counters |
| 0x101 | WRITE_BLOCK | Increment block counter, update timestamp |
| 0x102 | COMMIT_IMAGE | Exit download mode, log final statistics |
| 0x105 | ABORT | Exit download mode immediately |

**LED Feedback:**
- Enters download mode: **LED = Magenta** (R=255, G=0, B=255)
- On any command to exit: **LED = Off** (R=0, G=0, B=0)

### 2. bootloaderSendStatusMessage()

Sends periodic progress updates via CAN:

- **Frequency**: Every 5 seconds (configurable via `BOOTLOADER_STATUS_TIMEOUT`)
- **Message ID**: 0x201 (bootloader status response)
- **Payload**: 3 bytes = [blocks_count] [bytes_low] [bytes_high]
- **Checksum**: XOR of all payload bytes
- **Timeout Detection**: Exits if no messages for 10 seconds (configurable)

**Message Format:**
```
CAN ID: 0x201
Data[0]: 0x03 (payload length = 3)
Data[1]: Block count (0-255)
Data[2]: Bytes received low byte
Data[3]: Bytes received high byte
Data[4-6]: Padding (0x00)
Data[7]: XOR checksum
```

Example progress message:
```
0x201 | 0x03 0x0A 0xE8 0x00 0x00 0x00 0x00 0x00 0x00 | E8 (10 blocks, 232 bytes)
```

### 3. bootloaderExitDownloadMode()

Cleanup function called when download completes or times out:

```c
void bootloaderExitDownloadMode(void) {
    bootloaderDownloadActive = false;
    bootloaderBlocksReceived = 0;
    bootloaderBytesReceived = 0;
    
    // Turn off magenta LED
    LED.R = 0;
    LED.G = 0;
    LED.B = 0;
    updateLEDs();
    
    Serial_println("Bootloader: Download mode ended");
}
```

Actions:
- Clears all bootloader tracking variables
- Turns off the magenta LED
- Returns device to normal operation
- Logs completion to serial console

## Serial Debug Output

The implementation provides optional debug logging:

**When DEBUG_ENABLED or Serial available:**

```
Bootloader: Download mode started
BL: Block 10 (60 bytes total)
BL: Block 20 (120 bytes total)
BL Status: 20 blocks, 120 bytes
...
Bootloader: Image committed
  Total blocks: 200
  Total bytes:  1200
Bootloader: Download mode ended
```

**See serial output with:**
```c
#define DEBUG_ENABLED true  // Enable all debug prints
```

## Integration Points

### Main Loop Structure

```
┌─ System Initialization
├─ LED/BMS Configuration
└─ Main Loop
   ├─ Parse Serial Commands
   ├─ Receive CAN Messages
   │  ├─ Check Bootloader IDs (0x100-0x105)
   │  │  └─ handleBootloaderMessage()
   │  └─ Process ESC/Battery Messages (existing logic)
   ├─ Handle Button Presses
   ├─ [NEW] Send Bootloader Status if Active
   │  └─ bootloaderSendStatusMessage()
   └─ Delay 100ms
```

### LED State During Download

```
Normal Operation:  [LED Shows SOC/Battery Status]
                   ↓
Receive 0x100:     [LED = Magenta] ← Indicates bootloader active
(START_DOWNLOAD)   ↓
                   [Receiving firmware blocks...]
                   ↓
Receive 0x102 or  [LED = Off]      ← Download complete
Timeout:           ↓
                   [Back to normal operation]
```

## Timeout Behavior

The bootloader has two timeout mechanisms:

### 1. Download Timeout (10 seconds)

```c
#define BOOTLOADER_DOWNLOAD_TIMEOUT 10000

if((currentTime - bootloaderLastMessageTime) > BOOTLOADER_DOWNLOAD_TIMEOUT) {
    // Exit bootloader mode
    bootloaderExitDownloadMode();
}
```

If no bootloader messages received within 10 seconds, automatically exit.
Prevents device from getting stuck in bootloader mode.

### 2. Status Sending Interval (5 seconds)

```c
#define BOOTLOADER_STATUS_TIMEOUT 5000

if((currentTime - lastStatusTime) >= BOOTLOADER_STATUS_TIMEOUT) {
    // Send progress message
    CAN1_Transmit(CAN_PRIORITY_HIGH, &statusMsg);
}
```

Sends progress every 5 seconds to host tool showing current block count and bytes received.

## Configuration Options

### Adjust Timeouts

Edit these defines at the top of bootloader section in main.c:

```c
#define BOOTLOADER_STATUS_TIMEOUT        5000   // Change to 1000 for 1 second updates
#define BOOTLOADER_DOWNLOAD_TIMEOUT      10000  // Change to 30000 for 30 second timeout
```

### Change LED Color

Edit in `handleBootloaderMessage()`:

```c
// Current: Magenta
LED.R = 255;  // Red component (0-255)
LED.G = 0;    // Green component (0-255)
LED.B = 255;  // Blue component (0-255)

// For example, to use Cyan instead:
// LED.R = 0;
// LED.G = 255;
// LED.B = 255;
```

### Change Status Message CAN ID

Edit in `bootloaderSendStatusMessage()`:

```c
statusMsg.msgId = 0x201;  // Change this to any unused CAN ID
```

## Message Flow Diagram

```
Host Tool                Device
     │                     │
     ├─ Send 0x100 START ──┤ handleBootloaderMessage()
     │                     ├─ Set LED = Magenta
     │                     ├─ bootloaderDownloadActive = true
     │                     │
     ├─ Send 0x101 BLOCK ──┤ handleBootloaderMessage()
     │    (6 bytes)        ├─ blockCount++
     │                     ├─ bytesCount += 6
     │                     │
     ├─ Send 0x101 BLOCK ──┤ [Repeat for each block]
     │                     │
     │                ┌────┤ bootloaderSendStatusMessage() [every 5 sec]
     │                │    │
     │◄─ Get 0x201 ───┘    │ CAN1_Transmit()
     │   Status            │
     │                     │
     ├─ Send 0x102 COMMIT ─┤ handleBootloaderMessage()
     │   (with CRC)        ├─ bootloaderExitDownloadMode()
     │                     ├─ LED = Off
     │                     ├─ Reset counters
     │                     │
```

## Testing the Integration

### Manual CAN Testing

Using CANAnalyzerParticle device or direct CAN tool:

```
1. Connect to device CAN bus
2. Send: CAN ID 0x100, Data [0x01 0x00 0x00 0x00 0x00 0x00 0x00 0x01]
   (START_DOWNLOAD to Image 1)
3. Observe: LED turns magenta
4. Observe: Serial console shows "Bootloader: Download mode started"
5. Send: CAN ID 0x105 (ABORT)
6. Observe: LED turns off, console shows "Bootloader: Download mode ended"
```

### Automated Testing with Python Tool

```bash
python3 bootloader_update_tool_analyzer.py test_firmware.hex --port COM3 --image 0
```

Watch:
- LED turns magenta when download starts
- Serial console shows progress
- LED turns off when download completes
- Device returns to normal operation

## Compatibility

These changes are **non-intrusive** to existing functionality:

✓ ESC/Battery CAN messages still processed normally  
✓ Button controls still work  
✓ LED SOC display resumes after bootloader complete  
✓ Normal charging/discharging unaffected  
✓ Existing debug output preserved  

The bootloader mode only activates when CAN messages with IDs 0x100-0x105 are received.

## Future Enhancements

Possible improvements:

1. **Rollback on crash**: Auto-switch to previous image if app crashes
2. **Progress bar**: More detailed progress feedback
3. **Image verification**: CRC verification before commit
4. **Watchdog**: Hardware watchdog to force recovery
5. **Multi-image**: Support for 3+ application images

---

**Version:** 1.0  
**Last Updated:** 2024  
**System:** dsPIC33EP512GP504  
