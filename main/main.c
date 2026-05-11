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
#include "esp_timer.h"
#include "esp_log.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"

#define NVS_NAMESPACE "stepper"
#define NVS_DIR_KEY   "direction"

static char last_bt_command[80] = "None";

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

static char last_button_event[80] = "None";
static int button_press_count = 0;

void handle_button(void);

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

        if (i % 10 == 0) {
            handle_button();
        }

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
        memcpy(&stepper_dir, &bits, sizeof(float));
    }

    nvs_close(handle);
    return ret;
}


#define BUTTON GPIO_NUM_27

#define SHORT_PRESS_MS 100
#define LONG_PRESS_MS  1000

static int selected_port_index = 0;

void handle_button() {
    static int last_state = 1;
    static int64_t press_start = 0;

    int current = gpio_get_level(BUTTON);
    int64_t now = esp_timer_get_time() / 1000; // micro → ms

    // Button just pressed
    if (last_state == 1 && current == 0) {
        press_start = now;
        snprintf(last_button_event, sizeof(last_button_event), "Button pressed");
    }

    // Button just released
    if (last_state == 0 && current == 1) {
        int64_t duration = now - press_start;

        if (duration >= LONG_PRESS_MS) {
            // LONG PRESS
            snprintf(last_button_event, sizeof(last_button_event), "Long press -> reset");
            button_press_count++;
            selected_port_index = 0;
        } else if (duration >= SHORT_PRESS_MS) {
            // SHORT PRESS
            selected_port_index = (selected_port_index + 1) % WA_K_NEAREST;
            snprintf(last_button_event, sizeof(last_button_event),
            "Short press -> port %d/%d",
            selected_port_index + 1, WA_K_NEAREST);
            button_press_count++;
        }
    }

    last_state = current;
}

static void print_dashboard(sensor_data_t *data,
                            struct WA_Point target,
                            struct WA_NearestResult *nearest,
                            float relative_dir_to_port)
{
    char buf[2048];

    float to_turn = relative_dir_to_port - stepper_dir;

    while (to_turn > 180.0f) to_turn -= 360.0f;
    while (to_turn < -180.0f) to_turn += 360.0f;

    int len = snprintf(buf, sizeof(buf),
        "\033[H\033[J"
        "========================================\n"
        "        NEAREST PORT COMPASS\n"
        "========================================\n\n"
        "Position\n"
        "  Lat/Lon:       % .5f, % .5f\n\n"
        "Orientation\n"
        "  Heading:       % 3.2f deg\n"
        "  Stepper Dir:   % 3.2f deg\n"
        "  Turn Needed:   % 3.2f deg\n\n"
        "Target\n"
        "  Selected Port: %d/%d\n"
        "  Coordinates:   (% 8ld, % 8ld)\n"
        "  Relative Dir:  % 3.2f deg\n\n"
        "Controls\n"
        "  Last event:    %-20s\n"
        "  Press count:   % 2d\n"
        "Bluetooth\n"
        "  BT command:    %-20s\n"
        "========================================\n",
        data->latitude, data->longitude,
        data->heading,
        stepper_dir,
        to_turn,
        selected_port_index + 1, nearest->count,
        (long)target.lat, (long)target.lon,
        relative_dir_to_port,
        last_button_event,
        button_press_count,
        last_bt_command
    );

    if (len > 0 && len < sizeof(buf)) {
        printf("%s", buf);
        fflush(stdout);
    }
}

void spp_callback(esp_spp_cb_event_t event,
                  esp_spp_cb_param_t *param)
{
    switch (event) {

        case ESP_SPP_INIT_EVT:
            esp_spp_start_srv(
                ESP_SPP_SEC_NONE,
                ESP_SPP_ROLE_SLAVE,
                0,
                "JacksCompass"
            );
            break;

        case ESP_SPP_DATA_IND_EVT:
        snprintf(last_bt_command, sizeof(last_bt_command),
                "%.*s",
                param->data_ind.len,
                (char *)param->data_ind.data);
        break;

        default:
            break;
    }
}

void app_main(void)
{
    //must be first thing in app_main or pages will be misaligned with page tables
    esp_err_t ret = nvs_flash_init();
    esp_log_level_set("*", ESP_LOG_NONE);
    
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    nvs_load_direction();

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);

    esp_bluedroid_init();
    esp_bluedroid_enable();

    esp_spp_register_callback(spp_callback);

    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    ESP_ERROR_CHECK(esp_bluedroid_init());

    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_spp_register_callback(spp_callback));

    esp_spp_cfg_t spp_cfg = {
        .mode = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = true,
        .tx_buffer_size = 0
    };

    esp_spp_enhanced_init(&spp_cfg);

    printf("SD Card Pins: SCK=%d, MISO=%d, MOSI=%d, CS=%d\n",
        SDCardPins[2], SDCardPins[1], SDCardPins[0], SDCardPins[3]);
    
    sensor_data_t data = {0};

    gnss_init(GpsPins[0], GpsPins[1]);
    qmc5883l_init(MagnetometerPins[0], MagnetometerPins[1]);
    

    for (int i = 0; i < 4; i++) {
        gpio_set_direction(StepperPins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(StepperPins[i], 0);
    }

    gpio_config_t button_conf = {
        .pin_bit_mask = (1ULL << BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&button_conf);

    printf("\033[2J"); // clear screen once
    
    while (1) {
        gnss_update(&data);
        qmc5883l_update(&data);

        handle_button();

        int32_t my_lat = (int32_t)(data.latitude * WA_SCALE);
        int32_t my_lon = (int32_t)(data.longitude * WA_SCALE);

        struct WA_NearestResult nearest = get_k_nearest(my_lat, my_lon);

        struct WA_Point target = nearest.points[selected_port_index];

        float relative_dir_to_port = turn_to_face(my_lat, my_lon, target.lat, target.lon, data.heading); // contains direction relative to the magnetometer

        print_dashboard(&data, target, &nearest, relative_dir_to_port);

        for (int i = 0; i < 50; i++) {
            handle_button();
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        float to_turn = relative_dir_to_port - stepper_dir;
        
        while (to_turn > 180.0)
            to_turn -= 360.0;
        while (to_turn < -180.0)
            to_turn += 360.0;

        if (to_turn * to_turn > 25.0f) {
            turn_degree(to_turn);
            stepper_dir = relative_dir_to_port;
            nvs_save_direction();
        }
    };
}