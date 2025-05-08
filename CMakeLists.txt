cmake_minimum_required(VERSION 3.20)
project(Nexile VERSION 0.1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add custom modules path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# Make sure src directory is in include path
include_directories(${CMAKE_SOURCE_DIR}/src)

# Always use FetchContent for WebView2 since vcpkg integration seems problematic
include(FetchContent)
FetchContent_Declare(
    WebView2
    URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.1418.22
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(WebView2)

# Find WebView2 headers and libraries
set(WEBVIEW2_INCLUDE_DIR "${webview2_SOURCE_DIR}/build/native/include")
file(GLOB WEBVIEW2_LIBRARY "${webview2_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib")

# Try to find nlohmann/json - first with vcpkg if used, then with FindNlohmannJson module
if(DEFINED ENV{VCPKG_ROOT} OR DEFINED VCPKG_TARGET_TRIPLET)
    # Try with vcpkg, but don't require it
    find_package(nlohmann_json CONFIG)
endif()

# If not found via vcpkg, use our custom module
if(NOT nlohmann_json_FOUND)
    find_package(NlohmannJson)
    if(NlohmannJson_FOUND)
        # Create compatibility target
        if(NOT TARGET nlohmann_json::nlohmann_json AND TARGET nlohmann::json)
            add_library(nlohmann_json::nlohmann_json ALIAS nlohmann::json)
        endif()
    else()
        # Manual include path for nlohmann/json as a last resort
        message(STATUS "Using fallback method for nlohmann/json")
        include_directories(C:/json/include)
        # Create a dummy target for compatibility
        add_library(nlohmann_json_dummy INTERFACE)
        add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json_dummy)
    endif()
endif()

# Include directories
include_directories(
    ${WEBVIEW2_INCLUDE_DIR}
)

# Source files
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/*.h"
)

# Resource files
set(RESOURCE_FILES 
    "src/Resources.rc"
 "src/Utils/Logger.h")

# HTML and CSS resources
set(HTML_RESOURCES
    "src/UI/HTML/main_overlay.html"
    "src/UI/HTML/price_check_module.html"
)

# Create executable
add_executable(Nexile WIN32 ${SOURCES} ${RESOURCE_FILES} "src/Utils/Logger.h")

# Link libraries
target_link_libraries(Nexile PRIVATE
    ${WEBVIEW2_LIBRARY}
    nlohmann_json::nlohmann_json
    Shlwapi.lib
    Version.lib
    Ole32.lib
    OleAut32.lib
    windowsapp.lib
)

# Set output directory
set_target_properties(Nexile PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Copy WebView2 runtime files
add_custom_command(TARGET Nexile POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${webview2_SOURCE_DIR}/build/native/x64"
    "${CMAKE_BINARY_DIR}/bin"
)

# Copy HTML resources
add_custom_command(TARGET Nexile POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    "${CMAKE_BINARY_DIR}/bin/HTML"
)

foreach(HTML_FILE ${HTML_RESOURCES})
    get_filename_component(HTML_FILENAME ${HTML_FILE} NAME)
    add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/${HTML_FILE}"
        "${CMAKE_BINARY_DIR}/bin/HTML/${HTML_FILENAME}"
    )
endforeach()

# Create directories for game profiles
add_custom_command(TARGET Nexile POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    "${CMAKE_BINARY_DIR}/bin/Profiles"
)