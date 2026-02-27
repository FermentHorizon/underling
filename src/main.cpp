// ============================================================================
//  main.cpp — Underling Plant Transpiration Monitor
// ============================================================================
//
//  Firmware with ClickHouse integration.  Single FreeRTOS task on Core 1.
//
//  Pipeline (every 30 s):
//    1. Read ADC (50-sample trimmed mean)
//    2. Apply calibration:  weight = (raw - tare) / scale
//    3. Apply temperature compensation (disabled, coeff = 0)
//    4. Print one line to serial
//    5. POST row to ClickHouse via HTTP
//
//  Commands via serial: TARE | CAL <grams>
//  Tare button on GPIO 32 (active-LOW).
//
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "config.h"
#include "ads1232_driver.h"
#include "calibration_store.h"
#include "temperature_manager.h"

// ---- Global objects -------------------------------------------------------

static ADS1232Driver       adc;
static CalibrationStore    calStore;
static TemperatureManager  tempMgr;
static CalibrationData     calData;

// ---- Flags (set from button, consumed by measurement loop) ----------------

static volatile bool  gTareRequested   = false;
static volatile bool  gCalRequested    = false;
static volatile float gCalKnownWeightG = 0.0f;

// ============================================================================
//  Forward Declarations
// ============================================================================

static void measurementTask(void* param);
static void printBanner();
static void performTare();
static void performCalibration(float knownWeightG);
static bool connectWiFi();
static void syncNTP();
static bool postToClickHouse(float weightG, float tempC, uint32_t uptimeS);

// ============================================================================
//  Setup
// ============================================================================

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000) { delay(10); }
    delay(500);

    printBanner();

    // ---- NVS ----
    Serial.print(F("[INIT] Calibration store ........... "));
    calStore.begin();
    Serial.println(F("OK"));

    // ---- ADS1232 CLKIN (1 MHz from LEDC) ----
    Serial.print(F("[INIT] ADS1232 CLKIN (1 MHz) ....... "));
    ledcSetup(ads_cfg::CLKIN_LEDC_CH, ads_cfg::CLKIN_FREQ_HZ, ads_cfg::CLKIN_LEDC_RES);
    ledcAttachPin(pins::ADS_CLKIN, ads_cfg::CLKIN_LEDC_CH);
    ledcWrite(ads_cfg::CLKIN_LEDC_CH, 1);
    delay(50);
    Serial.println(F("OK (GPIO 25)"));

    // ---- ADS1232 ----
    Serial.print(F("[INIT] ADS1232 ADC ................. "));
    adc.begin(pins::ADS_SCLK, pins::ADS_DOUT, pins::ADS_PDWN,
              pins::ADS_A0,    pins::ADS_TEMP);
    adc.powerUp();
    Serial.println(F("OK"));

    // ---- DS18B20 ----
    Serial.print(F("[INIT] DS18B20 sensor .............. "));
    tempMgr.begin(pins::DS18B20_DATA);
    Serial.println(tempMgr.ds18b20Available() ? F("OK") : F("NOT FOUND"));

    // ---- Tare Button ----
    pinMode(pins::TARE_BUTTON, INPUT_PULLUP);

    // ---- WiFi ----
    Serial.print(F("[INIT] WiFi ........................ "));
    if (connectWiFi()) {
        Serial.printf("OK (%s, %s)\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.SSID().c_str());
        syncNTP();
    } else {
        Serial.println(F("FAILED (continuing offline)"));
    }

    // ---- Load or create calibration (NEVER auto-tare) ----
    if (calStore.load(calData) && calData.valid) {
        Serial.printf("[INIT] Calibration loaded from NVS:\n"
                      "       tare   = %.1f\n"
                      "       scale  = %.6f counts/g\n"
                      "       T_base = %.2f C\n",
                      calData.tareOffset, calData.scaleFactor,
                      calData.tempBaseline);
    } else {
        Serial.println(F("[INIT] No calibration in NVS -- send TARE to zero."));
        calData.tareOffset   = 0.0f;
        calData.scaleFactor  = 1.0f;
        calData.tempBaseline = 0.0f;
        calData.valid        = true;
    }

    // ---- Launch measurement task ----
    xTaskCreatePinnedToCore(measurementTask, "Meas", 8192, nullptr, 2, nullptr, 1);

    Serial.println();
    Serial.println(F("============================================================"));
    Serial.printf(   "  Ready | %lu s cycle | %u samples\n",
                   meas_cfg::INTERVAL_MS / 1000, meas_cfg::SAMPLES_PER_READ);
    Serial.println(F("============================================================"));
    Serial.println();
}

