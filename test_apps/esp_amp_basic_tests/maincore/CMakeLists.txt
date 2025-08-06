set(app_sources
    "test_app_main.c"
    "test_load_subcore_main.c"
    "test_rpmsg_main.c"
    "test_ept_main.c"
    "test_rpc_main.c"
    "test_sw_intr_main.c"
    "test_event_main.c"
    "test_queue_main.c"
    "test_libc_main.c"
    "test_panic_main.c"
)

idf_component_register(
    SRCS ${app_sources}
    REQUIRES esp_amp esp_timer test_utils unity
    WHOLE_ARCHIVE
)

list(LENGTH SUBCORE_APP_NAME app_name_len)
list(LENGTH SUBCORE_PROJECT_DIR project_dir_len)
if(NOT app_name_len STREQUAL project_dir_len)
    message(FATAL_ERROR "Subcore app name and project directory name must be the same")
endif()

math(EXPR app_len "${app_name_len}-1")
foreach(i RANGE 0 ${app_len})
    list(GET SUBCORE_APP_NAME ${i} app_name)
    list(GET SUBCORE_PROJECT_DIR ${i} project_dir)
    message(STATUS "Adding subcore app ${app_name} in ${project_dir}")
    esp_amp_add_subcore_project(${app_name} ${project_dir} EMBED)
endforeach()
