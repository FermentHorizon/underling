# Wiring Guide

Complete wiring reference for the Underling transpiration monitor.
Every GPIO choice is justified.

---

## GPIO Selection Rationale

The ESP32 WROOM-32 has 34 GPIOs, but many are restricted. We only use
unrestricted, clean GPIOs with no boot-strapping side effects.

### GPIOs to Avoid

| GPIO | Reason |
|------|--------|
| 6-11 | Connected to internal SPI flash, must not use |
| 16, 17 | Used by PSRAM on WROOM modules |
| 0 | Boot mode select (must be HIGH to boot) |
| 2 | Boot mode / on-board LED |
| 12 | Flash voltage select, HIGH at boot selects 1.8V and bricks |
| 15 | JTAG; LOW at boot suppresses boot log |
| 34, 35, 36, 39 | Input-only, cannot drive outputs |

### GPIOs We Use

| GPIO | Assignment | Direction | Why |
|------|-----------|-----------|-----|
| 4 | ADS1232 A0 | Output | Clean, no strapping |
| 18 | ADS1232 SCLK | Output | Clean, no restrictions |
| 19 | ADS1232 DOUT/DRDY | Input | Interrupt-safe (not 36/39) |
| 23 | ADS1232 PDWN | Output | Floats during boot = chip stays powered down (safe) |
| 25 | ADS1232 CLKIN | Output | LEDC PWM, 1 MHz square wave |
| 26 | DS18B20 DATA | I/O | Supports OneWire, not ADC2 (safe with WiFi) |
| 27 | ADS1232 TEMP | Output | Clean, away from flash/strapping |
| 32 | Tare Button | Input | Has internal pull-up |

Free for expansion: GPIOs 5, 13, 14, 21, 22, 33.

---

## Complete Wiring Diagram

```
    ESP32 WROOM-32                    ADS1232 Module (CJMCU-1232)

    GPIO 18  ---- SCLK ----------->  SCLK
    GPIO 19  ---- DOUT <-----------  DOUT / DRDY
    GPIO 23  ---- PDWN ----------->  PDWN
    GPIO  4  ---- A0   ----------->  A0
    GPIO 27  ---- TEMP ----------->  TEMP (labelled A1 on some boards)
    GPIO 25  ---- CLKIN ---------->  CLKI (1 MHz from LEDC)

    3V3      --------------------->  DVDD
    GND      --+------------------>  DGND
               |
               |   5V (regulated) -> AVDD / VCC
               |   5V (regulated) -> REFP (= E+)
               +------------------>  AGND / REFN

                                     AINP1 <--- S+ (Green wire)
                                     AINN1 <--- S- (Black wire)

                                     GAIN0 --- DVDD (3V3)
                                     GAIN1 --- DVDD (3V3)
                                     SPEED --- GND (LOW)

    Load Cell (40 kg beam):
      E+ (Red)  --- 5V supply / AVDD / REFP
      E- (White) -- GND / AGND
      S+ (Green) -- AINP1
      S- (Black) -- AINN1

    GPIO 26  ---- DS18B20 DATA (pin 2)
                      |            pin 1 = GND, pin 3 = 3V3
                   [4.7k]
    3V3      --------+

    GPIO 32  ---- TARE BUTTON ---- GND (normally open)
```

---

## Load Cell Wiring

Standard 4-wire Wheatstone bridge:

| Wire | Function | Goes To |
|------|----------|---------|
| E+ | Excitation positive | 5V regulated (same as AVDD and REFP) |
| E- | Excitation negative | GND (AGND) |
| S+ | Signal positive | ADS1232 AINP1 |
| S- | Signal negative | ADS1232 AINN1 |

### Wire Colours Vary!

The "standard" Red=E+, Black=E-, Green=S+, White=S- does NOT apply to all
cells. **Always identify pairs with a multimeter.**

### Identifying Wires with a Multimeter

With the load cell disconnected, measure resistance between every pair:

