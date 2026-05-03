#include "gnss.h"
#include "driver/uart.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

void gnss_init(gpio_num_t tx, gpio_num_t rx) {
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
    int len = uart_read_bytes(UART_NUM, (uint8_t *)buffer, BUF_SIZE - 1, 100 / portTICK_PERIOD_MS);
    if (len <= 0) return;

    buffer[len] = '\0';

    char *line = strtok(buffer, "\n");
    while (line != NULL) {
        if (strstr(line, "$GPRMC")) {
            char *token;

            char lat[16] = {0};
            char lon[16] = {0};
            char lat_dir = 0;
            char lon_dir = 0;
            int field = 0;

            token = strtok(line, ",");
            while (token != NULL) {
                if (field == 3 && token) strncpy(lat, token, sizeof(lat)-1);
                if (field == 4 && token) lat_dir = token[0];
                if (field == 5 && token) strncpy(lon, token, sizeof(lon)-1);
                if (field == 6 && token) lon_dir = token[0];

                token = strtok(NULL, ",");
                field++;
            }

            if (strlen(lat) > 0 && strlen(lon) > 0 && lat_dir && lon_dir) {
                data->latitude = convert_to_decimal(lat, lat_dir);
                data->longitude = convert_to_decimal(lon, lon_dir);
            }
        }
        line = strtok(NULL, "\n");
    }
}