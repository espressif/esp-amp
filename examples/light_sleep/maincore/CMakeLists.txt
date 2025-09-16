idf_component_register(
    SRCS main/app_main.c
    INCLUDE_DIRS "../common"
    REQUIRES esp_amp
)

esp_amp_add_subcore_project(${SUBCORE_APP_NAME} ${SUBCORE_PROJECT_DIR} PARTITION TYPE data SUBTYPE 0x40)
