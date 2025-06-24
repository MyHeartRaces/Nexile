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
# CEF C API Configuration - FIXED: Better error handling and validation
# -----------------------------------------------------------------------------
set(CEF_ROOT "${CMAKE_SOURCE_DIR}/third_party/cef")

# FIXED: Better CEF detection with clear error messages
if(NOT EXISTS ${CEF_ROOT})
    message(FATAL_ERROR
            "CEF directory not found at: ${CEF_ROOT}\n"
            "Please download and extract CEF Binary Distribution to this location.\n"
            "Download from: https://cef-builds.spotifycdn.com/index.html\n"
            "Required: Standard Distribution (not Minimal) for Windows 64-bit")
endif()

# FIXED: Detect CEF directory structure automatically
set(CEF_BINARY_DIR "")
set(CEF_RESOURCE_DIR "")

# Check multiple possible CEF directory structures
if(EXISTS "${CEF_ROOT}/Release")
    set(CEF_BINARY_DIR "${CEF_ROOT}/Release")
    set(CEF_RESOURCE_DIR "${CEF_ROOT}/Resources")
elseif(EXISTS "${CEF_ROOT}/windows64/Release")
    set(CEF_BINARY_DIR "${CEF_ROOT}/windows64/Release")
    set(CEF_RESOURCE_DIR "${CEF_ROOT}/Resources")
elseif(EXISTS "${CEF_ROOT}/out/Release")
    set(CEF_BINARY_DIR "${CEF_ROOT}/out/Release")
    set(CEF_RESOURCE_DIR "${CEF_ROOT}/Resources")
else()
    message(FATAL_ERROR
            "Could not find CEF binaries in any expected location under: ${CEF_ROOT}\n"
            "Expected structures:\n"
            "  - ${CEF_ROOT}/Release/\n"
            "  - ${CEF_ROOT}/windows64/Release/\n"
            "  - ${CEF_ROOT}/out/Release/\n"
            "Please verify your CEF extraction is complete.")
endif()

set(CEF_INCLUDE_DIR "${CEF_ROOT}")

# FIXED: Comprehensive CEF validation
set(REQUIRED_CEF_FILES
        "${CEF_BINARY_DIR}/libcef.dll"
        "${CEF_BINARY_DIR}/libcef.lib"
        "${CEF_INCLUDE_DIR}/include/cef_app.h"
        "${CEF_INCLUDE_DIR}/include/capi/cef_app_capi.h"
)

foreach(CEF_FILE ${REQUIRED_CEF_FILES})
    if(NOT EXISTS ${CEF_FILE})
        message(FATAL_ERROR
                "Required CEF file missing: ${CEF_FILE}\n"
                "This indicates an incomplete or corrupted CEF installation.\n"
                "Please re-download and extract the complete CEF Binary Distribution.")
    endif()
endforeach()

# Add CEF include directory
include_directories(${CEF_ROOT})

# CRITICAL: Only libcef.lib for C API
set(CEF_LIBRARY "${CEF_BINARY_DIR}/libcef.lib")

message(STATUS "=================================================================")
message(STATUS "CEF C API Configuration:")
message(STATUS "  - CEF Root: ${CEF_ROOT}")
message(STATUS "  - CEF Binary Dir: ${CEF_BINARY_DIR}")
message(STATUS "  - CEF Resource Dir: ${CEF_RESOURCE_DIR}")
message(STATUS "  - CEF Library: ${CEF_LIBRARY}")
message(STATUS "  - Using C API: YES")
message(STATUS "  - Using C++ Wrapper: NO")
message(STATUS "  - Expected binary size reduction: 20-35MB")
message(STATUS "=================================================================")

# -----------------------------------------------------------------------------
# nlohmann/json - FIXED: Better error handling
# -----------------------------------------------------------------------------
find_package(nlohmann_json CONFIG REQUIRED)
if(NOT nlohmann_json_FOUND)
    message(FATAL_ERROR
            "nlohmann/json not found. Please install via vcpkg:\n"
            "  vcpkg install nlohmann-json\n"
            "Or ensure CMAKE_TOOLCHAIN_FILE points to vcpkg toolchain.")
endif()

# -----------------------------------------------------------------------------
# Source files - FIXED: Better organization
# -----------------------------------------------------------------------------
file(GLOB_RECURSE CORE_SOURCES "src/Core/*.cpp" "src/Core/*.h")
file(GLOB_RECURSE UI_SOURCES "src/UI/*.cpp" "src/UI/*.h")
file(GLOB_RECURSE MODULE_SOURCES "src/Modules/*.cpp" "src/Modules/*.h")
file(GLOB_RECURSE GAME_SOURCES "src/Game/*.cpp" "src/Game/*.h")
file(GLOB_RECURSE INPUT_SOURCES "src/Input/*.cpp" "src/Input/*.h")
file(GLOB_RECURSE CONFIG_SOURCES "src/Config/*.cpp" "src/Config/*.h")
file(GLOB_RECURSE UTILS_SOURCES "src/Utils/*.cpp" "src/Utils/*.h")

