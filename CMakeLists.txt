cmake_minimum_required(VERSION 3.23)

set(VCPKG_TARGET_TRIPLET x64-windows-static)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")

project(
    Chronicle
    VERSION 1.0.0
    DESCRIPTION "MMO-style chat system for Skyrim SE"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

set(headers
    src/PCH.h
    src/Chronicle.h
    src/Events.h
    src/MessageBuilder.h
    src/PrismaUI.h
)

set(sources
    src/Plugin.cpp
    src/Chronicle.cpp
    src/Events.cpp
    src/PrismaUI.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${headers} ${sources})

find_package(CommonLibSSE CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME} SOURCES ${headers} ${sources})
add_library("${PROJECT_NAME}::${PROJECT_NAME}" ALIAS "${PROJECT_NAME}")

target_link_libraries(${PROJECT_NAME} PUBLIC
    CommonLibSSE::CommonLibSSE
    nlohmann_json::nlohmann_json
)

if(MSVC)
    add_compile_definitions(_UNICODE)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX /permissive- /Zc:preprocessor /EHsc)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/src>
    $<INSTALL_INTERFACE:src>)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

# Copy web UI alongside the DLL after build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Data"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Data"
    COMMENT "Copying Data directory to output"
)
