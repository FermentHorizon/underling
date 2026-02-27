// ============================================================================
//  calibration_store.h — NVS-Backed Calibration Persistence
// ============================================================================
//
//  Stores tare offset, scale factor, and temperature baseline in the ESP32's
//  Non-Volatile Storage (NVS) flash partition so they survive power cycles.
//
// ============================================================================
#pragma once

#include <Arduino.h>

/// All the values that define the system's calibration state.
struct CalibrationData {
    float tareOffset;       ///< Raw ADC value at zero load (tare point)
    float scaleFactor;      ///< ADC counts per gram (set during calibration)
    float tempBaseline;     ///< Ambient temperature (°C) at last tare
    bool  valid;            ///< true if this struct holds meaningful data
};

class CalibrationStore {
public:
    CalibrationStore();

    /// Open the NVS namespace.  Must be called once during setup().
    /// @return true on success.
    bool begin();

    /// Write all calibration fields to NVS.
    bool save(const CalibrationData& data);

    /// Load all calibration fields from NVS.
    /// Sets data.valid = false if no saved data exists.
    bool load(CalibrationData& data);

    /// Erase all calibration keys (factory reset).
    bool clear();

    // --- Convenience: save individual fields without touching the others ---
    bool saveTareOffset(float offset);
    bool saveScaleFactor(float factor);
    bool saveTempBaseline(float temp);

private:
    bool _ready;
};
