#include <stdio.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

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

static int step_index = 0;

static void step_motor(int s)
{
    for (int i = 0; i < 4; i++) {
        gpio_set_level(pins[i], steps[s][i]);
    }
}

static void motor_off(void)
{
    for (int i = 0; i < 4; i++) {
        gpio_set_level(pins[i], 0);
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
    for (int i = 0; i < 4; i++) {
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(pins[i], 0);
    }

    turn_degree(360.0f); // Turn one full rotation
}