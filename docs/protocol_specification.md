# Kaiser Nienhaus / Dooya KN Funk-Protokoll Spezifikation

## Version 1.0 -- Stand: April 2026
## Reverse-Engineered mit Flipper Zero und nanoCUL SIGNALduino

---

## 1. Übersicht

Die Kaiser Nienhaus Furohre Funk-Rohrmotoren (868 MHz) nutzen ein OOK-moduliertes
Funk-Protokoll basierend auf dem Dooya-Standard. In einigen Gateways wird es als
Typ "KN" (Kaiser Nienhaus) bezeichnet.

Das Protokoll ist **unidirektional** -- Befehle werden vom Sender zum Motor übertragen.
Es gibt **keine Bestätigung** vom Motor auf diesem Protokoll.

Die Motoren unterstützen zusätzlich das bidirektionale DY2-Protokoll (Dooya 2)
mit FSK-Modulation und Rolling-Code-Verschlüsselung. DY2 wird hier NICHT beschrieben,
da es proprietär und verschlüsselt ist.

---

## 2. Physikalische Schicht (RF Layer)

### 2.1 Frequenz

| Parameter | Wert |
|-----------|------|
| Trägerfrequenz | **868.35 MHz (Standard)
| Alternative | **868.15 MHz** (Original-Fernbedienung) |
| ISM-Band | SRD 868 MHz (Europa) |
| Bandbreite | ~200 kHz |

Die Motoren akzeptieren Befehle auf **beiden** Frequenzen.
Der SAVE-Befehl funktioniert ebenfalls auf beiden Frequenzen.

### 2.2 Modulation

| Parameter | Wert |
|-----------|------|
| Modulation | **OOK** (On-Off Keying) / ASK |
| Encoding | **Manchester** |
| Bitrate | ~1136 bps (1 / 880 us) |
| Sendeleistung | ~10 dBm (typisch) |

### 2.3 Puls-Timing

```
Puls-Typ         Dauer        Beschreibung
─────────────────────────────────────────────
SHORT             440 us      Halbe Bitzeit
LONG              880 us      Volle Bitzeit
SYNC            2.600 us      Synchronisations-Puls
INTER-GAP      15.000 us      Pause zwischen Wiederholungen
```

### 2.4 Manchester-Kodierung

Jedes Datenbit wird als Puls-Paar (LOW + HIGH) kodiert:

```
Bit 0:  ▁▁▁▁ ████████      440 us LOW + 880 us HIGH
        <-S-> <--LONG-->

Bit 1:  ▁▁▁▁▁▁▁▁ ████      880 us LOW + 440 us HIGH
        <--LONG--> <-S->
```

Die Gesamtdauer pro Bit beträgt immer **1.320 us** (440 + 880).

---

## 3. Rahmenstruktur (Frame Format)

### 3.1 Einzelner Frame

```
┌────────┬────────────────────────────────────────────┐
│  SYNC  │              64 Bit Daten                   │
│ 2600us │  Manchester-kodiert = 128 Pulse (64 Paare) │
│  HIGH  │                                             │
└────────┴────────────────────────────────────────────┘
│        │                                             │
│ 2.6 ms │              ~85 ms                         │
│        │                                             │
```

### 3.2 Übertragung (Wiederholungen)

Ein Befehl wird **6-mal** wiederholt mit **15 ms** Pause dazwischen:

```
┌──────┐         ┌──────┐         ┌──────┐
│Frame │  15 ms  │Frame │  15 ms  │Frame │  ...  (6x)
│  1   │─────────│  2   │─────────│  3   │
└──────┘         └──────┘         └──────┘
```

Gesamtdauer einer Übertragung: ~590 ms (6 x 87 ms + 5 x 15 ms)

### 3.3 Preamble-Toggle

Das erste Bit des Frames **alterniert** zwischen den Wiederholungen:

```
Frame 1: Preamble = 0x0F... (Bit 0 = 0)
Frame 2: Preamble = 0x8F... (Bit 0 = 1)
Frame 3: Preamble = 0x0F... (Bit 0 = 0)
...
```

Dies dient vermutlich der Empfänger-Synchronisation.

---

## 4. Datenformat (64 Bit)

### 4.1 Feld-Struktur

