set(srcs
    "syscalls.c"
)

if(CONFIG_ESP_AMP_SUBCORE_USE_ARITH64)
    list(APPEND srcs "arith64.c")
endif()

if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND srcs
        "port/soc/esp32p4/portasm.S"
        "port/soc/esp32p4/start.S"
        "port/soc/esp32p4/startup.c"
        "port/soc/esp32p4/port.c"

        "${IDF_PATH}/components/riscv/interrupt_clic.c"
        "${IDF_PATH}/components/riscv/interrupt.c"
        "${IDF_PATH}/components/riscv/vectors_clic.S"
        "${IDF_PATH}/components/riscv/vectors.S"
        "${IDF_PATH}/components/esp_system/port/arch/riscv/esp_ipc_isr_handler.S"
    )
endif()

idf_component_register(
    SRCS ${srcs}
    REQUIRES hal riscv esp_rom)

target_link_libraries(${COMPONENT_LIB} INTERFACE m)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u subcore_panic_dump")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u esp_amp_include_syscalls_impl")

if(CONFIG_ESP_AMP_SUBCORE_USE_ARITH64)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u esp_amp_include_arith64_impl")
endif()
