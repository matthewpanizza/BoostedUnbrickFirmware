# Bootloader System - Quick Start Guide

## Overview

This is a complete CAN Bus bootloader system for the dsPIC33EP512GP504 microcontroller in the Boosted Battery Pack. It enables firmware updates over CAN without physical programming tools.

**Key Features:**
- Dual-image architecture (two application slots)
- CAN-based command protocol
- Hardware device support (CANAnalyzerParticle)
- Automatic image validation and selection
- LED feedback (magenta during download)
- Periodic status reporting
- 10-second inactivity timeout

## System Architecture

### Memory Layout

| Region | Address | Size | Purpose |
|--------|---------|------|---------|
| Bootloader | 0x000000 | 8 KB | Firmware updater (always resident) |
| Image 1 | 0x002000 | 688 KB | Application slot 1 |
| Image 2 | 0x0B0000 | 688 KB | Application slot 2 |
| Metadata | 0x15B000 | 4 KB | Validity flags, CRC, active image |
| **Total** | | **512 KB** | Entire flash capacity filled |

### Hardware Interfaces

```
PC/Host
  ↓ USB
CANAnalyzerParticle (Serial 115200 baud)
  ↓ CAN (250/500 kbps)
dsPIC33EP512GP504
  ├─ CAN1: Receive bootloader commands, send status
  ├─ I2C2: Control TLC59108 LED (magenta = downloading)
  └─ UART1: Debug messages to serial console
```

## Files Overview

### Core Bootloader Implementation

| File | Lines | Purpose |
|------|-------|---------|
| `bootloader_config.h` | 155 | Memory addresses, constants, metadata offsets |
| `bootloader.h` | 231 | Public API, data structures, #defines |
| `bootloader.c` | 625 | State machine, CAN handlers, flash ops, CRC32 |
| `bootloader_linker.gld` | 94 | Reference linker script for dual-image layout |

### Host Tools

| File | Lines | Purpose |
|------|-------|---------|
| `bootloader_update_tool.py` | 431 | Direct CAN interface (python-can library) |
| `bootloader_update_tool_analyzer.py` | 524 | CANAnalyzerParticle serial interface |

### Device Integration

| File | Lines | Purpose |
|------|-------|---------|
| `main.c` (modified) | +214 | Bootloader message handlers, LED/status feedback |
| `CANBus.c` (modified) | +20 | CAN message routing to bootloader |
| `CANBus.h` (modified) | +3 | Function declarations |

### Documentation

| File | Purpose |
|------|---------|
| `BOOTLOADER_DOCUMENTATION.md` | Complete protocol specification |
| `BOOTLOADER_INTEGRATION_GUIDE.md` | How to integrate into your project |
| `BOOTLOADER_SUMMARY.md` | Architecture overview |
| `BOOTLOADER_QUICK_REFERENCE.md` | Command reference |
| `BOOTLOADER_ANALYZER_TOOL_README.md` | Hardware tool usage |
| `MAIN_C_BOOTLOADER_INTEGRATION.md` | main.c changes explained |

## Quick Start: 5-Minute Setup

### Step 1: Add Bootloader Files to MPLAB Project

1. Open **BoostedBattery.X** in MPLAB X IDE
2. Right-click **Source Files** → **Add Existing Item**
3. Select these three files:
   - `bootloader_config.h`
   - `bootloader.h`
   - `bootloader.c`
4. Click **Select** → **Open**
5. Files now visible in Source Files tree

### Step 2: Include Bootloader Header in main.c

At the top of `main.c`, add:

```c
#include "bootloader.h"
```

Already done in provided modifications, but verify it's there.

### Step 3: Initialize Bootloader in main()

Call this once during startup, after CAN is initialized:

```c
int main(void) {
    // ... existing initialization code ...
    
    // Initialize CAN1
    CAN1_Initialize();
    
    // NEW: Initialize bootloader
    BL_Init();
    
    // ... rest of main ...
}
```

### Step 4: Build Project

In MPLAB X:
- **Project** → **Clean and Build Main Project**
- Should compile without errors
- Hex file created in `BoostedBattery.X/dist/default/production/`

If you get errors:
- Check that `bootloader_config.h` is in project source tree
- Verify `#include "bootloader.h"` path is correct
- See **Troubleshooting** section below

### Step 5: Program Device

1. Connect Pickit 4 to dsPIC33EP512GP504
2. In MPLAB X: **Run** → **Program Device**
3. Device now has bootloader installed

### Step 6: Test with CANAnalyzerParticle

