// =============================================================================
// nanoCUL KN Controller Firmware v0.3
// =============================================================================
//
// Custom firmware for nanoCUL USB stick (ATmega328P + CC1101 + CH340)
// Controls Kaiser Nienhaus / Dooya roller shutter motors via 868.35 MHz OOK.
// Direct RF control for RaspberryMatic (OpenCCU) / CUxD and other home automation systems.
//
// Hardware: ATmega328P (Arduino Nano, Old Bootloader)
//   CC1101 SPI: SCK=D13, MISO=D12, MOSI=D11, CSN=D10, GDO0=D3
//   USB serial: CH340 at 57600 baud
//
// Protocol: 64-bit OOK Manchester
//   Sync:  2600us HIGH
//   Bit 0: 440us LOW + 880us HIGH   (verified from Flipper Zero .sub)
//   Bit 1: 880us LOW + 440us HIGH   (verified from Flipper Zero .sub)
//   Preamble toggle: byte 0 alternates 0x0F / 0x8F between repeats
//   Normal: 6 repeats with 15ms gap
//   SAVE:   continuous repeats for ~4 seconds (no gap)
//
// Serial commands:
//   SEND <kn_addr> UP        - Motor up   (KN 0x80, RF 0x7F)
//   SEND <kn_addr> DOWN      - Motor down (KN 0x20, RF 0xDF)
//   SEND <kn_addr> STOP      - Motor stop (KN 0x40, RF 0xBF)
//   SEND <kn_addr> POS       - Go to saved position (KN 0x4C, RF 0xB3)
//   SEND <kn_addr> SAVE      - Save current position (KN 0x0C, RF 0xF3)
//   SEND <kn_addr> <hex>     - Send raw KN command byte
//   RECV                     - Enter receive mode (any key to stop)
//   FREQ <MHz>               - Set frequency (e.g. FREQ 868.35)
//   VER                      - Show firmware version and CC1101 info
//   GET                      - Show all runtime parameters
//   SET <param> <value>      - Set runtime parameter (saved to EEPROM)
//   SET DEFAULTS             - Reset all parameters to factory defaults
//   HELP                     - Show full command list
//
// KN address format: 10 hex chars, e.g. F020AABB20
//   Full frame: F0 20 {addr_hi} {addr_lo} {channel} {cmd} AE AE
//   RF data:    XOR each byte with 0xFF
//
// No external libraries required - direct SPI register access.
//
// Author: Boris Huertgen / Claude
// Date: 2026-04-11
// License: MIT
// =============================================================================

#include <SPI.h>

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------
#define FW_VERSION "nanoCUL-KN v0.3"

// ---------------------------------------------------------------------------
// Pin definitions for nanoCUL PCB (schlauhaus.biz)
// IMPORTANT: D2=GDO2, D3=GDO0 on nanoCUL! (not D2=GDO0 as some docs say)
// ---------------------------------------------------------------------------
#define PIN_CSN    10   // CC1101 chip select (active low)
#define PIN_GDO0    3   // CC1101 GDO0 on D3 (nanoCUL standard!)
#define PIN_GDO2    2   // CC1101 GDO2 on D2 (interrupt capable)
#define PIN_SCK    13   // SPI clock
#define PIN_MISO   12   // SPI MISO
#define PIN_MOSI   11   // SPI MOSI

// ---------------------------------------------------------------------------
// KN Protocol timing DEFAULTS (microseconds) - verified from Flipper Zero
// These are the initial values. All can be changed at runtime via SET command
// and are stored persistently in EEPROM.
// ---------------------------------------------------------------------------
#define DEF_SYNC_US      2600   // Sync pulse HIGH duration
#define DEF_SHORT_US      440   // Short pulse duration (Manchester half-bit)
#define DEF_LONG_US       880   // Long pulse duration (2x short)
#define DEF_GAP_US      15000   // Gap between normal repeats
#define DEF_REPEATS         6   // Repeats for normal commands
#define DEF_SAVE_MS      4000   // Continuous send duration for SAVE (ms)
#define DEF_SAVE_GAP_US 15000   // Minimal gap between SAVE frames (us)
#define DEF_TX_POWER     0xC0   // CC1101 PA table value (~10 dBm)
#define DEF_PREAMBLE_TGL    1   // Preamble toggle on(1) / off(0)

// ---------------------------------------------------------------------------
// Runtime-configurable parameters (stored in EEPROM)
// Changed via serial SET command, persisted across reboots.
// ---------------------------------------------------------------------------
#include <EEPROM.h>

#define EEPROM_MAGIC_ADDR  0    // Magic byte to detect first boot
#define EEPROM_MAGIC_VAL   0x4B // 'K' for KN
#define EEPROM_PARAMS_ADDR 1    // Start of parameter block

struct KnParams {
  uint16_t sync_us;       // Sync pulse duration (us)
  uint16_t short_us;      // Short pulse duration (us)
  uint16_t long_us;       // Long pulse duration (us)
  uint16_t gap_us;        // Gap between repeats (us) - stored as gap/10 to fit uint16
  uint8_t  repeats;       // Number of repeats
  uint16_t save_ms;       // SAVE continuous duration (ms)
  uint16_t save_gap_us;   // SAVE inter-frame gap (us)
  uint8_t  tx_power;      // CC1101 PA table value
  uint8_t  preamble_tgl;  // Preamble toggle enabled (1/0)
};

KnParams params;

void loadParams() {
  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VAL) {
    // First boot or EEPROM cleared - use defaults
    params.sync_us      = DEF_SYNC_US;
    params.short_us     = DEF_SHORT_US;
    params.long_us      = DEF_LONG_US;
    params.gap_us       = DEF_GAP_US / 10;  // stored as /10
    params.repeats      = DEF_REPEATS;
    params.save_ms      = DEF_SAVE_MS;
    params.save_gap_us  = DEF_SAVE_GAP_US;
    params.tx_power     = DEF_TX_POWER;
    params.preamble_tgl = DEF_PREAMBLE_TGL;
    saveParams();
    EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);
  } else {
    EEPROM.get(EEPROM_PARAMS_ADDR, params);
  }
}

