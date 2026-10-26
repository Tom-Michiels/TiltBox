file(GLOB_RECURSE MP_SOURCES "src/*.c")
idf_component_register(SRCS ${MP_SOURCES} INCLUDE_DIRS "src")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -Wno-unused-function -fno-strict-aliasing)
