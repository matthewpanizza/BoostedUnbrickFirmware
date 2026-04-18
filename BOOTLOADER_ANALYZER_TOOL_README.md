# CAN Bus Bootloader Update Tool - CANAnalyzerParticle Version

## Overview

This version of the bootloader update tool communicates with a **CANAnalyzerParticle** hardware device instead of directly interfacing with a CAN adapter. The CANAnalyzerParticle is a Particle Photon-based CAN analyzer that has built-in CAN transceiver capabilities.

## Hardware Requirements

- **CANAnalyzerParticle device** (Particle Photon or P2 with MCP2515 CAN shield)
- **USB cable** to connect device to host PC
- **CAN bus connection** from device to dsPIC33EP512GP504 target

## Setup

### 1. Program the CANAnalyzerParticle Device

The CANAnalyzerParticle device should already be running firmware that supports serial command interface (see `CANAnalyzerParticle.cpp`).

### 2. Install Python Dependencies

```bash
pip install pyserial zlib
```

### 3. Identify Serial Port

- **Windows**: Check Device Manager for COM port (typically COM3-COM5)
- **Linux**: Usually `/dev/ttyUSB0` or `/dev/ttyACM0`
- **macOS**: Usually `/dev/cu.usbserial-*`

## Usage

### Basic Update

```bash
python3 bootloader_update_tool_analyzer.py firmware.hex --port COM3 --image 0
```

### Command-Line Options

```
positional arguments:
  firmware              Path to firmware file (.bin or .hex)

optional arguments:
  --port PORT           Serial port (default: COM3)
  --baud BAUD          Serial baud rate (default: 115200)
  --image {0,1}        Target image 0=Image1, 1=Image2 (default: 0)
  --timeout TIMEOUT    Serial read timeout in seconds (default: 2.0)
```

### Examples

**Update to Image 1 on COM3:**
```bash
python3 bootloader_update_tool_analyzer.py application.hex --port COM3 --image 0
```

**Update to Image 2 on COM5 with custom timeout:**
```bash
python3 bootloader_update_tool_analyzer.py firmware.bin --port COM5 --image 1 --timeout 3.0
```

**Update using Linux device:**
```bash
python3 bootloader_update_tool_analyzer.py app.hex --port /dev/ttyUSB0 --baud 115200
```

## How It Works

### Communication Flow

```
Host PC
  ↓
Python Tool (bootloader_update_tool_analyzer.py)
  ↓
Serial Port (UART)
  ↓
CANAnalyzerParticle Device
  ↓
CAN Bus
  ↓
dsPIC33EP512GP504 (Target)
```

### Command Format

The tool sends commands to the analyzer device using the format defined in `CANAnalyzerParticle.cpp`:

- **Send CAN frame**: `s <CAN_ID> <BYTE0> <BYTE1> ... <BYTE7>`
- **All messages**: `a` (enables printing of all received CAN frames)

Example:
```
s 0x100 0x01 0x00 0x00 0x00 0x00 0x00 0x00 0x01
        ↑    ↑    ↑    ↑    ↑    ↑    ↑    ↑    ↑
      ID   len  img  pad  pad  pad  pad  pad  chk
```

### Bootloader Protocol

The tool sends the same bootloader protocol as before:

| Command | ID | Purpose |
|---------|-----|---------|
| START_DOWNLOAD | 0x100 | Begin download, erase target |
| WRITE_BLOCK | 0x101 | Send 1-6 bytes of firmware |
| COMMIT_IMAGE | 0x102 | Finalize and verify |
| VERIFY_IMAGE | 0x103 | Check image validity |
| ABORT | 0x105 | Cancel operation |

Each frame is 8 bytes:
```
Byte 0:   Payload length (0-6)
Byte 1-6: Payload data
Byte 7:   XOR checksum of bytes 0-6
```

### Response Parsing

The tool monitors the serial output from the analyzer device for:
- Status messages on CAN ID 0x200
- Progress messages on CAN ID 0x201

These are automatically parsed and displayed to the user.

## Operation During Firmware Update

### On the Target Device (dsPIC33EP512GP504)

When the tool starts communication:

1. **CAN ID 0x100 received**: Device enters bootloader mode
   - LED set to **magenta** (R=255, G=0, B=255)
   - `bootloaderDownloadActive` flag set true
   - Progress counters reset

2. **CAN ID 0x101 (WRITE_BLOCK) received**: Device logs blocks
   - `bootloaderBlocksReceived` incremented
   - `bootloaderBytesReceived` updated
   - LED stays magenta

3. **Periodic status (every 500ms)**: Device sends progress
   - CAN ID 0x201 with block count and byte count
   - `bootloaderLastMessageTime` updated (timeout prevention)

4. **CAN ID 0x102 (COMMIT_IMAGE) received**: Download complete
   - Image verified
   - LED returns to off
   - `bootloaderDownloadActive` flag cleared
   - Returns to normal operation

### Timeout Handling

If no bootloader messages received for **10 seconds**:
- Download mode automatically exits
- LED turns off
- Device resumes normal operation

