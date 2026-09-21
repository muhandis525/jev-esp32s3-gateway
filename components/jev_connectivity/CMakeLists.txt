idf_component_register(
    SRCS "src/connectivity.c"
    INCLUDE_DIRS "include"
    REQUIRES jev_identity network_provisioning esp_wifi esp_event esp_netif nvs_flash
)
