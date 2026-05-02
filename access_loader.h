/**
 * esp32_water_access_loader.h
 *
 * Companion to query_water_access.py
 *
 * Provides two usage modes:
 *
 *   MODE A — Header array (small datasets, no filesystem needed)
 *     #include "water_access.h"
 *     WaterAccessFinder finder;
 *     finder.loadFromArray(WATER_ACCESS_POINTS, WATER_ACCESS_COUNT);
 *
 *   MODE B — Binary file from LittleFS (larger datasets)
 *     Upload water_access.bin via the ESP32 LittleFS uploader, then:
 *     #include "esp32_water_access_loader.h"
 *     WaterAccessFinder finder;
 *     finder.loadFromFile("/water_access.bin");
 *
 * After loading, call:
 *     NearestResult r = finder.findNearest(myLat, myLon);
 *     // r.distanceKm, r.bearingDeg, r.type, r.hasFee, r.index
 *
 * Optional: filter by type before searching
 *     finder.findNearest(myLat, myLon, TYPE_MASK_SLIPWAY | TYPE_MASK_MARINA);
 */

#pragma once
#include <Arduino.h>
#include <math.h>
#include "LittleFS.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constants & types
// ─────────────────────────────────────────────────────────────────────────────

#define WA_SCALE        10000
#define WA_MAX_ENTRIES  4000

enum WaterAccessType : uint8_t {
    WAT_SLIPWAY      = 0,
    WAT_MARINA       = 1,
    WAT_FERRY        = 2,
    WAT_PIER         = 3,
    WAT_JETTY        = 4,
    WAT_HARBOUR      = 5,
    WAT_DOCK         = 6,
    WAT_FISHING      = 7,
    WAT_WATER_SPORTS = 8,
    WAT_ANCHORAGE    = 9,
    WAT_BERTH        = 10,
    WAT_SEAMARK      = 11,
    WAT_OTHER        = 12,
};

// Bitmask flags for type filtering in findNearest()
#define TYPE_MASK_ALL          0xFFFF
#define TYPE_MASK_SLIPWAY      (1 << WAT_SLIPWAY)
#define TYPE_MASK_MARINA       (1 << WAT_MARINA)
#define TYPE_MASK_FERRY        (1 << WAT_FERRY)
#define TYPE_MASK_PIER         (1 << WAT_PIER)
#define TYPE_MASK_JETTY        (1 << WAT_JETTY)
#define TYPE_MASK_HARBOUR      (1 << WAT_HARBOUR)
#define TYPE_MASK_DOCK         (1 << WAT_DOCK)
#define TYPE_MASK_FISHING      (1 << WAT_FISHING)
#define TYPE_MASK_WATER_SPORTS (1 << WAT_WATER_SPORTS)
#define TYPE_MASK_ANCHORAGE    (1 << WAT_ANCHORAGE)
#define TYPE_MASK_BERTH        (1 << WAT_BERTH)
#define TYPE_MASK_SEAMARK      (1 << WAT_SEAMARK)
#define TYPE_MASK_OTHER        (1 << WAT_OTHER)

const char* accessTypeName(WaterAccessType t) {
    switch (t) {
        case WAT_SLIPWAY:      return "Boat Ramp";
        case WAT_MARINA:       return "Marina";
        case WAT_FERRY:        return "Ferry Terminal";
        case WAT_PIER:         return "Pier";
        case WAT_JETTY:        return "Jetty";
        case WAT_HARBOUR:      return "Harbour";
        case WAT_DOCK:         return "Dock";
        case WAT_FISHING:      return "Fishing Access";
        case WAT_WATER_SPORTS: return "Water Sports";
        case WAT_ANCHORAGE:    return "Anchorage";
        case WAT_BERTH:        return "Berth";
        case WAT_SEAMARK:      return "Seamark";
        default:               return "Water Access";
    }
}

struct WaterAccessPoint {
    int32_t lat;    // degrees * WA_SCALE
    int32_t lon;    // degrees * WA_SCALE
    uint8_t type;   // WaterAccessType
    uint8_t flags;  // bit 0 = fee charged
};

struct NearestResult {
    float           distanceKm;
    float           bearingDeg;   // True-north bearing, 0–360°
    WaterAccessType type;
    bool            hasFee;
    uint16_t        index;
    bool            valid;
};


// ─────────────────────────────────────────────────────────────────────────────
// WaterAccessFinder
// ─────────────────────────────────────────────────────────────────────────────

class WaterAccessFinder {
public:
    WaterAccessFinder() : _entries(nullptr), _count(0), _ownsMemory(false) {}

    ~WaterAccessFinder() {
        if (_ownsMemory && _entries) free((void*)_entries);
    }

    // ── MODE A: point at an in-memory or PROGMEM-style array ─────────────────
    void loadFromArray(const WaterAccessPoint* arr, uint16_t count) {
        if (_ownsMemory && _entries) free((void*)_entries);
        _entries    = arr;
        _count      = count;
        _ownsMemory = false;
        Serial.printf("[WaterAccess] Loaded %u entries from array.\n", count);
    }