void saveParams() {
  EEPROM.put(EEPROM_PARAMS_ADDR, params);
}

// Accessor: gap is stored as /10 to fit uint16
uint32_t getGapUs() { return (uint32_t)params.gap_us * 10; }
void setGapUs(uint32_t us) { params.gap_us = us / 10; }

// C3 fix: safe delay for values > 16383 us (delayMicroseconds limit on ATmega328P)
void safeDelayUs(uint32_t us) {
  while (us > 16000) {
    delay(16);
    us -= 16000;
  }
  if (us > 0) delayMicroseconds(us);
}

// ---------------------------------------------------------------------------
// Serial commands for parameter configuration:
//
//   SET SHORT <us>       - Short pulse (default 440)
//   SET LONG <us>        - Long pulse (default 880)
//   SET SYNC <us>        - Sync pulse (default 2600)
//   SET GAP <us>         - Inter-repeat gap (default 15000)
//   SET REPEATS <n>      - Number of repeats (default 6)
//   SET SAVETIME <ms>    - SAVE duration in ms (default 4000)
//   SET SAVEGAP <us>     - SAVE inter-frame gap (default 500)
//   SET TXPOWER <hex>    - PA table value (default C0, max C6)
//   SET PREAMBLE <0|1>   - Preamble toggle on/off (default 1)
//   SET DEFAULTS         - Reset all parameters to factory defaults
//   GET                  - Show all current parameters
//   SAVE                 - Save parameters to EEPROM (also auto-saved on SET)
//
// All SET commands immediately take effect AND save to EEPROM.
// Parameters survive reboot/power-cycle.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// CC1101 Register addresses
// ---------------------------------------------------------------------------
#define CC1101_IOCFG2    0x00
#define CC1101_IOCFG1    0x01
#define CC1101_IOCFG0    0x02
#define CC1101_FIFOTHR   0x03
#define CC1101_SYNC1     0x04
#define CC1101_SYNC0     0x05
#define CC1101_PKTLEN    0x06
#define CC1101_PKTCTRL1  0x07
#define CC1101_PKTCTRL0  0x08
#define CC1101_ADDR      0x09
#define CC1101_CHANNR    0x0A
#define CC1101_FSCTRL1   0x0B
#define CC1101_FSCTRL0   0x0C
#define CC1101_FREQ2     0x0D
#define CC1101_FREQ1     0x0E
#define CC1101_FREQ0     0x0F
#define CC1101_MDMCFG4   0x10
#define CC1101_MDMCFG3   0x11
#define CC1101_MDMCFG2   0x12
#define CC1101_MDMCFG1   0x13
#define CC1101_MDMCFG0   0x14
#define CC1101_DEVIATN   0x15
#define CC1101_MCSM2     0x16
#define CC1101_MCSM1     0x17
#define CC1101_MCSM0     0x18
#define CC1101_FOCCFG    0x19
#define CC1101_BSCFG     0x1A
#define CC1101_AGCCTRL2  0x1B
#define CC1101_AGCCTRL1  0x1C
#define CC1101_AGCCTRL0  0x1D
#define CC1101_WOREVT1   0x1E
#define CC1101_WOREVT0   0x1F
#define CC1101_WORCTRL   0x20
#define CC1101_FREND1    0x21
#define CC1101_FREND0    0x22
#define CC1101_FSCAL3    0x23
#define CC1101_FSCAL2    0x24
#define CC1101_FSCAL1    0x25
#define CC1101_FSCAL0    0x26
#define CC1101_RCCTRL1   0x27
#define CC1101_RCCTRL0   0x28
#define CC1101_FSTEST    0x29
#define CC1101_PTEST     0x2A
#define CC1101_AGCTEST   0x2B
#define CC1101_TEST2     0x2C
#define CC1101_TEST1     0x2D
#define CC1101_TEST0     0x2E

// CC1101 Strobe commands
#define CC1101_SRES      0x30
#define CC1101_SFSTXON   0x31
#define CC1101_SXOFF     0x32
#define CC1101_SCAL      0x33
#define CC1101_SRX       0x34
#define CC1101_STX       0x35
#define CC1101_SIDLE     0x36
#define CC1101_SWOR      0x38
#define CC1101_SPWD      0x39
#define CC1101_SFRX      0x3A
#define CC1101_SFTX      0x3B
#define CC1101_SNOP      0x3D

// CC1101 Status registers (read with burst bit)
#define CC1101_PARTNUM   0x30
#define CC1101_VERSION   0x31
#define CC1101_FREQEST   0x32
#define CC1101_LQI       0x33
#define CC1101_RSSI      0x34
#define CC1101_MARCSTATE 0x35
#define CC1101_PKTSTATUS 0x38

// CC1101 PA Table
#define CC1101_PATABLE   0x3E

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static float currentFreqMHz = 868.35;
static bool  receivingMode  = false;
static char  serialBuffer[80];
static byte  serialPos      = 0;

// ============================================================================
// CC1101 SPI low-level functions
// ============================================================================

static inline void cc1101_select() {
  digitalWrite(PIN_CSN, LOW);
}

static inline void cc1101_deselect() {
  digitalWrite(PIN_CSN, HIGH);
}

// Wait for MISO to go low (CC1101 ready), with timeout
static void cc1101_waitMiso() {
  uint8_t timeout = 200;
  while (digitalRead(PIN_MISO) && --timeout) {
    delayMicroseconds(1);
  }
}

static void cc1101_strobe(byte strobe) {
  cc1101_select();
  cc1101_waitMiso();
  SPI.transfer(strobe);
  cc1101_deselect();
}

static void cc1101_writeReg(byte addr, byte value) {
  cc1101_select();
  cc1101_waitMiso();
  SPI.transfer(addr);
  SPI.transfer(value);
  cc1101_deselect();
}

static byte cc1101_readReg(byte addr) {
  cc1101_select();
  cc1101_waitMiso();
  SPI.transfer(addr | 0x80);
  byte val = SPI.transfer(0);
  cc1101_deselect();
  return val;
}

