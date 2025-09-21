cmake_minimum_required(VERSION 3.14)
project(vulkan-raytracer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add all source files
set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/res")
set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(EXTERNAL_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/external")
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
file(GLOB GLSL_FILES CONFIGURE_DEPENDS
  "${SHADER_SOURCE_DIR}/*.glsl"
)
set_source_files_properties(${SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

source_group("Source files" FILES ${SOURCE_FILES})
source_group("Header files" FILES ${HEADER_FILES})
source_group("Shader files" FILES ${SHADER_FILES} ${GLSL_FILES})

include_directories(PRIVATE ${INCLUDE_DIR})
include_directories(PRIVATE "${EXTERNAL_FOLDER}/stb")
include_directories("${EXTERNAL_FOLDER}/tinygltf")
add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES} ${SHADER_FILES} ${GLSL_FILES})

string(LENGTH "${CMAKE_SOURCE_DIR}/" SOURCE_PATH_SIZE)
target_compile_definitions(${PROJECT_NAME}
    PRIVATE PROJECT_DIR="${PROJECT_SOURCE_DIR}/"
    PRIVATE RESOURCE_DIR="${RESOURCE_DIR}/"
    PRIVATE SHADER_BINARY_DIR="${SHADER_BINARY_DIR}"
    PRIVATE SOURCE_PATH_SIZE=${SOURCE_PATH_SIZE}
)

# Add dependencies
find_package(Vulkan REQUIRED)

add_subdirectory("${EXTERNAL_FOLDER}/args" EXCLUDE_FROM_ALL)
add_subdirectory("${EXTERNAL_FOLDER}/glfw" EXCLUDE_FROM_ALL)
add_subdirectory("${EXTERNAL_FOLDER}/glm" EXCLUDE_FROM_ALL)

target_link_libraries(${PROJECT_NAME}
  PUBLIC Vulkan::Vulkan glm::glm glfw taywee::args
)

add_dependencies(${PROJECT_NAME} glfw)

# Compile shaders
find_program(glslc_executable NAMES glslc HINTS Vulkan::glslc)

foreach(source IN LISTS SHADER_FILES)
  get_filename_component(FILENAME ${source} NAME)
  add_custom_command(
    COMMAND ${glslc_executable} --target-spv=spv1.4 -o ${SHADER_BINARY_DIR}/$<IF:$<CONFIG:Debug,RelWithDebInfo>,Debug,Release>/${FILENAME}.spv ${source} $<IF:$<CONFIG:Debug,RelWithDebInfo>,"-DDEBUG -O0","-DRELEASE -O">
    OUTPUT ${SHADER_BINARY_DIR}/$<IF:$<CONFIG:Debug,RelWithDebInfo>,Debug,Release>/${FILENAME}.spv
    DEPENDS ${source}
    COMMENT "Compiling ${FILENAME}: ${glslc_executable} --target-spv=spv1.4 -o ${SHADER_BINARY_DIR}/$<IF:$<CONFIG:Debug,RelWithDebInfo>,Debug,Release>/${FILENAME}.spv ${source} $<IF:$<CONFIG:Debug,RelWithDebInfo>,-DDEBUG -O0,-DRELEASE -O>"
  )
  list(APPEND SPV_SHADERS ${SHADER_BINARY_DIR}/$<IF:$<CONFIG:Debug,RelWithDebInfo>,Debug,Release>/${FILENAME}.spv)
endforeach()


add_custom_target(shaders ALL DEPENDS ${SPV_SHADERS})
target_compile_definitions(${PROJECT_NAME} PRIVATE 
    SHADER_BINARY_DIR="${SHADER_BINARY_DIR}/$<IF:$<CONFIG:Debug,RelWithDebInfo>,Debug,Release>"
)

add_dependencies(${PROJECT_NAME} shaders)