Connect analyzer to CAN bus and test:

```bash
python3 bootloader_update_tool_analyzer.py dummy_firmware.hex --port COM3 --image 0
```

Watch:
- LED turns **magenta** (R=255, G=0, B=255)
- Serial console: "Bootloader: Download mode started"
- After 5 seconds: "BL Status: 0 blocks, 0 bytes"
- Device exits bootloader mode after 10 seconds

**Success!** ✓ Bootloader is working.

## CAN Protocol Reference

### Command Summary

| CAN ID | Command | Payload | Response |
|--------|---------|---------|----------|
| 0x100 | START_DOWNLOAD | [length=2][image][crc_h][crc_m][crc_l][0][0] [chk] | 0x200 status |
| 0x101 | WRITE_BLOCK | [length=6][6 data bytes][checksum] | (no response) |
| 0x102 | COMMIT_IMAGE | [length=1][0] [0][0][0][0][0][0] [chk] | 0x200 status |
| 0x103 | VERIFY_IMAGE | [length=1][image][0][0][0][0][0] [chk] | 0x200 status/CRC |
| 0x105 | ABORT | [length=1][0] [0][0][0][0][0][0] [chk] | 0x200 status |

### Response Messages

| CAN ID | Meaning | Payload |
|--------|---------|---------|
| 0x200 | Status Response | [status_code][state_code][...] |
| 0x201 | Progress Update | [0x03][block_count][bytes_lo][bytes_hi][0][0][0][chk] |

### Example: Update Sequence

```
PC sends:   CAN 0x100 [length=2][0x00][crc_value]
Device:    LED = Magenta, enters download mode
           Sends: CAN 0x201 [0x03][0][0][0]... every 5 seconds

PC sends:   CAN 0x101 [length=6][64 bytes of firmware] × 100
Device:    Increments counters, updates timestamp
           Internally increments block counter

PC sends:   CAN 0x102 [length=1][0]
Device:    Validates firmware, updates metadata
           LED = Off, returns to normal
           Sends: CAN 0x200 [success_code]...
```

## Python Tool Usage

### Option A: Direct CAN Interface (Linux/Mac)

```bash
# Install dependencies
pip install python-can zlib-state

# Update firmware to Image 1
python3 bootloader_update_tool.py firmware.hex \
    --image 0 \
    --can-channel vcan0 \
    --bitrate 250000
```

### Option B: CANAnalyzerParticle Hardware (Windows/Linux/Mac)

```bash
# Install dependencies
pip install pyserial zlib-state

# Find COM port with CANAnalyzerParticle
# Windows: Device Manager → COM3, COM4, etc.
# Linux: ls -la /dev/ttyUSB*
# Mac: ls -la /dev/tty.usbserial*

# Update firmware to Image 1
python3 bootloader_update_tool_analyzer.py firmware.hex \
    --port COM3 \
    --baud 115200 \
    --image 0 \
    --timeout 2.0
```

### Command-Line Options

**bootloader_update_tool.py:**
```
--image {0,1}          Target image slot (default: 0)
--can-channel          CAN interface name (default: vcan0)
--bitrate {250k,500k}  CAN bitrate (default: 250k)
--verbose              Enable debug output
```

**bootloader_update_tool_analyzer.py:**
```
--port COM/DEV         Serial port (required)
--baud {9600...115200} Serial baud rate (default: 115200)
--image {0,1}          Target image slot (default: 0)
--timeout SECONDS      Message timeout (default: 2.0)
--verbose              Enable debug output
```

## Troubleshooting

### Problem: "bootloader.h: No such file or directory"

**Solution:**
1. Right-click project in MPLAB X → **Properties**
2. **Directories** tab
3. Under **Include Search Path**, add: (project root where bootloader.h is)
4. Click **OK** → Rebuild

### Problem: LED doesn't turn magenta during download

**Possible causes:**
1. main.c modifications not applied (verify `handleBootloaderMessage()` exists)
2. LED is broken or disconnected
3. Device not receiving CAN message

**Debug:**
1. Add `Serial_println("0x100 received");` in START_DOWNLOAD handler
2. Check serial console for message
3. Use CAN analyzer to verify 0x100 message sent to device

### Problem: Tool says "No CAN messages received"

**If using CANAnalyzerParticle:**
1. Verify serial port is correct: `python3 bootloader_update_tool_analyzer.py --port COM1`
2. Device may be in wrong mode; restart analyzer and device
3. Check CAN bus termination (120Ω resistors at both ends)