static byte cc1101_readStatus(byte addr) {
  cc1101_select();
  cc1101_waitMiso();
  SPI.transfer(addr | 0xC0);   // Read + burst = status register
  byte val = SPI.transfer(0);
  cc1101_deselect();
  return val;
}

static void cc1101_writePaTable(byte *table, byte len) {
  cc1101_select();
  cc1101_waitMiso();
  SPI.transfer(CC1101_PATABLE | 0x40);  // Burst write
  for (byte i = 0; i < len; i++) {
    SPI.transfer(table[i]);
  }
  cc1101_deselect();
}

// ============================================================================
// CC1101 initialization
// ============================================================================

static void cc1101_reset() {
  cc1101_deselect();
  delayMicroseconds(5);
  cc1101_select();
  delayMicroseconds(10);
  cc1101_deselect();
  delayMicroseconds(45);
  cc1101_strobe(CC1101_SRES);
  delay(10);
}

static void cc1101_setFrequency(float mhz) {
  // FREQ = Fcarrier * 2^16 / Fxosc, Fxosc = 26 MHz
  uint32_t freq = (uint32_t)((mhz * 1000000.0 / 26000000.0) * 65536.0 + 0.5);
  cc1101_writeReg(CC1101_FREQ2, (freq >> 16) & 0xFF);
  cc1101_writeReg(CC1101_FREQ1, (freq >> 8)  & 0xFF);
  cc1101_writeReg(CC1101_FREQ0,  freq        & 0xFF);
}

static void cc1101_initOOK() {
  cc1101_reset();

  // GDO2: High impedance (tri-state, not used)
  cc1101_writeReg(CC1101_IOCFG2,  0x29);

  // GDO0: Async serial data (0x0D)
  // TX: MCU drives GDO0 as data input to CC1101 modulator
  // RX: CC1101 outputs demodulated OOK data on GDO0
  cc1101_writeReg(CC1101_IOCFG0,  0x0D);

  // FIFO threshold: default
  cc1101_writeReg(CC1101_FIFOTHR, 0x47);

  // Packet config: async serial mode, no CRC
  // PKTCTRL0 bits 5:4 = 11 -> async serial mode
  // Bit 2 = 0 -> CRC disabled
  // Bits 1:0 = 10 -> infinite packet length
  cc1101_writeReg(CC1101_PKTCTRL0, 0x32);
  cc1101_writeReg(CC1101_PKTCTRL1, 0x00);
  cc1101_writeReg(CC1101_PKTLEN,   0x00);

  // Frequency
  cc1101_setFrequency(currentFreqMHz);

  // IF frequency: ~152 kHz (default)
  cc1101_writeReg(CC1101_FSCTRL1, 0x06);
  cc1101_writeReg(CC1101_FSCTRL0, 0x00);

  // Modem config: OOK, no sync word, no Manchester encoding by CC1101
  // (we do Manchester encoding ourselves in software)
  // MDMCFG4: RX filter BW ~232 kHz, suitable for OOK signals
  cc1101_writeReg(CC1101_MDMCFG4, 0x87);
  cc1101_writeReg(CC1101_MDMCFG3, 0x32);  // Data rate ~2.4 kBaud (not critical in async)
  cc1101_writeReg(CC1101_MDMCFG2, 0x30);  // OOK modulation, no sync word
  cc1101_writeReg(CC1101_MDMCFG1, 0x22);  // 2 preamble bytes (not used)
  cc1101_writeReg(CC1101_MDMCFG0, 0xF8);  // Channel spacing default

  // Deviation: 0 for OOK
  cc1101_writeReg(CC1101_DEVIATN,  0x00);

  // Main radio control state machine
  cc1101_writeReg(CC1101_MCSM2,   0x07);
  cc1101_writeReg(CC1101_MCSM1,   0x30);  // After TX/RX -> IDLE
  cc1101_writeReg(CC1101_MCSM0,   0x18);  // Auto-calibrate on IDLE->TX/RX

  // Frequency offset compensation
  cc1101_writeReg(CC1101_FOCCFG,  0x16);
  cc1101_writeReg(CC1101_BSCFG,   0x6C);

  // AGC control - tuned for OOK
  cc1101_writeReg(CC1101_AGCCTRL2, 0x43);
  cc1101_writeReg(CC1101_AGCCTRL1, 0x49);
  cc1101_writeReg(CC1101_AGCCTRL0, 0x91);

  // Front-end config: PA table index 1 for OOK "1"
  cc1101_writeReg(CC1101_FREND1,  0x56);
  cc1101_writeReg(CC1101_FREND0,  0x11);

  // Frequency synthesizer calibration (SmartRF Studio defaults)
  cc1101_writeReg(CC1101_FSCAL3,  0xE9);
  cc1101_writeReg(CC1101_FSCAL2,  0x2A);
  cc1101_writeReg(CC1101_FSCAL1,  0x00);
  cc1101_writeReg(CC1101_FSCAL0,  0x1F);

  // Test registers (SmartRF Studio OOK defaults)
  cc1101_writeReg(CC1101_TEST2,   0x81);
  cc1101_writeReg(CC1101_TEST1,   0x35);
  cc1101_writeReg(CC1101_TEST0,   0x09);

  // PA table: index 0 = 0x00 (OOK OFF), index 1 = tx_power (from params)
  byte paTable[2] = {0x00, params.tx_power};
  cc1101_writePaTable(paTable, 2);

  // Go to IDLE, flush FIFOs
  cc1101_strobe(CC1101_SIDLE);
  delay(1);
  cc1101_strobe(CC1101_SFRX);
  cc1101_strobe(CC1101_SFTX);
}

// ============================================================================
// TX mode control
// ============================================================================

static void cc1101_startTX() {
  cc1101_strobe(CC1101_SIDLE);
  delayMicroseconds(100);
  pinMode(PIN_GDO0, OUTPUT);
  digitalWrite(PIN_GDO0, LOW);   // RF off initially
  cc1101_strobe(CC1101_STX);
  delayMicroseconds(200);        // Wait for TX PLL to settle
}

