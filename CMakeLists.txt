cmake_minimum_required(VERSION 3.16.0)
list(APPEND EXTRA_COMPONENT_DIRS extra_components/esp-iot-solution/components/sensors/humiture/sht3x)
list(APPEND EXTRA_COMPONENT_DIRS extra_components/esp-iot-solution/components/sensors/light_sensor/bh1750)
list(APPEND EXTRA_COMPONENT_DIRS extra_components/esp-iot-solution/components/bus)

get_filename_component(configName "${CMAKE_BINARY_DIR}" NAME)
FILE(GLOB_RECURSE kconfigs_pio_lib_deps ${CMAKE_SOURCE_DIR}/.pio/libdeps/${configName}/*/Kconfig)
list(APPEND kconfigs ${kconfigs_pio_lib_deps})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(M5stack_clock)
