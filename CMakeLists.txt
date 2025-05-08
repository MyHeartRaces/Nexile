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
# WebView2 SDK (headers + static import lib) via NuGet package
# -----------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    WebView2
    URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.1418.22
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(WebView2)

set(WEBVIEW2_INCLUDE_DIR "${webview2_SOURCE_DIR}/build/native/include")
file(GLOB WEBVIEW2_LIBRARY
     "${webview2_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib")

# -----------------------------------------------------------------------------
# Fixed‑Version WebView2 runtime to ship with the build (no installer needed)
# -----------------------------------------------------------------------------
set(WEBVIEW2_FIXED_RUNTIME_DIR "${CMAKE_SOURCE_DIR}/third_party/webview2")

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
# Include directories for the whole project
# -----------------------------------------------------------------------------
include_directories(${WEBVIEW2_INCLUDE_DIR})

# -----------------------------------------------------------------------------
# Source & resource files
# -----------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h")

set(RESOURCE_FILES
    "src/Resources.rc"
    "src/Utils/Logger.h")

set(HTML_RESOURCES
    "src/UI/HTML/main_overlay.html"
    "src/UI/HTML/price_check_module.html")

# -----------------------------------------------------------------------------
# Target
# -----------------------------------------------------------------------------
add_executable(Nexile WIN32 ${SOURCES} ${RESOURCE_FILES})

# Link libraries
# -----------------------------------------------------------------------------
set(WINDOWS_LIBS Shlwapi.lib Version.lib Ole32.lib OleAut32.lib windowsapp.lib)

target_link_libraries(Nexile PRIVATE
    ${WEBVIEW2_LIBRARY}
    nlohmann_json::nlohmann_json
    ${WINDOWS_LIBS})

# Output directory (Debug/Release both land in bin/)
set_target_properties(Nexile PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# -----------------------------------------------------------------------------
# Post‑build steps
# -----------------------------------------------------------------------------
# 1) Copy WebView2 loader DLL from NuGet SDK
add_custom_command(TARGET Nexile POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${webview2_SOURCE_DIR}/build/native/x64/WebView2Loader.dll"
            "$<TARGET_FILE_DIR:Nexile>")

# 2) Copy fixed runtime so the app is self‑contained
add_custom_command(TARGET Nexile POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${WEBVIEW2_FIXED_RUNTIME_DIR}"
            "$<TARGET_FILE_DIR:Nexile>/webview2_runtime")

# 3) Copy HTML resources next to the EXE
add_custom_command(TARGET Nexile POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Nexile>/HTML")

foreach(HTML_FILE ${HTML_RESOURCES})
    get_filename_component(HTML_FILENAME ${HTML_FILE} NAME)
    add_custom_command(TARGET Nexile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/${HTML_FILE}"
                "$<TARGET_FILE_DIR:Nexile>/HTML/${HTML_FILENAME}")
endforeach()

# 4) Ensure Profiles folder exists
add_custom_command(TARGET Nexile POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Nexile>/Profiles")
