# Theory of Operation

How the Underling transpiration monitor achieves sub-gram resolution on a
40 kg load cell, and why each design decision was made.

---

## 1. Ratiometric Measurement

The single most important design choice.

A load cell output is proportional to its excitation voltage:

    V_out = V_EX * S * (W / W_max)

where S is the sensitivity (typically 2 mV/V) and W is the applied weight.

If excitation drifts by 0.1%, the output drifts by 0.1% of full-scale:
10 uV on a 10 mV signal, which would swamp sub-gram measurement.

**Solution:** Connect the ADC reference (REFP) to the same rail as the
load cell excitation (E+). The ADC computes:

    Code = (V_IN / V_REF) * 2^23 * Gain

Since V_IN is proportional to V_EX and V_REF = V_EX, excitation cancels:

    Code = S * (W / W_max) * 2^23 * Gain

The measurement depends only on the ratio, immune to supply drift.

---

## 2. Noise Budget

### ADS1232 at Gain 128, 10 SPS

| Parameter | Value |
|-----------|-------|
| RMS input-referred noise | 17 nV |
| Peak-to-peak noise (6.6 sigma) | ~112 nV |
| Noise-free resolution | ~19.2 bits |

### Mapping to Weight

With 5V excitation, 2 mV/V sensitivity, 40 kg capacity:

| Parameter | Value |
|-----------|-------|
| Full-scale output | 10 mV |
| ADC input range (Gain 128) | 39.06 mV |
| Counts at full scale (10 mV) | ~2,147,000 |
| Weight per count | 0.019 g/count |
| Noise-free resolution | 0.066 g |

### After 50-Sample Trimmed Mean

50 raw samples, sort, discard top/bottom 20%, average middle 30:

    sigma_avg = sigma_single / sqrt(30) = 0.066 / sqrt(30) = 0.012 g

### Observed Performance

At 11 kg load: +/-0.3 g (1 sigma), which is 0.003% of load. Higher than
theoretical due to mechanical noise, air currents, and breadboard wiring.
Still gives a 13:1 SNR per reading for a 500 mL/hr transpiration rate.

---

## 3. Self-Calibration: Disabled

The ADS1232 has built-in offset self-calibration (26 SCLK pulses instead
of 25). This is disabled because the CS1232 clone chip on our module
has a bug: mixing 25-clock and 26-clock reads causes ~980,000 count offset
shifts, producing wild alternating readings.

With self-cal disabled and trimmed-mean averaging, offset drift is managed
adequately.

---

## 4. Temperature Compensation

### Sources of Error

1. Load cell zero drift: ~0.8 g/C for a 40 kg cell (0.002%/C)
2. Load cell span drift: ~1.0 g/C at 5 kg load
3. ADC offset drift: Would be handled by self-cal (disabled)
4. Reference voltage drift: Cancelled by ratiometric measurement

### Current Status: Disabled

Temperature compensation is plumbed through the firmware but disabled
(LOAD_CELL_TC_PCT_PER_C = 0.0 in config.h).

Testing showed the default 0.002%/C coefficient was overcorrecting,
adding artificial drift of 0.8 g/C rather than removing it. A proper
thermal sweep is needed to determine the actual coefficient for this cell.

The DS18B20 still reads temperature and logs it to serial for external
analysis.

---

## 5. Data Rate: 10 SPS

| Rate | SPEED Pin | RMS Noise | 50/60 Hz Rejection |
|------|-----------|-----------|-------------------|
| 10 SPS | LOW | 17 nV | 100 dB simultaneous |
| 80 SPS | HIGH | 44 nV | 90 dB (50 Hz only) |

10 SPS gives 2.6x lower noise and simultaneous 50/60 Hz mains rejection.
50 samples takes ~5 s, fine for 30 s measurement intervals.

---

## 6. Gain: 128

| GAIN1 | GAIN0 | Gain | RMS Noise (10 SPS) |
|-------|-------|------|--------------------|
| LOW | LOW | 1 | 44 nV |
| LOW | HIGH | 2 | 36 nV |
| HIGH | LOW | 64 | 23 nV |
| HIGH | HIGH | 128 | 17 nV |

Gain 128 gives the best noise performance. Our 10 mV full-scale signal
fits well within the +/-39 mV input range.

---

## 7. Why No On-Device Rate Calculation

Transpiration rate (g/hr or mL/hr) is intentionally not computed on the
device.

- A first-difference over 30 s amplifies noise by 120x, unusable.
- A regression window long enough to smooth (10+ min) adds unacceptable
  lag for a 30 s output interval.
- Any on-device smoothing (moving average, EMA) adds propagation delay
  that masks real events.

**Design decision:** Output raw calibrated weight every 30 s. Rate
computation belongs in the downstream analysis (Python, spreadsheet, etc.)
where you can choose the window size after the fact without re-flashing.

---

## 8. Measurement Pipeline

Every 30 seconds, the single FreeRTOS task on Core 1 runs:

1. Check serial for TARE / CAL commands
2. Check tare button (GPIO 32, active-LOW, debounced)
3. Read ADC: 50 samples, insertion sort, discard top/bottom 20%,
   average middle 60% (~5 s)
4. Apply calibration: weight = (raw - tare) / scale
5. Apply temp compensation: weight -= offset (currently 0)
6. Print: [uptime] W: XXXXX.X g | T: XX.XC
7. vTaskDelayUntil, sleep until next 30 s mark

No filters, no rate estimation, no WiFi, no MQTT.

---

## 9. Timing

| Phase | Duration |
|-------|----------|
| ADC read (50 samples at 10 SPS) | ~5 s |
| Calibration + temp comp math | <1 ms |
| Serial printf | <1 ms |
| Sleep until next interval | ~25 s |
| Total cycle | 30 s |

Temperature is read every 10th cycle (~5 min) to avoid unnecessary
OneWire traffic.

---

## 10. NVS Persistence

Calibration data (tare, scale, temp baseline) is stored in the ESP32's
Non-Volatile Storage partition and survives power cycles and resets.

The system never auto-tares on boot. If you had a plant on the scale
and power was lost, the reading resumes correctly after restart.