void loop()
{
    vTaskDelay(portMAX_DELAY);
}

// ============================================================================
//  Measurement Task  (Core 1)
// ============================================================================

static void measurementTask(void* /*param*/)
{
    TickType_t lastWake = xTaskGetTickCount();

    uint32_t readingNum  = 0;
    float    lastTempExt = NAN;
    float    lastTempInt = NAN;
    float    lastCompG   = 0.0f;

    // Stabilise: discard first few readings after power-up.
    Serial.println(F("[MEAS] Stabilising ADC ..."));
    for (uint8_t i = 0; i < meas_cfg::STABILISE_READS; i++) {
        int32_t dummy;
        adc.read(dummy, false);
    }
    Serial.println(F("[MEAS] Stabilisation complete."));

    // ------- Main loop -----------------------------------------------------

    for (;;) {
        // ---- Serial commands (TARE / CAL <grams>) ----
        while (Serial.available()) {
            static char cmdBuf[64];
            static uint8_t cmdLen = 0;
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (cmdLen > 0) {
                    cmdBuf[cmdLen] = '\0';
                    if (strncasecmp(cmdBuf, "TARE", 4) == 0) {
                        Serial.println(F("[CMD] Tare requested."));
                        gTareRequested = true;
                    } else if (strncasecmp(cmdBuf, "CAL ", 4) == 0) {
                        float w = atof(cmdBuf + 4);
                        if (w > 0.0f) {
                            Serial.printf("[CMD] Calibrate: %.2f g\n", w);
                            gCalKnownWeightG = w;
                            gCalRequested = true;
                        } else {
                            Serial.println(F("[CMD] Usage: CAL <grams>"));
                        }
                    } else {
                        Serial.println(F("[CMD] Unknown. Commands: TARE | CAL <grams>"));
                    }
                    cmdLen = 0;
                }
            } else if (cmdLen < sizeof(cmdBuf) - 1) {
                cmdBuf[cmdLen++] = c;
            }
        }

        // ---- Tare button (active-LOW, debounce) ----
        static bool tareButtonConnected = (digitalRead(pins::TARE_BUTTON) == HIGH);
        if (tareButtonConnected && digitalRead(pins::TARE_BUTTON) == LOW) {
            delay(50);
            if (digitalRead(pins::TARE_BUTTON) == LOW) {
                Serial.println(F("[MEAS] Tare button pressed."));
                gTareRequested = true;
                while (digitalRead(pins::TARE_BUTTON) == LOW) { delay(10); }
            }
        }

        // ---- Handle tare ----
        if (gTareRequested) {
            gTareRequested = false;
            performTare();
        }

        // ---- Handle calibration ----
        if (gCalRequested) {
            gCalRequested = false;
            performCalibration(gCalKnownWeightG);
        }

        // ---- ADC read (trimmed mean, 50 samples, ~5 s) ----
        float rawAvg = 0.0f;
        ADS1232Error err = adc.readAverage(rawAvg, meas_cfg::SAMPLES_PER_READ, false);

        if (err != ADS1232Error::OK) {
            Serial.printf("[MEAS] ERROR: %s\n", ADS1232Driver::errorString(err));
            vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(meas_cfg::INTERVAL_MS));
            continue;
        }

        // ---- Periodic temperature (~5 min) ----
        if (readingNum % meas_cfg::TEMP_EVERY_N == 0) {
            float ds = tempMgr.readDS18B20();
            if (!isnan(ds)) lastTempExt = ds;

            float intTemp;
            if (adc.readInternalTemperature(intTemp) == ADS1232Error::OK) {
                lastTempInt = intTemp;
                tempMgr.setADSInternalTemp(intTemp);
            }
        }

        // ---- Calibration ----
        float weight = (rawAvg - calData.tareOffset) / calData.scaleFactor;

        // ---- Temperature compensation (disabled when coeff = 0) ----
        lastCompG = tempMgr.compensationOffset(calData.tempBaseline);
        weight   -= lastCompG;

        // ---- Serial output ----
        uint32_t now = millis();
        Serial.printf("[%7lu] W: %8.1f g",
                      now / 1000, weight);
        if (!isnan(lastTempExt)) Serial.printf(" | T: %.1fC", lastTempExt);

        // ---- ClickHouse POST ----
        if (WiFi.status() == WL_CONNECTED) {
            bool ok = postToClickHouse(weight, lastTempExt, now / 1000);
            Serial.printf(" | CH: %s", ok ? "OK" : "FAIL");
        } else {
            // Try reconnecting every cycle
            if (connectWiFi()) {
                Serial.print(F(" | WiFi reconnected"));
            }
        }

        Serial.println();

        readingNum++;
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(meas_cfg::INTERVAL_MS));
    }
}

