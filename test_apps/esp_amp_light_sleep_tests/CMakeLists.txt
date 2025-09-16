cmake_minimum_required(VERSION 3.16)

list(PREPEND SDKCONFIG_DEFAULTS "$ENV{IDF_PATH}/tools/test_apps/configs/sdkconfig.debug_helpers" "sdkconfig.defaults")

if(DEFINED ENV{ESP_AMP_PATH})
  set(ESP_AMP_PATH $ENV{ESP_AMP_PATH})
else()
  set(ESP_AMP_PATH ${CMAKE_CURRENT_LIST_DIR}/../..)
endif(DEFINED ENV{ESP_AMP_PATH})

set(EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/tools/unit-test-app/components"
    ${CMAKE_CURRENT_LIST_DIR}/maincore
    ${ESP_AMP_PATH}/components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

file(GLOB SUBCORE_APP_DIRS "${CMAKE_CURRENT_LIST_DIR}/subcore/*")
message(STATUS "SUBCORE_APP_DIRS: ${SUBCORE_APP_DIRS}")

foreach(SUBCORE_APP_DIR IN LISTS SUBCORE_APP_DIRS)
  set(SUBCORE_PROJECT_CMAKE "${SUBCORE_APP_DIR}/subcore_config.cmake")
  if(EXISTS ${SUBCORE_PROJECT_CMAKE})
    message(STATUS "Found subcore project cmake under ${SUBCORE_APP_DIR}")
    include(${SUBCORE_PROJECT_CMAKE})
  else()
    message(FATAL_ERROR "Subcore project cmake file not found in ${SUBCORE_APP_DIR}")
  endif()
endforeach()

# NOTE: workaround for using `idf_build_set_property(MINIMAL_BUILD ON)`
#       in IDF v5.3 and v5.4
set(COMPONENTS maincore)

project(esp_amp_basic_tests)
