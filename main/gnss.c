#include "gnss.h"
#include "driver/uart.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UART_NUM UART_NUM_1
#define BUF_SIZE 1024

static char buffer[BUF_SIZE];

static double convert_to_decimal(char *raw, char dir) {
    double val = atof(raw);
    int degrees = (int)(val / 100);
    double minutes = val - (degrees * 100);
    double decimal = degrees + minutes / 60.0;

    if (dir == 'S' || dir == 'W') decimal *= -1;
    return decimal;
}

void gnss_init(gpio_num_t rx, gpio_num_t tx) {
    uart_config_t config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(UART_NUM, &config);
    uart_set_pin(UART_NUM, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, 2048, 0, 0, NULL, 0);
}

void gnss_update(sensor_data_t *data) {
    int len = uart_read_bytes(UART_NUM, (uint8_t *)buffer, BUF_SIZE - 1, pdMS_TO_TICKS(100));
    if (len <= 0) return;

    buffer[len] = '\0';

    char *save_line;
    char *line = strtok_r(buffer, "\n", &save_line);

    while (line != NULL) {
        if (strstr(line, "$GPRMC") || strstr(line, "$GNRMC")) {
            char lat[16] = {0};
            char lon[16] = {0};
            char lat_dir = 0;
            char lon_dir = 0;
            char status = 0;

            char *save_token;
            char *token = strtok_r(line, ",", &save_token);
            int field = 0;

            while (token != NULL) {
                if (field == 2) status = token[0];
                if (field == 3) strncpy(lat, token, sizeof(lat) - 1);
                if (field == 4) lat_dir = token[0];
                if (field == 5) strncpy(lon, token, sizeof(lon) - 1);
                if (field == 6) lon_dir = token[0];

                token = strtok_r(NULL, ",", &save_token);
                field++;
            }

            if (status == 'A' && strlen(lat) > 0 && strlen(lon) > 0 && lat_dir && lon_dir) {
                data->latitude = convert_to_decimal(lat, lat_dir);
                data->longitude = convert_to_decimal(lon, lon_dir);
            }
        }

        line = strtok_r(NULL, "\n", &save_line);
    }
}