static void cc1101_stopTX() {
  digitalWrite(PIN_GDO0, LOW);
  cc1101_strobe(CC1101_SIDLE);
  delayMicroseconds(100);
}

// ============================================================================
// RX mode control
// ============================================================================

static void cc1101_startRX() {
  cc1101_strobe(CC1101_SIDLE);
  delayMicroseconds(100);
  pinMode(PIN_GDO0, INPUT);
  cc1101_strobe(CC1101_SRX);
  delayMicroseconds(200);
}

// ============================================================================
// Manchester encoding and frame transmission
// ============================================================================
// Direct port manipulation for timing accuracy.
// PIN_GDO0 = D3 = PD3 on ATmega328P (nanoCUL: D3=GDO0, D2=GDO2)

#define GDO0_HIGH()  (PORTD |=  (1 << 3))
#define GDO0_LOW()   (PORTD &= ~(1 << 3))

// Send a single Manchester-encoded frame (sync + 64 data bits)
// data[] must be 8 bytes
static void sendManchesterFrame(const byte *data) {
  // Sync pulse: HIGH for sync_us
  GDO0_HIGH();
  delayMicroseconds(params.sync_us);

  // 64 data bits, MSB first, Manchester encoded
  for (byte byteIdx = 0; byteIdx < 8; byteIdx++) {
    byte b = data[byteIdx];
    for (int8_t bitIdx = 7; bitIdx >= 0; bitIdx--) {
      if (b & (1 << bitIdx)) {
        // Bit 1: LONG LOW + SHORT HIGH  (verified: -880 +440)
        GDO0_LOW();
        delayMicroseconds(params.long_us);
        GDO0_HIGH();
        delayMicroseconds(params.short_us);
      } else {
        // Bit 0: SHORT LOW + LONG HIGH  (verified: -440 +880)
        GDO0_LOW();
        delayMicroseconds(params.short_us);
        GDO0_HIGH();
        delayMicroseconds(params.long_us);
      }
    }
  }

  // End with carrier off
  GDO0_LOW();
}

// ============================================================================
// Send KN command - normal mode (6 repeats with gap, preamble toggle)
// ============================================================================
static void sendNormalCommand(byte *rfData) {
  cc1101_startTX();

  // Save original byte 0 (should be 0x0F from F0^FF)
  byte origByte0 = rfData[0];
  // Toggled version: flip bit 7 -> 0x8F
  byte togByte0 = origByte0 ^ 0x80;

  for (byte rep = 0; rep < params.repeats; rep++) {
    // Alternate preamble: 0x0F on even, 0x8F on odd repeats
    if (params.preamble_tgl)
      rfData[0] = (rep & 1) ? togByte0 : origByte0;

    sendManchesterFrame(rfData);

    if (rep < params.repeats - 1) {
      safeDelayUs(getGapUs());
    }
  }

  cc1101_stopTX();

  // Restore original byte 0
  rfData[0] = origByte0;
}

// ============================================================================
// Send KN command - SAVE mode (continuous for ~4 seconds, preamble toggle)
// ============================================================================
static void sendSaveCommand(byte *rfData) {
  cc1101_startTX();

  byte origByte0 = rfData[0];
  byte togByte0 = origByte0 ^ 0x80;
  unsigned long startMs = millis();
  uint16_t rep = 0;  // C1 fix: uint16_t to avoid overflow after 255 frames

  while ((millis() - startMs) < params.save_ms) {
    if (params.preamble_tgl)
      rfData[0] = (rep & 1) ? togByte0 : origByte0;
    rep++;

    sendManchesterFrame(rfData);

    // Minimal gap - just enough for the signal to be recognized as separate frames
    safeDelayUs(params.save_gap_us);
  }

  cc1101_stopTX();
  rfData[0] = origByte0;

  Serial.print(F("SAVE: sent "));
  Serial.print(rep);
  Serial.print(F(" frames in ~"));
  Serial.print(params.save_ms / 1000);
  Serial.println(F("s"));
}

// ============================================================================
// Hex parsing utilities
// ============================================================================

static void printHexByte(byte b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}

// Parse 2 hex characters into a byte. Returns true on success.
static bool parseHexByte(const char *str, byte *out) {
  byte val = 0;
  for (byte i = 0; i < 2; i++) {
    char c = str[i];
    val <<= 4;
    if (c >= '0' && c <= '9')      val |= (c - '0');
    else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
    else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
    else return false;
  }
  *out = val;
  return true;
}

// Parse hex string into byte array. Returns number of bytes parsed.
static byte parseHex(const char *hex, byte *out, byte maxBytes) {
  byte count = 0;
  while (*hex && *(hex + 1) && count < maxBytes) {
    if (!parseHexByte(hex, &out[count])) return count;
    hex += 2;
    count++;
  }
  return count;
}

// Case-insensitive string comparison
static bool strEqualCI(const char *a, const char *b) {
  while (*a && *b) {
    char ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
    char cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
    if (ca != cb) return false;
    a++;
    b++;
  }
  return (*a == '\0' && *b == '\0');
}

// ============================================================================
// KN address to RF data conversion
// ============================================================================
// KN address: 10 hex chars (5 bytes), e.g. F020AABB20
// Full KN frame: {addr[0..4]} {cmd} AE AE  (8 bytes)
// RF data: XOR each byte with 0xFF
//
// Example: F020AABB20 + cmd 80
//   KN frame: F0 20 AA BB 20 80 AE AE
//   RF data:  0F DF 55 44 DF 7F 51 51

static bool buildRfData(const char *addrStr, byte knCmd, byte rfData[8]) {
  if (strlen(addrStr) != 10) return false;

  byte knAddr[5];
  for (byte i = 0; i < 5; i++) {
    if (!parseHexByte(addrStr + i * 2, &knAddr[i])) return false;
  }

  // Build KN frame
  byte knFrame[8];
  knFrame[0] = knAddr[0];   // F0
  knFrame[1] = knAddr[1];   // 20
  knFrame[2] = knAddr[2];   // remote addr high
  knFrame[3] = knAddr[3];   // remote addr low
  knFrame[4] = knAddr[4];   // channel
  knFrame[5] = knCmd;       // command
  knFrame[6] = 0xAE;        // suffix
  knFrame[7] = 0xAE;        // suffix

  // XOR with 0xFF to get RF data
  for (byte i = 0; i < 8; i++) {
    rfData[i] = knFrame[i] ^ 0xFF;
  }

  return true;
}

