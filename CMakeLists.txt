cmake_minimum_required(VERSION 3.20)
project(Nexile VERSION 0.1.0)

# Add custom modules path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add WebView2 SDK
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

# Find nlohmann/json
find_package(NlohmannJson REQUIRED)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
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
)

# HTML and CSS resources
set(HTML_RESOURCES
    "src/UI/HTML/main_overlay.html"
    "src/UI/HTML/price_check_module.html"
)

# Create executable
add_executable(Nexile WIN32 ${SOURCES} ${RESOURCE_FILES})

# Link libraries
target_link_libraries(Nexile
    ${WEBVIEW2_LIBRARY}
    nlohmann::json
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

# Setting up packaging with CPack
set(CPACK_PACKAGE_NAME "Nexile")
set(CPACK_PACKAGE_VENDOR "Nexile")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modular overlay assistant for Action RPGs")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Nexile")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_DISPLAY_NAME "Nexile - Game Overlay Assistant")

include(CPack)