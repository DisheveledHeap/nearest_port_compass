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

static const gpio_num_t pins[4] = {
    GPIO_NUM_25,
    GPIO_NUM_26,
    GPIO_NUM_32,
    GPIO_NUM_33
};

static const int steps[8][4] = {
    {1,0,0,1},
    {1,0,0,0},
    {1,1,0,0},
    {0,1,0,0},
    {0,1,1,0},
    {0,0,1,0},
    {0,0,1,1},
    {0,0,0,1}
};

static void step_motor(int s)
{
    for (int i = 0; i < 4; i++) {
        gpio_set_level(pins[i], steps[s][i]);
    }
}

void app_main(void)
{
    int step_size = 1;
    int direction = -1; // +1 or -1

    for (int i = 0; i < 4; i++) {
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
    }
    int step_index = 0;

while (1) {
    step_motor(step_index);

    step_index = (step_index + direction * step_size + 8) % 8;

    vTaskDelay(pdMS_TO_TICKS(15));
}

}