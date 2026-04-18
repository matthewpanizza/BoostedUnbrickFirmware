#!/usr/bin/env python3
"""
CAN Bus Bootloader Firmware Update Tool
For dsPIC33EP512GP504 with dual-image bootloader

Requirements:
    - python-can: pip install python-can
    - windows-curses (Windows): pip install windows-curses (optional, for progress bar)
    - struct, zlib (standard library)

Usage:
    python3 bootloader_update.py <fw_file> [--image 0|1] [--can-channel vcan0] [--bitrate 250000]

Example:
    python3 bootloader_update.py application.hex --image 0 --can-channel COM1
"""

import struct
import zlib
import time
import argparse
import sys
from pathlib import Path

try:
    import can
    from can import Message
except ImportError:
    print("Error: python-can not installed. Install with: pip install python-can")
    sys.exit(1)


class BootloaderUpdater:
    """CAN-based bootloader firmware updater"""
    
    # CAN Command IDs
    CMD_START_DOWNLOAD = 0x100
    CMD_WRITE_BLOCK = 0x101
    CMD_COMMIT_IMAGE = 0x102
    CMD_VERIFY_IMAGE = 0x103
    CMD_GET_STATUS = 0x104
    CMD_ABORT = 0x105
    
    RSP_STATUS = 0x200
    
    # Status codes
    STATUS_OK = 0x00
    STATUS_ERR_INVALID_CMD = 0x01
    STATUS_ERR_INVALID_STATE = 0x02
    STATUS_ERR_CHECKSUM = 0x03
    STATUS_ERR_FLASH = 0x04
    STATUS_ERR_MEMORY = 0x05
    STATUS_ERR_TIMEOUT = 0x06
    STATUS_ERR_CRC = 0x07
    STATUS_ERR_FULL = 0x08
    STATUS_ERR_NOT_INITIALIZED = 0x09
    
    # State codes
    STATE_IDLE = 0x00
    STATE_DOWNLOAD_ACTIVE = 0x01
    STATE_PROGRAMMING = 0x02
    STATE_VERIFYING = 0x03
    STATE_ERROR = 0x04
    
    STATUS_NAMES = {
        0x00: "OK",
        0x01: "Invalid Command",
        0x02: "Invalid State",
        0x03: "Checksum Error",
        0x04: "Flash Error",
        0x05: "Memory Error",
        0x06: "Timeout",
        0x07: "CRC Error",
        0x08: "Image Full",
        0x09: "Not Initialized"
    }
    
    STATE_NAMES = {
        0x00: "IDLE",
        0x01: "Download Active",
        0x02: "Programming",
        0x03: "Verifying",
        0x04: "Error"
    }
    
    def __init__(self, channel='vcan0', bitrate=250000, timeout=1.0):
        """
        Initialize bootloader updater
        
        Args:
            channel: CAN interface (e.g., 'vcan0', 'COM1', 'PEAK_USBCAN1')
            bitrate: CAN bus bitrate in bps
            timeout: CAN receive timeout in seconds
        """
        self.channel = channel
        self.bitrate = bitrate
        self.timeout = timeout
        self.bus = None
        self.firmware_data = None
        self.firmware_crc = None
        self.last_status = None
        self.last_state = None
        
    def connect(self):
        """Establish CAN connection"""
        try:
            self.bus = can.interface.Bus(
                channel=self.channel,
                bustype='vector',
                bitrate=self.bitrate
            )
            print(f"✓ Connected to CAN bus on {self.channel} at {self.bitrate} bps")
            return True
        except Exception as e:
            print(f"✗ Failed to connect to CAN bus: {e}")
            return False
    
    def disconnect(self):
        """Close CAN connection"""
        if self.bus:
            self.bus.shutdown()
            self.bus = None
    
    def read_firmware(self, filepath):
        """
        Read firmware file
        
        Args:
            filepath: Path to firmware file (.hex, .bin, or .elf)
        
        Returns:
            bytes: Firmware data
        """
        try:
            with open(filepath, 'rb') as f:
                data = f.read()
            
            # If hex file, parse it
            if filepath.lower().endswith('.hex'):
                data = self._parse_hex_file(filepath)
            
            self.firmware_data = data
            self.firmware_crc = zlib.crc32(data) & 0xffffffff
            
            print(f"✓ Loaded firmware: {len(data)} bytes")
            print(f"  CRC32: 0x{self.firmware_crc:08X}")
            
            return data
        except Exception as e:
            print(f"✗ Failed to read firmware file: {e}")
            return None
    
    def _parse_hex_file(self, filepath):
        """Parse Intel HEX format file"""
        try:
            import intelhex
            ih = intelhex.IntelHex(filepath)
            return ih.tobinarray().tobytes()
        except ImportError:
            print("Note: intelhex not available, reading as binary")
            with open(filepath, 'rb') as f:
                return f.read()
    
    def _calculate_checksum(self, data):
        """Calculate XOR checksum for CAN frame"""
        checksum = 0
        for byte in data:
            checksum ^= byte
        return checksum
    
    def _send_frame(self, can_id, payload):
        """
        Send bootloader frame via CAN
        
        Args:
            can_id: CAN message ID
            payload: Payload bytes (0-6)
        
        Returns:
            bool: Success
        """
        if not self.bus or len(payload) > 6:
            return False
        
        # Build frame: [length, payload..., checksum]
        length = len(payload)
        frame_data = [length] + list(payload) + [0] * (6 - length)
        checksum = self._calculate_checksum(frame_data)
        frame_data.append(checksum)
        
        msg = Message(
            arbitration_id=can_id,
            data=frame_data,
            is_extended_id=False,
            dlc=8
        )
        
        try:
            self.bus.send(msg)
            return True
        except Exception as e:
            print(f"✗ Failed to send CAN frame: {e}")
            return False
    
    def _receive_response(self, timeout=None):
        """
        Receive bootloader response
        
        Returns:
            tuple: (status_code, state_code) or None
        """
        if not self.bus:
            return None
        
        try:
            msg = self.bus.recv(timeout or self.timeout)
            if msg and msg.arbitration_id == self.RSP_STATUS:
                status = msg.data[0] if len(msg.data) > 0 else 0xFF
                state = msg.data[1] if len(msg.data) > 1 else 0xFF
                self.last_status = status
                self.last_state = state
                return (status, state)
        except can.CanOperationError:
            pass
        
        return None
    
    def start_download(self, target_image=0, wait_response=True):
        """
        Send START_DOWNLOAD command
        
        Args:
            target_image: Target image (0 or 1)
            wait_response: Wait for bootloader response
        
        Returns:
            bool: Success
        """
        print(f"Starting firmware download to image {target_image}...")
        
        if not self._send_frame(self.CMD_START_DOWNLOAD, bytes([target_image])):
            return False
        
        if wait_response:
            response = self._receive_response()
            if response:
                status, state = response
                if status == self.STATUS_OK:
                    print(f"✓ Download started (state: {self.STATE_NAMES.get(state, 'Unknown')})")
                    return True
                else:
                    print(f"✗ Error: {self.STATUS_NAMES.get(status, 'Unknown')}")
                    return False
            else:
                print("✗ No response from bootloader")
                return False
        
        return True
    
    def write_blocks(self, firmware_data=None, block_size=6):
        """
        Write firmware blocks
        
        Args:
            firmware_data: Firmware bytes to send (default: self.firmware_data)
            block_size: Bytes per block (1-6)
        
        Returns:
            bool: Success
        """
        if firmware_data is None:
            firmware_data = self.firmware_data
        
        if not firmware_data:
            print("✗ No firmware data loaded")
            return False
        
        print(f"Sending {len(firmware_data)} bytes in {block_size}-byte blocks...")
        
        total_blocks = (len(firmware_data) + block_size - 1) // block_size
        
        for block_num in range(total_blocks):
            offset = block_num * block_size
            block_data = firmware_data[offset:offset+block_size]
            
            if not self._send_frame(self.CMD_WRITE_BLOCK, block_data):
                print(f"✗ Failed to send block {block_num}")
                return False
            
            # Progress indicator
            if (block_num + 1) % 10 == 0 or block_num == total_blocks - 1:
                progress = ((block_num + 1) / total_blocks) * 100
                bytes_sent = block_num * block_size + len(block_data)
                print(f"  [{progress:3.0f}%] Block {block_num + 1}/{total_blocks} "
                      f"({bytes_sent} bytes)")
            
            # Small delay between blocks to allow processing
            time.sleep(0.01)
        
        print("✓ All blocks sent")
        return True
    
    def commit_image(self, firmware_crc=None, wait_response=True):
        """
        Commit and verify downloaded image
        
        Args:
            firmware_crc: CRC32 of firmware (default: self.firmware_crc)
            wait_response: Wait for bootloader response
        
        Returns:
            bool: Success
        """
        if firmware_crc is None:
            firmware_crc = self.firmware_crc
        
        if firmware_crc is None:
            print("✗ No CRC calculated")
            return False
        
        print(f"Committing image (CRC: 0x{firmware_crc:08X})...")
        
        # Build CRC payload (little-endian)
        crc_bytes = struct.pack('<I', firmware_crc)
        
        if not self._send_frame(self.CMD_COMMIT_IMAGE, crc_bytes):
            return False
        
        if wait_response:
            response = self._receive_response()
            if response:
                status, state = response
                if status == self.STATUS_OK:
                    print(f"✓ Image committed successfully")
                    return True
                else:
                    print(f"✗ Error: {self.STATUS_NAMES.get(status, 'Unknown')}")
                    return False
            else:
                print("✗ No response from bootloader")
                return False
        
        return True
    
    def verify_image(self, target_image=0, wait_response=True):
        """
        Verify image integrity
        
        Args:
            target_image: Image to verify
            wait_response: Wait for response
        
        Returns:
            bool: Valid
        """
        print(f"Verifying image {target_image}...")
        
        if not self._send_frame(self.CMD_VERIFY_IMAGE, bytes([target_image])):
            return False
        
        if wait_response:
            response = self._receive_response()
            if response:
                status, state = response
                if status == self.STATUS_OK:
                    print(f"✓ Image {target_image} valid")
                    return True
                else:
                    print(f"✗ Image invalid: {self.STATUS_NAMES.get(status, 'Unknown')}")
                    return False
        
        return True
    
    def abort(self):
        """Abort current operation"""
        print("Aborting...")
        return self._send_frame(self.CMD_ABORT, bytes())
    
    def update(self, fw_file, target_image=0):
        """
        Full firmware update sequence
        
        Args:
            fw_file: Path to firmware file
            target_image: Target image (0 or 1)
        
        Returns:
            bool: Success
        """
        try:
            # Load firmware
            if not self.read_firmware(fw_file):
                return False
            
            print()
            
            # Download sequence
            if not self.start_download(target_image):
                self.abort()
                return False
            
            time.sleep(0.2)
            
            if not self.write_blocks():
                self.abort()
                return False
            
            time.sleep(0.2)
            
            if not self.commit_image():
                self.abort()
                return False
            
            time.sleep(0.2)
            
            # Verify
            if not self.verify_image(target_image):
                return False
            
            print("\n✓ Firmware update complete!")
            return True
            
        except Exception as e:
            print(f"✗ Update failed: {e}")
            return False


