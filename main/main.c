#include <stdio.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "common.h"
#include "gnss.h"
#include "qmc5883l.h"
#include "water_accesses.h"

static const gpio_num_t StepperPins[4] = {
    GPIO_NUM_25,
    GPIO_NUM_26,
    GPIO_NUM_32,
    GPIO_NUM_33
};

static const gpio_num_t GpsPins[2] = {
    GPIO_NUM_16, // GPS TX, ESP32 RX
    GPIO_NUM_17 // GPS RX, ESP32 TX
};

static const gpio_num_t MagnetometerPins[2] = {
    GPIO_NUM_21, // SDA (Data line)
    GPIO_NUM_22, // SCL (Clock line)
};

static const gpio_num_t SDCardPins[4] = {
    GPIO_NUM_23, // MOSI (Master Out Slave In)
    GPIO_NUM_19, // MISO (Master In Slave Out)
    GPIO_NUM_18, // SCK (Serial Clock)
    GPIO_NUM_5,  // CS (Chip Select)
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

static int step_index = 0;

static void step_motor(int s)
{
    for (int i = 0; i < 4; i++) {
        gpio_set_level(StepperPins[i], steps[s][i]);
    }
}

static void motor_off(void)
{
    for (int i = 0; i < 4; i++) {
        gpio_set_level(StepperPins[i], 0);
    }
}

static void turn_degree(float degree)
{
    int direction = (degree >= 0) ? 1 : -1;
    int steps_to_move = (int)(fabsf(degree) / 360.0f * 4096.0f);

    for (int i = 0; i < steps_to_move; i++) {
        step_motor(step_index);
        step_index = (step_index + direction + 8) % 8;
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    motor_off();
}

void app_main(void)
{
    struct Quad q;
    sensor_data_t data = {0};

    printf("SD Card Pins: MOSI=%d, MISO=%d, SCK=%d, CS=%d\r\n",
        SDCardPins[0], SDCardPins[1], SDCardPins[2], SDCardPins[3]);

    gnss_init(GpsPins[0], GpsPins[1]);
    qmc5883l_init(MagnetometerPins[0], MagnetometerPins[1]);
    quad_initialize(q);
    

    for (int i = 0; i < 4; i++) {
        gpio_set_direction(StepperPins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(StepperPins[i], 0);
    }

    while (1) {
        gnss_update(&data);
        qmc5883l_update(&data);

        int32_t check_lat = (int32_t)(data.latitude * WA_SCALE);
        int32_t check_lon = (int32_t)(data.longitude * WA_SCALE);

        struct WA_Point nearest = get_nearest(&q, check_lat, check_lon);

        printf("Lat: %.6f, Lon: %.6f, Heading: %.2f; closest port at (%d, %d)\n",
            data.latitude,
            data.longitude,
            data.heading,
            nearest.lat,
            nearest.lon);
        vTaskDelay(pdMS_TO_TICKS(500));
    };
}