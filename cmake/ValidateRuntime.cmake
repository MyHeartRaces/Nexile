# ValidateRuntime.cmake - Validate that all required runtime dependencies are present

cmake_minimum_required(VERSION 3.20)

message(STATUS "=================================================================")
message(STATUS "Validating Nexile Runtime Dependencies...")
message(STATUS "=================================================================")

# Get the current directory (should be the build output directory)
set(BUILD_DIR ${CMAKE_CURRENT_LIST_DIR})
if(NOT BUILD_DIR)
    set(BUILD_DIR ${CMAKE_BINARY_DIR}/bin)
endif()

message(STATUS "Validating directory: ${BUILD_DIR}")

# FIXED: Comprehensive validation lists
set(REQUIRED_EXECUTABLES
        "Nexile.exe"
)

set(REQUIRED_CEF_DLLS
        "libcef.dll"
        "chrome_elf.dll"
        "d3dcompiler_47.dll"
        "libEGL.dll"
        "libGLESv2.dll"
)

set(OPTIONAL_CEF_DLLS
        "vk_swiftshader.dll"
        "vulkan-1.dll"
)

set(REQUIRED_CEF_RESOURCES
        "cef.pak"
        "cef_100_percent.pak"
        "cef_200_percent.pak"
        "icudtl.dat"
)

set(OPTIONAL_CEF_RESOURCES
        "devtools_resources.pak"
)

set(REQUIRED_DIRECTORIES
        "HTML"
        "Profiles"
        "Modules"
        "Logs"
)

set(OPTIONAL_DIRECTORIES
        "locales"
        "cef_resources"
)

set(REQUIRED_HTML_FILES
        "HTML/main_overlay.html"
        "HTML/price_check_module.html"
        "HTML/settings.html"
        "HTML/welcome.html"
        "HTML/browser.html"
)

# Validation functions
set(VALIDATION_ERRORS 0)
set(VALIDATION_WARNINGS 0)

function(validate_file FILEPATH DESCRIPTION IS_REQUIRED)
    if(EXISTS "${BUILD_DIR}/${FILEPATH}")
        message(STATUS "✓ Found ${DESCRIPTION}: ${FILEPATH}")
    else()
        if(${IS_REQUIRED})
            message(STATUS "✗ MISSING ${DESCRIPTION}: ${FILEPATH}")
            math(EXPR VALIDATION_ERRORS "${VALIDATION_ERRORS} + 1" OUTPUT_FORMAT DECIMAL)
            set(VALIDATION_ERRORS ${VALIDATION_ERRORS} PARENT_SCOPE)
        else()
            message(STATUS "⚠ Optional ${DESCRIPTION} missing: ${FILEPATH}")
            math(EXPR VALIDATION_WARNINGS "${VALIDATION_WARNINGS} + 1" OUTPUT_FORMAT DECIMAL)
            set(VALIDATION_WARNINGS ${VALIDATION_WARNINGS} PARENT_SCOPE)
        endif()
    endif()
endfunction()

function(validate_directory DIRPATH DESCRIPTION IS_REQUIRED)
    if(EXISTS "${BUILD_DIR}/${DIRPATH}" AND IS_DIRECTORY "${BUILD_DIR}/${DIRPATH}")
        message(STATUS "✓ Found ${DESCRIPTION}: ${DIRPATH}/")
    else()
        if(${IS_REQUIRED})
            message(STATUS "✗ MISSING ${DESCRIPTION}: ${DIRPATH}/")
            math(EXPR VALIDATION_ERRORS "${VALIDATION_ERRORS} + 1" OUTPUT_FORMAT DECIMAL)
            set(VALIDATION_ERRORS ${VALIDATION_ERRORS} PARENT_SCOPE)
        else()
            message(STATUS "⚠ Optional ${DESCRIPTION} missing: ${DIRPATH}/")
            math(EXPR VALIDATION_WARNINGS "${VALIDATION_WARNINGS} + 1" OUTPUT_FORMAT DECIMAL)
            set(VALIDATION_WARNINGS ${VALIDATION_WARNINGS} PARENT_SCOPE)
        endif()
    endif()
endfunction()

# Validate main executables
message(STATUS "\n--- Validating Executables ---")
foreach(EXECUTABLE ${REQUIRED_EXECUTABLES})
    validate_file(${EXECUTABLE} "executable" TRUE)
endforeach()

# Validate CEF DLLs
message(STATUS "\n--- Validating CEF DLLs ---")
foreach(DLL ${REQUIRED_CEF_DLLS})
    validate_file(${DLL} "CEF DLL" TRUE)
endforeach()

foreach(DLL ${OPTIONAL_CEF_DLLS})
    validate_file(${DLL} "CEF DLL" FALSE)
endforeach()

# Validate CEF Resources
message(STATUS "\n--- Validating CEF Resources ---")
foreach(RESOURCE ${REQUIRED_CEF_RESOURCES})
    validate_file(${RESOURCE} "CEF resource" TRUE)
endforeach()

foreach(RESOURCE ${OPTIONAL_CEF_RESOURCES})
    validate_file(${RESOURCE} "CEF resource" FALSE)
endforeach()

# Validate directories
message(STATUS "\n--- Validating Directories ---")
foreach(DIR ${REQUIRED_DIRECTORIES})
    validate_directory(${DIR} "directory" TRUE)
