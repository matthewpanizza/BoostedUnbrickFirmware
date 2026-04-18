#!/usr/bin/env python3
"""
CAN Bus Bootloader Firmware Update Tool (CANAnalyzerParticle Hardware Version)
For dsPIC33EP512GP504 with dual-image bootloader

This tool communicates with a CANAnalyzerParticle device via serial port.
The device has built-in CAN transceiver and sends/receives frames through serial commands.

Requirements:
    - pyserial: pip install pyserial
    - struct, zlib (standard library)

Usage:
    python3 bootloader_update_tool_analyzer.py <fw_file> [--port COM3] [--baud 115200] [--image 0|1]

Example:
    python3 bootloader_update_tool_analyzer.py application.hex --port COM3 --image 0
"""

import struct
import zlib
import time
import argparse
import sys
import re
from pathlib import Path
from serial import Serial, SerialException
from typing import Tuple, Optional, List


class CANAnalyzerBootloaderUpdater:
    """CAN-based bootloader firmware updater using CANAnalyzerParticle hardware device"""
    
    # CAN Command IDs
    CMD_START_DOWNLOAD = 0x100
    CMD_WRITE_BLOCK = 0x101
    CMD_COMMIT_IMAGE = 0x102
    CMD_VERIFY_IMAGE = 0x103
    CMD_GET_STATUS = 0x104
    CMD_ABORT = 0x105
    
    RSP_STATUS = 0x200
    RSP_DOWNLOAD_PROGRESS = 0x201
    RSP_BLOCK_ACK = 0x202
    
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
    
    def __init__(self, port='COM3', baudrate=115200, timeout=2.0, verbose=False):
        """
        Initialize bootloader updater with CANAnalyzerParticle device
        
        Args:
            port: Serial port (e.g., 'COM3', '/dev/ttyUSB0')
            baudrate: Serial baud rate
            timeout: Serial read timeout in seconds
            verbose: Enable debug output
        """
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.verbose = verbose
        self.serial = None
        self.firmware_data = None
        self.firmware_crc = None
        self.last_status = None
        self.last_state = None
        self.response_buffer = []
        self.last_activity_time = time.time()
        self.device_activity_timeout = 5.0  # Seconds without activity = device offline
        self.current_block_num = 0  # Current block number (0-255)
        
    def connect(self) -> bool:
        """Establish serial connection to CANAnalyzerParticle device"""
        try:
            self.serial = Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=self.timeout
            )
            print(f"✓ Connected to {self.port} at {self.baudrate} baud")
            
            # Wait for device to be ready
            time.sleep(1)
            
            # Clear any existing output
            self.serial.reset_input_buffer()
            self.serial.reset_output_buffer()
            
            # Enable all message printing on the analyzer
            print("Enabling message printing on analyzer device...")
            self._send_serial_command('a')
            time.sleep(0.5)
            
            return True
        except SerialException as e:
            print(f"✗ Failed to connect to serial port: {e}")
            return False
    
    def disconnect(self):
        """Close serial connection"""
        if self.serial and self.serial.is_open:
            self.serial.close()
            self.serial = None
    
    def _send_serial_command(self, command: str, wait_for_response: bool = True, retries: int = 3) -> bool:
        """
        Send command to analyzer device via serial with retry logic
        
        Args:
            command: Command string (e.g., 's 100 01 02 ...')
            wait_for_response: Whether to wait and read response
            retries: Number of retry attempts
        
        Returns:
            bool: Success
        """
        for attempt in range(retries):
            try:
                if not self.serial or not self.serial.is_open:
                    print(f"✗ Serial port closed")
                    return False
                
                self.serial.write((command + '\n').encode())
                self.serial.flush()
                self.last_activity_time = time.time()
                
                if self.verbose and attempt == 0:
                    print(f"[TX] {command}")
                
                if wait_for_response:
                    time.sleep(0.1)
                    self._read_serial_responses()
                
                return True
                
            except SerialException as e:
                if attempt < retries - 1:
                    if self.verbose:
                        print(f"⚠ Serial error (attempt {attempt + 1}/{retries}): {e}")
                    time.sleep(0.1 * (attempt + 1))  # Exponential backoff
                else:
                    print(f"✗ Serial communication error after {retries} attempts: {e}")
                    return False
        
        return False
    
    def _read_serial_responses(self):
        """Read and buffer all available responses from device"""
        try:
            while self.serial.in_waiting > 0:
                line = self.serial.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    self.response_buffer.append(line)
                    self.last_activity_time = time.time()  # Update activity timer
                    if self.verbose:
                        print(f"[RX] {line}")
        except SerialException:
            pass
    
    def _check_device_health(self) -> bool:
        """Check if device is still responding (not timed out)"""
        elapsed = time.time() - self.last_activity_time
        if elapsed > self.device_activity_timeout:
            print(f"✗ Device activity timeout: {elapsed:.1f}s without response")
            return False
        return True
    
    def _parse_can_message(self, line: str) -> Optional[Tuple[int, List[int]]]:
        """
        Parse CAN message from device output
        
        Output format: "CAN ID: 0xHHH Data: 0xHH 0xHH 0xHH ..."
        or "msgId: 0x100, Data: 0x01, 0x02, ..."
        """
        # Try various patterns
        patterns = [
            r"CAN\s+ID:\s+0x([0-9A-Fa-f]+)\s+Data:\s+((?:0x[0-9A-Fa-f]+\s*)+)",
            r"msgId:\s+0x([0-9A-Fa-f]+).*Data:\s+((?:0x[0-9A-Fa-f]+[,\s]*)+)",
            r"ID\s+0x([0-9A-Fa-f]+).*Data:\s+((?:0x[0-9A-Fa-f]+[,\s]*)+)"
        ]
        
        for pattern in patterns:
            match = re.search(pattern, line, re.IGNORECASE)
            if match:
                can_id = int(match.group(1), 16)
                data_str = match.group(2)
                # Extract all hex values
                data_bytes = re.findall(r'0x([0-9A-Fa-f]{2})', data_str, re.IGNORECASE)
                data = [int(b, 16) for b in data_bytes]
                return (can_id, data)
        
        return None
    
    def _wait_for_message(self, can_id: Optional[int] = None, timeout: float = 2.0) -> Optional[Tuple[int, List[int]]]:
        """
        Wait for specific CAN message from device
        
        Args:
            can_id: Expected CAN ID (None = any message)
            timeout: Maximum wait time in seconds
        
        Returns:
            Tuple of (can_id, data_bytes) or None on timeout
        """
        start_time = time.time()
        
        while (time.time() - start_time) < timeout:
            self._read_serial_responses()
            
            while self.response_buffer:
                response = self.response_buffer.pop(0)
                parsed = self._parse_can_message(response)
                
                if parsed:
                    rx_id, rx_data = parsed
                    if can_id is None or rx_id == can_id:
                        return (rx_id, rx_data)
            
            time.sleep(0.05)
        
        return None
    
    def read_firmware(self, filepath: str) -> Optional[bytes]:
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
            
            # If hex file, try to parse it
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
    
    def _parse_hex_file(self, filepath: str) -> bytes:
        """Parse Intel HEX format file"""
        try:
            import intelhex
            ih = intelhex.IntelHex(filepath)
            return ih.tobinarray().tobytes()
        except ImportError:
            print("Note: intelhex not available, reading as binary")
            with open(filepath, 'rb') as f:
                return f.read()
    
    def _calculate_checksum(self, data: bytes) -> int:
        """Calculate XOR checksum for CAN frame"""
        checksum = 0
        for byte in data:
            checksum ^= byte
        return checksum
    
    def _send_can_frame(self, can_id: int, payload: bytes, retries: int = 3, include_block_num: bool = False) -> bool:
        """
        Send bootloader frame via analyzer device with retry logic
        
        Args:
            can_id: CAN message ID
            payload: Payload bytes (0-5 for WRITE_BLOCK, varies for others)
            retries: Number of retry attempts
            include_block_num: Include block number in frame (for WRITE_BLOCK)
        
        Returns:
            bool: Success
        """
        if not self.serial:
            return False
        
        # Check device is still responsive
        if not self._check_device_health():
            return False
        
        # For WRITE_BLOCK, use format: [length(5), block_num, data(4 bytes), padding, checksum]
        if include_block_num:
            # WRITE_BLOCK format: [length | block_num | data(4) | reserved | checksum]
            if len(payload) > 4:
                print(f"✗ Payload too large for block format: {len(payload)} bytes (max 4)")
                return False
            length = 5  # Always 5 bytes: block_num + 4 data bytes
            frame_data = bytes([length, self.current_block_num]) + payload + bytes([0] * (4 - len(payload)))
        else:
            # Other frame types: [length | payload... | checksum]
            if len(payload) > 6:
                print(f"✗ Payload too large: {len(payload)} bytes (max 6)")
                return False
            length = len(payload)
            frame_data = bytes([length]) + payload + bytes([0] * (6 - length))
        
        checksum = self._calculate_checksum(frame_data)
        frame_data += bytes([checksum])
        
        # Build analyzer command: s <ID> <data0> <data1> ... <data7>
        cmd_parts = [f"0x{can_id:03X}"] + [f"0x{b:02X}" for b in frame_data]
        command = "s " + " ".join(cmd_parts)
        
        return self._send_serial_command(command, wait_for_response=False, retries=retries)
    
    def start_download(self, target_image: int = 0, wait_response: bool = True) -> bool:
        """
        Send START_DOWNLOAD command
        
        Args:
            target_image: Target image (0 or 1)
            wait_response: Wait for bootloader response
        
        Returns:
            bool: Success
        """
        print(f"Starting firmware download to image {target_image}...")
        
        # START_DOWNLOAD should never be retried - if sent once, it's received
        # Multiple retries cause duplicate START messages on device
        if not self._send_can_frame(self.CMD_START_DOWNLOAD, bytes([target_image]), retries=1):
            return False
        
        if wait_response:
            time.sleep(0.5)
            # Look for status response on ID 0x200 or 0x201
            response = self._wait_for_message(can_id=None, timeout=2.0)
            
            if response:
                rx_id, rx_data = response
                if len(rx_data) >= 2:
                    status = rx_data[0]
                    state = rx_data[1]
                    
                    if status == self.STATUS_OK:
                        print(f"✓ Download started (state: {self.STATE_NAMES.get(state, 'Unknown')})")
                        return True
                    else:
                        print(f"✗ Error: {self.STATUS_NAMES.get(status, 'Unknown')}")
                        return False
            else:
                print("⚠ No response from bootloader (device may not send status)")
                return True  # Assume success if device doesn't respond
        
        return True
    
    def write_blocks(self, firmware_data: Optional[bytes] = None, block_size: int = 4, block_delay: float = 0.01) -> bool:
        """
        Write firmware blocks with timeout detection and retry logic
        
        Args:
            firmware_data: Firmware bytes to send (default: self.firmware_data)
            block_size: Bytes per block (1-4 for new 5-byte payload format)
            block_delay: Delay between blocks in seconds (increase if device is slow)
        
        Returns:
            bool: Success
        """
        if firmware_data is None:
            firmware_data = self.firmware_data
        
        if not firmware_data:
            print("✗ No firmware data loaded")
            return False
        
        print(f"Sending {len(firmware_data)} bytes in {block_size}-byte blocks (delay: {block_delay*1000:.0f}ms)...")
        
        total_blocks = (len(firmware_data) + block_size - 1) // block_size
        consecutive_failures = 0
        max_consecutive_failures = 3
        self.current_block_num = 0
        
        for block_num in range(total_blocks):
            offset = block_num * block_size
            block_data = firmware_data[offset:offset+block_size]
            
            # Check device health before each block
            if not self._check_device_health():
                print(f"✗ Device timeout at block {block_num + 1}/{total_blocks}")
                return False
            
            # Send block with minimal retry (only 1 attempt, no retries)
            # Use new format with block number in frame [length | block_num | data(4) | checksum]
            success = self._send_can_frame(self.CMD_WRITE_BLOCK, block_data, retries=1, include_block_num=True)
            
            if success:
                # Wait briefly for block acknowledgment from device (CAN ID 0x202)
                #ack = self._wait_for_message(can_id=self.RSP_BLOCK_ACK, timeout=0.5)
                #if ack:
                #    rx_id, rx_data = ack
                #    if len(rx_data) >= 1:
                #        ack_block_num = rx_data[0]
                #        if ack_block_num != self.current_block_num:
                #            print(f"⚠ Block ACK mismatch: expected {self.current_block_num}, got {ack_block_num}")
                # Increment block counter (wraps at 256)
                self.current_block_num = (self.current_block_num + 1) & 0xFF
            
            if not success:
                consecutive_failures += 1
                print(f"✗ Failed to send block {block_num + 1}/{total_blocks} (attempt {consecutive_failures}/{max_consecutive_failures})")
                
                if consecutive_failures >= max_consecutive_failures:
                    print(f"✗ Too many consecutive failures, aborting")
                    return False
                
                # Increase delay and retry same block
                time.sleep(0.2)
                continue
            else:
                consecutive_failures = 0  # Reset counter on success
            
            # Progress indicator
            if (block_num + 1) % 10 == 0 or block_num == total_blocks - 1:
                progress = ((block_num + 1) / total_blocks) * 100
                bytes_sent = (block_num + 1) * block_size
                elapsed = time.time() - self.last_activity_time
                print(f"  [{progress:3.0f}%] Block {block_num + 1}/{total_blocks} ({bytes_sent} bytes) [last activity: {elapsed:.1f}s ago]")
            
            # Delay between blocks (configurable)
            time.sleep(block_delay)
            
            # Periodically drain serial buffer to prevent overflow
            if block_num % 50 == 0:
                self._read_serial_responses()
        
        print("✓ All blocks sent")
        return True
    
    def commit_image(self, firmware_crc: Optional[int] = None, wait_response: bool = True) -> bool:
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
        
        if not self._send_can_frame(self.CMD_COMMIT_IMAGE, crc_bytes):
            return False
        
        if wait_response:
            time.sleep(0.5)
            response = self._wait_for_message(can_id=None, timeout=3.0)
            
            if response:
                rx_id, rx_data = response
                if len(rx_data) >= 1:
                    status = rx_data[0]
                    
                    if status == self.STATUS_OK:
                        print(f"✓ Image committed successfully")
                        return True
                    else:
                        print(f"✗ Error: {self.STATUS_NAMES.get(status, 'Unknown')}")
                        return False
            else:
                print("⚠ No response from bootloader (device may not send status)")
                return True
        
        return True
    
    def verify_image(self, target_image: int = 0, wait_response: bool = True) -> bool:
        """
        Verify image integrity
        
        Args:
            target_image: Image to verify
            wait_response: Wait for response
        
        Returns:
            bool: Valid
        """
        print(f"Verifying image {target_image}...")
        
        if not self._send_can_frame(self.CMD_VERIFY_IMAGE, bytes([target_image])):
            return False
        
        if wait_response:
            time.sleep(0.5)
            response = self._wait_for_message(can_id=None, timeout=2.0)
            
            if response:
                rx_id, rx_data = response
                if len(rx_data) >= 1:
                    status = rx_data[0]
                    
                    if status == self.STATUS_OK:
                        print(f"✓ Image {target_image} valid")
                        return True
                    else:
                        print(f"✗ Image invalid: {self.STATUS_NAMES.get(status, 'Unknown')}")
                        return False
            else:
                print("⚠ No verify response received")
                return True
        
        return True
    
    def abort(self) -> bool:
        """Abort current operation"""
        print("Aborting...")
        return self._send_can_frame(self.CMD_ABORT, bytes())
    
    def update(self, fw_file: str, target_image: int = 0, block_delay: float = 0.05) -> bool:
        """
        Full firmware update sequence
        
        Args:
            fw_file: Path to firmware file
            target_image: Target image (0 or 1)
            block_delay: Delay between blocks in seconds
        
        Returns:
            bool: Success
        """
        try:
            # Load firmware
            if not self.read_firmware(fw_file):
                return False
            
            print()
            
            # Download sequence
            if not self.start_download(target_image, wait_response=False):
                print("Attempting abort...")
                self.abort()
                return False
            
            time.sleep(0.5)
            
            if not self.write_blocks(block_delay=block_delay):
                print("Attempting abort...")
                self.abort()
                return False
            
            time.sleep(0.5)
            
            if not self.commit_image(wait_response=False):
                print("Attempting abort...")
                self.abort()
                return False
            
            time.sleep(1)
            
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
        description="CAN Bus Bootloader Firmware Update Tool (CANAnalyzerParticle)"
    )
    parser.add_argument(
        'firmware',
        help='Path to firmware file (.bin or .hex)'
    )
    parser.add_argument(
        '--port', default='COM3',
        help='Serial port for analyzer device (default: COM3)'
    )
    parser.add_argument(
        '--baud', type=int, default=115200,
        help='Serial baud rate (default: 115200)'
    )
    parser.add_argument(
        '--image', type=int, default=0, choices=[0, 1],
        help='Target image (0=Image1, 1=Image2) (default: 0)'
    )
    parser.add_argument(
        '--timeout', type=float, default=2.0,
        help='Serial read timeout in seconds (default: 2.0)'
    )
    parser.add_argument(
        '--block-delay', type=float, default=0.05,
        help='Delay between blocks in seconds (increase if device is slow) (default: 0.05)'
    )
    parser.add_argument(
        '--verbose', '-v', action='store_true',
        help='Enable verbose debug output'
    )
    
    args = parser.parse_args()
    
    # Verify firmware file exists
    fw_path = Path(args.firmware)
    if not fw_path.exists():
        print(f"✗ Firmware file not found: {fw_path}")
        sys.exit(1)
    
    # Create updater and run
    updater = CANAnalyzerBootloaderUpdater(
        port=args.port,
        baudrate=args.baud,
        timeout=args.timeout,
        verbose=args.verbose
    )
    
    if not updater.connect():
        sys.exit(1)
    
    try:
        success = updater.update(str(fw_path), target_image=args.image, block_delay=args.block_delay)
        sys.exit(0 if success else 1)
    finally:
        updater.disconnect()


if __name__ == '__main__':
    main()
