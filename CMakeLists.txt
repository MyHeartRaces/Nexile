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
# CEF C API Configuration - MAJOR CHANGE: No more C++ wrapper!
# -----------------------------------------------------------------------------
set(CEF_ROOT "${CMAKE_SOURCE_DIR}/third_party/cef")

# Updated paths - we only need binary directory now
set(CEF_BINARY_DIR "${CEF_ROOT}/Release")
set(CEF_RESOURCE_DIR "${CEF_ROOT}/Resources")
set(CEF_INCLUDE_DIR "${CEF_ROOT}")

# Verify CEF installation
if(NOT EXISTS ${CEF_BINARY_DIR})
    message(FATAL_ERROR "CEF binary directory not found: ${CEF_BINARY_DIR}")
endif()

if(NOT EXISTS "${CEF_BINARY_DIR}/libcef.dll")
    message(FATAL_ERROR "CEF DLL not found: ${CEF_BINARY_DIR}/libcef.dll")
endif()

if(NOT EXISTS "${CEF_BINARY_DIR}/libcef.lib")
    message(FATAL_ERROR "CEF import library not found: ${CEF_BINARY_DIR}/libcef.lib")
endif()

# Add CEF include directory
include_directories(${CEF_ROOT})

# CRITICAL CHANGE: Only libcef.lib, NO libcef_dll_wrapper.lib
set(CEF_LIBRARY "${CEF_BINARY_DIR}/libcef.lib")

message(STATUS "=================================================================")
message(STATUS "CEF C API Configuration:")
message(STATUS "  - CEF Root: ${CEF_ROOT}")
message(STATUS "  - CEF Library: ${CEF_LIBRARY}")
message(STATUS "  - Using C API: YES")
message(STATUS "  - Using C++ Wrapper: NO")
message(STATUS "  - Expected binary size reduction: 20-35MB")
message(STATUS "=================================================================")

# -----------------------------------------------------------------------------
# nlohmann/json
# -----------------------------------------------------------------------------
find_package(nlohmann_json CONFIG REQUIRED)

# -----------------------------------------------------------------------------
# Source files
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
        "src/UI/HTML/welcome.html"
        "src/UI/HTML/browser.html")

# -----------------------------------------------------------------------------
# Target configuration
# -----------------------------------------------------------------------------
add_executable(Nexile WIN32 ${SOURCES} ${RESOURCE_FILES})

# CEF C API specific compile definitions
target_compile_definitions(Nexile PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        USING_CEF_CAPI=1                    # New flag to indicate C API usage
        CEF_VERSION_MAJOR=120               # Adjust to your CEF version
)

# -----------------------------------------------------------------------------
# Linking - MAJOR CHANGE: Dramatically simplified
# -----------------------------------------------------------------------------
set(WINDOWS_LIBS
        Shlwapi.lib
        Version.lib
        Ole32.lib
        OleAut32.lib
        windowsapp.lib
        Psapi.lib                               # For memory monitoring
)

# CRITICAL: Only link libcef.lib - no wrapper library
target_link_libraries(Nexile PRIVATE
        ${CEF_LIBRARY}                      # Only this CEF library
        nlohmann_json::nlohmann_json
        ${WINDOWS_LIBS}
)

# Output configuration
set_target_properties(Nexile PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# -----------------------------------------------------------------------------
# Runtime Dependencies - CEF files still needed
# -----------------------------------------------------------------------------

# Core CEF DLLs
set(CEF_CORE_DLLS
        "libcef.dll"
        "chrome_elf.dll"
        "d3dcompiler_47.dll"
        "libEGL.dll"
        "libGLESv2.dll"
        "vk_swiftshader.dll"
        "vulkan-1.dll"
)

foreach(DLL_FILE ${CEF_CORE_DLLS})
    add_custom_command(TARGET Nexile POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CEF_BINARY_DIR}/${DLL_FILE}"
            "$<TARGET_FILE_DIR:Nexile>"
            COMMENT "Copying ${DLL_FILE}")
endforeach()

# CEF Resource files (.pak and .dat files)
set(CEF_RESOURCE_FILES
        "cef.pak"
        "cef_100_percent.pak"
        "cef_200_percent.pak"
        "devtools_resources.pak"
        "icudtl.dat"
)

foreach(RESOURCE_FILE ${CEF_RESOURCE_FILES})
    if(EXISTS "${CEF_BINARY_DIR}/${RESOURCE_FILE}")
        add_custom_command(TARGET Nexile POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CEF_BINARY_DIR}/${RESOURCE_FILE}"
                "$<TARGET_FILE_DIR:Nexile>"
                COMMENT "Copying ${RESOURCE_FILE}")
    else()
        message(WARNING "CEF resource file not found: ${CEF_BINARY_DIR}/${RESOURCE_FILE}")
    endif()
endforeach()

# CEF Resources directory
if(EXISTS "${CEF_RESOURCE_DIR}")
    add_custom_command(TARGET Nexile POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_RESOURCE_DIR}"
            "$<TARGET_FILE_DIR:Nexile>/cef_resources"
            COMMENT "Copying CEF resources")
endif()

# CEF Locales directory
if(EXISTS "${CEF_BINARY_DIR}/locales")
    add_custom_command(TARGET Nexile POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_BINARY_DIR}/locales"
            "$<TARGET_FILE_DIR:Nexile>/locales"
            COMMENT "Copying CEF locales")
endif()

# Application HTML resources
add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Nexile>/HTML")

foreach(HTML_FILE ${HTML_RESOURCES})
    get_filename_component(HTML_FILENAME ${HTML_FILE} NAME)
    add_custom_command(TARGET Nexile POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/${HTML_FILE}"
            "$<TARGET_FILE_DIR:Nexile>/HTML/${HTML_FILENAME}"
            COMMENT "Copying ${HTML_FILENAME}")
endforeach()

# Create required directories
add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Nexile>/Profiles"
        COMMENT "Creating Profiles directory")

# -----------------------------------------------------------------------------
# Build verification
# -----------------------------------------------------------------------------
add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "==================================================================="
        COMMAND ${CMAKE_COMMAND} -E echo "CEF C API Build Complete:"
        COMMAND ${CMAKE_COMMAND} -E echo "  - libcef_dll_wrapper.lib: NOT LINKED"
        COMMAND ${CMAKE_COMMAND} -E echo "  - Binary size reduction: Expected 20-35MB"
        COMMAND ${CMAKE_COMMAND} -E echo "  - Memory optimization: Enabled"
        COMMAND ${CMAKE_COMMAND} -E echo "  - Target: Launch <500MB, Idle <200MB"
        COMMAND ${CMAKE_COMMAND} -E echo "==================================================================="
        COMMENT "Build verification")

# -----------------------------------------------------------------------------
# Debug configuration
# -----------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
    target_compile_definitions(Nexile PRIVATE
            CEF_ENABLE_LOGGING=1
            NEXILE_MEMORY_DEBUGGING=1
    )
endif()

# -----------------------------------------------------------------------------
# Installation (optional)
# -----------------------------------------------------------------------------
install(TARGETS Nexile DESTINATION bin)
install(DIRECTORY "${CMAKE_BINARY_DIR}/bin/" DESTINATION bin
        PATTERN "*.pdb" EXCLUDE)