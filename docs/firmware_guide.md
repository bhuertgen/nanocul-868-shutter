# nanoCUL Firmware Guide

Backup, Update, Restore and Decision Guide

---

## 1. Current Firmware

```
Device:    nanoCUL 868 MHz (schlauHAUS)
Hardware:  ATmega328P + CC1101 + CH340 USB-C
Original:  SIGNALduino v4.0.0 cc1101
Baudrate:  57600
```

---

## 2. Why a Custom Firmware?

### 2.1 Problems with SIGNALduino v4.0.0

The stock SIGNALduino firmware has **three critical limitations**
for our use case (Kaiser Nienhaus shutter control):

**Problem 1: Serial input buffer only 14 characters**

```c
char IB_1[14]; // Input Buffer one - capture commands
```

The main input buffer is only 14 bytes. Any command longer than that
is rejected with "Command to long".

**Problem 2: Send commands do not work reliably**

Although the send path (SR, SM, SC commands) uses a separate 256-byte buffer
and commands are accepted, the motors do not respond.

Possible causes:
- SIGNALduino's Manchester encoding has a different polarity than the KN protocol
- The CC1101 TX configuration (PA-Table, FREND0) is not set correctly
- The pulse timing is not precise enough (Arduino interrupt overhead)
- The SR command truncates data at ~43 characters (of 64 needed)

**Problem 3: No SAVE mode (continuous sending)**

The SAVE command (save position) requires continuous sending without
pause for 3-5 seconds. SIGNALduino has no such mode.

### 2.2 What SIGNALduino Does Well

- **Receiving** works excellently on 868.35 MHz
- Decodes Manchester signals automatically (Mu/Ms format)
- Supports many protocols (FS20, HomeMatic, IT, etc.)
- Well integrated into FHEM

### 2.3 Decision Guide

| Use Case | SIGNALduino | Custom Firmware |
|----------|-------------|-----------------|
| Receive KN commands (sniffing) | Yes | Yes |
| Send KN commands | No | Yes |
| SAVE (save position) | No | Yes |
| Other 868 MHz protocols | Yes (many) | Only KN |
| FHEM integration | Yes | No (own script) |
| Direct RF control (no gateway needed) | No | Yes |

**Recommendation:**
- If you **only want KN shutters** -> flash the **Custom Firmware**
- If you need the nanoCUL for **other protocols** too -> keep **SIGNALduino** and get a second nanoCUL for KN
- **Always make a backup** of SIGNALduino first -> you can always restore it

---

## 3. Firmware Backup (Save SIGNALduino)

### 3.1 What is Backed Up?

| File | Content | Description |
|------|---------|-------------|
| `backup_flash.hex` | Complete flash memory (32 KB) | Firmware + Bootloader |
| `backup_eeprom.hex` | EEPROM (1 KB) | Configuration, frequency, protocol settings |

### 3.2 Prerequisites

- **avrdude** installed (comes with Arduino IDE)
- Path to Arduino IDE avrdude:
  ```
  C:\Users\<USER>\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17\bin\avrdude.exe
  ```
- Or install separately: https://github.com/avrdudes/avrdude/releases

### 3.3 Create Backup

```bash
# Set paths (adjust to your system!)
set AVRDUDE="C:\Users\<USER>\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17\bin\avrdude.exe"
set CONF="C:\Users\<USER>\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17\etc\avrdude.conf"

# Backup flash (complete firmware + bootloader)
%AVRDUDE% -C %CONF% -p m328p -c arduino -P COMx -b 57600 -U flash:r:backup_flash.hex:i

# Backup EEPROM (configuration)
%AVRDUDE% -C %CONF% -p m328p -c arduino -P COMx -b 57600 -U eeprom:r:backup_eeprom.hex:i
```

**Keep both files safe!**

### 3.4 Verify Backup

```bash
%AVRDUDE% -C %CONF% -p m328p -c arduino -P COMx -b 57600 -U flash:v:backup_flash.hex:i
```

Output should show `avrdude: ... verified`.

---

## 4. Flash Custom Firmware

### 4.1 Method A: Arduino IDE (Recommended)

#### Arduino IDE Version: Use Legacy 1.8.x!

**IMPORTANT:** There are two versions of the Arduino IDE:
- **Arduino IDE 2.x** (new, Electron-based) -- has issues with some Nano clones and CH340
- **Arduino IDE 1.8.x** (Legacy) -- **RECOMMENDED**, proven and stable with nanoCUL

**Download Arduino IDE 1.8.x (Legacy):**
- https://www.arduino.cc/en/software
- Scroll down to **"Legacy IDE (1.8.x)"**
- Direct link: https://www.arduino.cc/en/software#legacy-ide-18x
- Windows Installer: `arduino-1.8.19-windows.exe`
- Or Windows ZIP: `arduino-1.8.19-windows.zip` (no installation needed)

