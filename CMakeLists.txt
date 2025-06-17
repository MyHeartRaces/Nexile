cmake_minimum_required(VERSION 3.20)
project(Nexile VERSION 0.1.0)

# -----------------------------------------------------------------------------
# Global compiler settings
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Allow our custom CMake modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# Make sure src directory headers are reachable everywhere
include_directories(${CMAKE_SOURCE_DIR}/src)

# -----------------------------------------------------------------------------
# CEF Configuration
# -----------------------------------------------------------------------------
# Set CEF root directory - adjust this path to where you've extracted CEF
set(CEF_ROOT "${CMAKE_SOURCE_DIR}/third_party/cef")

# Determine the platform
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CEF_PLATFORM "windows64")
else()
    set(CEF_PLATFORM "windows32")
endif()

# CEF directories
set(CEF_BINARY_DIR "${CEF_ROOT}/${CEF_PLATFORM}")
set(CEF_RESOURCE_DIR "${CEF_ROOT}/Resources")
set(CEF_INCLUDE_DIR "${CEF_ROOT}")

# Check if CEF exists
if(NOT EXISTS ${CEF_BINARY_DIR})
    message(FATAL_ERROR "CEF binary directory not found: ${CEF_BINARY_DIR}")
endif()

# Add CEF include directory
include_directories(${CEF_INCLUDE_DIR})

# Find CEF libraries
find_library(CEF_LIBRARY
        NAMES libcef
        PATHS ${CEF_BINARY_DIR}
        NO_DEFAULT_PATH
)

find_library(CEF_WRAPPER_LIBRARY
        NAMES libcef_dll_wrapper
        PATHS ${CEF_BINARY_DIR}
        NO_DEFAULT_PATH
)

if(NOT CEF_LIBRARY)
    message(FATAL_ERROR "CEF library not found")
endif()

if(NOT CEF_WRAPPER_LIBRARY)
    message(FATAL_ERROR "CEF wrapper library not found")
endif()

# -----------------------------------------------------------------------------
# nlohmann/json – first try vcpkg, then fallback to FindNlohmannJson module
# -----------------------------------------------------------------------------
if(DEFINED ENV{VCPKG_ROOT} OR DEFINED VCPKG_TARGET_TRIPLET)
    find_package(nlohmann_json CONFIG)
endif()

if(NOT nlohmann_json_FOUND)
    find_package(NlohmannJson)
    if(NlohmannJson_FOUND)
        if(NOT TARGET nlohmann_json::nlohmann_json AND TARGET nlohmann::json)
            add_library(nlohmann_json::nlohmann_json ALIAS nlohmann::json)
        endif()
    else()
        message(STATUS "Using fallback method for nlohmann/json")
        include_directories(C:/json/include)
        add_library(nlohmann_json_dummy INTERFACE)
        add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json_dummy)
    endif()
endif()

# -----------------------------------------------------------------------------
# Source & resource files
# -----------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h")

set(RESOURCE_FILES
        "src/Resources.rc"
        "src/Utils/Logger.h"
        "src/Modules/SettingsModule.h")

set(HTML_RESOURCES
        "src/UI/HTML/main_overlay.html"
        "src/UI/HTML/price_check_module.html"
        "src/UI/HTML/settings.html"
        "src/UI/HTML/welcome.html")

# -----------------------------------------------------------------------------
# Target
# -----------------------------------------------------------------------------
add_executable(Nexile WIN32 ${SOURCES} ${RESOURCE_FILES})

# CEF compile definitions
target_compile_definitions(Nexile PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

# Link libraries
# -----------------------------------------------------------------------------
set(WINDOWS_LIBS Shlwapi.lib Version.lib Ole32.lib OleAut32.lib windowsapp.lib)

target_link_libraries(Nexile PRIVATE
        ${CEF_LIBRARY}
        ${CEF_WRAPPER_LIBRARY}
        nlohmann_json::nlohmann_json
        ${WINDOWS_LIBS}
)

# Output directory (Debug/Release both land in bin/)
set_target_properties(Nexile PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# -----------------------------------------------------------------------------
# Post‑build steps
# -----------------------------------------------------------------------------
# 1) Copy CEF binaries
add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CEF_BINARY_DIR}/libcef.dll"
        "$<TARGET_FILE_DIR:Nexile>")

add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CEF_BINARY_DIR}/chrome_elf.dll"
        "$<TARGET_FILE_DIR:Nexile>")

add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CEF_BINARY_DIR}/d3dcompiler_47.dll"
        "$<TARGET_FILE_DIR:Nexile>")

add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CEF_BINARY_DIR}/libEGL.dll"
        "$<TARGET_FILE_DIR:Nexile>")

add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CEF_BINARY_DIR}/libGLESv2.dll"
        "$<TARGET_FILE_DIR:Nexile>")

add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CEF_BINARY_DIR}/vk_swiftshader.dll"
        "$<TARGET_FILE_DIR:Nexile>")

add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CEF_BINARY_DIR}/vulkan-1.dll"
        "$<TARGET_FILE_DIR:Nexile>")

# 2) Copy CEF resources
add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CEF_RESOURCE_DIR}"
        "$<TARGET_FILE_DIR:Nexile>/cef_resources")

# 3) Copy CEF locales
add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CEF_BINARY_DIR}/locales"
        "$<TARGET_FILE_DIR:Nexile>/locales")

# 4) Copy HTML resources next to the EXE
add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Nexile>/HTML")

foreach(HTML_FILE ${HTML_RESOURCES})
    get_filename_component(HTML_FILENAME ${HTML_FILE} NAME)
    add_custom_command(TARGET Nexile POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/${HTML_FILE}"
            "$<TARGET_FILE_DIR:Nexile>/HTML/${HTML_FILENAME}")
endforeach()

# 5) Ensure Profiles folder exists
add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Nexile>/Profiles")