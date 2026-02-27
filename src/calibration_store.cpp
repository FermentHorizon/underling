// ============================================================================
//  calibration_store.cpp — NVS-Backed Calibration Persistence Implementation
// ============================================================================
#include "calibration_store.h"
#include "config.h"
#include <Preferences.h>

// We use the ESP32 Preferences library (wraps NVS) for type-safe, wear-levelled
// flash storage.  Each key is limited to 15 characters.

static Preferences prefs;

CalibrationStore::CalibrationStore()
    : _ready(false)
{}

bool CalibrationStore::begin()
{
    _ready = prefs.begin(nvs_cfg::NAMESPACE, false);   // false = read/write
    if (!_ready) {
        Serial.println(F("[NVS] ERROR — failed to open namespace."));
    }
    return _ready;
}

bool CalibrationStore::save(const CalibrationData& data)
{
    if (!_ready) return false;

    prefs.putFloat(nvs_cfg::KEY_TARE,      data.tareOffset);
    prefs.putFloat(nvs_cfg::KEY_SCALE,     data.scaleFactor);
    prefs.putFloat(nvs_cfg::KEY_TEMP_BASE, data.tempBaseline);

    Serial.printf("[NVS] Saved — tare: %.1f  scale: %.6f  T_base: %.2f °C\n",
                  data.tareOffset, data.scaleFactor, data.tempBaseline);
    return true;
}

bool CalibrationStore::load(CalibrationData& data)
{
    if (!_ready) {
        data.valid = false;
        return false;
    }

    // isKey() returns true only if the key has been written at least once.
    if (!prefs.isKey(nvs_cfg::KEY_TARE)) {
        data.valid = false;
        return false;
    }

    data.tareOffset   = prefs.getFloat(nvs_cfg::KEY_TARE,      0.0f);
    data.scaleFactor  = prefs.getFloat(nvs_cfg::KEY_SCALE,     1.0f);
    data.tempBaseline = prefs.getFloat(nvs_cfg::KEY_TEMP_BASE, 25.0f);
    data.valid        = true;

    return true;
}

bool CalibrationStore::clear()
{
    if (!_ready) return false;

    prefs.clear();
    Serial.println(F("[NVS] All calibration data erased."));
    return true;
}

// ---------------------------------------------------------------------------
//  Convenience: update a single field without clobbering the others
// ---------------------------------------------------------------------------

bool CalibrationStore::saveTareOffset(float offset)
{
    if (!_ready) return false;
    prefs.putFloat(nvs_cfg::KEY_TARE, offset);
    return true;
}

bool CalibrationStore::saveScaleFactor(float factor)
{
    if (!_ready) return false;
    prefs.putFloat(nvs_cfg::KEY_SCALE, factor);
    return true;
}

bool CalibrationStore::saveTempBaseline(float temp)
{
    if (!_ready) return false;
    prefs.putFloat(nvs_cfg::KEY_TEMP_BASE, temp);
    return true;
}
