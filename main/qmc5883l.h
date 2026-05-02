#ifndef QMC5883L_H
#define QMC5883L_H

#include "common.h"

void qmc5883l_init(void);
void qmc5883l_update(sensor_data_t *data);

#endif