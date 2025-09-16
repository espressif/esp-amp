# skip building component in maincore project, only trigger sdkconfig generation
if(NOT SUBCORE_BUILD)
    idf_component_register()
    return()
endif()

idf_component_register(
    SRCS "myrand.c"
    INCLUDE_DIRS "."
    LDFRAGMENTS myrand.lf
)
