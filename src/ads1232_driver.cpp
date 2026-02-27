// ============================================================================
//  ads1232_driver.cpp — ADS1232 24-Bit ADC Driver Implementation
// ============================================================================
#include "ads1232_driver.h"
#include "config.h"

// ============================================================================
//  Constructor
// ============================================================================

ADS1232Driver::ADS1232Driver()
    : _sclk(GPIO_NUM_NC)
    , _dout(GPIO_NUM_NC)
    , _pdwn(GPIO_NUM_NC)
    , _a0(GPIO_NUM_NC)
    , _temp(GPIO_NUM_NC)
    , _initialised(false)
    , _powered(false)
    , _channel(ADS1232Channel::CHANNEL_1)
{}

// ============================================================================
//  Lifecycle
// ============================================================================

void ADS1232Driver::begin(gpio_num_t sclk, gpio_num_t dout, gpio_num_t pdwn,
                          gpio_num_t a0,   gpio_num_t temp)
{
    _sclk = sclk;
    _dout = dout;
    _pdwn = pdwn;
    _a0   = a0;
    _temp = temp;

    pinMode(_sclk, OUTPUT);
    pinMode(_dout, INPUT);
    pinMode(_pdwn, OUTPUT);
    pinMode(_a0,   OUTPUT);
    pinMode(_temp,  OUTPUT);

    // Idle state: SCLK LOW, PDWN LOW (chip in power-down), CH1, no temp.
    digitalWrite(_sclk, LOW);
    digitalWrite(_pdwn, LOW);
    digitalWrite(_a0,   LOW);
    digitalWrite(_temp,  LOW);

    _initialised = true;
    _powered     = false;
    _channel     = ADS1232Channel::CHANNEL_1;
}

// ============================================================================
//  Power Management
// ============================================================================

void ADS1232Driver::powerUp()
{
    if (!_initialised) return;

    // Datasheet Section 8.3.5 — Power-Up Sequence:
    //   1. Ensure SCLK = LOW  (required before PDWN rising edge)
    //   2. Hold PDWN LOW for ≥ 26 µs
    //   3. Drive PDWN HIGH → first conversion begins
    //   4. Wait for the first DRDY assertion (≈ 100 ms @ 10 SPS)

    digitalWrite(_sclk, LOW);
    digitalWrite(_pdwn, LOW);
    delayMicroseconds(ads_cfg::PDWN_PULSE_US);

    digitalWrite(_pdwn, HIGH);
    delay(ads_cfg::POWERUP_MS);

    _powered = true;
}

void ADS1232Driver::powerDown()
{
    if (!_initialised) return;

    // Datasheet: PDWN LOW + SCLK HIGH → power-down mode (< 1 µA).
    digitalWrite(_pdwn, LOW);
    digitalWrite(_sclk, HIGH);

    _powered = false;
}

// ============================================================================
//  Channel Control
// ============================================================================

void ADS1232Driver::selectChannel(ADS1232Channel ch)
{
    if (!_initialised) return;

    //  TEMP | A0 | Input
    //  -----+----+------
    //    0  |  0 | CH1  (AINP1/AINN1)
    //    0  |  1 | CH2  (AINP2/AINN2)
    //    1  |  0 | Temperature sensor

    switch (ch) {
        case ADS1232Channel::CHANNEL_1:
            digitalWrite(_temp, LOW);
            digitalWrite(_a0,   LOW);
            break;
        case ADS1232Channel::CHANNEL_2:
            digitalWrite(_temp, LOW);
            digitalWrite(_a0,   HIGH);
            break;
        case ADS1232Channel::TEMPERATURE:
            digitalWrite(_a0,   LOW);
            digitalWrite(_temp, HIGH);
            break;
    }

    _channel = ch;
}

// ============================================================================
//  Low-Level Helpers
// ============================================================================

bool ADS1232Driver::isDataReady() const
{
    return digitalRead(_dout) == LOW;
}

