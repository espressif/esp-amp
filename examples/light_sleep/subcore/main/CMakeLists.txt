idf_component_register(
    INCLUDE_DIRS "../../common"
    SRCS main.c
    REQUIRES esp_amp add myrand
    LDFRAGMENTS main.lf
)
