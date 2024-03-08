# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)
idf_component_register(SRCS ${app_sources}
    INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/extra_lib/flatbuffers/include
    REQUIRES esp32-wifi-manager
)

#find_program(FLATBUFFERS_FLATC_EXECUTABLE NAMES flatc)
#MESSAGE(AUTHOR_WARNING "toto")
#MESSAGE(AUTHOR_WARNING ${FLATBUFFERS_FLATC_EXECUTABLE})
#add_custom_command(OUTPUT weather_api_generated.h
#     COMMAND flatc ${CMAKE_SOURCE_DIR}/src/sdk/flatbuffers/weather_api.fbs
#     DEPENDS ${CMAKE_SOURCE_DIR}/src/sdk/flatbuffers/weather_api.fbs
#     VERBATIM)

#add_custom_target(weather_api_generated DEPENDS weather_api_generated.h)
#add_dependencies(${COMPONENT_LIB}  weather_api_generated)

#set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY
#     ADDITIONAL_CLEAN_FILES  weather_api_generated.h)