```
Byte:   0    1    2    3    4    5    6    7
Hex:   [PP] [PP] [AA] [AA] [SS] [CC] [XX] [XX]
       └─Preamble─┘ └─Adresse─┘  │    │  └─Suffix──┘
                                  │    └── Befehl
                                  └─────── Separator
```

| Feld | Bits | Länge | Wert | Beschreibung |
|------|------|-------|------|-------------|
| **Preamble** | 0-15 | 2 Bytes | `0FDF` / `8FDF` | Fix, Bit 0 togglet bei Wiederholungen |
| **Adresse** | 16-31 | 2 Bytes | variabel | Motor-/Fernbedienungs-Adresse |
| **Separator** | 32-39 | 1 Byte | `DF` | Fix |
| **Befehl** | 40-47 | 1 Byte | siehe Abschnitt 5 | Steuerungsbefehl |
| **Suffix** | 48-63 | 2 Bytes | `5151` | Fix |

### 4.2 KN-Adressformat

Das KN-Adressformat verwendet ein **10-stelliges Hex-Adressformat**:

```
F020{REMOTE}{CHANNEL}{CMD}AEAE
```

Die RF-Daten werden durch **bitweise Invertierung** (XOR 0xFF) erzeugt:

```
KN Format:  F0  20  AA  BB  20  80  AE  AE
XOR 0xFF:    0F  DF  55  44  DF  7F  51  51
             ──  ──  ──  ──  ──  ──  ──  ──
RF-Daten:    [Preamble] [Adresse] [S] [Cmd] [Suffix]
```

**Formel:** `RF_Byte[i] = KN_Byte[i] XOR 0xFF`

### 4.3 Adress-Mapping

Die RF-Adresse (2 Bytes) ergibt sich aus der KN-Adresse:

```
KN:    F020 [REMOTE_HI][REMOTE_LO] [CHANNEL] ...
RF-Adresse: [REMOTE_HI XOR FF] [REMOTE_LO XOR FF]
```

**Formel:** `RF_Adresse = KN_Bytes[2:4] XOR 0xFFFF`

Beispiele:

| Beschreibung | KN-Adresse | Remote | XOR | RF-Adresse |
|--------------|-------------------|--------|-----|-----------|
| Motor A | F020**AABB**20 | AABB | FFFF | **5544** |
| Motor B | F020**CCDD**20 | CCDD | FFFF | **3322** |
| Motor C (Kanal 5) | F020**EEFF**80 | EEFF | FFFF | **1100** |
| Motor D (Kanal 2) | F020**1122**10 | 1122 | FFFF | **EEDD** |

### 4.4 Kanalzuordnung (Channel)

Das 5. Byte der KN-Adresse ist der **Kanal** der Fernbedienung.
Mehrere Motoren können auf der gleichen Fernbedienung (Remote) liegen:

| KN-Byte 5 | RF-Byte (XOR) | Beschreibung |
|-----------------|---------------|-------------|
| 08 | F7 | Kanal 1 (z.B. Gruppensteuerung) |
| 10 | EF | Kanal 2 |
| 20 | DF | Kanal 3 (häufigster Einzelkanal) |
| 40 | BF | Kanal 4 |
| 80 | 7F | Kanal 5 |

Eine 5-Kanal-Fernbedienung hat die Kanäle 08, 10, 20, 40, 80 für 5 verschiedene Motoren.

---

## 5. Befehlscodes

### 5.1 Befehlstabelle

| Befehl | KN-Code | RF-Code (XOR) | Beschreibung |
|--------|----------------|---------------|-------------|
| **RAUF** | `80` | `7F` | Motor fährt hoch bis Endlage oder STOP |
| **RUNTER** | `20` | `DF` | Motor fährt runter bis Endlage oder STOP |
| **STOP** | `40` | `BF` | Motor stoppt sofort |
| **POSITION** | `4C` | `B3` | Motor fährt auf gespeicherte Lieblingsposition |
| **SAVE** | `0C` | `F3` | Aktuelle Position als Lieblingsposition speichern |

### 5.2 Spezialverhalten

**STOP:**
- Sofortiges Anhalten des Motors
- Wenn per Fernbedienung 6-8 Sekunden lang gedrückt: löst POSITION aus
  (Motor interpretiert langen STOP intern als Position-Befehl)