// ============================================================================
// Resolve named command to KN command byte
// ============================================================================
// Returns true on success, sets *knCmd

static bool resolveCommand(const char *cmdStr, byte *knCmd) {
  if (strEqualCI(cmdStr, "UP"))   { *knCmd = 0x80; return true; }
  if (strEqualCI(cmdStr, "DOWN")) { *knCmd = 0x20; return true; }
  if (strEqualCI(cmdStr, "STOP")) { *knCmd = 0x40; return true; }
  if (strEqualCI(cmdStr, "POS"))  { *knCmd = 0x4C; return true; }
  if (strEqualCI(cmdStr, "SAVE")) { *knCmd = 0x0C; return true; }

  // Try as raw hex byte (exactly 2 hex chars)
  if (strlen(cmdStr) == 2) {
    return parseHexByte(cmdStr, knCmd);
  }

  return false;
}

// ============================================================================
// Receive mode - Manchester decode via polling
// ============================================================================
// We poll GDO0 and measure pulse durations to detect sync + decode bits.
// An interrupt-based approach would be more robust, but polling is simpler
// and sufficient for debugging/sniffing.

#define TIMING_TOLERANCE 180
#define SYNC_MIN_US      2000
#define SYNC_MAX_US      3200

static bool isShort(unsigned long us) {
  // C2 fix: prevent unsigned underflow when params < TIMING_TOLERANCE
  uint16_t low = (params.short_us > TIMING_TOLERANCE) ? (params.short_us - TIMING_TOLERANCE) : 0;
  return (us > low) && (us < (params.short_us + TIMING_TOLERANCE));
}

static bool isLong(unsigned long us) {
  // C2 fix: prevent unsigned underflow when params < TIMING_TOLERANCE
  uint16_t low = (params.long_us > TIMING_TOLERANCE) ? (params.long_us - TIMING_TOLERANCE) : 0;
  return (us > low) && (us < (params.long_us + TIMING_TOLERANCE));
}

static bool isSync(unsigned long us) {
  return (us >= SYNC_MIN_US) && (us <= SYNC_MAX_US);
}

// RX state machine
#define RX_WAIT_SYNC    0
#define RX_DECODE_BITS  1

static byte rxData[8];
static byte rxBitCount;

static void receiveLoop() {
  static byte state = RX_WAIT_SYNC;
  static unsigned long lastEdge = 0;
  static byte lastLevel = LOW;
  static byte pendingHalf = 0;  // 0=none, 1=short-low, 3=long-low

  byte level = digitalRead(PIN_GDO0);

  if (level != lastLevel) {
    unsigned long now = micros();
    unsigned long duration = now - lastEdge;
    lastEdge = now;

    switch (state) {

      case RX_WAIT_SYNC:
        // Sync = HIGH for ~2600us (falling edge after sync)
        if (lastLevel == HIGH && isSync(duration)) {
          state = RX_DECODE_BITS;
          memset(rxData, 0, 8);
          rxBitCount = 0;
          pendingHalf = 0;
        }
        break;

      case RX_DECODE_BITS:
        if (lastLevel == LOW) {
          // Rising edge - LOW period ended
          if (isShort(duration))      pendingHalf = 1;  // short LOW
          else if (isLong(duration))  pendingHalf = 3;  // long LOW
          else                        state = RX_WAIT_SYNC;
        } else {
          // Falling edge - HIGH period ended
          if (pendingHalf == 1 && isLong(duration)) {
            // Short LOW + Long HIGH = Bit 0
            rxBitCount++;
            pendingHalf = 0;
          } else if (pendingHalf == 3 && isShort(duration)) {
            // Long LOW + Short HIGH = Bit 1
            byte byteIdx = rxBitCount / 8;
            byte bitIdx  = 7 - (rxBitCount % 8);
            if (byteIdx < 8) rxData[byteIdx] |= (1 << bitIdx);
            rxBitCount++;
            pendingHalf = 0;
          } else if (isShort(duration)) {
            // Short HIGH - could be a transition, ignore and re-arm
            pendingHalf = 2;
          } else {
            state = RX_WAIT_SYNC;
          }
        }

        // Complete frame?
        if (rxBitCount >= 64) {
          // Print RF hex
          Serial.print(F("RX RF: "));
          for (byte i = 0; i < 8; i++) printHexByte(rxData[i]);

          // Print KN equivalent (XOR FF) with address separated from command
          // Format: KN: F020CCDD20 80 AEAE  (address space command space suffix)
          Serial.print(F(" KN: "));
          for (byte i = 0; i < 5; i++) printHexByte(rxData[i] ^ 0xFF);  // Address (5 bytes)
          Serial.print(' ');
          printHexByte(rxData[5] ^ 0xFF);  // Command (1 byte)
          Serial.print(' ');
          for (byte i = 6; i < 8; i++) printHexByte(rxData[i] ^ 0xFF);  // Suffix (2 bytes)

          // Decode command
          byte rfCmd = rxData[5];
          Serial.print(F(" ["));
          switch (rfCmd) {
            case 0x7F: Serial.print(F("UP"));   break;
            case 0xDF: Serial.print(F("DOWN")); break;
            case 0xBF: Serial.print(F("STOP")); break;
            case 0xB3: Serial.print(F("POS"));  break;
            case 0xF3: Serial.print(F("SAVE")); break;
            default:   printHexByte(rfCmd);      break;
          }
          Serial.print(']');

          // RSSI
          int8_t rssiRaw = (int8_t)cc1101_readStatus(CC1101_RSSI);
          int16_t rssiDbm;
          if (rssiRaw >= 128)
            rssiDbm = ((int16_t)rssiRaw - 256) / 2 - 74;
          else
            rssiDbm = rssiRaw / 2 - 74;
          Serial.print(F(" RSSI="));
          Serial.print(rssiDbm);
          Serial.print(F("dBm"));

          Serial.println();
          state = RX_WAIT_SYNC;
        }
        break;
    }

    lastLevel = level;
  }
}

