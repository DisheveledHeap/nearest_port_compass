idf_component_register(
    SRCS "water_accesses.c" "motor.c" "main.c" "gnss.c" "qmc5883l.c"
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp_driver_gpio esp_driver_i2c esp_driver_uart esp_timer esp_system
)