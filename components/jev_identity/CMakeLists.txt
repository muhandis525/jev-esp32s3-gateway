idf_component_register(
    SRCS "src/identity.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_hw_support esp_system
)