// ============================================================================
// Serial command processing
// ============================================================================

static void processCommand(char *cmd) {
  // Trim leading whitespace
  while (*cmd == ' ' || *cmd == '\t') cmd++;

  // Trim trailing whitespace/newline
  char *end = cmd + strlen(cmd) - 1;
  while (end > cmd && (*end == ' ' || *end == '\r' || *end == '\n')) {
    *end-- = '\0';
  }

  if (strlen(cmd) == 0) return;

  // Extract first word (command keyword)
  char keyword[8];
  byte ki = 0;
  const char *p = cmd;
  while (*p && *p != ' ' && ki < 7) {
    keyword[ki] = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p;
    ki++;
    p++;
  }
  keyword[ki] = '\0';

  // ----- VER -----
  if (strcmp(keyword, "VER") == 0) {
    Serial.println(F(FW_VERSION));
    Serial.print(F("Freq: "));
    Serial.print(currentFreqMHz, 2);
    Serial.println(F(" MHz"));
    byte partnum = cc1101_readStatus(CC1101_PARTNUM);
    byte ver = cc1101_readStatus(CC1101_VERSION);
    Serial.print(F("CC1101 Part=0x"));
    printHexByte(partnum);
    Serial.print(F(" Ver=0x"));
    printHexByte(ver);
    Serial.println();
    Serial.print(F("MARCSTATE=0x"));
    printHexByte(cc1101_readStatus(CC1101_MARCSTATE) & 0x1F);
    Serial.println();
    Serial.print(F("Params: sync="));   Serial.print(params.sync_us);
    Serial.print(F(" short="));         Serial.print(params.short_us);
    Serial.print(F(" long="));          Serial.print(params.long_us);
    Serial.print(F(" gap="));           Serial.print(getGapUs());
    Serial.print(F(" rpt="));           Serial.print(params.repeats);
    Serial.print(F(" save_ms="));       Serial.print(params.save_ms);
    Serial.print(F(" save_gap="));      Serial.print(params.save_gap_us);
    Serial.print(F(" txpwr=0x"));       printHexByte(params.tx_power);
    Serial.print(F(" preamble="));      Serial.println(params.preamble_tgl);
    return;
  }

  // ----- FREQ <mhz> -----
  if (strcmp(keyword, "FREQ") == 0) {
    while (*p == ' ') p++;
    float freq = atof(p);
    if (freq < 300.0 || freq > 928.0) {
      Serial.println(F("ERR: Frequency must be 300-928 MHz"));
      return;
    }
    currentFreqMHz = freq;
    bool wasRx = receivingMode;
    if (receivingMode) {
      receivingMode = false;
      cc1101_strobe(CC1101_SIDLE);
      delay(1);
    }
    cc1101_setFrequency(currentFreqMHz);
    cc1101_strobe(CC1101_SCAL);
    delay(2);
    Serial.print(F("Freq: "));
    Serial.print(currentFreqMHz, 2);
    Serial.println(F(" MHz"));
    if (wasRx) {
      cc1101_startRX();
      receivingMode = true;
      Serial.println(F("RX resumed"));
    }
    return;
  }

  // ----- RECV -----
  if (strcmp(keyword, "RECV") == 0) {
    receivingMode = true;
    cc1101_startRX();
    Serial.print(F("RX: listening on "));
    Serial.print(currentFreqMHz, 2);
    Serial.println(F(" MHz (send any command to stop)"));
    return;
  }

  // ----- SEND -----
  if (strcmp(keyword, "SEND") == 0) {
    // Stop RX if active
    if (receivingMode) {
      receivingMode = false;
      cc1101_strobe(CC1101_SIDLE);
      delay(1);
    }

    while (*p == ' ') p++;

    // First argument: either a 10-char KN address or 16-char raw RF hex
    char arg1[20];
    byte a1len = 0;
    while (*p && *p != ' ' && a1len < 19) {
      arg1[a1len++] = *p++;
    }
    arg1[a1len] = '\0';

    // --- Mode A: SEND <kn_addr> <command> ---
    // KN address is 10 hex chars (5 bytes)
    if (a1len == 10) {
      while (*p == ' ') p++;

      // Parse command keyword or hex byte
      char cmdStr[10];
      byte cslen = 0;
      while (*p && *p != ' ' && *p != '\r' && *p != '\n' && cslen < 9) {
        cmdStr[cslen++] = *p++;
      }
      cmdStr[cslen] = '\0';

      if (cslen == 0) {
        Serial.println(F("ERR: Missing command. Use UP/DOWN/STOP/POS/SAVE or hex byte"));
        return;
      }

      byte knCmd;
      if (!resolveCommand(cmdStr, &knCmd)) {
        Serial.println(F("ERR: Unknown command. Use UP/DOWN/STOP/POS/SAVE or 2-char hex"));
        return;
      }

      byte rfData[8];
      if (!buildRfData(arg1, knCmd, rfData)) {
        Serial.println(F("ERR: Invalid KN address (need 10 hex chars, e.g. F020AABB20)"));
        return;
      }

      byte rfCmd = knCmd ^ 0xFF;
      bool isSave = (knCmd == 0x0C);

      // Print what we are sending
      Serial.print(F("TX: addr="));
      Serial.print(arg1);
      Serial.print(F(" cmd="));
      // Print uppercase command name if known
      if (strEqualCI(cmdStr, "UP"))        Serial.print(F("UP"));
      else if (strEqualCI(cmdStr, "DOWN")) Serial.print(F("DOWN"));
      else if (strEqualCI(cmdStr, "STOP")) Serial.print(F("STOP"));
      else if (strEqualCI(cmdStr, "POS"))  Serial.print(F("POS"));
      else if (strEqualCI(cmdStr, "SAVE")) Serial.print(F("SAVE"));
      else                                 Serial.print(cmdStr);
      Serial.print(F(" (KN=0x"));
      printHexByte(knCmd);
      Serial.print(F(" RF=0x"));
      printHexByte(rfCmd);
      Serial.print(F(") data="));
      for (byte i = 0; i < 8; i++) printHexByte(rfData[i]);
      Serial.println();

      if (isSave) {
        Serial.println(F("SAVE: continuous send for ~4s..."));
        sendSaveCommand(rfData);
      } else {
        sendNormalCommand(rfData);
        Serial.print(F("TX: "));
        Serial.print(params.repeats);
        Serial.println(F(" repeats"));
      }
      Serial.println(F("OK"));
      return;
    }

    // --- Mode B: SEND <16-hex-chars> [count] ---
    // Raw 64-bit RF data as 16 hex chars (legacy/advanced mode)
    if (a1len == 16) {
      byte rfData[8];
      byte len = parseHex(arg1, rfData, 8);
      if (len != 8) {
        Serial.println(F("ERR: Invalid hex data"));
        return;
      }

      // Optional repeat count
      byte repeats = params.repeats;
      while (*p == ' ') p++;
      if (*p) {
        int r = atoi(p);
        if (r > 0 && r <= 255) repeats = (byte)r;
      }

      Serial.print(F("TX RAW: "));
      for (byte i = 0; i < 8; i++) printHexByte(rfData[i]);
      Serial.print(F(" x"));
      Serial.println(repeats);

      // For raw mode with many repeats, check if this might be SAVE
      // (user can force continuous mode by specifying repeats > 20)
      if (repeats > 20) {
        // Continuous mode: interpret repeats as duration in number of frames
        cc1101_startTX();
        byte origByte0 = rfData[0];
        byte togByte0 = origByte0 ^ 0x80;
        for (byte rep = 0; rep < repeats; rep++) {
          if (params.preamble_tgl)
            rfData[0] = (rep & 1) ? togByte0 : origByte0;
          sendManchesterFrame(rfData);
          safeDelayUs(params.save_gap_us);
        }
        cc1101_stopTX();
        rfData[0] = origByte0;
      } else {
        // Normal mode with preamble toggle
        cc1101_startTX();
        byte origByte0 = rfData[0];
        byte togByte0 = origByte0 ^ 0x80;
        for (byte rep = 0; rep < repeats; rep++) {
          if (params.preamble_tgl)
            rfData[0] = (rep & 1) ? togByte0 : origByte0;
          sendManchesterFrame(rfData);
          if (rep < repeats - 1) {
            safeDelayUs(getGapUs());
          }
        }
        cc1101_stopTX();
        rfData[0] = origByte0;
      }

      Serial.println(F("OK"));
      return;
    }

    // Neither 10 nor 16 chars
    Serial.println(F("ERR: Expected 10-char KN address + command, or 16-char raw RF hex"));
    Serial.println(F("  SEND F020AABB20 UP       (KN address + named command)"));
    Serial.println(F("  SEND F020AABB20 80       (KN address + hex command)"));
    Serial.println(F("  SEND 0FDF5544DF7F5151    (raw RF hex, 16 chars)"));
    return;
  }

  // ----- GET -----
  if (strcmp(keyword, "GET") == 0) {
    Serial.println(F("=== Current Parameters ==="));
    Serial.print(F("  SHORT    = ")); Serial.print(params.short_us);    Serial.println(F(" us"));
    Serial.print(F("  LONG     = ")); Serial.print(params.long_us);     Serial.println(F(" us"));
    Serial.print(F("  SYNC     = ")); Serial.print(params.sync_us);     Serial.println(F(" us"));
    Serial.print(F("  GAP      = ")); Serial.print(getGapUs());         Serial.println(F(" us"));
    Serial.print(F("  REPEATS  = ")); Serial.println(params.repeats);
    Serial.print(F("  SAVETIME = ")); Serial.print(params.save_ms);     Serial.println(F(" ms"));
    Serial.print(F("  SAVEGAP  = ")); Serial.print(params.save_gap_us); Serial.println(F(" us"));
    Serial.print(F("  TXPOWER  = 0x")); printHexByte(params.tx_power);  Serial.println();
    Serial.print(F("  PREAMBLE = ")); Serial.println(params.preamble_tgl);
    return;
  }

  // ----- SET -----
  if (strcmp(keyword, "SET") == 0) {
    while (*p == ' ') p++;

    // Extract sub-keyword
    char subkey[12];
    byte si = 0;
    while (*p && *p != ' ' && si < 11) {
      subkey[si] = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p;
      si++;
      p++;
    }
    subkey[si] = '\0';

    // Parse value (skip spaces)
    while (*p == ' ') p++;
    unsigned long val = strtoul(p, NULL, 0);

    if (strcmp(subkey, "SHORT") == 0) {
      if (val < 100 || val > 5000) { Serial.println(F("ERR: SHORT range 100-5000")); return; }
      params.short_us = val;
    } else if (strcmp(subkey, "LONG") == 0) {
      if (val < 100 || val > 5000) { Serial.println(F("ERR: LONG range 100-5000")); return; }
      params.long_us = val;
    } else if (strcmp(subkey, "SYNC") == 0) {
      if (val < 500 || val > 10000) { Serial.println(F("ERR: SYNC range 500-10000")); return; }
      params.sync_us = val;
    } else if (strcmp(subkey, "GAP") == 0) {
      if (val > 65530) { Serial.println(F("ERR: GAP max 65530")); return; }
      setGapUs(val);
    } else if (strcmp(subkey, "REPEATS") == 0) {
      if (val < 1 || val > 255) { Serial.println(F("ERR: REPEATS range 1-255")); return; }
      params.repeats = val;
    } else if (strcmp(subkey, "SAVETIME") == 0) {
      if (val < 500 || val > 30000) { Serial.println(F("ERR: SAVETIME range 500-30000")); return; }
      params.save_ms = val;
    } else if (strcmp(subkey, "SAVEGAP") == 0) {
      if (val > 65535) { Serial.println(F("ERR: SAVEGAP max 65535")); return; }
      params.save_gap_us = val;
    } else if (strcmp(subkey, "TXPOWER") == 0) {
      // Parse as hex
      byte txVal;
      if (strlen(p) >= 2 && parseHexByte(p, &txVal)) {
        params.tx_power = txVal;
        // Update PA table on CC1101 immediately
        byte paTable[2] = {0x00, params.tx_power};
        cc1101_writePaTable(paTable, 2);
      } else {
        Serial.println(F("ERR: TXPOWER needs 2-char hex (e.g. C0, C6, 60)"));
        return;
      }
    } else if (strcmp(subkey, "PREAMBLE") == 0) {
      if (val > 1) { Serial.println(F("ERR: PREAMBLE must be 0 or 1")); return; }
      params.preamble_tgl = val;
    } else if (strcmp(subkey, "DEFAULTS") == 0) {
      params.sync_us      = DEF_SYNC_US;
      params.short_us     = DEF_SHORT_US;
      params.long_us      = DEF_LONG_US;
      params.gap_us       = DEF_GAP_US / 10;
      params.repeats      = DEF_REPEATS;
      params.save_ms      = DEF_SAVE_MS;
      params.save_gap_us  = DEF_SAVE_GAP_US;
      params.tx_power     = DEF_TX_POWER;
      params.preamble_tgl = DEF_PREAMBLE_TGL;
      // Update PA table
      byte paTable[2] = {0x00, params.tx_power};
      cc1101_writePaTable(paTable, 2);
      saveParams();
      Serial.println(F("OK: All parameters reset to factory defaults"));
      return;
    } else {
      Serial.println(F("ERR: Unknown SET parameter"));
      Serial.println(F("  SET SHORT|LONG|SYNC|GAP|REPEATS|SAVETIME|SAVEGAP|TXPOWER|PREAMBLE <val>"));
      Serial.println(F("  SET DEFAULTS"));
      return;
    }

    saveParams();
    Serial.print(F("OK: "));
    Serial.print(subkey);
    Serial.print(F(" = "));
    if (strcmp(subkey, "TXPOWER") == 0) {
      Serial.print(F("0x")); printHexByte(params.tx_power); Serial.println();
    } else if (strcmp(subkey, "GAP") == 0) {
      Serial.println(getGapUs());
    } else {
      Serial.println(val);
    }
    return;
  }

  // ----- HELP -----
  if (strcmp(keyword, "HELP") == 0) {
    Serial.println(F("=== nanoCUL-KN Commands ==="));
    Serial.println(F("SEND <addr> <cmd>  - Send KN command (UP/DOWN/STOP/POS/SAVE/hex)"));
    Serial.println(F("SEND <16hex> [n]   - Send raw RF data, optional repeat count"));
    Serial.println(F("RECV               - Enter receive mode"));
    Serial.println(F("FREQ <MHz>         - Set frequency (e.g. 868.35)"));
    Serial.println(F("VER                - Show version and status"));
    Serial.println(F("GET                - Show all current parameters"));
    Serial.println(F("SET SHORT <us>     - Short pulse (def 440)"));
    Serial.println(F("SET LONG <us>      - Long pulse (def 880)"));
    Serial.println(F("SET SYNC <us>      - Sync pulse (def 2600)"));
    Serial.println(F("SET GAP <us>       - Inter-repeat gap (def 15000)"));
    Serial.println(F("SET REPEATS <n>    - Repeat count (def 6)"));
    Serial.println(F("SET SAVETIME <ms>  - SAVE duration (def 4000)"));
    Serial.println(F("SET SAVEGAP <us>   - SAVE inter-frame gap (def 500)"));
    Serial.println(F("SET TXPOWER <hex>  - PA table value (def C0)"));
    Serial.println(F("SET PREAMBLE <0|1> - Preamble toggle (def 1)"));
    Serial.println(F("SET DEFAULTS       - Reset to factory defaults"));
    Serial.println(F("HELP               - This help"));
    return;
  }

  // ----- Unknown command -----
  Serial.println(F("ERR: Unknown command"));
  Serial.println(F("Commands: SEND | RECV | FREQ | VER | GET | SET | HELP"));
  Serial.println(F("  Type HELP for full command list"));
}

