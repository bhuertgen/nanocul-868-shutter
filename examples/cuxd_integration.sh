#!/bin/bash
# =============================================================================
# nanocul-868-shutter: RaspberryMatic CUxD Integration Example
# =============================================================================
#
# Controls 868 MHz roller shutters via nanoCUL from RaspberryMatic/HomeMatic.
#
# Prerequisites:
#   - nanoCUL with KN controller firmware connected to RaspberryMatic
#   - CUxD addon installed (https://github.com/jens-maus/cuxd)
#   - CUxD device configured as "System > Exec" (CUX2801001:1)
#
# The nanoCUL appears as /dev/ttyUSB0 (or /dev/ttyUSB1) on the RaspberryMatic.
# You can verify with: ls -la /dev/ttyUSB*
#
# Usage:
#   ./cuxd_integration.sh <address> <command>
#   ./cuxd_integration.sh F020AABB20 UP
#   ./cuxd_integration.sh F020AABB20 DOWN
#   ./cuxd_integration.sh F020AABB20 STOP
#   ./cuxd_integration.sh F020AABB20 POS
#   ./cuxd_integration.sh F020AABB20 SAVE
#
# =============================================================================

# Configuration
SERIAL_PORT="/dev/ttyUSB0"
BAUD_RATE=57600

# Validate arguments
if [ $# -lt 2 ]; then
    echo "Usage: $0 <kn_address> <command>"
    echo "  Address: 10 hex chars, e.g. F020AABB20"
    echo "  Command: UP | DOWN | STOP | POS | SAVE"
    echo ""
    echo "Examples:"
    echo "  $0 F020AABB20 UP"
    echo "  $0 F020AABB20 STOP"
    exit 1
fi

KN_ADDR="$1"
COMMAND=$(echo "$2" | tr '[:lower:]' '[:upper:]')

# Validate address format (10 hex characters)
if ! echo "$KN_ADDR" | grep -qE '^[0-9A-Fa-f]{10}$'; then
    echo "Error: Invalid KN address '$KN_ADDR' (must be 10 hex chars)"
    exit 1
fi

# Validate command
case "$COMMAND" in
    UP|DOWN|STOP|POS|SAVE) ;;
    *)
        echo "Error: Unknown command '$COMMAND'"
        echo "Valid commands: UP, DOWN, STOP, POS, SAVE"
        exit 1
        ;;
esac

# Configure serial port (once, on first use)
# stty sets the baud rate and raw mode (no echo, no special processing)
stty -F "$SERIAL_PORT" "$BAUD_RATE" raw -echo -echoe -echok 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Error: Cannot configure $SERIAL_PORT"
    echo "Is the nanoCUL connected? Check: ls -la /dev/ttyUSB*"
    exit 1
fi

# Send command to nanoCUL
echo "Sending: SEND $KN_ADDR $COMMAND"
echo "SEND $KN_ADDR $COMMAND" > "$SERIAL_PORT"

# Wait for response (optional, for logging)
sleep 1
if [ -r "$SERIAL_PORT" ]; then
    # Read response with timeout
    timeout 2 cat "$SERIAL_PORT" 2>/dev/null &
    READER_PID=$!
    sleep 1
    kill $READER_PID 2>/dev/null
fi

echo "Done."

# =============================================================================
# CUxD Integration Notes
# =============================================================================
#
# To call this script from HomeMatic programs via CUxD:
#
# 1. Install CUxD addon on RaspberryMatic
# 2. Create a CUxD device:
#    - Type: (28) System
#    - Function: Exec
#    - Serial: CUX2801001
# 3. In HomeMatic program, use:
#    - DOM.GetObject("CUxD.CUX2801001:1.CMD_EXEC")
#      .State("/path/to/cuxd_integration.sh F020AABB20 UP");
#
# Alternative: Direct serial write without this script:
#    - DOM.GetObject("CUxD.CUX2801001:1.CMD_EXEC")
#      .State("echo 'SEND F020AABB20 UP' > /dev/ttyUSB0");
#
# =============================================================================
#
# For scheduled operations (e.g. close all shutters at sunset):
#
# Create a HomeMatic program with:
#   Condition: Time trigger (e.g. sunset)
#   Activity:  Script:
#     DOM.GetObject("CUxD.CUX2801001:1.CMD_EXEC")
#       .State("/path/to/cuxd_integration.sh F020AABB20 DOWN");
#
# For multiple shutters, add a small delay between commands:
#     var cmd = "/path/to/cuxd_integration.sh F020AABB20 DOWN";
#     cmd = cmd # " && sleep 2 && /path/to/cuxd_integration.sh F020CCDD20 DOWN";
#     DOM.GetObject("CUxD.CUX2801001:1.CMD_EXEC").State(cmd);
#
# =============================================================================
