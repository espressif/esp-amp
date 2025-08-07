idf_build_get_property(target IDF_TARGET)

set(srcs "")
set(includes "")
set(priv_includes "")

set(srcs_common
    "${ESP_AMP_PATH}/components/esp_amp/src/esp_amp.c"
    "${ESP_AMP_PATH}/components/esp_amp/src/esp_amp_sys_info.c"
    "${ESP_AMP_PATH}/components/esp_amp/src/esp_amp_sw_intr.c"
    "${ESP_AMP_PATH}/components/esp_amp/src/esp_amp_queue.c"
    "${ESP_AMP_PATH}/components/esp_amp/src/esp_amp_rpmsg.c"
    "${ESP_AMP_PATH}/components/esp_amp/src/esp_amp_utils.c"
    "${ESP_AMP_PATH}/components/esp_amp/src/rpc/esp_amp_rpc_client.c"
    "${ESP_AMP_PATH}/components/esp_amp/src/rpc/esp_amp_rpc_server.c"

    "${ESP_AMP_PATH}/components/esp_amp/system/esp_amp_print.c"
    "${ESP_AMP_PATH}/components/esp_amp/system/esp_amp_service.c"
    "${ESP_AMP_PATH}/components/esp_amp/system/esp_amp_panic/panic_common.c"
    "${ESP_AMP_PATH}/components/esp_amp/system/esp_amp_system.c"
)

set(includes_common
    "${ESP_AMP_PATH}/components/esp_amp/include"
    "${ESP_AMP_PATH}/components/esp_amp/port/include"
    "${ESP_AMP_PATH}/components/esp_amp/system/include"
)

set(priv_includes_common
    "${ESP_AMP_PATH}/components/esp_amp/priv_include"
    "${ESP_AMP_PATH}/components/esp_amp/system/priv_include"
)

if(BOOTLOADER_BUILD)
    list(APPEND includes ${includes_common})
    list(APPEND priv_includes ${priv_includes_common})

    list(APPEND srcs
        ${srcs_common}
        "${ESP_AMP_PATH}/components/esp_amp/src/event/baremetal/esp_amp_event.c"
        "${ESP_AMP_PATH}/components/esp_amp/port/env/baremetal/esp_amp_env.c"
        "${ESP_AMP_PATH}/components/esp_amp/system/esp_amp_panic/panic_subcore.c"
    )

    if(CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE)
        list(APPEND srcs
            "${ESP_AMP_PATH}/components/esp_amp/port/platform/sw_intr/pmu/esp_amp_platform_sw_intr.c"
            "${ESP_AMP_PATH}/components/esp_amp/port/platform/lp_core/esp_amp_platform.c"
        )
    else()
        list(APPEND srcs
            "${ESP_AMP_PATH}/components/esp_amp/port/platform/sw_intr/intmtx/esp_amp_platform_sw_intr.c"
            "${ESP_AMP_PATH}/components/esp_amp/port/platform/hp_core/esp_amp_platform.c"
        )
    endif()

    list(APPEND reqs soc hal esp_app_format)

    if(IDF_TARGET STREQUAL "esp32c6" OR IDF_TARGET STREQUAL "esp32c5")
        list(APPEND reqs ulp)
    endif()

    idf_component_register(
        SRCS ${srcs}
        PRIV_INCLUDE_DIRS ${priv_includes}
        INCLUDE_DIRS ${includes}
        PRIV_INCLUDE_DIRS ${priv_includes}
        REQUIRES ${reqs}
    )

    idf_build_get_property(sdkconfig_header SDKCONFIG_HEADER)
    idf_build_get_property(idf_path IDF_PATH)
    idf_build_get_property(idf_target IDF_TARGET)

    function(create_arg_file arguments output_file)
        # Escape all spaces
        list(TRANSFORM arguments REPLACE " " "\\\\ ")
        # Create a single string with all args separated by space
        list(JOIN arguments " " arguments)
        # Write it into the response file
        file(WRITE ${output_file} ${arguments})
    endfunction()

    # Header files needed by linker script generator
    get_filename_component(sdkconfig_dir ${SDKCONFIG_HEADER} DIRECTORY)
    list(APPEND ESP_AMP_PREPRO_ARGS -I${sdkconfig_dir})
    list(APPEND ESP_AMP_PREPRO_ARGS -I${IDF_PATH}/components/esp_system/ld)
    list(APPEND ESP_AMP_PREPRO_ARGS -I${ESP_AMP_PATH}/components/esp_amp/priv_include)
    list(APPEND ESP_AMP_PREPRO_ARGS -I${IDF_PATH}/components/esp_common/include)

    idf_component_get_property(soc_includes soc INCLUDE_DIRS)
    foreach(soc_include ${soc_includes})
        list(APPEND ESP_AMP_PREPRO_ARGS -I${IDF_PATH}/components/soc/${soc_include})
    endforeach()

    # Pre-process the linker script
    set(ESP_AMP_LD_TEMPLATE ${ESP_AMP_PATH}/components/esp_amp/ld/${IDF_TARGET}/memory.ld)
    get_filename_component(ESP_AMP_LD_SCRIPT ${ESP_AMP_LD_TEMPLATE} NAME)

    # Put all arguments to the list
    set(preprocessor_args -D__ASSEMBLER__ -E -P -xc -o ${ESP_AMP_LD_SCRIPT} ${ESP_AMP_PREPRO_ARGS} ${ESP_AMP_LD_TEMPLATE})
    set(compiler_arguments_file ${CMAKE_CURRENT_BINARY_DIR}/${ESP_AMP_LD_SCRIPT}_args.txt)
    create_arg_file("${preprocessor_args}" "${compiler_arguments_file}")

    add_custom_command(OUTPUT ${ESP_AMP_LD_SCRIPT}
        COMMAND ${CMAKE_C_COMPILER} @${compiler_arguments_file}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        MAIN_DEPENDENCY ${ESP_AMP_LD_TEMPLATE}
        DEPENDS ${SDKCONFIG_HEADER}
        COMMENT "Generating ${ESP_AMP_LD_SCRIPT} linker script..."
        VERBATIM)
    add_custom_target(ld_script DEPENDS ${ESP_AMP_LD_SCRIPT})
    add_dependencies(${COMPONENT_LIB} ld_script)

    target_linker_script(${COMPONENT_LIB} INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/${ESP_AMP_LD_SCRIPT})

    # To avoid warning "Manually-specified variables were not used by the project"
    set(bypassWarning "${IDF_TARGET}")
    set(bypassWarning "${CONFIG_ESP_ROM_HAS_LP_ROM}")
    set(bypassWarning "${SUBCORE_APP_NAME}")

