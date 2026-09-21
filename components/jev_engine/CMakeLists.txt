idf_component_register(
    SRCS "src/engine.c"
    INCLUDE_DIRS "include"
    REQUIRES jev_connectivity jev_core jev_tools freertos
)
