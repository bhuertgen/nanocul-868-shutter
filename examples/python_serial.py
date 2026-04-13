#!/usr/bin/env python3
"""
nanocul-868-shutter: Python Serial Control Example

Controls 868 MHz roller shutters (Gaposa/Dooya/Kaiser Nienhaus)
via nanoCUL USB stick with custom KN controller firmware.

Requirements:
    pip install pyserial

Usage:
    python python_serial.py                  # Interactive mode
    python python_serial.py UP               # Send UP to default address
    python python_serial.py F020AABB20 DOWN  # Send DOWN to specific address

Serial protocol:
    The nanoCUL firmware accepts commands over serial at 57600 baud.
    Commands are terminated with newline (\\n).
    Responses are printed line by line.
"""

import sys
import time
import serial
import serial.tools.list_ports


# ============================================================================
# Configuration -- adjust to your setup
# ============================================================================

# Serial port of the nanoCUL stick
# Linux:   /dev/ttyUSB0
# macOS:   /dev/tty.usbserial-*
# Windows: COM3, COM4, etc.
SERIAL_PORT = '/dev/ttyUSB0'

BAUD_RATE = 57600
TIMEOUT = 2  # seconds

# Default KN address (replace with your motor's address!)
# Use RECV mode to discover your motor's address from an existing remote.
DEFAULT_ADDRESS = 'F020AABB20'


# ============================================================================
# nanoCUL Serial Interface
# ============================================================================

class NanoCUL:
    """Interface to nanoCUL KN controller firmware via serial port."""

    def __init__(self, port=SERIAL_PORT, baudrate=BAUD_RATE, timeout=TIMEOUT):
        self.ser = serial.Serial(port, baudrate, timeout=timeout)
        # Wait for firmware boot message
        time.sleep(2)
        # Flush any boot messages
        self.ser.reset_input_buffer()

    def send_command(self, command):
        """Send a command and return the response lines."""
        self.ser.write((command + '\n').encode())
        time.sleep(0.1)

        lines = []
        while True:
            line = self.ser.readline().decode().strip()
            if not line:
                break
            lines.append(line)
            print(f"  < {line}")

        return lines

    def shutter_up(self, address=DEFAULT_ADDRESS):
        """Send UP command to a motor."""
        print(f"Sending UP to {address}")
        return self.send_command(f'SEND {address} UP')

    def shutter_down(self, address=DEFAULT_ADDRESS):
        """Send DOWN command to a motor."""
        print(f"Sending DOWN to {address}")
        return self.send_command(f'SEND {address} DOWN')

    def shutter_stop(self, address=DEFAULT_ADDRESS):
        """Send STOP command to a motor."""
        print(f"Sending STOP to {address}")
        return self.send_command(f'SEND {address} STOP')

    def shutter_position(self, address=DEFAULT_ADDRESS):
        """Send POS (favorite position) command to a motor."""
        print(f"Sending POS to {address}")
        return self.send_command(f'SEND {address} POS')

    def shutter_save(self, address=DEFAULT_ADDRESS):
        """Send SAVE command (continuous ~4s TX) to save current position."""
        print(f"Sending SAVE to {address} (this takes ~4 seconds)")
        return self.send_command(f'SEND {address} SAVE')

    def get_version(self):
        """Get firmware version and CC1101 info."""
        return self.send_command('VER')

    def get_params(self):
        """Get all current parameters."""
        return self.send_command('GET')

    def set_frequency(self, mhz):
        """Set the RF frequency in MHz (e.g. 868.35)."""
        return self.send_command(f'FREQ {mhz}')

    def close(self):
        """Close the serial connection."""
        self.ser.close()


# ============================================================================
# Helper: find nanoCUL port automatically
# ============================================================================

def find_nanocul_port():
    """Try to find the nanoCUL by looking for CH340 USB-serial devices."""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        desc = (port.description or '').lower()
        if 'ch340' in desc or 'ch341' in desc:
            print(f"Found nanoCUL candidate: {port.device} ({port.description})")
            return port.device
    return None


# ============================================================================
# Main
# ============================================================================

def main():
    # Auto-detect port if default is not available
    port = SERIAL_PORT
    auto_port = find_nanocul_port()
    if auto_port:
        port = auto_port

    print(f"Connecting to nanoCUL on {port} at {BAUD_RATE} baud...")

    try:
        cul = NanoCUL(port=port)
    except serial.SerialException as e:
        print(f"Error: Could not open {port}: {e}")
        print("\nAvailable ports:")
        for p in serial.tools.list_ports.comports():
            print(f"  {p.device}: {p.description}")
        sys.exit(1)

    # Show firmware version
    print("\n--- Firmware Info ---")
    cul.get_version()

    # Command line arguments
    if len(sys.argv) >= 3:
        # python_serial.py <address> <command>
        address = sys.argv[1]
        command = sys.argv[2].upper()
    elif len(sys.argv) == 2:
        # python_serial.py <command>  (use default address)
        address = DEFAULT_ADDRESS
        command = sys.argv[1].upper()
    else:
        command = None

    if command:
        print(f"\n--- Sending {command} to {address} ---")
        commands = {
            'UP': cul.shutter_up,
            'DOWN': cul.shutter_down,
            'STOP': cul.shutter_stop,
            'POS': cul.shutter_position,
            'SAVE': cul.shutter_save,
        }
        func = commands.get(command)
        if func:
            func(address)
        else:
            print(f"Unknown command: {command}")
            print("Valid commands: UP, DOWN, STOP, POS, SAVE")
    else:
        # Interactive demo
        print("\n--- Interactive Demo ---")
        print("Sending UP...")
        cul.shutter_up()
        time.sleep(3)

        print("\nSending STOP...")
        cul.shutter_stop()

    cul.close()
    print("\nDone.")


if __name__ == '__main__':
    main()