#### CH340 USB Driver

The nanoCUL uses a CH340 USB-to-serial chip. Windows 10/11 usually installs
the driver automatically. If the COM port is not detected:
- **CH340 Driver Download:** https://www.wch-ic.com/downloads/CH341SER_EXE.html
- Install, unplug and replug the nanoCUL
- Check in Device Manager: "Ports (COM & LPT)" -> "USB-SERIAL CH340 (COMx)"

#### Step-by-Step Guide

1. **Install Arduino IDE 1.8.x** (see above)

2. **Start Arduino IDE and configure board:**
   - Tools -> Board -> **"Arduino Nano"**
   - Tools -> Processor -> **"ATmega328P (Old Bootloader)"** <-- IMPORTANT!
     (NOT "ATmega328P" without suffix -- that uses 115200 baud and will fail!)
   - Tools -> Port -> **COMx** (whichever port your nanoCUL is on)

3. **Open firmware:**
   - File -> Open -> `firmware/nanocul_kn_controller.ino`

4. **Compile (optional, to verify):**
   - Sketch -> Verify/Compile (checkmark button)
   - Should complete without errors
   - No external libraries needed -- only standard SPI.h

5. **Upload:**
   - Sketch -> Upload (arrow button)
   - LEDs on the nanoCUL will blink during upload
   - Wait until "Done uploading" appears
   - Duration: ~10-15 seconds

6. **Test:**
   - Tools -> Serial Monitor (or Ctrl+Shift+M)
   - Bottom right: set baud rate to **57600**
   - Line ending: **"Newline"** or **"Both NL & CR"**
   - Type: `VER` -> should show firmware version and frequency
   - Type: `HELP` -> shows all available commands

#### Common Flashing Errors

| Error | Cause | Solution |
|-------|-------|---------|
| `avrdude: stk500_getsync(): not in sync` | Wrong processor selected | Select **"ATmega328P (Old Bootloader)"**! |
| `Serial port COMx not found` | nanoCUL not connected or driver missing | Install CH340 driver, check USB cable |
| `Sketch too large` | Should not happen (~8 KB of 30 KB) | Is board "Arduino Nano" selected? |
| IDE 2.x upload hangs | Known issue with CH340 | Use Arduino IDE 1.8.x (Legacy) |

### 4.2 Method B: avrdude (Command Line)

```bash
# If the firmware is available as .hex:
%AVRDUDE% -C %CONF% -p m328p -c arduino -P COMx -b 57600 -U flash:w:nanocul_kn_controller.hex:i

# If the firmware is a .ino, it must be compiled first
# -> Easier via Arduino IDE (Method A)
```

### 4.3 Method C: XLoader (GUI Tool)

1. Download XLoader: https://github.com/xinabox/xLoader
2. Settings:
   - Hex file: `nanocul_kn_controller.hex`
   - Device: **Duemilanove/Nano(ATmega328)**
   - COM port: COMx
   - Baud rate: **57600**
3. Click "Upload"

---

## 5. Restore SIGNALduino Firmware

### 5.1 From Your Own Backup

```bash
# Restore flash
%AVRDUDE% -C %CONF% -p m328p -c arduino -P COMx -b 57600 -U flash:w:backup_flash.hex:i

# Restore EEPROM
%AVRDUDE% -C %CONF% -p m328p -c arduino -P COMx -b 57600 -U eeprom:w:backup_eeprom.hex:i
```

### 5.2 Fresh SIGNALduino Install (No Backup)

If you have no backup, the SIGNALduino firmware can be re-flashed:

1. **Download firmware:**
   - https://github.com/RFD-FHEM/SIGNALDuino/releases
   - File: `SIGNALDuino_nanocc1101.hex` (for nanoCUL with CC1101)

2. **Flash:**
   ```bash
   %AVRDUDE% -C %CONF% -p m328p -c arduino -P COMx -b 57600 -U flash:w:SIGNALDuino_nanocc1101.hex:i
   ```

3. **Initialize EEPROM:**
   After flashing, on first start:
   - Open serial monitor (57600 baud)
   - Send command `e` (Factory Reset of CC1101 config)
   - Send `W0F21` then `W1065` then `W11E8` (set 868.35 MHz)
   - Send `R` (Reload)

### 5.3 schlauHAUS Original Firmware

The nanoCUL from schlauhaus.biz ships with SIGNALduino v4.0.0.
The exact firmware version can be requested from the manufacturer:
https://schlauhaus.biz/en/product-2/nanocul-868/

---

## 6. Firmware Comparison

### SIGNALduino v4.0.0