// ============================================================================
//  Tare & Calibration
// ============================================================================

static void performTare()
{
    Serial.println(F("[TARE] Starting tare ..."));

    float rawAvg;
    ADS1232Error err = adc.readAverage(rawAvg, meas_cfg::TARE_SAMPLES, false);

    if (err != ADS1232Error::OK) {
        Serial.printf("[TARE] FAILED: %s\n", ADS1232Driver::errorString(err));
        return;
    }

    float tBase = 25.0f;
    float ds = tempMgr.readDS18B20();
    if (!isnan(ds)) tBase = ds;

    calData.tareOffset   = rawAvg;
    calData.tempBaseline = tBase;
    if (calData.scaleFactor == 0.0f) calData.scaleFactor = 1.0f;
    calData.valid = true;

    calStore.save(calData);

    Serial.printf("[TARE] Done -- offset: %.1f  T_base: %.2f C\n", rawAvg, tBase);
}

static void performCalibration(float knownWeightG)
{
    Serial.printf("[CAL] Calibrating with %.2f g ...\n", knownWeightG);

    float rawAvg;
    ADS1232Error err = adc.readAverage(rawAvg, meas_cfg::TARE_SAMPLES, false);

    if (err != ADS1232Error::OK) {
        Serial.printf("[CAL] FAILED: %s\n", ADS1232Driver::errorString(err));
        return;
    }

    float delta = rawAvg - calData.tareOffset;

    if (fabsf(delta) < 100.0f) {
        Serial.println(F("[CAL] FAILED -- ADC delta too small. Is weight on the cell?"));
        return;
    }

    calData.scaleFactor = delta / knownWeightG;
    calStore.saveScaleFactor(calData.scaleFactor);

    Serial.printf("[CAL] Done -- scale: %.6f counts/g\n", calData.scaleFactor);
}

// ============================================================================
//  WiFi & ClickHouse
// ============================================================================

static bool connectWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // Scan for available networks
    int n = WiFi.scanNetworks();
    if (n <= 0) {
        Serial.println(F("[WIFI] No networks found"));
        WiFi.scanDelete();
        return false;
    }

    // Find the strongest RSSI among our configured SSIDs
    const char* bestSSID = nullptr;
    int32_t     bestRSSI = -999;

    for (int i = 0; i < n; i++) {
        for (size_t s = 0; s < net_cfg::WIFI_SSID_COUNT; s++) {
            if (WiFi.SSID(i) == net_cfg::WIFI_SSIDS[s] &&
                WiFi.RSSI(i) > bestRSSI) {
                bestSSID = net_cfg::WIFI_SSIDS[s];
                bestRSSI = WiFi.RSSI(i);
            }
        }
    }
    WiFi.scanDelete();

    if (!bestSSID) {
        Serial.println(F("[WIFI] No matching SSID found"));
        return false;
    }

    Serial.printf("[WIFI] Connecting to %s (RSSI %d dBm)\n", bestSSID, bestRSSI);
    WiFi.begin(bestSSID, net_cfg::WIFI_PASS);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           (millis() - start) < net_cfg::WIFI_TIMEOUT_MS) {
        delay(250);
    }
    return WiFi.status() == WL_CONNECTED;
}