endforeach()

foreach(DIR ${OPTIONAL_DIRECTORIES})
    validate_directory(${DIR} "directory" FALSE)
endforeach()

# Validate HTML files
message(STATUS "\n--- Validating HTML Files ---")
foreach(HTML_FILE ${REQUIRED_HTML_FILES})
    validate_file(${HTML_FILE} "HTML file" TRUE)
endforeach()

# Additional checks
message(STATUS "\n--- Additional Checks ---")

# Check file sizes for CEF DLLs (basic sanity check)
if(EXISTS "${BUILD_DIR}/libcef.dll")
    file(SIZE "${BUILD_DIR}/libcef.dll" LIBCEF_SIZE)
    if(LIBCEF_SIZE LESS 100000000)  # Less than 100MB is suspicious
        message(STATUS "⚠ libcef.dll seems unusually small (${LIBCEF_SIZE} bytes)")
        math(EXPR VALIDATION_WARNINGS "${VALIDATION_WARNINGS} + 1")
    else()
        message(STATUS "✓ libcef.dll size looks reasonable (${LIBCEF_SIZE} bytes)")
    endif()
endif()

# Check if locales directory has content
if(EXISTS "${BUILD_DIR}/locales" AND IS_DIRECTORY "${BUILD_DIR}/locales")
    file(GLOB LOCALE_FILES "${BUILD_DIR}/locales/*.pak")
    list(LENGTH LOCALE_FILES LOCALE_COUNT)
    if(LOCALE_COUNT GREATER 0)
        message(STATUS "✓ Found ${LOCALE_COUNT} locale files")
    else()
        message(STATUS "⚠ Locales directory exists but is empty")
        math(EXPR VALIDATION_WARNINGS "${VALIDATION_WARNINGS} + 1")
    endif()
endif()

# Memory estimation
message(STATUS "\n--- Memory Footprint Estimation ---")
if(EXISTS "${BUILD_DIR}/Nexile.exe")
    file(SIZE "${BUILD_DIR}/Nexile.exe" NEXILE_SIZE)
    math(EXPR NEXILE_SIZE_MB "${NEXILE_SIZE} / 1024 / 1024")
    message(STATUS "Nexile.exe size: ${NEXILE_SIZE_MB} MB")

    if(NEXILE_SIZE_MB GREATER 50)
        message(STATUS "⚠ Nexile.exe is larger than expected (${NEXILE_SIZE_MB} MB > 50 MB)")
        message(STATUS "  This might indicate the C API migration didn't reduce binary size as expected")
        math(EXPR VALIDATION_WARNINGS "${VALIDATION_WARNINGS} + 1")
    else()
        message(STATUS "✓ Nexile.exe size looks optimized (${NEXILE_SIZE_MB} MB)")
    endif()
endif()

# Calculate total CEF footprint
set(TOTAL_CEF_SIZE 0)
foreach(DLL ${REQUIRED_CEF_DLLS} ${OPTIONAL_CEF_DLLS})
    if(EXISTS "${BUILD_DIR}/${DLL}")
        file(SIZE "${BUILD_DIR}/${DLL}" DLL_SIZE)
        math(EXPR TOTAL_CEF_SIZE "${TOTAL_CEF_SIZE} + ${DLL_SIZE}")
    endif()
endforeach()

foreach(RESOURCE ${REQUIRED_CEF_RESOURCES} ${OPTIONAL_CEF_RESOURCES})
    if(EXISTS "${BUILD_DIR}/${RESOURCE}")
        file(SIZE "${BUILD_DIR}/${RESOURCE}" RESOURCE_SIZE)
        math(EXPR TOTAL_CEF_SIZE "${TOTAL_CEF_SIZE} + ${RESOURCE_SIZE}")
    endif()
endforeach()

math(EXPR TOTAL_CEF_SIZE_MB "${TOTAL_CEF_SIZE} / 1024 / 1024")
message(STATUS "Total CEF footprint: ${TOTAL_CEF_SIZE_MB} MB")

# Final validation summary
message(STATUS "\n=================================================================")
message(STATUS "Validation Summary:")
message(STATUS "  Errors: ${VALIDATION_ERRORS}")
message(STATUS "  Warnings: ${VALIDATION_WARNINGS}")

if(VALIDATION_ERRORS GREATER 0)
    message(STATUS "  Status: ✗ VALIDATION FAILED")
    message(STATUS "")
    message(STATUS "Critical files are missing. The application may not run correctly.")
    message(STATUS "Please check your CEF installation and build configuration.")
    message(FATAL_ERROR "Runtime validation failed with ${VALIDATION_ERRORS} errors")
else()
    if(VALIDATION_WARNINGS GREATER 0)
        message(STATUS "  Status: ⚠ VALIDATION PASSED WITH WARNINGS")
        message(STATUS "")
        message(STATUS "Some optional files are missing, but the application should run.")
        message(STATUS "Consider addressing the warnings for optimal functionality.")
    else()
        message(STATUS "  Status: ✓ VALIDATION PASSED")
        message(STATUS "")
        message(STATUS "All required dependencies are present.")
    endif()

    message(STATUS "")
    message(STATUS "Build appears ready for testing!")
    message(STATUS "Memory optimization target: Launch <500MB, Idle <200MB")
endif()

message(STATUS "=================================================================")