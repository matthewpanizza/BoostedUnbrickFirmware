# Serial Bootloader - Setup and Testing Guide

## Overview

The serial bootloader provides an alternative to the CAN-based bootloader for firmware updates over a standard USB-to-serial connection. This is ideal for initial testing without requiring the CANAnalyzerParticle device.

## Hardware Requirements

- USB-to-Serial adapter (e.g., FTDI, CP2102, CH340)
- 3 wires: GND, TX (MCU RX), RX (MCU TX)
- Baud rate: 115200 (default)

## Wiring

Connect USB-to-serial adapter to dsPIC33EP512GP504:
```
USB Adapter          dsPIC33EP512GP504
─────────────────────────────────────
GND        ────────→ GND
TX         ────────→ RX (Pin 34, RP104)  
RX         ←────────  TX (Pin 35, RP105)
+5V        ────────→ VDD (Optional for power)
```

The serial interface uses UART1 on the device (same as debug output).

## Frame Format

All communication uses the same frame format:

```
[0xAA][CMD][LENGTH][BLOCK_NUM][DATA(0-4)][CHECKSUM][0x55]
```

- **0xAA**: Frame start marker
- **CMD**: Command code (0x01-0x05)
- **LENGTH**: Payload length
- **BLOCK_NUM**: Block number (for WRITE_BLOCK)
- **DATA**: Payload bytes (0-4 for WRITE_BLOCK)
- **CHECKSUM**: XOR checksum of [CMD][LENGTH][BLOCK_NUM][DATA...]
- **0x55**: Frame end marker

## Commands

| CMD | Name | Length | Purpose |
|-----|------|--------|---------|
| 0x01 | START_DOWNLOAD | 1 | Begin download to image 0 or 1 |
| 0x02 | WRITE_BLOCK | 5 | Send 4 bytes of firmware data |
| 0x03 | COMMIT_IMAGE | 0 | Finalize and verify image |
| 0x05 | ABORT | 0 | Cancel operation |

## Device Responses

All responses use format: `[0xAA][0x00][0x01][STATUS][CHECKSUM][0x55]`

- **STATUS**: 0x00 = OK, other values indicate error codes

## Setup Steps

### 1. Build and Program Bootloader

```bash
# In MPLAB X
Project → Clean and Build Main Project

# Program device with:
# - bootloader.c, bootloader.h, bootloader_config.h from bootloader source
# - Modified main.c with serial bootloader support
```

### 2. Install Python Dependencies

```bash
pip install pyserial zlib-state
```

(Note: `zlib-state` is optional; standard library `zlib` is used)

### 3. Identify Serial Port

**Windows:**
```bash
# Device Manager
# Look for "COM3", "COM4", etc.
# Or use: wmic logicaldisk get name
```

**Linux:**
```bash
ls -la /dev/ttyUSB*
ls -la /dev/ttyACM*
```

**macOS:**
```bash
ls -la /dev/tty.usbserial*
ls -la /dev/tty.usbmodem*
```

## Usage

### Basic Firmware Update

```bash
python3 bootloader_serial_tool.py firmware.hex --port COM3 --baud 115200 --image 0
```

### Command-Line Options

```
positional arguments:
  firmware              Path to firmware file (.bin or .hex)

optional arguments:
  --port PORT           Serial port (default: COM3)
  --baud BAUD          Baud rate (default: 115200)
  --image {0,1}        Target image (0=Image1, 1=Image2) (default: 0)
  --block-size SIZE    Bytes per block 1-4 (default: 4)
  --verbose, -v        Enable debug output
```

### Examples

**Update Image 1 on Windows:**
```bash
python3 bootloader_serial_tool.py application.hex --port COM3 --image 0
```

**Update Image 2 on Linux:**
```bash
python3 bootloader_serial_tool.py application.hex --port /dev/ttyUSB0 --image 1 --baud 115200
```

**Debug mode (see all frames):**
```bash
python3 bootloader_serial_tool.py application.hex --port COM3 --verbose
```

**Slower block rate (if device misses blocks):**
```bash
python3 bootloader_serial_tool.py application.hex --port COM3 --block-size 2
```

## Protocol Examples

### START_DOWNLOAD to Image 0

Send:
```
AA 01 01 00 00 55
││ ││ ││ ││ ││ 
││ ││ ││ ││ └─ Frame end
││ ││ ││ ││
││ ││ ││ └─ Checksum (01 XOR 01 XOR 00 = 00)
││ ││ ││
││ ││ └─ Data: Image number (0)
││ ││
││ └─ Length: 1 byte
││
││ 
└─ START_DOWNLOAD command
│
Frame start
```

