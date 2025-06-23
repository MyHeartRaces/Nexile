# CMake Build System

This directory contains CMake scripts and modules for the Nexile build system.

## Files

### ValidateRuntime.cmake
Runtime dependency validation script that checks for:

- **Required executables**: Nexile.exe
- **CEF DLLs**: libcef.dll, chrome_elf.dll, d3dcompiler_47.dll, etc.
- **CEF resources**: .pak files, locales, icudtl.dat
- **Application files**: HTML resources, directories
- **Memory footprint estimation**: Binary size validation for C API optimization

#### Usage
Automatically executed during build via:
```cmake
add_custom_command(TARGET verify_build POST_BUILD
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/cmake/ValidateRuntime.cmake"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    COMMENT "Validating runtime dependencies"
)
```

#### Manual Execution
```bash
cd build/bin
cmake -P ../../cmake/ValidateRuntime.cmake
```

#### Validation Output
- ✓ **Found**: Required dependency present
- ⚠ **Warning**: Optional dependency missing
- ✗ **Error**: Required dependency missing (fails build)

#### Success Criteria
- All required CEF DLLs present
- All HTML resources copied correctly
- Binary size optimization achieved (Nexile.exe <50MB indicates successful C API migration)
- Total CEF footprint reasonable for deployment

The script provides detailed troubleshooting information for any missing dependencies and estimates memory footprint to verify the C API migration benefits are achieved.