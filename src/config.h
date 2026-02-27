// ============================================================================
//  config.h — Central Configuration for Underling Transpiration Monitor
// ============================================================================
//
//  Every tuneable constant and every GPIO assignment lives here.
//  Nothing is scattered — change it once, change it everywhere.
//
// ============================================================================
#pragma once

#include <Arduino.h>

// ============================================================================
//  Firmware Identity
// ============================================================================
#define FW_NAME    "Underling"
#define FW_VERSION "2.0.0"

// ============================================================================
//  GPIO Pin Assignments — ESP32 WROOM-32
// ============================================================================
namespace pins {

    // --- ADS1232 Interface ---
    constexpr gpio_num_t ADS_SCLK  = GPIO_NUM_18;   // Serial clock        (OUTPUT)
    constexpr gpio_num_t ADS_DOUT  = GPIO_NUM_19;   // Data ready / data   (INPUT)
    constexpr gpio_num_t ADS_PDWN  = GPIO_NUM_23;   // Power-down control  (OUTPUT)
    constexpr gpio_num_t ADS_A0    = GPIO_NUM_4;     // Channel mux bit 0   (OUTPUT)
    constexpr gpio_num_t ADS_TEMP  = GPIO_NUM_27;    // Temp sensor select  (OUTPUT)
    constexpr gpio_num_t ADS_CLKIN = GPIO_NUM_25;    // 1 MHz system clock  (OUTPUT via LEDC)

    // --- DS18B20 Temperature Sensor ---
    constexpr gpio_num_t DS18B20_DATA = GPIO_NUM_26; // OneWire data (4.7 kOhm pull-up to 3V3)

    // --- Tare Button (active-LOW, internal pull-up enabled) ---
    constexpr gpio_num_t TARE_BUTTON = GPIO_NUM_32;

}  // namespace pins

// ============================================================================
//  ADS1232 ADC Configuration
// ============================================================================
namespace ads_cfg {

    constexpr uint8_t  GAIN              = 128;
    constexpr uint8_t  DATA_RATE_SPS     = 10;
    constexpr uint32_t CLKIN_FREQ_HZ     = 1000000;
    constexpr uint8_t  CLKIN_LEDC_CH     = 0;
    constexpr uint8_t  CLKIN_LEDC_RES    = 1;
    constexpr float    VREF_V            = 5.0f;
    constexpr uint32_t READ_TIMEOUT_MS   = 250;
    constexpr uint32_t PDWN_PULSE_US     = 50;
    constexpr uint32_t SETTLE_MS         = 420;
    constexpr uint32_t POWERUP_MS        = 1000;
    constexpr uint8_t  DISCARD_AFTER_MUX = 2;

}  // namespace ads_cfg

// ============================================================================
//  Measurement Configuration
// ============================================================================
//  40 kg beam-type load cell.  Trimmed-mean ADC averaging (50 samples,
//  discard top/bottom 20%).  No additional filtering — raw calibrated
//  weight is printed to serial every 30 s.
// ============================================================================
namespace meas_cfg {

    constexpr uint8_t  SAMPLES_PER_READ  = 50;       // ADC samples per measurement (~5 s at 10 SPS)
    constexpr uint8_t  TARE_SAMPLES      = 50;       // Samples for tare
    constexpr uint32_t INTERVAL_MS       = 30000;    // 30 s between measurements
    constexpr uint8_t  TEMP_EVERY_N      = 10;       // Read temperature every 10 cycles (~5 min)
    constexpr uint8_t  STABILISE_READS   = 5;        // Discarded reads on boot

    // --- Temperature compensation (disabled) ---
    //  Set LOAD_CELL_TC_PCT_PER_C to a non-zero value to enable.
    //  Was 0.002 but overcorrected -- needs a proper thermal sweep.
    constexpr float    LOAD_CELL_CAPACITY_G   = 40000.0f;
    constexpr float    LOAD_CELL_TC_PCT_PER_C = 0.0f;

}  // namespace meas_cfg

// ============================================================================
//  NVS (Non-Volatile Storage) Keys
// ============================================================================
namespace nvs_cfg {

    constexpr const char* NAMESPACE     = "underling";
    constexpr const char* KEY_TARE      = "tare";
    constexpr const char* KEY_SCALE     = "scale";
    constexpr const char* KEY_TEMP_BASE = "tbase";

}  // namespace nvs_cfg
