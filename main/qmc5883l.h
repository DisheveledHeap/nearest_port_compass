#ifndef QMC5883L_H
#define QMC5883L_H

#include "common.h"

void qmc5883l_init(gpio_num_t sda, gpio_num_t scl);
void qmc5883l_update(sensor_data_t *data);

#endif