**If using direct CAN interface:**
1. Verify CAN interface exists: `ip link show | grep vcan`
2. If missing, create virtual CAN: `sudo ip link add dev vcan0 type vcan`
3. Bring up interface: `sudo ip link set vcan0 up`

### Problem: Firmware update fails with CRC error

**Solution:**
1. Verify Intel HEX file is valid: `hexdump -C firmware.hex | head`
2. Ensure image doesn't exceed 688 KB
3. Try again; CRC mismatch could be transient CAN error
4. Check CAN bus noise with analyzer: `python3 bootloader_update_tool_analyzer.py --port COM3 --verbose`

### Problem: Device hangs or won't exit bootloader mode

**Solution:**
1. Bootloader has 10-second inactivity timeout (resets automatically)
2. Can force exit by sending ABORT (0x105)
3. If device unresponsive, power cycle (removes bootloader from RAM)
4. Check for physical CAN problems (bus collision, no termination)

## Status LED Behavior

| LED Color | Meaning |
|-----------|---------|
| **Off** | Normal operation |
| **Magenta** (R=255, G=0, B=255) | Bootloader active, waiting for commands |
| **Green** (R=0, G=255, B=0) | Normal charge indicator (from main app) |
| Other | Battery pack application LED indication |

## Memory Configuration Details

### Metadata Region (0x15B000)

Stores image validity flags and bookkeeping:

```c
typedef struct {
    uint16_t image1_valid;    // 0xA5A5 = valid, 0xFFFF = invalid
    uint16_t image2_valid;    // 0xA5A5 = valid, 0xFFFF = invalid
    uint16_t active_image;    // 0 = Image1, 1 = Image2
    uint32_t image1_crc32;    // CRC32 of Image 1
    uint32_t image2_crc32;    // CRC32 of Image 2
    uint16_t boot_count;      // Number of boots
    uint8_t  reserved[4];     // Future
} BL_METADATA_t;
```

### Bootloading to Specific Image

```bash
# Update Image 1 (0x002000-0x0AFFFF)
python3 bootloader_update_tool_analyzer.py app1.hex --image 0

# Update Image 2 (0x0B0000-0x15AFFF)
python3 bootloader_update_tool_analyzer.py app2.hex --image 1
```

Device automatically switches to most recent valid image on next boot.

## Advanced: Dual-Image Workflow

**Scenario:** You want to update firmware with rollback capability.

### Step 1: Current State
- Device has Image 1 (v1.0) loaded and running
- Want to test Image 2 (v2.0) without losing v1.0

### Step 2: Update Image 2
```bash
python3 bootloader_update_tool_analyzer.py firmware_v2.0.hex --image 1 --port COM3
```

### Step 3: Switch to Image 2
After update completes, device automatically boots Image 2.

### Step 4: If v2.0 has problems
```bash
# Device will revert to Image 1 on next boot if v2.0 fails CRC
# Or manually switch: perform update to Image 1 again to revert
```

## Next Steps

1. **Integrate into project**: Follow "Quick Start: 5-Minute Setup" above
2. **Test with hardware**: Connect CANAnalyzerParticle and run tool
3. **Deploy to production**: Include bootloader in production firmware builds
4. **Monitor**: Check CAN 0x201 status messages for confident updates

## Support Files

For detailed information, see:

- **Protocol details**: [BOOTLOADER_DOCUMENTATION.md](BOOTLOADER_DOCUMENTATION.md)
- **Integration help**: [BOOTLOADER_INTEGRATION_GUIDE.md](BOOTLOADER_INTEGRATION_GUIDE.md)
- **Command reference**: [BOOTLOADER_QUICK_REFERENCE.md](BOOTLOADER_QUICK_REFERENCE.md)
- **Hardware setup**: [BOOTLOADER_ANALYZER_TOOL_README.md](BOOTLOADER_ANALYZER_TOOL_README.md)
- **main.c explanation**: [MAIN_C_BOOTLOADER_INTEGRATION.md](MAIN_C_BOOTLOADER_INTEGRATION.md)

## Version Information

| Component | Version | Status |
|-----------|---------|--------|
| Bootloader | 1.0 | Complete |
| Protocol | 1.0 | Stable |
| Python Tools | 1.0 | Tested |
| Device Integration | 1.0 | Ready |
| Documentation | 1.0 | Complete |

---

**Questions?** Check the troubleshooting section or review detailed documentation files.

**Ready to start?** Follow the "Quick Start: 5-Minute Setup" section, then test with your CANAnalyzerParticle device.

Good luck! 🚀