```
Type:         Universal 868/433 MHz receiver/sender
Protocols:    FS20, HomeMatic, IT, Oregon, many more (~100)
Receiving:    Excellent, automatic protocol detection
Sending:      Limited (14-byte input buffer, unreliable timing)
KN sending:   NOT possible (buffer too small, Manchester polarity wrong)
Integration:  FHEM (native), ioBroker (via adapter)
Baudrate:     57600
Size:         ~28 KB flash
```

### Custom KN-Controller (nanocul_kn_controller.ino)

```
Type:         Specialized KN/Dooya shutter controller
Protocols:    Only KN/Dooya OOK Manchester on 868 MHz
Receiving:    Decode KN signals and output as hex
Sending:      All 6 KN commands (UP/DOWN/STOP/POS/SAVE + Raw)
KN sending:   Yes, with correct Manchester polarity and SAVE mode
Integration:  Serial via Python/Script, CUxD compatible
Baudrate:     57600
Size:         ~8 KB flash
Dependencies: None (only Arduino SPI.h)
```

---

## 7. Running Two nanoCULs in Parallel

If you want both SIGNALduino (for other protocols) and the KN controller,
you can use **two nanoCUL sticks**:

```
USB Port 1: nanoCUL #1 with SIGNALduino  -> FHEM/other protocols
USB Port 2: nanoCUL #2 with KN-Controller -> Shutter control
```

Cost: ~30 EUR for a second nanoCUL.

**Alternatively:** A single nanoCUL with the KN controller is enough if you
only want to control the shutters.

---

## 8. Troubleshooting

### Flashing Fails

```
avrdude: stk500_getsync(): not in sync
```
-> Select **"ATmega328P (Old Bootloader)"** in Arduino IDE (not "ATmega328P")

### COM Port Not Found

-> Install CH340 driver: https://www.wch-ic.com/downloads/CH341SER_EXE.html
-> Unplug and replug the nanoCUL

### Firmware Does Not Respond After Flashing

-> Set serial monitor to **57600 baud**
-> Type `VER` or `HELP`
-> If no response: press reset button on nanoCUL

### SIGNALduino Restore Fails

-> Bootloader may have been overwritten
-> Solution: Re-flash bootloader with an ISP programmer
-> Or: buy a new nanoCUL (~30 EUR)
-> **IMPORTANT: Arduino IDE does NOT overwrite the bootloader.**
  Only avrdude with `-e` flag (chip erase) deletes the bootloader.
  Normal upload via Arduino IDE is safe.

---

## 9. Test Plan After Flashing

### Stepwise Test with Flipper Zero as Monitor

The Flipper Zero serves as an independent RF monitor to verify that the
nanoCUL is transmitting correctly BEFORE talking to a motor.

| Step | Command on nanoCUL | Monitor | Expected Result |
|------|-------------------|---------|-----------------|
| 1 | `VER` | Serial Monitor | Firmware version + frequency |
| 2 | `SEND 0FDF5544DF7F5151` | Flipper: Sub-GHz > Read RAW, 868.35, AM650 | Flipper receives OOK signal |
| 3 | Compare signal | Flipper RAW data | Pulses ~440/880us? Sync ~2600us? |
| 4 | `SEND F020AABB20 UP` | Observe shutter | Motor goes up |
| 5 | `SEND F020AABB20 STOP` | Observe shutter | Motor stops |
| 6 | `SEND F020AABB20 DOWN` | Observe shutter | Motor goes down |
| 7 | `SEND F020AABB20 POS` | Observe shutter | Motor goes to favorite position |
| 8 | `SEND F020AABB20 SAVE` | Observe shutter | Motor twitches both directions |
| 9 | `RECV` + send from remote | nanoCUL output | Receives and decodes hex data |

**Steps 2-3 are critical:** If the Flipper receives the signal correctly,
the motor will respond too. If the timing is wrong, you can see it on the
Flipper before involving a motor.

---

## 10. Checklist: Firmware Switch

### Before switching to Custom Firmware:
- [ ] Backup Flash: `avrdude ... -U flash:r:backup_flash.hex:i`
- [ ] Backup EEPROM: `avrdude ... -U eeprom:r:backup_eeprom.hex:i`
- [ ] Backup files stored safely
- [ ] Arduino IDE installed and board configured
- [ ] nanoCUL connected via USB
- [ ] Serial monitor tested (57600 baud)

### After flashing:
- [ ] `VER` -> Check firmware version
- [ ] `SEND F020AABB20 STOP` -> Safe test (does not move shutter if already stopped)
- [ ] `RECV` -> Receive test (send from remote and check output)

### Back to SIGNALduino:
- [ ] `avrdude ... -U flash:w:backup_flash.hex:i`
- [ ] `avrdude ... -U eeprom:w:backup_eeprom.hex:i`
- [ ] `V` -> Check SIGNALduino version