    // ── MODE B: load binary file from LittleFS ────────────────────────────────
    bool loadFromFile(const char* path = "/water_access.bin") {
        if (!LittleFS.begin(false)) {
            Serial.println("[WaterAccess] ERROR: LittleFS mount failed.");
            return false;
        }

        File f = LittleFS.open(path, "r");
        if (!f) {
            Serial.printf("[WaterAccess] ERROR: Cannot open %s\n", path);
            return false;
        }

        // Validate magic
        char magic[4];
        if (f.read((uint8_t*)magic, 4) != 4 ||
            magic[0] != 'W' || magic[1] != 'A' ||
            magic[2] != '_' || magic[3] != 'E') {
            Serial.println("[WaterAccess] ERROR: Invalid file magic.");
            f.close();
            return false;
        }

        uint16_t count = 0;
        if (f.read((uint8_t*)&count, 2) != 2 || count == 0 || count > WA_MAX_ENTRIES) {
            Serial.printf("[WaterAccess] ERROR: Bad entry count %u\n", count);
            f.close();
            return false;
        }

        WaterAccessPoint* buf = (WaterAccessPoint*)malloc(count * sizeof(WaterAccessPoint));
        if (!buf) {
            Serial.println("[WaterAccess] ERROR: malloc failed.");
            f.close();
            return false;
        }

        // Read field-by-field to avoid struct padding issues (10 bytes per entry)
        bool ok = true;
        for (uint16_t i = 0; i < count && ok; i++) {
            ok  = (f.read((uint8_t*)&buf[i].lat,   4) == 4);
            ok &= (f.read((uint8_t*)&buf[i].lon,   4) == 4);
            ok &= (f.read((uint8_t*)&buf[i].type,  1) == 1);
            ok &= (f.read((uint8_t*)&buf[i].flags, 1) == 1);
        }
        f.close();

        if (!ok) {
            Serial.println("[WaterAccess] ERROR: File truncated during read.");
            free(buf);
            return false;
        }

        if (_ownsMemory && _entries) free((void*)_entries);
        _entries    = buf;
        _count      = count;
        _ownsMemory = true;

        Serial.printf("[WaterAccess] Loaded %u entries from %s\n", count, path);
        return true;
    }

    // ── Find nearest access point, with optional type filter ─────────────────
    //
    // typeMask: OR together TYPE_MASK_* constants to restrict which types
    //           are considered. Defaults to TYPE_MASK_ALL.
    //
    // Example — find nearest public launch or marina, ignore fee-only berths:
    //   finder.findNearest(lat, lon, TYPE_MASK_SLIPWAY | TYPE_MASK_MARINA);
    //
    NearestResult findNearest(float myLat, float myLon,
                              uint16_t typeMask = TYPE_MASK_ALL) const {
        NearestResult result = {0, 0, WAT_OTHER, false, 0, false};

        if (!_entries || _count == 0) {
            Serial.println("[WaterAccess] No data loaded.");
            return result;
        }

        float minDist = 1e9f;

        for (uint16_t i = 0; i < _count; i++) {
            uint16_t bit = (1 << _entries[i].type);
            if (!(typeMask & bit)) continue;   // filtered out

            float wbLat = (float)_entries[i].lat / (float)WA_SCALE;
            float wbLon = (float)_entries[i].lon / (float)WA_SCALE;
            float d     = _haversineKm(myLat, myLon, wbLat, wbLon);

            if (d < minDist) {
                minDist           = d;
                result.distanceKm = d;
                result.bearingDeg = _bearingDeg(myLat, myLon, wbLat, wbLon);
                result.type       = (WaterAccessType)_entries[i].type;
                result.hasFee     = (_entries[i].flags & 0x01) != 0;
                result.index      = i;
                result.valid      = true;
            }
        }

        return result;
    }

    // ── Return lat/lon of a loaded entry (for display/logging) ───────────────
    void getCoords(uint16_t index, float& lat, float& lon) const {
        if (!_entries || index >= _count) { lat = lon = 0; return; }
        lat = (float)_entries[index].lat / (float)WA_SCALE;
        lon = (float)_entries[index].lon / (float)WA_SCALE;
    }

    uint16_t count() const { return _count; }

private:
    const WaterAccessPoint* _entries;
    uint16_t                _count;
    bool                    _ownsMemory;

    static float _haversineKm(float lat1, float lon1, float lat2, float lon2) {
        const float R = 6371.0f;
        float dLat = radians(lat2 - lat1);
        float dLon = radians(lon2 - lon1);
        float a = sinf(dLat / 2) * sinf(dLat / 2)
                + cosf(radians(lat1)) * cosf(radians(lat2))
                * sinf(dLon / 2) * sinf(dLon / 2);
        return 2.0f * R * asinf(sqrtf(a));
    }

    static float _bearingDeg(float lat1, float lon1, float lat2, float lon2) {
        float dLon = radians(lon2 - lon1);
        float y    = sinf(dLon) * cosf(radians(lat2));
        float x    = cosf(radians(lat1)) * sinf(radians(lat2))
                   - sinf(radians(lat1)) * cosf(radians(lat2)) * cosf(dLon);
        float bear = degrees(atan2f(y, x));
        return fmodf(bear + 360.0f, 360.0f);
    }
};