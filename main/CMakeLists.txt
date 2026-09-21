idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES jev_ble jev_connectivity jev_core jev_engine jev_http
             jev_identity jev_tools nvs_flash esp_system
)
