#!/usr/bin/env python3
"""
Serial Bootloader Firmware Update Tool
For dsPIC33EP512GP504 with dual-image bootloader via Serial/UART interface

This tool communicates with the microcontroller via UART serial port.
Frame format: [0xAA][CMD][LENGTH][BLOCK_NUM][DATA(4)][CHECKSUM][0x55]

Requirements:
    - pyserial: pip install pyserial
    - struct, zlib (standard library)

Usage:
    python3 bootloader_serial_tool.py <fw_file> [--port COM3] [--baud 115200] [--image 0]

Example:
    python3 bootloader_serial_tool.py application.hex --port COM3 --baud 115200 --image 0
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


class SerialBootloaderUpdater:
    """Serial-based bootloader firmware updater for dsPIC33EP512GP504"""
    
    # Commands
    CMD_START_DOWNLOAD = 0x01
    CMD_WRITE_BLOCK = 0x02
    CMD_COMMIT_IMAGE = 0x03
    CMD_ABORT = 0x05
    
    # Frame markers
    FRAME_START = 0xAA
    FRAME_END = 0x55
    
    # Status codes (from device responses)
    STATUS_OK = 0x00
    STATUS_ERR_CHECKSUM = 0x03
    STATUS_ERR_CRC = 0x07
    
    def __init__(self, port='COM7', baudrate=115200, timeout=2.0, verbose=False):
        """
        Initialize serial bootloader updater
        
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
        self.current_block_num = 0
        
    def connect(self) -> bool:
        """Establish serial connection to bootloader"""
        try:
            # Use a SHORT timeout (100ms) for responsive reads, not the 2.0s timeout for overall operations
            self.serial = Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=2.0  # 100ms read timeout - short enough to not block, long enough for data to arrive
            )
            print(f"✓ Connected to {self.port} at {self.baudrate} baud")
            
            # Small delay for device to be ready
            time.sleep(0.5)
            
            # Clear any existing data in buffers
            self.serial.reset_input_buffer()
            self.serial.reset_output_buffer()
            
            print("✓ Serial bootloader ready")
            return True
            
        except SerialException as e:
            print(f"✗ Failed to connect to serial port: {e}")
            return False
    
    def disconnect(self):
        """Close serial connection"""
        if self.serial and self.serial.is_open:
            self.serial.close()
            self.serial = None
    
    def _calculate_checksum(self, data: bytes) -> int:
        """Calculate XOR checksum for frame"""
        checksum = 0
        for byte in data:
            checksum ^= byte
        return checksum
    
    def _send_frame(self, cmd: int, payload: bytes = b'', block_num: int = 0, wait_ack: bool = False, ack_timeout: float = 2.0) -> bool:
        """
        Send bootloader frame via serial
        
        Args:
            cmd: Command code (0x01-0x05)
            payload: Payload bytes (0-4 for data frames)
            block_num: Block number (for WRITE_BLOCK)
            wait_ack: Whether to wait for acknowledgment
            ack_timeout: Timeout for ACK in seconds
        
        Returns:
            bool: Success
        """
        start_write = time.time()

        if not self.serial or not self.serial.is_open:
            return False
        
        # Build frame: [0xAA][CMD][LENGTH][BLOCK_NUM][DATA(0-4)][CHECKSUM][0x55]
        if cmd == self.CMD_WRITE_BLOCK:
            # Always use 5 bytes payload for WRITE_BLOCK: block_num + 4 data bytes
            if len(payload) > 4:
                print(f"✗ Payload too large: {len(payload)} bytes (max 4)")
                return False
            
            # Frame: [0xAA][0x02][0x05][block_num][data(4)][checksum][0x55]
            frame_data = bytes([self.CMD_WRITE_BLOCK, 0x05, block_num]) + payload + bytes([0] * (4 - len(payload)))
        else:
            # Other commands: [CMD][LENGTH][PAYLOAD...]
            frame_data = bytes([cmd, len(payload)]) + payload
        
        # Calculate checksum (XOR of frame_data)
        checksum = self._calculate_checksum(frame_data)
        
        # Build complete frame: [0xAA][...data...][checksum][0x55]
        complete_frame = bytes([self.FRAME_START]) + frame_data + bytes([checksum, self.FRAME_END])
        
        try:
            
            self.serial.write(complete_frame)
            self.serial.flush()
            
            if self.verbose:
                print(f"[TX] {' '.join(f'{b:02X}' for b in complete_frame)}")
            
            if wait_ack:
                # Wait for acknowledgment frame with configurable timeout
                ack = self._read_response(timeout=ack_timeout)
                if ack:
                    end_write = time.time()
                    write_ms = (end_write - start_write) * 1000.0
                    #print(f"[SEND FRAME] {write_ms:.3f} ms")
                    return True
                else:
                    print(f"✗ No acknowledgment received (timeout: {ack_timeout}s)")
                    return False
            
            

            return True
            
        except SerialException as e:
            print(f"✗ Serial error: {e}")
            return False
    
    def _read_response(self, timeout: float = 2.0):
        """
        Read and validate response frame from bootloader
        
        OPTIMIZED VERSION: Uses serial.in_waiting to check for available bytes
        and reads them all at once instead of byte-by-byte. This eliminates
        the 1ms sleep latency and reduces frame read time from ~65ms to ~5ms.
        
        Robust state machine that handles:
        - Bytes arriving out of order
        - Garbage data in stream
        - Frame synchronization recovery
        
        Returns:
            tuple: (cmd, payload) or None on timeout/error
        """
        start_time = time.time()
        state = "WAIT_START"
        buffer = bytearray()
        garbage_bytes = 0
        max_garbage = 100  # Max consecutive garbage bytes before giving up

        start_read = time.time()   # <-- mark start of this read cycle

        while (time.time() - start_time) < timeout:
            # Check if any bytes are available in the input buffer
            bytes_available = self.serial.in_waiting
            
            if bytes_available > 0:
                # Read all available bytes at once (much faster than reading 1 byte at a time)
                byte_chunk = self.serial.read(bytes_available)
                
                # Process each byte in the chunk
                for byte in byte_chunk:
                    # =========================================
                    # STATE: WAIT_START - Looking for frame start
                    # =========================================
                    if state == "WAIT_START":
                        if byte == self.FRAME_START:
                            buffer = bytearray([byte])
                            state = "READING"
                            garbage_bytes = 0
                        else:
                            garbage_bytes += 1
                            if garbage_bytes > max_garbage:
                                if self.verbose:
                                    print(f"Too much garbage data, giving up after {garbage_bytes} bytes")
                                return None
                        continue

                    # =========================================
                    # STATE: READING - Building frame
                    # =========================================
                    if state == "READING":
                        buffer.append(byte)

                        # Minimum frame length = 6 bytes:
                        # [AA][CMD][LEN][DATA][CHECKSUM][55]
                        if len(buffer) >= 3:
                            # Check length field (at index 2) to calculate expected frame size
                            length = buffer[2]
                            expected_len = 5 + length  # AA + CMD + LEN + LENGTH + CHK + 55

                            # Sanity check on length field
                            if length > 10:
                                # Invalid length, resync to start marker
                                state = "WAIT_START"
                                buffer = bytearray()
                                garbage_bytes = 0
                                continue

                            # Too many bytes → invalid → reset
                            if len(buffer) > expected_len:
                                state = "WAIT_START"
                                buffer = bytearray()
                                garbage_bytes = 0
                                continue

                            # Exact match → validate frame
                            if len(buffer) == expected_len:
                                # Must end with FRAME_END
                                if buffer[-1] != self.FRAME_END:
                                    state = "WAIT_START"
                                    buffer = bytearray()
                                    garbage_bytes = 0
                                    continue

                                # Validate checksum
                                calc = self._calculate_checksum(buffer[1:-2])
                                recv = buffer[-2]

                                if calc != recv:
                                    if self.verbose:
                                        print(f"Checksum mismatch! Calculated: 0x{calc:02X}, Received: 0x{recv:02X}")
                                    state = "WAIT_START"
                                    buffer = bytearray()
                                    garbage_bytes = 0
                                    continue

                                # SUCCESS - Valid frame received
                                cmd = buffer[1]
                                payload = bytes(buffer[3:-2])
                                if self.verbose:
                                    print(f"[RX] {' '.join(f'{b:02X}' for b in buffer)}")

                                #read_duration = (time.time() - start_read) * 1000.0
                                #if self.verbose or True:
                                #    print(f"[READ] {bytes_available} bytes in {read_duration:.5f} ms")
                                return (cmd, payload)
            else:
                # No bytes available - sleep a tiny bit to avoid busy-waiting
                # but much less aggressive than before (~100µs instead of 1ms)
                time.sleep(0.0001)

        # Timeout reached
        return None
    
    def read_firmware(self, filepath: str) -> Optional[bytes]:
        """
        Read firmware file
        
        Args:
            filepath: Path to firmware file (.hex or .bin)
        
        Returns:
            bytes: Firmware data
        """
        try:
            if filepath.lower().endswith('.hex'):
                data = self._parse_hex_file(filepath)
            else:
                with open(filepath, 'rb') as f:
                    data = f.read()
            
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
    
    def start_download(self, target_image: int = 0) -> bool:
        """
        Send START_DOWNLOAD command
        
        Args:
            target_image: Target image (0 or 1)
        
        Returns:
            bool: Success
        """
        print(f"Starting firmware download to image {target_image}...")
        
        payload = bytes([target_image])
        success = self._send_frame(self.CMD_START_DOWNLOAD, payload, wait_ack=True)
        
        if success:
            print("✓ Download started successfully")
            self.current_block_num = 0
            return True
        else:
            print("✗ Failed to start download")
            return False
    
    def write_blocks(self, firmware_data: Optional[bytes] = None, block_size: int = 4, block_delay: float = 0.001, ack_timeout: float = 2.0) -> bool:
        """
        Write firmware blocks
        
        Args:
            firmware_data: Firmware bytes to send
            block_size: Bytes per block (1-4)
            block_delay: Delay between blocks in seconds (default 1ms - optimized response reading allows fast sending)
            ack_timeout: Timeout waiting for ACK in seconds (default 2s)
        
        Returns:
            bool: Success
        """
        if firmware_data is None:
            firmware_data = self.firmware_data
        
        if not firmware_data:
            print("✗ No firmware data loaded")
            return False
        
        total_blocks = (len(firmware_data) + block_size - 1) // block_size
        print(f"Sending {len(firmware_data)} bytes in {block_size}-byte blocks...")
        print(f"Block delay: {block_delay*1000:.1f}ms, ACK timeout: {ack_timeout}s\n")
        
        ack_count = 0
        failed_block = None
        
        for block_num in range(total_blocks):
            offset = block_num * block_size
            block_data = firmware_data[offset:offset+block_size]
            
            # Send block with block number
            if not self._send_frame(self.CMD_WRITE_BLOCK, block_data, 
                                   block_num=self.current_block_num, wait_ack=True, ack_timeout=ack_timeout):
                print(f"✗ Failed to send block {block_num + 1}/{total_blocks} (block_num=0x{self.current_block_num:02X})")
                failed_block = block_num + 1
                return False
            
            ack_count += 1
            self.current_block_num = (self.current_block_num + 1) & 0xFF
            
            # Progress indicator
            if (block_num + 1) % 50 == 0 or block_num == total_blocks - 1:
                progress = ((block_num + 1) / total_blocks) * 100
                bytes_sent = (block_num + 1) * block_size
                print(f"  [{progress:3.0f}%] Block {block_num + 1}/{total_blocks} ({bytes_sent} bytes, {ack_count} ACKs)")
            
            # Delay between blocks to allow firmware to process
            #time.sleep(block_delay)
        
        print(f"\n✓ All {total_blocks} blocks sent with {ack_count} acknowledgments")
        return True
    
    def commit_image(self) -> bool:
        """
        Commit and finalize the downloaded image
        
        Returns:
            bool: Success
        """
        print("Committing image...")
        
        if not self._send_frame(self.CMD_COMMIT_IMAGE, wait_ack=True):
            print("✗ Failed to commit image")
            return False
        
        print("✓ Image committed successfully")
        return True
    
    def abort(self) -> bool:
        """Abort bootloader download"""
        print("Aborting bootloader...")
        return self._send_frame(self.CMD_ABORT, wait_ack=True)
    
    def update(self, fw_file: str, target_image: int = 0, block_size: int = 4, block_delay: float = 0.05) -> bool:
        """
        Full firmware update sequence
        
        Args:
            fw_file: Path to firmware file
            target_image: Target image (0 or 1)
            block_size: Bytes per block (1-4)
            block_delay: Delay between blocks in seconds (50ms recommended for stability)
        
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
                print("Attempting abort...")
                self.abort()
                return False
            
            time.sleep(0.2)
            
            if not self.write_blocks(block_size=block_size, block_delay=block_delay):
                print("Attempting abort...")
                self.abort()
                return False
            
            time.sleep(0.2)
            
            if not self.commit_image():
                print("Attempting abort...")
                self.abort()
                return False
            
            print("\n✓ Firmware update complete!")
            print("Device will restart with new firmware shortly...")
            
            return True
            
        except Exception as e:
            print(f"✗ Update failed: {e}")
            import traceback
            traceback.print_exc()
            return False


def main():
    """Command-line interface"""
    parser = argparse.ArgumentParser(
        description="Serial Bootloader Firmware Update Tool"
    )
    parser.add_argument(
        'firmware',
        help='Path to firmware file (.bin or .hex)'
    )
    parser.add_argument(
        '--port', default='COM3',
        help='Serial port (default: COM3)'
    )
    parser.add_argument(
        '--baud', type=int, default=115200,
        help='Baud rate (default: 115200)'
    )
    parser.add_argument(
        '--image', type=int, default=0, choices=[0, 1],
        help='Target image (0=Image1, 1=Image2) (default: 0)'
    )
    parser.add_argument(
        '--block-size', type=int, default=4,
        help='Bytes per block (1-4) (default: 4)'
    )
    parser.add_argument(
        '--block-delay', type=float, default=0.05,
        help='Delay between blocks in seconds (default: 0.05 = 50ms) - increase if having issues'
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
    
    # Create updater
    updater = SerialBootloaderUpdater(
        port=args.port,
        baudrate=args.baud,
        verbose=args.verbose
    )
    
    # Connect and update
    if not updater.connect():
        sys.exit(1)
    
    try:
        success = updater.update(str(fw_path), target_image=args.image, 
                                block_size=args.block_size, block_delay=args.block_delay)
        sys.exit(0 if success else 1)
    finally:
        updater.disconnect()


if __name__ == '__main__':
    main()
