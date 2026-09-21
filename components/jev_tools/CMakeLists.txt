idf_component_register(
    SRCS "src/tools.c"
    INCLUDE_DIRS "include"
    REQUIRES jev_core esp_adc esp_driver_gpio esp_driver_ledc esp_driver_i2c
             esp_system spi_flash
)
