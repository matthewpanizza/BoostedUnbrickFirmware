# Serial Bootloader - Quick Start Checklist

## ✓ Completed
- [x] main.c: Added serial bootloader variables and frame parser
- [x] main.c: Added serial frame processor to main loop
- [x] main.c: Added `processSerialBootloaderByte()` function (state machine parser)
- [x] main.c: Added `handleSerialBootloaderFrame()` function (CAN frame converter)
- [x] Created `bootloader_serial_tool.py` with full firmware update capability
- [x] Created `SERIAL_BOOTLOADER_GUIDE.md` with setup and testing instructions

## ⬜ Next Steps (Compile & Test)

### Phase 1: Compilation (MPLAB X)
- [ ] Open `BoostedBattery.X` in MPLAB X
- [ ] Verify bootloader files are in project:
  - [ ] `bootloader.c`
  - [ ] `bootloader.h`
  - [ ] `bootloader_config.h`
  - [ ] `bootloader_linker.gld` (linker script in project settings)
- [ ] Project → Clean and Build
- [ ] Verify: 0 errors, 0 warnings
- [ ] Program device via debugger or programmer

### Phase 2: Hardware Setup
- [ ] Get USB-to-serial adapter (CP2102, FTDI, or CH340)
- [ ] Connect to device UART1:
  - [ ] USB GND → Device GND
  - [ ] USB TX → Device RX (Pin 34, RP104)
  - [ ] USB RX → Device TX (Pin 35, RP105)
- [ ] Insert USB to PC
- [ ] Identify COM port in Device Manager (Windows) or `/dev/ttyUSB*` (Linux)

### Phase 3: Python Tool Setup
- [ ] Install pyserial: `pip install pyserial`
- [ ] Create test firmware file or use existing `.hex`
- [ ] Place `bootloader_serial_tool.py` in accessible directory

### Phase 4: Initial Test
- [ ] Run with verbose output:
  ```bash
  python3 bootloader_serial_tool.py firmware.hex --port COM3 --verbose
  ```
- [ ] Verify output:
  - [ ] "Connecting to device..."
  - [ ] "Starting firmware download..."
  - [ ] "Block X/Y received (ACK)"
  - [ ] "Image verified successfully"
  - [ ] Device restarts with new firmware

### Phase 5: Validation
- [ ] New firmware runs on device (LED changes, serial output, etc.)
- [ ] Download completes without errors
- [ ] Block counts match between host and device
- [ ] Device automatically jumps to new firmware

## 🔍 Verification Points

**In MPLAB X Console:**
- [ ] No compilation errors
- [ ] No linker errors
- [ ] Bootloader section size < 8KB

**On Device Serial Output (via other COM port or USB)::**
- [ ] "SERIAL_BL: START_DOWNLOAD for image 0" message appears
- [ ] Block messages appear: "Block X, total Y" 
- [ ] CRC validation message: "✓ Image validated"
- [ ] Jump message: "Jumping to application..."

**With Serial Tool:**
- [ ] Progress bar shows 0% → 100%
- [ ] No "Timeout" errors
- [ ] No "Checksum mismatch" errors
- [ ] Final message: "Update complete!"

## 📋 File Locations

```
Repository Root/
├── SERIAL_BOOTLOADER_GUIDE.md (full documentation)
├── SERIAL_BOOTLOADER_CHECKLIST.md (this file)
├── bootloader_serial_tool.py (host tool - NEW)
├── bootloader_update_tool_analyzer.py (CAN tool - existing)
├── BoostedBattery.X/
│   ├── main.c (modified with serial support)
│   ├── bootloader.c (unchanged)
│   ├── bootloader.h (unchanged)
│   ├── bootloader_config.h (unchanged)
│   └── nbproject/
│       └── Makefile-impl.mk (references linker script)
```

## 🛠️ Troubleshooting Quick Reference

| Problem | Solution |
|---------|----------|
| "Connection refused" | Check COM port in Device Manager |
| No response from device | Verify UART1 wiring (GND, RX, TX) |
| "Checksum mismatch" | Check USB cable (too long?), try slower rate |
| Download hangs | Increase `--block-size` in command |
| Device doesn't restart | Verify `BL_JumpToImage()` in bootloader.c called |
| "Frame timeout" on device | Try adding delay: modify tool block timing |

## 📞 Testing Matrix

| Test Case | How | Expected |
|-----------|-----|----------|
| Connect | Run tool with `--verbose` | "Connecting..." then "Ready" |
| Small file (1KB) | Use minimal firmware | Completes in <5s |
| Medium file (100KB) | Use typical app | Completes in 30-60s |
| Large file (200KB) | Use full app | Completes in 60-120s |
| Block ACKs | Watch verbose output | "Block 0/10 (ACK)", etc. |
| Two updates in sequence | Run tool twice | Second update works same as first |

## 🎯 Success Criteria

✓ **Compile:** 0 errors, 0 warnings in MPLAB X  
✓ **Connect:** Device responds to serial frames  
✓ **Download:** All blocks transmitted and acknowledged  
✓ **Verify:** CRC check passes on device  
✓ **Jump:** New firmware starts automatically  
✓ **Repeat:** Can update multiple times without reboot  

---

## Notes

- Serial bootloader uses same CAN protocol internally (transmitted over UART)
- Block numbering (0-255) prevents duplicate processing
- 1-second timeout resets partial frames
- Bootloader stays resident (8KB), app loads from 0x002000
- Dual-image allows safe rollback (image 0 vs image 1)

## Important: Compilation Dependencies

The bootloader requires these in BoostedBattery.X project:

**Linker Script:** Must include bootloader region
```
Memory Map (as configured in bootloader_config.h):
- 0x000000: Bootloader (8 KB) - BL_START_ADDR
- 0x002000: Image 1 (688 KB) - BL_IMAGE1_START
- 0x0B0000: Image 2 (688 KB) - BL_IMAGE2_START  
- 0x15B000: Metadata (4 KB) - BL_METADATA_ADDR
```

**MCC Modules:** Already in project
- UART1 (for debug and serial bootloader)
- CAN1 (for CAN boot mode)
- FLASH (for programming)

---

**Next Action:** Compile project → Connect USB adapter → Run Python tool  
**Status:** Ready for test phase
