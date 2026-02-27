// ============================================================================
//  ads1232_driver.h — Low-Level ADS1232 24-Bit ADC Driver
// ============================================================================
//
//  Custom bit-bang driver for the Texas Instruments ADS1232.
//  Designed for ESP32 — uses portENTER_CRITICAL during the 24-bit serial
//  read to guarantee clean timing even when WiFi ISRs are active.
//
//  Features:
//    • 24-bit differential read with sign extension
//    • Offset self-calibration (26-pulse mode)
//    • Internal temperature sensor reading
//    • Channel multiplexer control (CH1 / CH2 / TEMP)
//    • Proper power-up sequencing (PDWN pulse)
//    • Counterfeit-chip validation (CS1232 detection)
//    • Timeout-based error handling — never blocks forever
//
//  References:
//    TI ADS1232 Datasheet — SBAS288I (Rev. I, Oct 2015)
//    Section 8: Application Information
//    Section 9: Serial Interface Timing
//
// ============================================================================
#pragma once

#include <Arduino.h>

// ----------------------------------------------------------------------------
//  Enums
// ----------------------------------------------------------------------------

/// ADC input channel / mux selection.
///
///  TEMP | A0 | Selection
///  -----+----+-----------
///   0   |  0 | Channel 1  (AINP1 / AINN1) — load cell
///   0   |  1 | Channel 2  (AINP2 / AINN2) — auxiliary
///   1   |  0 | Internal temperature sensor
enum class ADS1232Channel : uint8_t {
    CHANNEL_1    = 0,   ///< Differential input 1 (load cell)
    CHANNEL_2    = 1,   ///< Differential input 2 (auxiliary)
    TEMPERATURE  = 2    ///< On-chip temperature diode (gain forced to 1×)
};

/// Error codes returned by every read operation.
enum class ADS1232Error : uint8_t {
    OK              = 0,
    TIMEOUT         = 1,   ///< DRDY did not assert within the timeout window
    NOT_INITIALISED = 2,   ///< begin() has not been called
    POWERED_DOWN    = 3    ///< Chip is in power-down mode
};

// ----------------------------------------------------------------------------
//  Driver Class
// ----------------------------------------------------------------------------

class ADS1232Driver {
public:
    ADS1232Driver();

    // --- Lifecycle ---------------------------------------------------------

    /// Configures GPIO directions and sets the chip to a known idle state.
    /// Does NOT power up the ADC — call powerUp() explicitly.
    void begin(gpio_num_t sclk, gpio_num_t dout, gpio_num_t pdwn,
               gpio_num_t a0,   gpio_num_t temp);

    // --- Power Management --------------------------------------------------

    /// Execute the datasheet power-up sequence:
    ///   SCLK LOW → PDWN LOW (≥ 26 µs) → PDWN HIGH → wait for first DRDY.
    void powerUp();

    /// Enter power-down mode (< 1 µA supply current).
    void powerDown();

    /// @return true if the chip is currently powered up.
    bool isPoweredUp() const { return _powered; }

    // --- Channel Control ---------------------------------------------------

    /// Select the active input channel.  The next read will come from this
    /// channel after the required settling time.
    void selectChannel(ADS1232Channel ch);

    /// @return The currently selected channel.
    ADS1232Channel currentChannel() const { return _channel; }

    // --- Data Acquisition --------------------------------------------------

    /// Read a single 24-bit conversion result.
    ///
    /// @param[out] raw          Signed 32-bit value (−8 388 608 … +8 388 607).
    /// @param      selfCalibrate  true → send 26 SCLK pulses (triggers offset
    ///                            self-calibration).  false → send 25 pulses
    ///                            (normal read, forces DOUT high).
    /// @return ADS1232Error::OK on success.
    ADS1232Error read(int32_t& raw, bool selfCalibrate = true);

    /// Read and average multiple conversions.
    ///
    /// @param[out] average      Floating-point mean of valid readings.
    /// @param      samples      Number of conversions to average (1–255).
    /// @param      selfCalibrate  Passed through to each read() call.
    /// @return ADS1232Error::OK if at least one valid sample was obtained.
    ADS1232Error readAverage(float& average, uint8_t samples = 10,
                             bool selfCalibrate = true);

    // --- Temperature -------------------------------------------------------

    /// Switch to the internal temperature sensor, read it, then restore the
    /// previous channel.  Handles settling time and discard reads internally.
    ///
    /// Conversion:  T(°C) = 25 + (V_temp − 111.7 mV) / 0.379 mV/°C
    ///
    /// @param[out] celsius  Temperature in degrees Celsius.
    /// @return ADS1232Error::OK on success.
    ADS1232Error readInternalTemperature(float& celsius);

    // --- Diagnostics -------------------------------------------------------

    /// Run a self-calibration validation test.
    ///
    /// Reads N samples without self-cal (25 pulses), then N samples with
    /// self-cal (26 pulses), and compares variance.  Genuine TI ADS1232 chips
    /// show stable or improved readings after calibration.  Counterfeit
    /// ChipSea CS1232 clones often show erratic behaviour.
    ///
    /// Results are printed to Serial.
    ///
    /// @return true if the chip appears genuine.
    bool validateChip();

    /// @return true if DRDY/DOUT is currently LOW (conversion ready).
    bool isDataReady() const;

    /// Human-readable error string for logging.
    static const char* errorString(ADS1232Error err);

private:
    // GPIO pins
    gpio_num_t     _sclk;
    gpio_num_t     _dout;
    gpio_num_t     _pdwn;
    gpio_num_t     _a0;
    gpio_num_t     _temp;

    // State
    bool           _initialised;
    bool           _powered;
    ADS1232Channel _channel;

    // Low-level helpers
    bool    waitForReady(uint32_t timeoutMs);
    int32_t shiftIn24();
    void    clockPulse();
};
