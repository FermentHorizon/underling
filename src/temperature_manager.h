// ============================================================================
//  temperature_manager.h — Dual Temperature Sensor Manager
// ============================================================================
//
//  Wraps the external DS18B20 (accurate ambient reading) and the ADS1232's
//  internal temperature diode (relative drift tracking).  Provides a unified
//  interface and computes the load-cell temperature-compensation offset.
//
// ============================================================================
#pragma once

#include <Arduino.h>

/// Snapshot of both temperature sources.
struct TemperatureReadings {
    float    ds18b20;            ///< External sensor (°C), high accuracy
    float    adsInternal;        ///< ADS1232 internal (°C), relative only
    bool     ds18b20Valid;       ///< false if sensor not detected or CRC error
    bool     adsInternalValid;   ///< false if read failed
    uint32_t timestampMs;        ///< millis() at time of reading
};

class TemperatureManager {
public:
    TemperatureManager();

    /// Initialise the OneWire bus and scan for a DS18B20.
    /// @param pin  GPIO connected to the DS18B20 data line (needs 4.7 kΩ pull-up).
    void begin(gpio_num_t pin);

    /// Read the DS18B20 (blocking, ≈ 750 ms for 12-bit conversion).
    /// @return Temperature in °C, or NAN on failure.
    float readDS18B20();

    /// Store the latest ADS1232 internal temperature (called from measurement
    /// task after using ADS1232Driver::readInternalTemperature).
    void setADSInternalTemp(float celsius);

    /// Get a full snapshot of the most recent readings from both sources.
    TemperatureReadings latest() const;

    /// Compute the weight compensation offset (grams) caused by a temperature
    /// change from the baseline.
    ///
    ///   ΔW = ΔT × capacity_g × TC_pct / 100
    ///
    /// @param baselineTemp  Temperature at last tare (°C).
    /// @return  Offset to *subtract* from the measured weight (grams).
    float compensationOffset(float baselineTemp) const;

    /// @return true if a DS18B20 was found on the bus.
    bool ds18b20Available() const { return _ds18b20Found; }

private:
    float    _lastDS18B20;
    float    _lastADSInternal;
    bool     _ds18b20Found;
    bool     _adsInternalValid;
    uint32_t _lastTimestamp;

    // Opaque pointers — the OneWire / DallasTemperature objects are owned
    // by the .cpp file so we don't leak library headers into every TU.
    void*    _oneWire;
    void*    _dallas;
};