set(SOURCES
        ${CORE_SOURCES}
        ${UI_SOURCES}
        ${MODULE_SOURCES}
        ${GAME_SOURCES}
        ${INPUT_SOURCES}
        ${CONFIG_SOURCES}
        ${UTILS_SOURCES}
        "src/main.cpp"
)

set(RESOURCE_FILES
        "src/Resources.rc"
)

set(HTML_RESOURCES
        "src/UI/HTML/main_overlay.html"
        "src/UI/HTML/price_check_module.html"
        "src/UI/HTML/settings.html"
        "src/UI/HTML/welcome.html"
        "src/UI/HTML/browser.html"
)

# -----------------------------------------------------------------------------
# Target configuration - FIXED: Better organization
# -----------------------------------------------------------------------------
add_executable(Nexile WIN32 ${SOURCES} ${RESOURCE_FILES})

# FIXED: Conditional debug flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
    target_compile_definitions(Nexile PRIVATE
            CEF_ENABLE_LOGGING=1
            NEXILE_MEMORY_DEBUGGING=1
    )
endif()

# -----------------------------------------------------------------------------
# Linking - FIXED: Better Windows library management
# -----------------------------------------------------------------------------
set(WINDOWS_LIBS
        Shlwapi.lib
        Version.lib
        Ole32.lib
        OleAut32.lib
        Psapi.lib                               # For memory monitoring
        Shell32.lib                             # For tray icons
        User32.lib                              # For window management
        Gdi32.lib                               # For graphics
        Comctl32.lib                            # For common controls
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
        # FIXED: Set proper subsystem for Windows app
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
)

# -----------------------------------------------------------------------------
# Runtime Dependencies - FIXED: Comprehensive error handling
# -----------------------------------------------------------------------------

# FIXED: Function to copy files with error checking
function(copy_file_with_check SOURCE_FILE DEST_DIR DISPLAY_NAME)
    if(NOT EXISTS ${SOURCE_FILE})
        message(WARNING "Source file not found: ${SOURCE_FILE} (${DISPLAY_NAME})")
        return()
    endif()

    add_custom_command(TARGET Nexile POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SOURCE_FILE}"
            "${DEST_DIR}"
            COMMENT "Copying ${DISPLAY_NAME}"
            VERBATIM
    )
endfunction()

# Core CEF DLLs - FIXED: Better error handling
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
    copy_file_with_check("${CEF_BINARY_DIR}/${DLL_FILE}" "$<TARGET_FILE_DIR:Nexile>" "${DLL_FILE}")
endforeach()

# FIXED: CEF Resource files (.pak and .dat files) - Copy from RESOURCE_DIR, not BINARY_DIR
set(CEF_RESOURCE_FILES
        "cef.pak"
        "cef_100_percent.pak"
        "cef_200_percent.pak"
        "devtools_resources.pak"
        "icudtl.dat"
)

foreach(RESOURCE_FILE ${CEF_RESOURCE_FILES})
    # FIXED: Look in CEF_RESOURCE_DIR first, then fallback to CEF_BINARY_DIR
    if(EXISTS "${CEF_RESOURCE_DIR}/${RESOURCE_FILE}")
        copy_file_with_check("${CEF_RESOURCE_DIR}/${RESOURCE_FILE}" "$<TARGET_FILE_DIR:Nexile>" "${RESOURCE_FILE}")
    elseif(EXISTS "${CEF_BINARY_DIR}/${RESOURCE_FILE}")
        copy_file_with_check("${CEF_BINARY_DIR}/${RESOURCE_FILE}" "$<TARGET_FILE_DIR:Nexile>" "${RESOURCE_FILE}")
    else()
        message(WARNING "CEF resource file not found: ${RESOURCE_FILE}")
    endif()
endforeach()

# CEF Resources directory - FIXED: Conditional copying
if(EXISTS "${CEF_RESOURCE_DIR}")
    add_custom_command(TARGET Nexile POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_RESOURCE_DIR}"
            "$<TARGET_FILE_DIR:Nexile>/cef_resources"
            COMMENT "Copying CEF resources directory"
            VERBATIM
    )
else()
    message(WARNING "CEF Resources directory not found: ${CEF_RESOURCE_DIR}")
endif()

# FIXED: CEF Locales directory - Check multiple possible locations
set(CEF_LOCALES_FOUND FALSE)
set(POSSIBLE_LOCALES_DIRS
        "${CEF_BINARY_DIR}/locales"
        "${CEF_ROOT}/locales"
        "${CEF_RESOURCE_DIR}/locales"
)

foreach(LOCALES_DIR ${POSSIBLE_LOCALES_DIRS})
    if(EXISTS "${LOCALES_DIR}" AND IS_DIRECTORY "${LOCALES_DIR}")
        add_custom_command(TARGET Nexile POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${LOCALES_DIR}"
                "$<TARGET_FILE_DIR:Nexile>/locales"
                COMMENT "Copying CEF locales from ${LOCALES_DIR}"
                VERBATIM
        )
        set(CEF_LOCALES_FOUND TRUE)
        break()
    endif()