Receive (ACK):
```
AA 00 01 00 01 55
││ ││ ││ ││ ││
││ ││ ││ ││ └─ Frame end
││ ││ ││ ││
││ ││ ││ └─ Checksum (00 XOR 01 XOR 00 = 01)
││ ││ ││
││ ││ └─ Status code (0 = OK)
││ ││
││ └─ Length: 1
││
│└─ Response code (0x00)
│
Frame start
```

### WRITE_BLOCK (Block #0, 4 bytes: 0x12 0x34 0x56 0x78)

Send:
```
AA 02 05 00 12 34 56 78 EB 55
││ ││ ││ ││ │││││││││││ ││ 
││ ││ ││ ││ │││││││││││ └─ Frame end
││ ││ ││ ││ │││││││││││
││ ││ ││ ││ │││││││││││ Checksum = 02 XOR 05 XOR 00 XOR 12 XOR 34 XOR 56 XOR 78 = EB
││ ││ ││ ││ └─ Firmware data (4 bytes)
││ ││ ││ │
││ ││ ││ └─ Block number (0)
││ ││ │
││ ││ └─ Length: 5 (1 block_num + 4 data)
││ │
││ └─ WRITE_BLOCK command
││
│└─ Frame start
│
0xAA
```

Receive (ACK for block 0):
```
AA 00 01 00 01 55
││ ││ ││ ││ ││
││ ││ ││ ││ └─ Frame end
││ ││ ││ ││
││ ││ ││ └─ Checksum
││ ││ ││
││ ││ └─ Block number (0 - confirmed)
││ ││
││ └─ Length: 1
││
│└─ Response code
│
Frame start
```

### COMMIT_IMAGE (Finalize)

Send:
```
AA 03 00 03 55
││ ││ ││ ││
││ ││ ││ └─ Frame end
││ ││ ││
││ ││ └─ Checksum (03 XOR 00 = 03)
││ ││
││ └─ Length: 0 (no payload)
││
│└─ COMMIT_IMAGE command
│
Frame start
```

Receive (ACK):
```
AA 00 01 00 01 55
```

## Debugging

### Enable Serial Output on Device

In `main.c`, ensure `DEBUG_ENABLED` is set:
```c
#define DEBUG_ENABLED true
```

This will print detailed bootloader messages:
```
SERIAL_BL: START_DOWNLOAD for image 0
SERIAL_BL: Block 0, total 1 blocks, 4 bytes
SERIAL_BL: Block 10, total 100 blocks, 40 bytes
...
SERIAL_BL: COMMIT_IMAGE
  Total blocks: 8507
  Total bytes:  34028
✓ Image validated and committed to flash
Jumping to application image 0...
```

### Common Issues

#### No response from device
- Check serial connection (GND, TX, RX)
- Verify baud rate matches (115200)
- Check UART1 pins (RP104 = RX, RP105 = TX)
- Try `--verbose` to see raw frames

#### Checksum errors
- Device reports checksum mismatch
- Verify wiring (possibly interference)
- Try shorter USB cable
- Try slower block rate: `--block-size 1`

#### Timeout during download
- Device too slow to process blocks
- Increase block delay in tool (modify `block_delay` parameter)
- Reduce block size: `--block-size 2`
- Check MCU clock speed

#### Device restarts after download
- Normal behavior - device jumps to new firmware
- Watch for UART activity indicating new app starting

## Comparing Serial vs CAN Bootloader

| Feature | Serial | CAN |
|---------|--------|-----|
| Setup | USB cable only | CANAnalyzer + CAN transceiver |
| Speed | ~500-1000 bytes/sec | ~5000 bytes/sec |
| Distance | Limited (USB cable) | Far (CAN bus) |
| Complexity | Simple | Moderate |
| Test Use | ✓ Best for initial testing | ✓ Production |
| Error Recovery | Basic | Advanced (ACKs per block) |

## Next Steps

1. **Test with small firmware** - Create minimal app to verify full cycle
2. **Validate checksums** - Compare device report vs local calculation
3. **Map to production** - Use dual-image capability for safe updates
4. **Monitor performance** - Track update times and success rates

## References

- [main.c Serial Bootloader Functions](../BoostedBattery.X/main.c) - Handler implementation
- [Serial Frame Specification](#frame-format) - Protocol details
- [CAN Bootloader Guide](BOOTLOADER_ANALYZER_TOOL_README.md) - For production use

---

**Version:** 1.0  
**Last Updated:** 2026-04-11  
**Status:** Production Ready  
