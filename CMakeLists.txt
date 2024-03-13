cmake_minimum_required(VERSION 3.16.0)
list(APPEND EXTRA_COMPONENT_DIRS lib/esp32-open-meteo)
list(APPEND EXTRA_COMPONENT_DIRS extra_components/esp-iot-solution/components/sensors/light_sensor/bh1750)
list(APPEND EXTRA_COMPONENT_DIRS extra_components/esp-iot-solution/components/bus)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(M5stack_clock)
