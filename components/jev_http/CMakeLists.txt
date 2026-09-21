idf_component_register(
    SRCS "src/http_api.c"
    INCLUDE_DIRS "include"
    REQUIRES jev_connectivity jev_engine jev_identity esp_http_server esp_timer esp_system
)
