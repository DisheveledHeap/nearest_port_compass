idf_component_register(
    SRCS "water_accesses.c" "motor.c" "main.c" "gnss.c" "qmc5883l.c"
    INCLUDE_DIRS "."
    requires nvs_flash
)