bool ADS1232Driver::waitForReady(uint32_t timeoutMs)
{
    // The ADS1232 DOUT/DRDY pin goes LOW when a new conversion is ready.
    // At 10 SPS, conversions take ~100ms.  After a 26-clock self-cal read,
    // the chip recalibrates before the next conversion, which can extend
    // the wait significantly.
    //
    // If DOUT is already LOW, data is ready now.
    // If DOUT is HIGH, we poll until it goes LOW or we time out.
    //
    // We poll at 10 µs intervals to avoid missing the narrow DRDY window.

    uint32_t t0 = millis();
    while (digitalRead(_dout) == HIGH) {
        if (millis() - t0 > timeoutMs) return false;
        delayMicroseconds(10);
    }
    return true;
}

void ADS1232Driver::clockPulse()
{
    // Minimum SCLK high/low time: 100 ns (datasheet Table 2).
    // 1 µs each side gives generous margin for GPIO overhead.
    digitalWrite(_sclk, HIGH);
    delayMicroseconds(1);
    digitalWrite(_sclk, LOW);
    delayMicroseconds(1);
}

int32_t ADS1232Driver::shiftIn24()
{
    // Read 24 bits MSB-first.  Each rising edge of SCLK shifts the next bit
    // out onto DOUT.  We read DOUT while SCLK is HIGH.
    //
    //   Bit 23 (MSB) is presented on the first rising edge.
    //   Bit  0 (LSB) is presented on the 24th rising edge.

    int32_t value = 0;

    for (int8_t bit = 23; bit >= 0; bit--) {
        digitalWrite(_sclk, HIGH);
        delayMicroseconds(1);

        if (digitalRead(_dout)) {
            value |= (1L << bit);
        }

        digitalWrite(_sclk, LOW);
        delayMicroseconds(1);
    }

    // Sign-extend from 24-bit two's complement to 32-bit.
    // If bit 23 is set, the value is negative.
    if (value & 0x00800000) {
        value |= 0xFF000000;
    }

    return value;
}

// ============================================================================
//  Data Acquisition
// ============================================================================

ADS1232Error ADS1232Driver::read(int32_t& raw, bool selfCalibrate)
{
    if (!_initialised) return ADS1232Error::NOT_INITIALISED;
    if (!_powered)     return ADS1232Error::POWERED_DOWN;

    // 1. Wait for DRDY/DOUT to go LOW (new conversion available).
    if (!waitForReady(ads_cfg::READ_TIMEOUT_MS)) {
        return ADS1232Error::TIMEOUT;
    }

    // 2. Enter critical section.
    //    Disables interrupts on *this* core so WiFi/BT ISRs (on Core 0) and
    //    FreeRTOS tick (on Core 1) cannot disrupt the bit-bang timing.
    //    Total time in critical section: ~52 µs (26 clocks × 2 µs).
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);

    // 3. Clock out 24 data bits.
    raw = shiftIn24();

    // 4. Extra clock pulses:
    //      25 total → forces DOUT HIGH (normal read)
    //      26 total → triggers offset self-calibration
    if (selfCalibrate) {
        clockPulse();   // 25th
        clockPulse();   // 26th  — self-calibration triggered
    } else {
        clockPulse();   // 25th  — DOUT forced HIGH
    }

    portEXIT_CRITICAL(&mux);

    return ADS1232Error::OK;
}

