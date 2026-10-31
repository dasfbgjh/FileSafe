cmake_minimum_required(VERSION 3.10)
project(resource
    VERSION 1.0.0
    DESCRIPTION "resource to code"
    LANGUAGES CXX
)

set(GENERATE_NAME ${RESOURCE_NAME_PREIFX}_generate)
set(LIBRARY_NAME ${RESOURCE_NAME_PREIFX}_resource)

set(RESOURCE_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

file(GLOB_RECURSE RESOURCE_DATA_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES true "${RESOURCE_PATH}/*")

add_executable(${GENERATE_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/generate.cpp)
target_compile_definitions(${GENERATE_NAME} PRIVATE
    -DINPUT_DIR="${RESOURCE_PATH}"
    -DOUTPUT_NAME="${RESOURCE_OUTPUT_DIR}/data.h"
)

add_custom_command(
    OUTPUT "${RESOURCE_OUTPUT_DIR}/data.h"
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${GENERATE_NAME}
    DEPENDS ${GENERATE_NAME} ${RESOURCE_DATA_FILES}
    COMMENT "<=== Generating resource header file ===>"
)

add_custom_target(${GENERATE_NAME}_run DEPENDS "${RESOURCE_OUTPUT_DIR}/data.h")

# resource library
add_library(${LIBRARY_NAME} STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/resource.cpp)
target_include_directories(${LIBRARY_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${LIBRARY_NAME} PRIVATE ${RESOURCE_OUTPUT_DIR})
add_dependencies(${LIBRARY_NAME} ${GENERATE_NAME}_run)