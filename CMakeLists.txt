cmake_minimum_required(VERSION 3.14)
project(vulkan-raytracer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add all source files
set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")


file(GLOB SOURCE_FILES CONFIGURE_DEPENDS
  "${SOURCE_DIR}/*.c"
  "${SOURCE_DIR}/*.cpp"
)

file(GLOB HEADER_FILES CONFIGURE_DEPENDS
  "${INCLUDE_DIR}/*.h"
  "${INCLUDE_DIR}/*.hpp"
)

file(GLOB SHADER_FILES CONFIGURE_DEPENDS
  "${SHADER_SOURCE_DIR}/*.vert"
  "${SHADER_SOURCE_DIR}/*.frag"
  "${SHADER_SOURCE_DIR}/*.comp"
  "${SHADER_SOURCE_DIR}/*.geom"
  "${SHADER_SOURCE_DIR}/*.tesc"
  "${SHADER_SOURCE_DIR}/*.tese"
  "${SHADER_SOURCE_DIR}/*.mesh"
  "${SHADER_SOURCE_DIR}/*.task"
  "${SHADER_SOURCE_DIR}/*.rgen"
  "${SHADER_SOURCE_DIR}/*.rchit"
  "${SHADER_SOURCE_DIR}/*.rahit"
  "${SHADER_SOURCE_DIR}/*.rmiss"
  "${SHADER_SOURCE_DIR}/*.rint"
)
set_source_files_properties(${SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

source_group("Source files" FILES ${SOURCE_FILES})
source_group("Header files" FILES ${HEADER_FILES})
source_group("Shader files" FILES ${SHADER_FILES})

include_directories(PRIVATE ${INCLUDE_DIR})
add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES} ${SHADER_FILES})

string(LENGTH "${CMAKE_SOURCE_DIR}/" SOURCE_PATH_SIZE)
target_compile_definitions(${PROJECT_NAME}
    PRIVATE PROJECT_DIR="${PROJECT_SOURCE_DIR}/"
    PRIVATE SHADER_BINARY_DIR="${SHADER_BINARY_DIR}"
    PRIVATE SOURCE_PATH_SIZE=${SOURCE_PATH_SIZE}
)

# Add dependencies
set(EXTERNAL_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/external")
find_package(Vulkan REQUIRED)

add_subdirectory("${EXTERNAL_FOLDER}/glfw" EXCLUDE_FROM_ALL)
add_subdirectory("${EXTERNAL_FOLDER}/glm" EXCLUDE_FROM_ALL)

target_link_libraries(${PROJECT_NAME}
  PUBLIC Vulkan::Vulkan glm::glm glfw
)

add_dependencies(${PROJECT_NAME} glfw)

# Compile shaders
find_program(glslc_executable NAMES glslc HINTS Vulkan::glslc)

add_custom_command(
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BINARY_DIR}
  OUTPUT ${SHADER_BINARY_DIR}
  COMMENT "Creating ${SHADER_BINARY_DIR}"
)

foreach(source IN LISTS SHADER_FILES)
  get_filename_component(FILENAME ${source} NAME)
  add_custom_command(
    COMMAND ${glslc_executable} -o ${SHADER_BINARY_DIR}/${FILENAME}.spv ${source}
    OUTPUT ${SHADER_BINARY_DIR}/${FILENAME}.spv
    DEPENDS ${source} ${SHADER_BINARY_DIR}
    COMMENT "Compiling ${FILENAME}"
  )
  list(APPEND SPV_SHADERS ${SHADER_BINARY_DIR}/${FILENAME}.spv)
endforeach()

add_custom_target(shaders ALL DEPENDS ${SPV_SHADERS})
add_dependencies(${PROJECT_NAME} shaders)
