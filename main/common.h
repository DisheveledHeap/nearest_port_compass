#ifndef COMMON_H
#define COMMON_H

#include <math.h>
#include <stdint.h>

#define DEG2RAD (M_PI / 180.0)
#define RAD2DEG (180.0 / M_PI)

typedef struct {
    double latitude;
    double longitude;
    float heading;
} sensor_data_t;

#endif