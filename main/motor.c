#include <math.h>
#include <stdint.h>
#include "common.h"
#include "motor.h"

#define SCALE   10000.0f

/*
 * lat/lon are int32_t scaled by 10000 (e.g., 45.1234° -> 451234)
 * heading_deg is a float (degrees, magnetic north reference)
 *
 * Returns smallest signed turn angle in degrees (float):
 *   >0  -> turn right
 *   <0  -> turn left
 */
float turn_to_face(int32_t lat1_i, int32_t lon1_i,
                   int32_t lat2_i, int32_t lon2_i,
                   float heading_deg)
{
    // Convert fixed-point to degrees
    float lat1 = lat1_i / SCALE;
    float lon1 = lon1_i / SCALE;
    float lat2 = lat2_i / SCALE;
    float lon2 = lon2_i / SCALE;

    // Convert to radians
    float φ1 = lat1 * DEG2RAD;
    float φ2 = lat2 * DEG2RAD;
    float λ1 = lon1 * DEG2RAD;
    float λ2 = lon2 * DEG2RAD;

    float dλ = λ2 - λ1;

    // Initial bearing calculation
    float y = sinf(dλ) * cosf(φ2);
    float x = cosf(φ1) * sinf(φ2) -
              sinf(φ1) * cosf(φ2) * cosf(dλ);

    float bearing = atan2f(y, x) * RAD2DEG;

    // Normalize bearing to [0, 360)
    bearing = fmodf((bearing + 360.0f), 360.0f);

    // Compute required turn
    float turn = bearing - heading_deg;

    // Normalize to [-180, 180]
    while (turn > 180.0f) turn -= 360.0f;
    while (turn < -180.0f) turn += 360.0f;

    return turn;
}