#include <stdio.h>
#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "common.h"
#include "gnss.h"
#include "qmc5883l.h"
#include "motor.h"
#include "water_accesses.h"

#define NVS_NAMESPACE "stepper"
#define NVS_DIR_KEY   "direction"

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

static float stepper_dir = 0.0;

esp_err_t nvs_save_direction() {
    uint32_t bits;
    memcpy(&bits, &stepper_dir, sizeof(bits)); // safe type-pun, no UB

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_u32(handle, NVS_DIR_KEY, bits);
    if (ret == ESP_OK) ret = nvs_commit(handle);

    nvs_close(handle);
    return ret;
}

esp_err_t nvs_load_direction() {
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        stepper_dir = 0.0f;
        return ESP_OK;
    }

    uint32_t bits = 0;
    ret = nvs_get_u32(handle, NVS_DIR_KEY, &bits);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        stepper_dir = 0.0f;
        ret = ESP_OK;
    } else {
        memcpy(stepper_dir, &bits, sizeof(float));
    }

    nvs_close(handle);
    return ret;
}


void app_main(void)
{
    //must be first thing in app_main or pages will be misaligned with page tables
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    nvs_load_direction();

    
    struct Quad q;
    sensor_data_t data = {0};
    turn_degree(0); // Initialize motor state

    printf("SD Card Pins: MOSI=%d, MISO=%d, SCK=%d, CS=%d\r\n",
        SDCardPins[0], SDCardPins[1], SDCardPins[2], SDCardPins[3]);

    gnss_init(GpsPins[0], GpsPins[1]);
    qmc5883l_init(MagnetometerPins[0], MagnetometerPins[1]);
    quad_initialize(&q);
    

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

        float relative_dir_to_port = turn_to_face(check_lat, check_lon, nearest.lat, nearest.lon, data.heading); // contains direction relative to the magnetometer

        printf("Lat: %.6f, Lon: %.6f, Heading: %.2f; closest port at (%ld, %ld); comparative direction to nearest port: %.2f\n",
            data.latitude,
            data.longitude,
            data.heading,
            nearest.lat,
            nearest.lon,
            relative_dir_to_port);
        vTaskDelay(pdMS_TO_TICKS(500));

        float to_turn = relative_dir_to_port - stepper_dir;
        if (to_turn != 0.0) {
            turn_degree(to_turn);
            stepper_dir = relative_dir_to_port;
            nvs_save_direction();
        }
    };
}