/**
 * ESP32 – QMC5883L Compass + REYAX RY725AI GNSS
 *
 * Wiring
 * ──────────────────────────────────────────────
 * QMC5883L (I2C)
 *   VCC  → 3.3 V
 *   GND  → GND
 *   SDA  → GPIO 21
 *   SCL  → GPIO 22
 *
 * REYAX RY725AI (UART – 9600 baud, NMEA output)
 *   VCC  → 3.3 V
 *   GND  → GND
 *   TX   → GPIO 16  (ESP32 RX2)
 *   RX   → GPIO 17  (ESP32 TX2)
 */
#include <stdio.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/i2c.h"

#include "esp_log.h"

// ── QMC5883L ──────────────────────────────────────────────────────────────────
#define QMC5883L_ADDR  0x0D
#define QMC_REG_XOUT_L 0x00
#define QMC_REG_STATUS 0x06
#define QMC_REG_CFG1   0x09
#define QMC_REG_CFG2   0x0A
#define QMC_REG_SR     0x0B

#define COMPASS_OFFSET_FROM_ENCLOSURE 0.0f

// ── GNSS ──────────────────────────────────────────────────────────────────────
#define GNSS_SERIAL  Serial2
#define GNSS_BAUD    9600
#define GNSS_RX_PIN  16
#define GNSS_TX_PIN  17

#define NMEA_BUF_SIZE 128
#define PRINT_INTERVAL_MS 500

// portfinder
WaterAccessFinder finder;
finder.loadFromArray(WATER_ACCESS_POINTS, WATER_ACCESS_COUNT);

// =============================================================================
//  QMC5883L
// =============================================================================

static bool qmcWrite(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(QMC5883L_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool qmcInit() {
    if (!qmcWrite(QMC_REG_SR,   0x01)) return false;
    if (!qmcWrite(QMC_REG_CFG2, 0x00)) return false;
    if (!qmcWrite(QMC_REG_CFG1, 0x01)) return false; // Continuous | 10 Hz | 2G | OSR 512
    return true;
}

static bool qmcRead(int16_t &x, int16_t &y, int16_t &z) {
    Wire.beginTransmission(QMC5883L_ADDR);
    Wire.write(QMC_REG_STATUS);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(QMC5883L_ADDR, (uint8_t)1);
    if (!Wire.available()) return false;
    if (!(Wire.read() & 0x01)) return false; // DRDY not set

    Wire.beginTransmission(QMC5883L_ADDR);
    Wire.write(QMC_REG_XOUT_L);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(QMC5883L_ADDR, (uint8_t)6) != 6) return false;

    uint8_t buf[6];
    for (int i = 0; i < 6; i++) buf[i] = Wire.read();

    x = (int16_t)((buf[1] << 8) | buf[0]);
    y = (int16_t)((buf[3] << 8) | buf[2]);
    z = (int16_t)((buf[5] << 8) | buf[4]);
    return true;
}

// =============================================================================
//  NMEA helpers
// =============================================================================

static bool nmeaChecksum(const char *s) {
    if (s[0] != '$') return false;
    const char *p = s + 1;
    uint8_t calc = 0;
    while (*p && *p != '*') calc ^= (uint8_t)(*p++);
    if (*p != '*') return false;
    return calc == (uint8_t)strtol(p + 1, nullptr, 16);
}

static void nmeaField(const char *s, int index, char *out, size_t outLen) {
    out[0] = '\0';
    int field = 0;
    const char *p = s;
    while (*p && field < index) { if (*p++ == ',') field++; }
    if (!*p || field != index) return;
    size_t i = 0;
    while (*p && *p != ',' && *p != '*' && i < outLen - 1) out[i++] = *p++;
    out[i] = '\0';
}

static double nmeaToDecimal(const char *value, char hemi) {
    if (!value[0]) return 0.0;
    double raw = atof(value);
    int deg = (int)(raw / 100);
    double dec = deg + (raw - deg * 100.0) / 60.0;
    if (hemi == 'S' || hemi == 'W') dec = -dec;
    return dec;
}

// =============================================================================
//  Global state
// =============================================================================

static char    nmeaBuf[NMEA_BUF_SIZE];
static uint8_t nmeaIdx = 0;

static double  gLat = 0.0, gLon = 0.0;
static bool    gFix = false;

static uint32_t lastPrintMs = 0;

// =============================================================================
//  Setup / Loop
// =============================================================================

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

    Wire.begin();
    Wire.setClock(400000);
    qmcInit();

    GNSS_SERIAL.begin(GNSS_BAUD, SERIAL_8N1, GNSS_RX_PIN, GNSS_TX_PIN);

    lastPrintMs = millis();
}

void loop() {
    // ── Ingest NMEA sentences ──────────────────────────────────────────────
    while (GNSS_SERIAL.available()) {
        char c = (char)GNSS_SERIAL.read();

        if (c == '$') {
            nmeaIdx = 0;
            nmeaBuf[nmeaIdx++] = c;
        } else if (nmeaIdx > 0) {
            if (c == '\n') {
                nmeaBuf[nmeaIdx] = '\0';
                nmeaIdx = 0;

                if (nmeaChecksum(nmeaBuf)) {
                    const char *type = nmeaBuf + 3; // skip $GP / $GN talker prefix
                    if (strncmp(type, "RMC,", 4) == 0) {
                        char status[2], latStr[12], latDir[2], lonStr[12], lonDir[2];
                        nmeaField(nmeaBuf, 2, status,  sizeof(status));
                        nmeaField(nmeaBuf, 3, latStr,  sizeof(latStr));
                        nmeaField(nmeaBuf, 4, latDir,  sizeof(latDir));
                        nmeaField(nmeaBuf, 5, lonStr,  sizeof(lonStr));
                        nmeaField(nmeaBuf, 6, lonDir,  sizeof(lonDir));
                        gFix = (status[0] == 'A');
                        if (gFix) {
                            gLat = nmeaToDecimal(latStr, latDir[0]);
                            gLon = nmeaToDecimal(lonStr, lonDir[0]);
                        }
                    }
                }
            } else if (c != '\r') {
                if (nmeaIdx < NMEA_BUF_SIZE - 1) nmeaBuf[nmeaIdx++] = c;
                else nmeaIdx = 0;
            }
        }
    }

    // ── Print at interval ──────────────────────────────────────────────────
    uint32_t now = millis();
    if (now - lastPrintMs >= PRINT_INTERVAL_MS) {
        lastPrintMs = now;

        // Compass: XY heading in radians [-π, π]
        int16_t cx, cy, cz;
        if (qmcRead(cx, cy, cz)) {
            float heading = atan2f((float)cy, (float)cx) + COMPASS_OFFSET_FROM_ENCLOSURE;
            Serial.printf("Heading (XY): %.4f rad (%.1f°) |  ", heading, heading * 180.0f / M_PI);
        } else {
            Serial.print("Heading (XY): --        |  ");
        }

        // GNSS
        if (gFix) {
            Serial.printf("Lat: %.6f  Lon: %.6f\n", gLat, gLon);
        } else {
            Serial.println("Lat: --        Lon: --");
        }

        // Find bearing to nearest port
        NearestResult r = finder.findNearest(myLat, myLon);

        Serial.println("Found a %s %dkm away at a bearing of %.1f°", r.type, r.distanceKm, r.bearingDeg);
    }
}