static void syncNTP()
{
    configTzTime("UTC", "pool.ntp.org", "time.nist.gov");
    Serial.print(F("[INIT] NTP sync .................... "));
    struct tm t;
    if (getLocalTime(&t, 5000)) {
        Serial.printf("OK (%04d-%02d-%02d %02d:%02d:%02d UTC)\n",
                      t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                      t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        Serial.println(F("TIMEOUT (will use uptime)"));
    }
}

static bool postToClickHouse(float weightG, float tempC, uint32_t uptimeS)
{
    // Build timestamp from NTP or fall back to a fixed epoch + uptime
    char tsBuf[32];
    struct tm t;
    if (getLocalTime(&t, 0)) {
        strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%d %H:%M:%S", &t);
    } else {
        snprintf(tsBuf, sizeof(tsBuf), "1970-01-01 00:00:%02lu", uptimeS % 60);
    }

    // Build URL with query param
    char url[256];
    snprintf(url, sizeof(url),
             "http://%s:%u/?user=%s&password=%s"
             "&query=INSERT%%20INTO%%20%s.%s%%20FORMAT%%20JSONEachRow",
             net_cfg::CH_HOST, net_cfg::CH_PORT,
             net_cfg::CH_USER, net_cfg::CH_PASS,
             net_cfg::CH_DATABASE, net_cfg::CH_TABLE);

    // Build JSON body
    char body[256];
    if (isnan(tempC)) {
        snprintf(body, sizeof(body),
                 "{\"timestamp\":\"%s\",\"weight_g\":%.1f,"
                 "\"uptime_s\":%lu,\"device_id\":\"underling\"}",
                 tsBuf, weightG, (unsigned long)uptimeS);
    } else {
        snprintf(body, sizeof(body),
                 "{\"timestamp\":\"%s\",\"weight_g\":%.1f,"
                 "\"temperature_c\":%.1f,\"uptime_s\":%lu,"
                 "\"device_id\":\"underling\"}",
                 tsBuf, weightG, tempC, (unsigned long)uptimeS);
    }

    HTTPClient http;
    http.begin(url);
    http.setTimeout(net_cfg::HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST((uint8_t*)body, strlen(body));
    http.end();

    return (code == 200);
}

// ============================================================================
//  Boot Banner
// ============================================================================

static void printBanner()
{
    Serial.println();
    Serial.println(F("============================================================"));
    Serial.println(F("     _   _           _           _ _"));
    Serial.println(F("    | | | |_ __   __| | ___ _ __| (_)_ __   __ _"));
    Serial.println(F("    | | | | '_ \\ / _` |/ _ \\ '__| | | '_ \\ / _` |"));
    Serial.println(F("    | |_| | | | | (_| |  __/ |  | | | | | | (_| |"));
    Serial.println(F("     \\___/|_| |_|\\__,_|\\___|_|  |_|_|_| |_|\\__, |"));
    Serial.println(F("                                            |___/"));
    Serial.println(F("         Plant Transpiration Monitor"));
    Serial.printf(   "         Firmware v%s\n", FW_VERSION);
    Serial.println(F("============================================================"));
    Serial.println(F("  MCU:  ESP32 WROOM-32"));
    Serial.println(F("  ADC:  ADS1232 (24-bit, Gain 128, 10 SPS)"));
    Serial.println(F("  Cell: 40 kg beam-type load cell"));
    Serial.println(F("  Temp: DS18B20"));
    Serial.println(F("============================================================"));
    Serial.println();
}