**POSITION (B3):**
- Fährt auf die zuvor gespeicherte Zwischenstellung
- Wenn keine Position gespeichert: keine Reaktion
- Der KN-Code fuer POSITION ist `4C` gesendet

**SAVE (F3):**
- **Spezielles Sende-Verhalten erforderlich!**
- Muss **durchgehend ohne Pause** gesendet werden (wie langer Tastendruck)
- Dauer: mindestens 3-5 Sekunden kontinuierliches Senden
- Motor bestätigt durch **kurzes Rucken** in beide Richtungen
- Einige Gateways koennen SAVE nicht ausfuehren (zu lange Pausen zwischen Sends)
- Funktioniert über: Flipper Zero ("hold to repeat"), nanoCUL (custom Firmware), Fernbedienung
- Hinweis: Einige Gateways senden den falschen Code A0 (RF 5F) fuer SAVE.
  Der korrekte Code `0C` (RF `F3`) wurde durch Reverse-Engineering der Fernbedienung ermittelt

---

## 6. Vollständige Beispiele

### 6.1 Motor A RAUF (UP)

```
KN API Beispiel (10-stellige Adresse + Befehl + Suffix):

KN Bytes:  F0  20  AA  BB  20  80  AE  AE
XOR 0xFF:       0F  DF  55  44  DF  7F  51  51

RF Hex:         0FDF5544DF7F5151
RF Binär:       0000 1111 1101 1111 0101 0101 0100 0100
                1101 1111 0111 1111 0101 0001 0101 0001

Manchester-Pulse (S=440us, L=880us, Sync=2600us):
  [SYNC +2600]
  [-S +L] [-S +L] [-S +L] [-S +L]   (0000 = Preamble Hi)
  [-L +S] [-L +S] [-L +S] [-L +S]   (1111)
  [-L +S] [-L +S] [-S +L] [-L +S]   (1101)
  [-L +S] [-L +S] [-L +S] [-L +S]   (1111)
  ... (64 Bit-Paare insgesamt)
  [GAP -15000]
  (5x wiederholen, Preamble alterniert 0F/8F)
```

### 6.2 Motor D STOP

```
KN:     F020112210 40 AEAE
RF:          0FDF EEDD EF BF 5151

Bedeutung:
  0FDF     = Preamble
  EEDD     = Remote-Adresse (1122 XOR FFFF)
  EF       = Kanal (10 XOR FF)
  BF       = STOP (40 XOR FF)
  5151     = Suffix
```

### 6.3 Motor C POSITION

```
KN:     F020EEFF80 4C AEAE
RF:          0FDF 1100 7F B3 5151

Bedeutung:
  0FDF     = Preamble
  1100     = Remote-Adresse (EEFF XOR FFFF)
  7F       = Kanal (80 XOR FF)
  B3       = POSITION (4C XOR FF)
  5151     = Suffix
```

---

## 7. CC1101 Transceiver Konfiguration

Zum Senden/Empfangen mit einem CC1101-basierten Modul (nanoCUL, ESP32+CC1101):

### 7.1 Register für OOK TX

| Register | Adresse | Wert | Beschreibung |
|----------|---------|------|-------------|
| FREQ2 | 0x0D | 0x21 | Frequenz High-Byte |
| FREQ1 | 0x0E | 0x65 | Frequenz Mid-Byte |
| FREQ0 | 0x0F | 0xE8 | Frequenz Low-Byte (= 868.35 MHz) |
| MDMCFG2 | 0x12 | 0x30 | ASK/OOK Modulation, keine Sync-Word-Erkennung |
| PKTCTRL0 | 0x08 | 0x32 | Async serial mode, infinite packet length |
| IOCFG0 | 0x02 | 0x0D | GDO0 = async serial data output |
| FREND0 | 0x22 | 0x11 | PA table index 1 für OOK HIGH |
| PA_TABLE | - | {0x00, 0xC0} | OOK OFF/ON bei ~10 dBm |

### 7.2 Sende-Ablauf

1. CC1101 in TX-Modus versetzen (Strobe STX = 0x35)
2. GDO0 als OUTPUT konfigurieren
3. Sync-Puls senden: GDO0 = HIGH für 2600 us
4. Für jedes Bit der 64-Bit Daten:
   - Bit 0: GDO0 = LOW (440 us), GDO0 = HIGH (880 us)
   - Bit 1: GDO0 = LOW (880 us), GDO0 = HIGH (440 us)