| Pair | Resistance |
|------|------------|
| Wire A - Wire B | note |
| Wire A - Wire C | note |
| Wire A - Wire D | note |
| Wire B - Wire C | note |
| Wire B - Wire D | note |
| Wire C - Wire D | note |

The two pairs with the **highest and roughly equal** resistance are the
bridge diagonals: one is excitation (E+/E-), the other is signal (S+/S-).
The bridge is symmetric. Pick one pair for excitation, the other for signal.

- If weight reads **negative**: swap S+ and S-.
- If ADC is **railed** at +/-8388608: swap excitation and signal pairs.

### Our 40 kg Beam Cell (Actual Measured)

| Wire Colour | Function | Connected To |
|-------------|----------|-------------|
| Red | E+ (VCC) | 5V supply / AVDD / REFP |
| White | E- (GND) | GND / AGND |
| Green | S+ | ADS1232 AINP1 |
| Black | S- | ADS1232 AINN1 |

---

## ADS1232 Module Details

### Signal Pins

| Module Pin | ESP32 GPIO | Notes |
|-----------|-----------|-------|
| SCLK | 18 | Serial clock, MCU drives |
| DOUT | 19 | DRDY (goes LOW when data ready) + 24-bit serial data |
| PDWN | 23 | HIGH = run, LOW = power-down (<1 uA) |
| A0 | 4 | LOW = CH1 (load cell), HIGH = CH2 |
| TEMP | 27 | HIGH = route internal temp sensor. Some boards label this A1 |
| CLKI | 25 | 1 MHz system clock from ESP32 LEDC PWM |

### Hard-Wired Config Pins

| Pin | Wire To | Level | Effect |
|-----|---------|-------|--------|
| GAIN0 | DVDD (3.3V) | HIGH | Gain = 128 (best noise) |
| GAIN1 | DVDD (3.3V) | HIGH | |
| SPEED | GND | LOW | 10 SPS (lowest noise, 100 dB rejection) |

### External Clock (CLKIN)

Our module has no on-board crystal, so ESP32 generates 1 MHz via LEDC:

| Parameter | Value |
|-----------|-------|
| Frequency | 1 MHz |
| Duty | 50% (1-bit resolution, duty=1) |
| LEDC channel | 0 |
| GPIO | 25 |

Started before ADS1232 power-up so the chip has a stable clock from the start.

### Ratiometric Reference

| Pin | Connected To | Why |
|-----|-------------|-----|
| REFP | Same 5V as load cell E+ | Supply drift cancels out |
| REFN | AGND | Reference ground |

REFP must NOT float or DOUT will stay permanently HIGH.

---

## DS18B20 Temperature Sensor

| Pin | Connected To |
|-----|-------------|
| 1 (GND) | GND |
| 2 (DATA) | ESP32 GPIO 26 |
| 3 (VDD) | 3V3 |

4.7 kOhm pull-up between DATA and 3V3 is required.

---

## Tare Button

Momentary push-button between GPIO 32 and GND.

- Internal pull-up enabled (~45 kOhm)
- Software debounce (50 ms)
- If no button connected, firmware auto-detects and disables polling

---

## Decoupling (Recommended)

| Location | Component | Value |
|----------|-----------|-------|
| AVDD to AGND | Ceramic cap | 100 nF |
| AVDD to AGND | Electrolytic | 10 uF |
| DVDD to DGND | Ceramic cap | 100 nF |
| REFP to REFN | Ceramic cap | 100 nF |
| ESP32 3V3 to DVDD | Ferrite bead | ~600 Ohm at 100 MHz |

---

## Pin Summary

| GPIO | Function | Dir |
|------|----------|-----|
| 4 | ADS1232 A0 | OUT |
| 18 | ADS1232 SCLK | OUT |
| 19 | ADS1232 DOUT | IN |
| 23 | ADS1232 PDWN | OUT |
| 25 | ADS1232 CLKIN | OUT |
| 26 | DS18B20 DATA | I/O |
| 27 | ADS1232 TEMP | OUT |
| 32 | Tare Button | IN |
