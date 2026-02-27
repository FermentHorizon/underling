# Calibration Guide

How to tare and calibrate the Underling transpiration monitor.
Calibration data is stored in the ESP32's NVS flash and survives power cycles.

---

## Overview

Two steps:

1. **Tare** — sets the zero point (raw ADC counts at no load)
2. **Scale Factor** — maps ADC counts to grams using a known weight

Both are performed via **serial commands** or the **tare button** (GPIO 32).

---

## Serial Commands

115200 baud. Type each command followed by Enter:

| Command | Action |
|---------|--------|
| `TARE` | Zero the scale at current load |
| `CAL <grams>` | Calibrate with a known weight (e.g. `CAL 11000`) |

---

## Step 1: Tare

### What It Does

Reads 50 ADC samples (trimmed mean: sorts, discards top/bottom 20%, averages
middle 60%), stores the result as the zero offset. Also records the ambient
temperature as the baseline for temperature compensation.

### When to Tare

- First setup (boots with tare=0, scale=1 if NVS is empty)
- After changing mechanical setup (moving cell, changing pot holder)
- Before a new experiment (tare with empty pot + soil on cell)

The system does **not** auto-tare on boot.

### How to Tare

**Serial:** Type `TARE` and press Enter.

**Button:** Press and release the momentary button on GPIO 32.

**Script:**
```bash
python3 auto_setup.py 11000   # tares first, then calibrates
```

Output:
```
[TARE] Done -- offset: -159654.0  T_base: 24.69 C
```

---

## Step 2: Calibrate (Scale Factor)

### What It Does

Computes: `scaleFactor = (raw_loaded - raw_tare) / knownWeight_g`

### Procedure

1. Tare the scale (step 1) with cell unloaded
2. Place known weight on the cell
3. Wait 30-60 seconds for readings to settle
4. Type `CAL 11000` (or your weight in grams)
5. Scale factor saved to NVS immediately

Output:
```
[CAL] Done -- scale: -95.337296 counts/g
```

### Verify

- Remove weight: reading should return to ~0 g
- Replace weight: should read close to your known value
- Try a different known weight to check linearity

---

## NVS Stored Values

| Key | What | Set By |
|-----|------|--------|
| `tare` | Raw ADC offset at zero load | TARE |
| `scale` | ADC counts per gram | CAL |
| `tbase` | Temperature at last tare | TARE |

Printed on every boot:
```
[INIT] Calibration loaded from NVS:
       tare   = -159654.0
       scale  = -95.337296 counts/g
       T_base = 24.69 C
```

### Factory Reset

```bash
esptool.py --chip esp32 erase_region 0x9000 0x5000
```

---

## How Trimmed-Mean Works

Each measurement takes 50 raw ADC samples (~5 s at 10 SPS):

1. Sort all 50 values by insertion sort
2. Discard the bottom 20% (10 samples) and top 20% (10 samples)
3. Average the remaining 30 (middle 60%)

This aggressively rejects outliers from EMI, vibration, or ADC glitches
while preserving the true signal.

---

## Tips

- Heavier calibration weight = more accurate (better signal-to-noise)
- Calibrate at mid-range if possible (e.g. 5 kg for a 5 kg plant)
- Place calibration weight at the same position the plant will sit
- Room temperature should be stable during calibration
- Wait at least 60 s after placing load before taring (mechanical settling)
