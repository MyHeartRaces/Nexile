#include "Core/NexileApp.h"
#include <Windows.h>
#include <iostream>
#include <memory>

// CEF C API includes - CRITICAL CHANGE: Different include structure
#include "include/cef_app.h"           // For main functions like cef_execute_process
#include "include/cef_sandbox_win.h"   // For sandbox (if used)

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    // Enable heap corruption detection
    HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);

    // CEF main args - C API structure
    cef_main_args_t main_args = {};
    main_args.instance = hInstance;

    // Optional: Sandbox support (usually disabled for overlays)
    void* sandbox_info = nullptr;
#if defined(CEF_USE_SANDBOX)
    // Note: Sandbox typically not used for overlay applications
    // CefScopedSandboxInfo scoped_sandbox;
    // sandbox_info = scoped_sandbox.sandbox_info();
#endif

    // CRITICAL CHANGE: Using C API function instead of C++ wrapper
    // OLD: int exit_code = CefExecuteProcess(main_args, nullptr, sandbox_info);
    // NEW: Using C API directly
    int exit_code = cef_execute_process(&main_args, nullptr, sandbox_info);
    if (exit_code >= 0) {
        // This is a subprocess, exit immediately
        return exit_code;
    }

    try {
        // Create and run the main application
        // Note: NexileApp constructor now handles CEF C API initialization internally
        auto app = std::make_unique<Nexile::NexileApp>(hInstance);

        // Run the application - this includes the CEF message loop
        int result = app->Run(nCmdShow);

        // Explicit cleanup before CEF shutdown
        app.reset();

        return result;
    }
    catch (const std::exception& e) {
        // Enhanced error handling for CEF C API issues
        std::string errorMsg = "Nexile - Unhandled Exception: ";
        errorMsg += e.what();
        errorMsg += "\n\nThis may be related to CEF initialization. Please ensure:\n";
        errorMsg += "- CEF binaries are present\n";
        errorMsg += "- System has sufficient memory\n";
        errorMsg += "- No other CEF applications are conflicting";

        MessageBoxA(NULL, errorMsg.c_str(), "Nexile - Critical Error", MB_OK | MB_ICONERROR);
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    catch (...) {
        MessageBoxA(NULL,
                   "An unknown error occurred during application startup.\n"
                   "This may be related to CEF C API initialization failure.",
                   "Nexile - Critical Error",
                   MB_OK | MB_ICONERROR);
        return 1;
    }
}