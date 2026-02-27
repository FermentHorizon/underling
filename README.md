# Underling — Plant Transpiration Monitor

A precision weight-tracking system for measuring plant water loss over time,
built on an **ESP32 WROOM-32**, a **TI ADS1232** 24-bit delta-sigma ADC, and a
**40 kg beam-type load cell**.

Outputs one calibrated weight reading every 30 seconds over serial.
No WiFi, no MQTT, no cloud — just clean data.

---

## Performance

| Metric | Value |
|--------|-------|
| Resolution | ~0.07 g (noise-free) |
| Noise (1 sigma) | +/-0.3 g at 11 kg load |
| Interval | 30 seconds |
| ADC samples per reading | 50 (trimmed mean, middle 60%) |
| Read time | ~5 s at 10 SPS |
| Output | Serial 115200 baud |

---

## Serial Output

```
[     31] W:  11061.7 g | T: 23.3C
[     55] W:  11061.9 g | T: 23.3C
[     85] W:  11062.1 g | T: 23.3C
```

Format: `[uptime_s] W: weight_g | T: temp_C`

Temperature is included when a DS18B20 sensor is connected.

---

## Hardware

| Component | Specification |
|-----------|--------------|
| MCU | ESP32 WROOM-32 (any DevKit v1 / v4 board) |
| ADC | ADS1232 module (CJMCU-1232 or similar breakout) |
| Load Cell | 40 kg beam-type, 4-wire Wheatstone bridge |
| Temp Sensor | DS18B20 (optional, waterproof probe recommended) |
| Tare Button | Momentary push-button, normally-open (GPIO 32 to GND) |
| Pull-up | 4.7 kOhm resistor for DS18B20 data line to 3V3 |

---

## Wiring Summary

Full wiring guide: **[docs/wiring.md](docs/wiring.md)**

```
  ESP32                 ADS1232 Module
  GPIO 18  ---------->  SCLK
  GPIO 19  <----------  DOUT (DRDY)
  GPIO 23  ---------->  PDWN
  GPIO  4  ---------->  A0
  GPIO 27  ---------->  TEMP
  GPIO 25  ---------->  CLKIN (1 MHz via LEDC)
  3V3      ---------->  DVDD
  GND      ---------->  DGND

  5V (reg) ---------->  AVDD / REFP ------>  Load Cell E+
  GND      ---------->  AGND / REFN ------>  Load Cell E-

                        AINP1  <----------  Load Cell S+
                        AINN1  <----------  Load Cell S-

  Hard-wired on module:
    GAIN0 -> DVDD (HIGH)
    GAIN1 -> DVDD (HIGH)  -> Gain = 128
    SPEED -> GND  (LOW)   -> 10 SPS

  GPIO 26 -------- DS18B20 DATA  (4.7 kOhm pull-up to 3V3)
  GPIO 32 -------- Tare Button   (other leg to GND)
```

Wire colours vary by manufacturer. Use a multimeter to identify
excitation and signal pairs. See docs/wiring.md.

---

## Quick Start

### 1. Build and Flash

```bash
cd Underling
pio run -t upload
```

### 2. Monitor

```bash
pio device monitor
```

### 3. Calibrate

See **[docs/calibration.md](docs/calibration.md)** for the full procedure.

Quick version:
1. Open serial monitor: `pio device monitor`
2. With platform empty, type `TARE` and press Enter
3. Place known weight, wait 30 seconds
4. Type `CAL 11000` (or your weight in grams) and press Enter
5. Calibration saved to NVS, survives power cycles

Automated:
```bash
python3 auto_setup.py 11000
```

---

## Serial Commands

| Command | Action |
|---------|--------|
| `TARE` | Zero the scale at current load |
| `CAL <grams>` | Calibrate with a known weight (e.g. `CAL 11000`) |

The tare button on GPIO 32 also triggers a tare.

---

## Project Structure

```
Underling/
  platformio.ini              Project config and dependencies
  README.md                   This file
  auto_setup.py               Non-interactive tare + cal script
  setup_scale.py              Interactive tare + cal script
  quick_cal.py                Minimal cal script
  diag.py                     Diagnostic serial reader
  docs/
    wiring.md                 Detailed wiring guide
    calibration.md            Calibration procedure
    theory.md                 Theory of operation and noise budget
  src/
    config.h                  Pin assignments and constants
    ads1232_driver.h/.cpp     ADS1232 bit-bang SPI driver
    calibration_store.h/.cpp  NVS persistence for cal data
    temperature_manager.h/.cpp DS18B20 + ADS1232 internal temp
    main.cpp                  Setup, FreeRTOS task, serial I/O
```

---

## Architecture

Single FreeRTOS task on Core 1. Every 30 seconds:

1. ADC read (50-sample trimmed mean, ~5 s)
2. Apply calibration: weight = (raw - tare) / scale
3. Apply temp compensation (disabled, coeff = 0)
4. Serial printf

No filters, no rate estimation, no WiFi, no MQTT.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| TIMEOUT (DRDY not asserted) | No clock, PDWN low, DOUT broken | Check SCLK/DOUT/PDWN wiring |
| Raw stuck at +/-8388608 | Excitation/signal pairs swapped | Use multimeter, see docs/wiring.md |
| Readings stuck at 0 | Signal wires disconnected | Verify S+/S- to AINP1/AINN1 |
| CAL FAILED, delta too small | No weight on cell | Place known weight before CAL |
| DS18B20 NOT FOUND | Missing 4.7k pull-up or wrong GPIO | Check resistor on GPIO 26 |
| Weight negative | S+ and S- swapped | Swap the two signal wires |

---

## Further Reading

- [docs/wiring.md](docs/wiring.md) — Complete wiring reference
- [docs/theory.md](docs/theory.md) — Noise budget, ratiometric measurement, design rationale
- [docs/calibration.md](docs/calibration.md) — Step-by-step calibration guide
- [TI ADS1232 Datasheet](https://www.ti.com/lit/ds/symlink/ads1232.pdf)

---

## Licence

MIT
