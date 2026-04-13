# nanocul-868-shutter

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: Arduino](https://img.shields.io/badge/Platform-Arduino-teal.svg)](https://www.arduino.cc/)
[![Hardware: ATmega328P + CC1101](https://img.shields.io/badge/Hardware-ATmega328P%20%2B%20CC1101-orange.svg)]()

Open-source firmware and protocol documentation for controlling **868 MHz roller shutters** (Gaposa / Dooya / Kaiser Nienhaus) via a **nanoCUL USB stick** (ATmega328P + CC1101).

A standalone, low-cost alternative for direct RF control of 868 MHz roller shutter motors.

> **Hardware:** [nanoCUL 868 MHz USB-C Stick](https://schlauhaus.biz/en/product-2/nanocul-868/) from schlauHAUS (~30 EUR) — ATmega328P + CC1101 + CH340, with enclosure and SMA antenna

---

## Features

- **Send all shutter commands**: UP, DOWN, STOP, POSITION, SAVE
- **Receive and decode** incoming 868 MHz KN signals
- **SAVE mode**: Continuous transmission for saving favorite positions directly via RF
- **Adjustable RF parameters**: Frequency, timing, TX power, repeat count -- all via serial commands
- **EEPROM persistence**: Parameters survive reboot
- **No external libraries**: Only Arduino `SPI.h` required
- **Compact**: ~8 KB flash, ~520 bytes SRAM
- **Flipper Zero compatible**: Example `.sub` files included

---

## Supported Hardware

| Device | Description | Tested |
|--------|-------------|--------|
| **nanoCUL 868 MHz** (schlauHAUS) | ATmega328P + CC1101 + CH340 USB-C | Yes |
| Any ATmega328P + CC1101 board | Arduino Nano + CC1101 module | Should work (check pin mapping) |
| **Flipper Zero** | For signal capture, replay, and verification | Yes (`.sub` files) |

### Pin Mapping (nanoCUL from schlauHAUS)

```
ATmega328P    CC1101
──────────    ──────
D10 (CSN)  →  CSN
D11 (MOSI) →  SI
D12 (MISO) →  SO
D13 (SCK)  →  SCLK
D3  (GDO0) →  GDO0    ← IMPORTANT: D3, not D2!
D2  (GDO2) →  GDO2
```

---

## Supported Motors

- **Gaposa** roller shutter motors (868 MHz)
- **Dooya** tubular motors (868 MHz, KN protocol variant)
- **Kaiser Nienhaus** Furohre 868 MHz tubular motors (Art.Nr. 140100-144100)

All motors using the unidirectional OOK-based KN protocol at 868.35 MHz.

## Supported Remotes (Protocol-Compatible)

- Gaposa QCTX1 (1-channel)
- Gaposa QCTX5 (5-channel)
- Gaposa QCXTAB (tabletop remote)
- Gaposa QCXTAB4 (4-channel tabletop)
- Gaposa QCTDX (6-channel with timer)

---

## Quick Start

### 1. Get Hardware

- **nanoCUL 868 MHz USB stick** from [schlauHAUS](https://schlauhaus.biz/en/product-2/nanocul-868/) (~30 EUR)
- USB cable (USB-C for newer models)

### 2. Flash Firmware

1. Install **Arduino IDE 1.8.x** (Legacy) -- [Download](https://www.arduino.cc/en/software#legacy-ide-18x)
2. Open `firmware/nanocul_kn_controller.ino`
3. Board settings:
   - Board: **Arduino Nano**
   - Processor: **ATmega328P (Old Bootloader)** -- important!
   - Port: your COM port
   - Baudrate: 57600
4. Click **Upload**

See [docs/firmware_guide.md](docs/firmware_guide.md) for detailed instructions.

### 3. Control Your Shutters

Open a serial terminal at **57600 baud** and type:

```
SEND F020AABB20 UP       # Motor goes up
SEND F020AABB20 DOWN     # Motor goes down
SEND F020AABB20 STOP     # Motor stops
SEND F020AABB20 POS      # Go to saved favorite position
SEND F020AABB20 SAVE     # Save current position (continuous TX ~4s)
```

Replace `F020AABB20` with your motor's KN address.

> **Finding your address:** Use `RECV` mode to listen for your existing remote control and read the KN address directly.

---

## Command Reference

| Command | Description |
|---------|-------------|
| `SEND <addr> UP` | Motor up |
| `SEND <addr> DOWN` | Motor down |
| `SEND <addr> STOP` | Motor stop |
| `SEND <addr> POS` | Go to saved favorite position |
| `SEND <addr> SAVE` | Save current position (~4s continuous TX) |
| `SEND <addr> <hex>` | Send raw KN command byte |
| `SEND <16hex> [n]` | Send raw 64-bit RF data with optional repeat count |
| `RECV` | Enter receive mode (decode incoming signals) |
| `FREQ <MHz>` | Set frequency (e.g. `FREQ 868.35`) |
| `VER` | Show firmware version and CC1101 info |
| `GET` | Show all runtime parameters |
| `SET <param> <value>` | Set parameter (saved to EEPROM) |
| `SET DEFAULTS` | Reset all parameters to factory defaults |
| `HELP` | Show command list |

### Tunable Parameters (SET/GET)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `SHORT` | 440 us | Manchester short pulse |
| `LONG` | 880 us | Manchester long pulse |
| `SYNC` | 2600 us | Sync pulse duration |
| `GAP` | 15000 us | Gap between repeats |
| `REPEATS` | 6 | Number of transmit repeats |
| `SAVETIME` | 4000 ms | SAVE continuous TX duration |
| `SAVEGAP` | 15000 us | SAVE inter-frame gap |
| `TXPOWER` | 0xC0 | CC1101 PA table value (~10 dBm) |
| `PREAMBLE` | 1 | Preamble toggle on/off |

---

## Protocol Overview

The KN protocol is a **unidirectional OOK** (On-Off Keying) radio protocol:

- **Frequency**: 868.35 MHz (standard) / 868.15-868.30 MHz (some remotes)
- **Modulation**: OOK with Manchester encoding
- **Frame size**: 64 bits (8 bytes)
- **Timing**: 440 us / 880 us Manchester pulses, 2600 us sync
- **Repeats**: 6 transmissions with 15 ms gap
- **Security**: None (no encryption, no rolling code)

### Frame Format

```
Byte:   0    1    2    3    4    5    6    7
Hex:   [PP] [PP] [AA] [AA] [SS] [CC] [XX] [XX]
       |Preamble| |Address|  |   |Cmd| |Suffix|
                            Sep
```

The KN address format is XOR 0xFF of the over-the-air RF data:
```
KN:  F0  20  AA  BB  20  80  AE  AE
RF:  0F  DF  55  44  DF  7F  51  51
```

See [docs/protocol_specification.md](docs/protocol_specification.md) for the full reverse-engineered specification.

---

## Where to Buy

| Item | Source | Price |
|------|--------|-------|
| nanoCUL 868 MHz USB stick | [schlauHAUS](https://schlauhaus.biz/en/product-2/nanocul-868/) | ~30 EUR |
| Flipper Zero (optional, for debugging) | [flipperzero.one](https://flipperzero.one/) | ~170 EUR |

The nanoCUL comes pre-flashed with SIGNALduino firmware. This project provides an alternative firmware specialized for KN shutter control. The original SIGNALduino firmware can be restored at any time -- see the [firmware guide](docs/firmware_guide.md).

---

## How to Flash

### Arduino IDE 1.8.x (Recommended)

1. Download [Arduino IDE 1.8.x Legacy](https://www.arduino.cc/en/software#legacy-ide-18x)
2. Install CH340 driver if needed: [WCH CH341SER](https://www.wch-ic.com/downloads/CH341SER_EXE.html)
3. Open `firmware/nanocul_kn_controller.ino`
4. Settings:
   - Board: **Arduino Nano**
   - Processor: **ATmega328P (Old Bootloader)**
   - Port: your nanoCUL COM port
5. Click Upload

### Common Issues

| Error | Solution |
|-------|----------|
| `stk500_getsync(): not in sync` | Select **"ATmega328P (Old Bootloader)"** |
| COM port not found | Install CH340 driver, reconnect USB |
| IDE 2.x upload hangs | Use Arduino IDE 1.8.x (Legacy) instead |

---

## Integration Options

### RaspberryMatic (OpenCCU) / CUxD

Control shutters from HomeMatic/RaspberryMatic via [CUxD](https://github.com/jens-maus/cuxd) addon.
Create virtual blind actuators (HM-LC-Bl1-FM) that call the nanoCUL via serial:

```bash
# Send command via serial port
echo "SEND F020AABB20 UP" > /dev/ttyUSB0
```

Supports: LEVEL slider (0-100%), UP/DOWN/STOP buttons, position buttons, room/function assignment.

See [examples/cuxd_integration.sh](examples/cuxd_integration.sh) for a complete example.

### Python Serial

```python
import serial
ser = serial.Serial('/dev/ttyUSB0', 57600, timeout=2)
ser.write(b'SEND F020AABB20 UP\n')
response = ser.readline()
print(response.decode())
```

See [examples/python_serial.py](examples/python_serial.py) for a full example with error handling.

### Home Assistant (Future)

A Home Assistant integration is planned but not yet available. In the meantime, you can use:
- Shell commands via `command_line` integration
- Python scripts via `python_script` integration
- The serial port directly via a custom component

### Flipper Zero

Example `.sub` files for Flipper Zero are included in the `flipper/` directory. These can be used for:
- Testing and verifying motor responses
- Debugging protocol timing
- Quick manual control without a PC

---

## Project Structure

```
nanocul-868-shutter/
  firmware/
    nanocul_kn_controller.ino   # Main firmware (Arduino sketch)
  docs/
    protocol_specification.md   # Full KN protocol reverse-engineering
    firmware_guide.md           # Flashing, backup, restore guide
  flipper/
    example_up.sub              # Flipper Zero example: UP command
    example_down.sub            # Flipper Zero example: DOWN command
  examples/
    python_serial.py            # Python serial control example
    cuxd_integration.sh         # RaspberryMatic CUxD example
  LICENSE                       # MIT License
  README.md                     # This file
```

---

## Contributing

Contributions are welcome! Some areas where help is appreciated:

- **Home Assistant integration** (custom component)
- **ESPHome component** (ESP32 + CC1101)
- **Additional motor brands** testing and documentation
- **DY2 protocol** reverse-engineering (bidirectional, FSK, rolling code)
- **FHEM module** for direct integration

Please open an issue first to discuss significant changes.

---

## License

This project is licensed under the **MIT License** -- see [LICENSE](LICENSE) for details.

The MIT License covers the firmware source code, example scripts, and documentation
created by the authors. The RF protocol documentation describes publicly observable
radio signals for interoperability purposes, similar to projects like
[rtl_433](https://github.com/merbanan/rtl_433),
[ESPHome](https://esphome.io/components/remote_transmitter.html#dooya), and
[Flipper Zero](https://docs.flipper.net/zero/sub-ghz/supported-vendors).

---

## Disclaimer

This project is for **educational and interoperability purposes only**.

- Use at your own risk. The authors accept no liability for any damage or issues
  caused by using this firmware or documentation.
- Ensure compliance with your local RF regulations when transmitting on 868 MHz.
- The 868 MHz ISM band is license-free in Europe (ETSI EN 300 220) but may be
  restricted in other regions.
- This project does not circumvent any encryption, copy protection, or security
  mechanisms. The KN protocol is unencrypted and uses standard OOK modulation.

---

## Trademarks

- **Gaposa** is a trademark of Gaposa Srl (Italy).
- **Dooya** is a trademark of Ningbo Dooya Mechanic & Electronic Technology Co., Ltd. (China).
- **Kaiser Nienhaus** is a trademark of Kaiser Nienhaus Komfort & Technik GmbH (Germany).
- **Flipper Zero** is a trademark of Flipper Devices Inc.
- **HomeMatic**, **RaspberryMatic**, and **OpenCCU** are trademarks of eQ-3 AG (Germany).

This project is **not affiliated with, endorsed by, or sponsored by** any of
these companies. All trademarks are the property of their respective owners.
Product names are used solely for identification and interoperability purposes.

---

## Credits / Authors

- **Boris Huertgen** -- Project lead, reverse engineering, hardware testing, protocol verification
- **[Claude Code](https://claude.ai/code)** (Anthropic) -- AI-assisted firmware development, protocol analysis, documentation. This project was developed using Claude Code (Claude Opus 4.6) as a collaborative AI coding assistant for signal decoding, CC1101 register configuration, Manchester encoding implementation, and automated testing via serial/Flipper Zero integration.

### Tools Used

- [Claude Code](https://claude.ai/code) -- AI coding assistant (Anthropic Claude Opus 4.6)
- [Flipper Zero](https://flipperzero.one/) -- RF signal capture, frequency analysis, protocol verification
- [nanoCUL / SIGNALduino](https://github.com/RFD-FHEM/SIGNALDuino) -- Initial signal reception and analysis

### Acknowledgments

- [schlauHAUS](https://schlauhaus.biz/) -- nanoCUL hardware
- [SIGNALduino](https://github.com/RFD-FHEM/SIGNALDuino) -- Original nanoCUL firmware
- [cc1101-tool](https://github.com/mcore1976/cc1101-tool) -- CC1101 reference implementation
- [rtl_433](https://github.com/merbanan/rtl_433) -- Dooya protocol decoder reference
- [ESPHome](https://esphome.io/) -- Dooya protocol support reference
- [CUxD](https://github.com/jens-maus/cuxd) -- HomeMatic/RaspberryMatic virtual device addon
- Texas Instruments -- CC1101 Low-Power Sub-1 GHz RF Transceiver datasheet
