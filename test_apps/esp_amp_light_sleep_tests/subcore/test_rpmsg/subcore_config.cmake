# subcore_project.cmake file must be manually included in the project's top level CMakeLists.txt before project()
# SUBCORE_APP_NAME and SUBCORE_PROJECT_DIR must be defined before idf build process starts

# subcore app name
set(app_name subcore_test_rpmsg)
idf_build_set_property(SUBCORE_APP_NAME "${app_name}" APPEND)

# subcore project dir
get_filename_component(directory "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE DIRECTORY)
idf_build_set_property(SUBCORE_PROJECT_DIR "${directory}" APPEND)