else()
    if(CONFIG_ESP_AMP_ENABLED)
        list(APPEND includes ${includes_common})
        list(APPEND priv_includes ${priv_includes_common})

        list(APPEND srcs
            ${srcs_common}
            "${ESP_AMP_PATH}/components/esp_amp/src/event/freertos/esp_amp_event.c"
            "${ESP_AMP_PATH}/components/esp_amp/port/env/freertos/esp_amp_env.c"
            "${ESP_AMP_PATH}/components/esp_amp/port/platform/hp_core/esp_amp_platform.c"

            "${ESP_AMP_PATH}/components/esp_amp/system/esp_amp_loader.c"
            "${ESP_AMP_PATH}/components/esp_amp/system/esp_amp_cpu.c"
            "${ESP_AMP_PATH}/components/esp_amp/system/esp_amp_panic/panic_maincore.c"
        )

        if(CONFIG_ESP_AMP_SUBCORE_TYPE_LP_CORE)
            list(APPEND srcs
                "${ESP_AMP_PATH}/components/esp_amp/port/platform/sw_intr/pmu/esp_amp_platform_sw_intr.c"
            )
        else()
            list(APPEND srcs
                "${ESP_AMP_PATH}/components/esp_amp/port/platform/sw_intr/intmtx/esp_amp_platform_sw_intr.c"
            )
        endif()
    endif()

    list(APPEND reqs bootloader_support esp_partition spi_flash esp_app_format)

    if(IDF_TARGET STREQUAL "esp32c6" OR IDF_TARGET STREQUAL "esp32c5")
        list(APPEND reqs ulp)
    endif()

    idf_component_register(SRCS ${srcs}
        INCLUDE_DIRS ${includes}
        PRIV_INCLUDE_DIRS ${priv_includes}
        REQUIRES ${reqs})

    if(CONFIG_ESP_AMP_ENABLED)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC IS_MAIN_CORE)
    endif()

    idf_build_get_property(project_name PROJECT_NAME)
    idf_build_get_property(python PYTHON)
    add_custom_target(check_symbols ALL
        COMMAND ${python} ${ESP_AMP_PATH}/components/esp_amp/scripts/check_symbols.py --elf_file ${project_name}.elf
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Checking symbols"
        VERBATIM)
    add_dependencies(check_symbols app)
endif() # BOOTLOADER_BUILD

idf_component_get_property(esp_amp_reqs ${COMPONENT_NAME} REQUIRES)
message(STATUS "esp_amp_reqs: ${esp_amp_reqs}")
