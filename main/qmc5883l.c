#include "qmc5883l.h"
#include "driver/i2c.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include <stdint.h>

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
        .master.clk_speed = 10000
    };

    i2c_param_config(I2C_NUM_0, &conf);
    i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);

    // sensor setup...
    write_reg(0x0B, 0x01); // Set/Reset period
    write_reg(0x09, 0x1D); // Continuous mode, 200Hz, 8G, 512 OSR
}

void qmc5883l_update(sensor_data_t *data) {
    uint8_t raw[6];
    static float last_good_heading = 0.0f;

    esp_err_t err = read_data(0x00, raw, 6);
    if (err != ESP_OK) {
        printf("QMC read failed: %s\n", esp_err_to_name(err));
        data->heading = last_good_heading;
        return;
    }

    int16_t x = (raw[1] << 8) | raw[0];
    int16_t y = (raw[3] << 8) | raw[2];

    if (x == 0 && y == 0) {
        printf("QMC returned 0,0; keeping last heading\n");
        data->heading = last_good_heading;
        return;
    }

    float heading = atan2f((float)y, (float)x) * 180.0f / M_PI;
    if (heading < 0) heading += 360.0f;

    last_good_heading = heading;
    data->heading = heading;
        printf("QMC raw: %d %d\n", x, y);
    }