def main():
    """Command-line interface"""
    parser = argparse.ArgumentParser(
        description="CAN Bus Bootloader Firmware Update Tool"
    )
    parser.add_argument(
        'firmware',
        help='Path to firmware file (.bin or .hex)'
    )
    parser.add_argument(
        '--image', type=int, default=0, choices=[0, 1],
        help='Target image (0=Image1, 1=Image2) (default: 0)'
    )
    parser.add_argument(
        '--can-channel', default='vcan0',
        help='CAN interface channel (default: vcan0)'
    )
    parser.add_argument(
        '--bitrate', type=int, default=250000,
        help='CAN bitrate in bps (default: 250000)'
    )
    parser.add_argument(
        '--timeout', type=float, default=1.0,
        help='CAN response timeout in seconds (default: 1.0)'
    )
    
    args = parser.parse_args()
    
    # Verify firmware file exists
    fw_path = Path(args.firmware)
    if not fw_path.exists():
        print(f"✗ Firmware file not found: {fw_path}")
        sys.exit(1)
    
    # Create updater and run
    updater = BootloaderUpdater(
        channel=args.can_channel,
        bitrate=args.bitrate,
        timeout=args.timeout
    )
    
    if not updater.connect():
        sys.exit(1)
    
    try:
        success = updater.update(str(fw_path), target_image=args.image)
        sys.exit(0 if success else 1)
    finally:
        updater.disconnect()


if __name__ == '__main__':
    main()
