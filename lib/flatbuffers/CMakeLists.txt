#FILE(GLOB_RECURSE app_sources flatbufers/src/*.*)

#COMPILE_DEFINITIONS
#CXX_COMPILE_OPTIONS

idf_component_register(INCLUDE_DIRS flatbuffers/include )

target_compile_definitions(${COMPONENT_TARGET} PRIVATE)