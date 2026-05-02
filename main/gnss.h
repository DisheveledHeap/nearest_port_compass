#ifndef GNSS_H
#define GNSS_H

#include "common.h"

void gnss_init(gpio_num_t tx, gpio_num_t rx);
void gnss_update(sensor_data_t *data);

#endif