This prevents getting stuck in bootloader mode.

## Troubleshooting

### "Failed to connect to serial port"

**Causes:**
- Wrong COM port number
- Device not plugged in
- Port already in use by another application
- Driver not installed

**Solutions:**
```bash
# Check available ports
mode  # Windows

ls -la /dev/tty*  # Linux

# Try different port
python3 bootloader_update_tool_analyzer.py firmware.hex --port COM4

# Retry with slower baud rate
python3 bootloader_update_tool_analyzer.py firmware.hex --port COM3 --baud 115200
```

### "No response from bootloader"

**Causes:**
- Target device not receiving START_DOWNLOAD message
- CAN bus not properly connected
- CANAnalyzerParticle not properly configured

**Solutions:**
1. Verify CAN bus wiring to target
2. Check CANAnalyzerParticle CAN transceiver LED
3. Use analyzer device's terminal interface to send test message:
   - Connect to device via serial
   - Type `a` to enable all message printing
   - Manually send test: `s 0x100 0x01 0x00 0x00 0x00 0x00 0x00 0x00 0x01`

### "CRC Error"

**Causes:**
- Corrupted firmware file
- CAN frame loss during transmission
- Bootloader CRC calculation mismatch

**Solutions:**
1. Verify firmware file: `md5sum firmware.hex`
2. Use shorter block sizes (edit tool source)
3. Add delays between blocks in Python tool:
   - Edit `bootloader_update_tool_analyzer.py`
   - Increase `time.sleep(0.02)` to `time.sleep(0.05)` in `write_blocks()`

### "Image Full" error

**Causes:**
- Firmware larger than ~688 KB (see memory map)
- Target image space corrupted

**Solutions:**
1. Check firmware size: `ls -lh firmware.hex`
2. Try updating to second image: `--image 1`
3. Erase device first using separate bootloader erase tool

### Message Parsing Issues

The tool tries to parse various message formats from the analyzer. If messages aren't being recognized:

1. **View raw serial output**: Uncomment this line in the code:
   ```python
   # print(f"[RX] {line}")  # Uncomment for debugging
   ```

2. **Check analyzer output format**: Connect directly to analyzer and verify message format matches expected pattern

3. **Update regex patterns**: Edit `_parse_can_message()` method if analyzer uses different format

## Advanced Usage

### Integration with Shell Scripts

**Windows batch file:**
```batch
@echo off
python3 bootloader_update_tool_analyzer.py %1 --port COM3 --image 0
if errorlevel 1 (
    echo Firmware update failed!
    pause
) else (
    echo Firmware update successful!
)
```

**Linux bash script:**
```bash
#!/bin/bash
python3 bootloader_update_tool_analyzer.py "$1" --port /dev/ttyUSB0 --image 0
if [ $? -ne 0 ]; then
    echo "Firmware update failed!"
    exit 1
else
    echo "Firmware update successful!"
fi
```

### Custom Baud Rates

If your analyzer is configured for non-standard baud rates:

```bash
python3 bootloader_update_tool_analyzer.py firmware.hex --port COM3 --baud 57600
```

### Manual Message Sending

For debugging, you can send raw commands to the analyzer directly:

```python
from bootloader_update_tool_analyzer import CANAnalyzerBootloaderUpdater

updater = CANAnalyzerBootloaderUpdater(port='COM3', baudrate=115200)
updater.connect()

# Send raw command to analyzer
updater._send_serial_command('s 0x100 0x01 0x00 0x00 0x00 0x00 0x00 0x00 0x01')

# Wait for response
print(updater._wait_for_message(can_id=0x201, timeout=2.0))

updater.disconnect()
```

## Comparing with Direct CAN Tool

| Feature | bootloader_update_tool.py | bootloader_update_tool_analyzer.py |
|---------|---------------------------|-------------------------------------|
| Requires | python-can, CAN adapter | CANAnalyzerParticle device |
| Setup | Direct CAN interface | Serial UART connection |
| Complexity | Lower | Higher (intermediate device) |
| Cost | Higher (CAN interface) | Lower (Particle board ~$30) |
| Use Case | Production, direct integration | Development, lab testing |
| Hardware | Kingpin Pro, PEAK, Vector | CANAnalyzerParticle |

## See Also

- [bootloader_update_tool.py](bootloader_update_tool.py) - Direct CAN interface version
- [BOOTLOADER_DOCUMENTATION.md](BOOTLOADER_DOCUMENTATION.md) - Protocol details
- [CANAnalyzerParticle.cpp](../CANAnalyzer/CANAnalyzerParticle/) - Analyzer device firmware
- [main.c bootloader handling](main.c) - Device-side bootloader integration

## Support

For issues or help:

1. Check troubleshooting section above
2. Verify device connections
3. Enable debugging by uncommenting print statements
4. Check analyzer device logs if available
5. Review serial port configuration

---

**Version:** 1.0  
**Last Updated:** 2024  
**Tested On:** Python 3.8+, Windows 10/11, Linux  