ADS1232Error ADS1232Driver::readAverage(float& average, uint8_t samples,
                                        bool selfCalibrate)
{
    if (samples == 0) samples = 1;

    // Collect successful reads into an array for robust averaging.
    // At 10 SPS each read takes ~100 ms.  We allow up to (samples × 2)
    // attempts so occasional timeouts don't reduce quality.

    constexpr uint8_t MAX_BUF = 100;
    int32_t values[MAX_BUF];
    uint8_t valid = 0;
    uint8_t maxAttempts = static_cast<uint8_t>(
        (static_cast<uint16_t>(samples) * 2 > MAX_BUF)
            ? MAX_BUF : samples * 2);

    for (uint8_t attempt = 0; attempt < maxAttempts && valid < samples; attempt++) {
        int32_t raw;
        ADS1232Error err = read(raw, selfCalibrate);
        if (err == ADS1232Error::OK) {
            values[valid++] = raw;
        }
        // If timeout, the next conversion should be imminent — just retry.
    }

    if (valid == 0) return ADS1232Error::TIMEOUT;

    if (valid == 1) {
        average = static_cast<float>(values[0]);
        return ADS1232Error::OK;
    }

    // --- Trimmed mean (outlier rejection) ---
    //
    // The ADS1232 occasionally produces a bogus value (possibly from a
    // spurious DRDY assertion during internal settling).  A simple mean
    // would be ruined by one outlier; a trimmed mean discards the extreme
    // 20 % at each end and averages the middle 60 %.
    //
    // For ≤ 4 values we fall back to the median — can't trim usefully.

    // Insertion sort (N ≤ 40, trivially fast).
    for (uint8_t i = 1; i < valid; i++) {
        int32_t key = values[i];
        int8_t  j   = static_cast<int8_t>(i) - 1;
        while (j >= 0 && values[j] > key) {
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = key;
    }

    uint8_t trim = valid / 5;                      // 20 % from each end
    if (trim == 0 && valid >= 5) trim = 1;         // at least 1 if we have ≥ 5

    int64_t sum   = 0;
    uint8_t count = 0;
    for (uint8_t i = trim; i < valid - trim; i++) {
        sum += values[i];
        count++;
    }

    if (count == 0) {
        // Edge case: use median.
        average = static_cast<float>(values[valid / 2]);
    } else {
        average = static_cast<float>(sum) / static_cast<float>(count);
    }

    return ADS1232Error::OK;
}

// ============================================================================
//  Temperature
// ============================================================================

ADS1232Error ADS1232Driver::readInternalTemperature(float& celsius)
{
    // Save the current channel so we can restore it afterwards.
    ADS1232Channel prev = _channel;

    // Switch to temperature sensor.
    selectChannel(ADS1232Channel::TEMPERATURE);

    // Wait the full settling time (≈ 4 conversion cycles @ 10 SPS = 400 ms).
    delay(ads_cfg::SETTLE_MS);

    // Discard the first readings after the mux change (they contain
    // partially-settled data).
    for (uint8_t i = 0; i < ads_cfg::DISCARD_AFTER_MUX; i++) {
        int32_t dummy;
        read(dummy, false);
    }

    // Average a few readings for a clean temperature value.
    float avgRaw;
    ADS1232Error err = readAverage(avgRaw, 5, false);

    if (err == ADS1232Error::OK) {
        // In temperature mode the PGA is bypassed (gain = 1).
        // Input range = ±VREF / 2.
        //
        //   V_temp = (raw / 2^23) × (VREF / 2)
        //
        // Temperature sensor transfer function (TI datasheet + E2E):
        //
        //   V_temp(25 °C) ≈ 111.7 mV   (nominal)
        //   Slope          ≈ 379 µV/°C
        //
        //   T(°C) = 25 + (V_temp − 0.1117) / 0.000379

        const float vTemp = (avgRaw / 8388608.0f) * (ads_cfg::VREF_V / 2.0f);
        celsius = 25.0f + (vTemp - 0.1117f) / 0.000379f;
    }

    // Restore previous channel.
    selectChannel(prev);
    delay(ads_cfg::SETTLE_MS);

    // Discard stale data from the restored channel.
    for (uint8_t i = 0; i < ads_cfg::DISCARD_AFTER_MUX; i++) {
        int32_t dummy;
        read(dummy, false);
    }

    return err;
}

// ============================================================================
//  Chip Validation (Counterfeit Detection)
// ============================================================================

bool ADS1232Driver::validateChip()
{
    // Many cheap purple "ADS1232" breakout modules ship with ChipSea CS1232
    // clones.  The 26-pulse offset self-calibration either does nothing or
    // produces erratic behaviour on those parts.
    //
    // Test:
    //   1. Read 10 samples WITHOUT self-cal (25 pulses).
    //   2. Read 10 samples WITH self-cal (26 pulses).
    //   3. On a genuine TI chip, variance should remain stable or decrease
    //      after calibration.  On a clone, it typically spikes.

    const uint8_t N = 10;

    Serial.println(F("  ┌─ Chip Validation Test ──────────────────────────"));

    // --- Phase 1: No self-calibration ---
    Serial.println(F("  │  Phase 1: Reading without self-calibration ..."));
    int64_t  sum1   = 0, sumSq1 = 0;
    uint8_t  ok1    = 0;

    for (uint8_t i = 0; i < N; i++) {
        int32_t raw;
        if (read(raw, false) == ADS1232Error::OK) {
            sum1   += raw;
            sumSq1 += static_cast<int64_t>(raw) * raw;
            ok1++;
        }
    }

    if (ok1 < 5) {
        Serial.println(F("  │  FAIL — could not obtain enough samples."));
        Serial.println(F("  │  Check wiring and power supply."));
        Serial.println(F("  └──────────────────────────────────────────────"));
        return false;
    }

    float mean1 = static_cast<float>(sum1) / ok1;
    float var1  = (static_cast<float>(sumSq1) / ok1) - (mean1 * mean1);

    Serial.printf("  │  No-cal  → mean: %10.1f   variance: %12.1f   (n=%u)\n",
                  mean1, var1, ok1);

    // --- Phase 2: With self-calibration ---
    Serial.println(F("  │  Phase 2: Reading with self-calibration ..."));
    int64_t  sum2   = 0, sumSq2 = 0;
    uint8_t  ok2    = 0;

    for (uint8_t i = 0; i < N; i++) {
        int32_t raw;
        if (read(raw, true) == ADS1232Error::OK) {
            sum2   += raw;
            sumSq2 += static_cast<int64_t>(raw) * raw;
            ok2++;
        }
    }

    if (ok2 < 5) {
        Serial.println(F("  │  FAIL — could not obtain calibrated samples."));
        Serial.println(F("  └──────────────────────────────────────────────"));
        return false;
    }

    float mean2 = static_cast<float>(sum2) / ok2;
    float var2  = (static_cast<float>(sumSq2) / ok2) - (mean2 * mean2);

    Serial.printf("  │  Cal     → mean: %10.1f   variance: %12.1f   (n=%u)\n",
                  mean2, var2, ok2);
    Serial.printf("  │  Offset shift after cal: %+.1f counts\n", mean2 - mean1);

    // --- Verdict ---
    // Genuine chip: variance should not blow up after calibration.
    // If both variances are very small (< 100), the chip is reading
    // consistently in both modes — that's healthy, not a clone indicator.
    bool bothQuiet = (var1 < 100.0f) && (var2 < 100.0f);
    bool genuine   = (ok2 >= 5) && (bothQuiet || (var2 < var1 * 10.0f));

    if (genuine) {
        Serial.println(F("  │"));
        Serial.println(F("  │  ✓ PASS — Self-calibration is functional."));
        Serial.println(F("  │          Chip appears to be a genuine TI ADS1232."));
    } else {
        Serial.println(F("  │"));
        Serial.println(F("  │  ⚠ WARNING — Self-calibration may not be working."));
        Serial.println(F("  │              Chip may be a ChipSea CS1232 clone."));
        Serial.println(F("  │              System will continue, but offset drift"));
        Serial.println(F("  │              compensation may be degraded."));
    }

    Serial.println(F("  └──────────────────────────────────────────────"));
    return genuine;
}

// ============================================================================
//  Error String
// ============================================================================

const char* ADS1232Driver::errorString(ADS1232Error err)
{
    switch (err) {
        case ADS1232Error::OK:              return "OK";
        case ADS1232Error::TIMEOUT:         return "TIMEOUT (DRDY not asserted)";
        case ADS1232Error::NOT_INITIALISED: return "NOT INITIALISED (call begin())";
        case ADS1232Error::POWERED_DOWN:    return "POWERED DOWN (call powerUp())";
        default:                            return "UNKNOWN";
    }
}
