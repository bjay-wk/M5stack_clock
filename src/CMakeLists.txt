# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)
idf_component_register(SRCS ${app_sources}
    REQUIRES esp32-wifi-manager esp32-ipgeolocation-io
    EMBED_TXTFILES ${CMAKE_SOURCE_DIR}/extra_components/posix_tz_db/zones.json
)