5. GDO0 = LOW (Signal-Ende)
6. 15.000 us warten
7. Schritte 3-6 für 6 Wiederholungen
8. CC1101 in IDLE (Strobe SIDLE = 0x36)

### 7.3 Frequenz-Berechnung

```
Frequenz (MHz) = (FREQ2 x 2^16 + FREQ1 x 2^8 + FREQ0) x 26 / 2^16

868.35 MHz: (0x21 x 65536 + 0x65 x 256 + 0xE8) x 26 / 65536 = 868.350 MHz
868.15 MHz: (0x21 x 65536 + 0x64 x 256 + 0xBA) x 26 / 65536 = 868.149 MHz
```

---

## 8. Sicherheit und Einschränkungen

### 8.1 Keine Verschlüsselung

Das KN-Protokoll hat **keine Verschlüsselung** und **keinen Rolling Code**.
Jeder mit einem 868 MHz Sender kann die Jalousien steuern.
Dies ist ein reines Komfort-Protokoll, kein Sicherheits-Protokoll.

### 8.2 Kein Status-Feedback

Das KN-Protokoll ist unidirektional. Es gibt keine Möglichkeit zu prüfen:
- Ob der Motor den Befehl empfangen hat
- Welche Position die Jalousie hat
- Ob der Motor fehlerfrei läuft

Die Motoren senden zwar DY2-Statusmeldungen (FSK, bidirektional), aber dieses
Protokoll ist proprietär und verschlüsselt (Rolling Code).

### 8.3 Kollisionsvermeidung

Es gibt keine Kollisionserkennung. Wenn zwei Sender gleichzeitig auf der gleichen
Frequenz senden, können sich die Signale überlagern und der Motor reagiert nicht.

---

## 9. Methodik / Quellen

Dieses Protokoll wurde reverse-engineered mit folgenden Werkzeugen:

1. **Flipper Zero** -- RAW SubGHz Capture auf 868.35 MHz und 868.15 MHz
   - OOK Preset (AM650) fuer KN-Signale
   - Frequency Analyzer für Fernbedienungs-Frequenz (868.149 MHz)
   - Sende-Test: .sub Dateien für UP/DOWN/STOP/POS/SAVE verifiziert

2. **nanoCUL** (SIGNALduino v4.0.0) -- Signal-Empfang auf 868.35 MHz
   - Ms-Format Dekodierung der KN- und Fernbedienungs-Signale
   - CC1101 Register-Konfiguration für Frequenz-Einstellung

3. **Kompatible Gateways** -- als Signalquelle fuer Protokollanalyse
   - `SendSC&type=KN` zum Senden bekannter Befehle
   - RF-Logs für Protokoll-Analyse

4. **Kaiser Nienhaus Anleitung** (Art.Nr. 140100-144100)
   - Tastenbelegung der Fernbedienung
   - Anlernen, Endpunkte, Verschattungsposition

---

## Anhang A: Flipper Zero .sub Datei Beispiel

```
Filetype: Flipper SubGhz RAW File
Version: 1
Frequency: 868350000
Preset: FuriHalSubGhzPresetOok650Async
Protocol: RAW
RAW_Data: -5000 2600 -440 880 -440 880 -440 880 -440 880 -880 440 -880 440 ...
RAW_Data: -15000 2600 -880 440 -440 880 -440 880 -440 880 -880 440 -880 440 ...
(6 Wiederholungen, Preamble alterniert)
```

## Anhang B: SIGNALduino Ms-Format

```
Ms;P0;P1;P2;P3;P4;P5;D<sync><data>;C<clock>;S<sync_idx>;R<rssi>;

Sync-Prefix im D-Feld:
  4  = Sync-Puls (P4 = 2600us HIGH)
  P  = Sync-Pause Standard
  Q  = Sync-Pause Variante (Fernbedienung)

Daten-Digits:
  0 = Manchester Bit 0 (SHORT-LOW + LONG-HIGH)
  1 = Variante von Bit 0 (leicht anderes Timing)
  2 = Manchester Bit 1 (LONG-LOW + SHORT-HIGH)
```