// ============================================================================
// Arduino setup
// ============================================================================
void setup() {
  Serial.begin(57600);
  while (!Serial) { ; }

  // Initialize SPI
  pinMode(PIN_CSN, OUTPUT);
  digitalWrite(PIN_CSN, HIGH);
  SPI.begin();
  SPI.setClockDivider(SPI_CLOCK_DIV4);
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);

  // Load saved parameters from EEPROM (before CC1101 init uses tx_power)
  loadParams();

  // Initialize CC1101
  cc1101_initOOK();

  // Verify CC1101
  byte version = cc1101_readStatus(CC1101_VERSION);
  byte partnum = cc1101_readStatus(CC1101_PARTNUM);

  Serial.println();
  Serial.println(F(FW_VERSION));
  Serial.print(F("CC1101 Part=0x"));
  printHexByte(partnum);
  Serial.print(F(" Ver=0x"));
  printHexByte(version);
  Serial.println();

  if (version == 0x00 || version == 0xFF) {
    Serial.println(F("WARNING: CC1101 not detected! Check SPI wiring:"));
    Serial.println(F("  SCK=D13, MISO=D12, MOSI=D11, CSN=D10, GDO0=D2"));
  } else {
    Serial.print(F("CC1101 OK. Freq: "));
    Serial.print(currentFreqMHz, 2);
    Serial.println(F(" MHz"));
  }

  Serial.println(F("Commands: SEND | RECV | FREQ | VER | GET | SET | HELP"));
  Serial.println();
}

// ============================================================================
// Arduino main loop
// ============================================================================
void loop() {
  // Process serial input
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialPos > 0) {
        serialBuffer[serialPos] = '\0';
        processCommand(serialBuffer);
        serialPos = 0;
      }
    } else if (serialPos < sizeof(serialBuffer) - 1) {
      serialBuffer[serialPos++] = c;
    }
  }

  // Run receive decoder if in RX mode
  if (receivingMode) {
    receiveLoop();
  }
}
