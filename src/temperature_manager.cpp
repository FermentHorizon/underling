// ============================================================================
//  temperature_manager.cpp — Dual Temperature Sensor Implementation
// ============================================================================
#include "temperature_manager.h"
#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// ---------------------------------------------------------------------------
//  Construction
// ---------------------------------------------------------------------------

TemperatureManager::TemperatureManager()
    : _lastDS18B20(NAN)
    , _lastADSInternal(NAN)
    , _ds18b20Found(false)
    , _adsInternalValid(false)
    , _lastTimestamp(0)
    , _oneWire(nullptr)
    , _dallas(nullptr)
{}

// ---------------------------------------------------------------------------
//  Initialisation
// ---------------------------------------------------------------------------

void TemperatureManager::begin(gpio_num_t pin)
{
    // Allocate the OneWire bus and DallasTemperature on the heap so the
    // header doesn't need to include library-specific types.
    auto* ow  = new OneWire(pin);
    auto* dt  = new DallasTemperature(ow);
    _oneWire  = ow;
    _dallas   = dt;

    dt->begin();

    uint8_t count = dt->getDeviceCount();
    _ds18b20Found = (count > 0);

    if (_ds18b20Found) {
        // Use 12-bit resolution (0.0625 °C, ≈ 750 ms conversion).
        dt->setResolution(12);
        dt->setWaitForConversion(true);   // blocking read

        DeviceAddress addr;
        if (dt->getAddress(addr, 0)) {
            Serial.printf("[TEMP] DS18B20 found — address: "
                          "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
                          addr[0], addr[1], addr[2], addr[3],
                          addr[4], addr[5], addr[6], addr[7]);
        }
    } else {
        Serial.println(F("[TEMP] WARNING — No DS18B20 detected on bus."));
        Serial.println(F("[TEMP]   Check wiring and 4.7 kΩ pull-up resistor."));
    }
}

// ---------------------------------------------------------------------------
//  DS18B20 Read
// ---------------------------------------------------------------------------

float TemperatureManager::readDS18B20()
{
    if (!_ds18b20Found || _dallas == nullptr) return NAN;

    auto* dt = static_cast<DallasTemperature*>(_dallas);
    dt->requestTemperatures();

    float t = dt->getTempCByIndex(0);

    // The library returns DEVICE_DISCONNECTED_C (−127) on error.
    if (t <= -100.0f) {
        _lastDS18B20 = NAN;
        return NAN;
    }

    _lastDS18B20    = t;
    _lastTimestamp  = millis();
    return t;
}

// ---------------------------------------------------------------------------
//  ADS1232 Internal Temperature (set externally)
// ---------------------------------------------------------------------------

void TemperatureManager::setADSInternalTemp(float celsius)
{
    _lastADSInternal  = celsius;
    _adsInternalValid = !isnan(celsius);
    _lastTimestamp     = millis();
}

// ---------------------------------------------------------------------------
//  Latest Readings
// ---------------------------------------------------------------------------

TemperatureReadings TemperatureManager::latest() const
{
    TemperatureReadings r;
    r.ds18b20         = _lastDS18B20;
    r.adsInternal     = _lastADSInternal;
    r.ds18b20Valid    = !isnan(_lastDS18B20);
    r.adsInternalValid = _adsInternalValid;
    r.timestampMs     = _lastTimestamp;
    return r;
}

// ---------------------------------------------------------------------------
//  Temperature Compensation
// ---------------------------------------------------------------------------

float TemperatureManager::compensationOffset(float baselineTemp) const
{
    // Use the DS18B20 as the authoritative temperature source.
    // Fall back to the ADS1232 internal sensor if the DS18B20 is unavailable.
    float currentTemp;

    if (!isnan(_lastDS18B20)) {
        currentTemp = _lastDS18B20;
    } else if (_adsInternalValid) {
        currentTemp = _lastADSInternal;
    } else {
        return 0.0f;  // No temperature data — no compensation.
    }

    //  ΔW = ΔT × capacity × TC%
    //
    //  Example with defaults:
    //    ΔT = 5 °C,  capacity = 40 000 g,  TC = 0.002 %/°C
    //    ΔW = 5 × 40000 × 0.00002 = 4.0 g
    //
    //  This offset is *subtracted* from the measured weight to remove the
    //  thermal artifact.

    float deltaT = currentTemp - baselineTemp;
    return deltaT * meas_cfg::LOAD_CELL_CAPACITY_G
                   * (meas_cfg::LOAD_CELL_TC_PCT_PER_C / 100.0f);
}
