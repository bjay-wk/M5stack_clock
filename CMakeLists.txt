cmake_minimum_required(VERSION 3.16.0)
list(APPEND EXTRA_COMPONENT_DIRS lib/esp32-open-meteo)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(M5stack_clock)
