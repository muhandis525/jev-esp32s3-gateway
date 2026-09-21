idf_component_register(
    SRCS "src/ble_control.c"
    INCLUDE_DIRS "include"
    REQUIRES jev_core jev_engine jev_identity bt nvs_flash
)
