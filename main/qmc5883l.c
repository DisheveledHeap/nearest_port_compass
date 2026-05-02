#include "qmc5883l.h"
#include "driver/i2c.h"
#include <math.h>

#define I2C_PORT I2C_NUM_0
#define ADDR 0x0D

static int16_t x_offset = 0;
static int16_t y_offset = 0;

static esp_err_t write_reg(uint8_t reg, uint8_t val) {
    uint8_t data[2] = {reg, val};
    return i2c_master_write_to_device(I2C_PORT, ADDR, data, 2, 100 / portTICK_PERIOD_MS);
}

static esp_err_t read_data(uint8_t reg, uint8_t *buf, size_t len) {
    return i2c_master_write_read_device(I2C_PORT, ADDR, &reg, 1, buf, len, 100 / portTICK_PERIOD_MS);
}

void qmc5883l_init(gpio_num_t sda, gpio_num_t scl) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000
    };

    i2c_param_config(I2C_NUM_0, &conf);
    i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);

    // sensor setup...
}

void qmc5883l_update(sensor_data_t *data) {
    uint8_t raw[6];

    if (read_data(0x00, raw, 6) != ESP_OK) return;

    int16_t x = (raw[1] << 8) | raw[0];
    int16_t y = (raw[3] << 8) | raw[2];

    x -= x_offset;
    y -= y_offset;

    float heading = atan2f((float)y, (float)x) * 180.0 / M_PI;
    if (heading < 0) heading += 360;

    data->heading = heading;
}