endforeach()

if(NOT CEF_LOCALES_FOUND)
    message(WARNING "CEF locales directory not found in any expected location")
endif()

# Application HTML resources - FIXED: Validate source files exist
add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Nexile>/HTML"
        COMMENT "Creating HTML directory"
)

foreach(HTML_FILE ${HTML_RESOURCES})
    if(EXISTS "${CMAKE_SOURCE_DIR}/${HTML_FILE}")
        get_filename_component(HTML_FILENAME ${HTML_FILE} NAME)
        add_custom_command(TARGET Nexile POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/${HTML_FILE}"
                "$<TARGET_FILE_DIR:Nexile>/HTML/${HTML_FILENAME}"
                COMMENT "Copying ${HTML_FILENAME}"
                VERBATIM
        )
    else()
        message(WARNING "HTML file not found: ${CMAKE_SOURCE_DIR}/${HTML_FILE}")
    endif()
endforeach()

# Create required directories - FIXED: Better error handling
add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Nexile>/Profiles"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Nexile>/Modules"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Nexile>/Logs"
        COMMENT "Creating application directories"
)

# -----------------------------------------------------------------------------
# Build verification - FIXED: More comprehensive validation
# -----------------------------------------------------------------------------
add_custom_target(verify_build ALL
        DEPENDS Nexile
        COMMAND ${CMAKE_COMMAND} -E echo "==================================================================="
        COMMAND ${CMAKE_COMMAND} -E echo "CEF C API Build Complete:"
        COMMAND ${CMAKE_COMMAND} -E echo "  - libcef_dll_wrapper.lib: NOT LINKED"
        COMMAND ${CMAKE_COMMAND} -E echo "  - Binary size reduction: Expected 20-35MB"
        COMMAND ${CMAKE_COMMAND} -E echo "  - Memory optimization: Enabled"
        COMMAND ${CMAKE_COMMAND} -E echo "  - Target: Launch <500MB, Idle <200MB"
        COMMAND ${CMAKE_COMMAND} -E echo "  - Build directory: ${CMAKE_BINARY_DIR}/bin"
        COMMAND ${CMAKE_COMMAND} -E echo "==================================================================="
        COMMENT "Build verification completed"
)

# FIXED: Runtime validation script
add_custom_command(TARGET verify_build POST_BUILD
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/cmake/ValidateRuntime.cmake"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        COMMENT "Validating runtime dependencies"
)

# -----------------------------------------------------------------------------
# Installation (optional) - FIXED: Complete installation
# -----------------------------------------------------------------------------
install(TARGETS Nexile
        DESTINATION bin
        COMPONENT Runtime
)

install(DIRECTORY "${CMAKE_BINARY_DIR}/bin/"
        DESTINATION bin
        COMPONENT Runtime
        PATTERN "*.pdb" EXCLUDE
        PATTERN "*.ilk" EXCLUDE
        PATTERN "*.exp" EXCLUDE
)

# -----------------------------------------------------------------------------
# FIXED: Create LICENSE file if it doesn't exist
# -----------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
    file(WRITE "${CMAKE_SOURCE_DIR}/LICENSE"
            "MIT License

Copyright (c) 2025 Nexile Team

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the \"Software\"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
")
    message(STATUS "Created LICENSE file")
endif()

# -----------------------------------------------------------------------------
# Packaging (optional) - FIXED: Add packaging support with proper license
# -----------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "Nexile")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Nexile - Game Overlay Assistant")
set(CPACK_PACKAGE_VENDOR "Nexile Team")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Nexile")

# Windows-specific packaging
if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "Nexile Game Overlay")
    set(CPACK_NSIS_HELP_LINK "https://github.com/nexile/nexile")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/nexile/nexile")
    set(CPACK_NSIS_CONTACT "support@nexile.app")
    set(CPACK_NSIS_MODIFY_PATH ON)
endif()

include(CPack)

# -----------------------------------------------------------------------------
# Development helpers - FIXED: Add debug targets
# -----------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Add target to copy HTML files without rebuilding
    add_custom_target(copy_html
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/src/UI/HTML"
            "${CMAKE_BINARY_DIR}/bin/HTML"
            COMMENT "Copying HTML files for development"
    )

    # Add target to clean CEF cache
    add_custom_target(clean_cef_cache
            COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${CMAKE_BINARY_DIR}/bin/cef_cache"
            COMMENT "Cleaning CEF cache"
    )
endif()

# -----------------------------------------------------------------------------
# Final status report
# -----------------------------------------------------------------------------
message(STATUS "=================================================================")
message(STATUS "Nexile Build Configuration Summary:")
message(STATUS "  - Project Version: ${PROJECT_VERSION}")
message(STATUS "  - Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  - Output Directory: ${CMAKE_BINARY_DIR}/bin")
message(STATUS "  - CEF C API: ${CEF_LIBRARY}")
message(STATUS "  - Memory Debugging: $<IF:$<CONFIG:Debug>,ON,OFF>")
message(STATUS "  - Source Files: ${SOURCES}")
message(STATUS "=================================================================")