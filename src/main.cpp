#include "Core/NexileApp.h"
#include <Windows.h>
#include <iostream>
#include <memory>

// FIXED: CEF C API includes - Different include structure for main process handling
#include "include/cef_app.h"           // For main functions like cef_execute_process
#include "include/cef_sandbox_win.h"   // For sandbox (if used)

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    // Enable heap corruption detection
    HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);

    // FIXED: CEF main args - C API structure
    cef_main_args_t main_args = {};
    main_args.instance = hInstance;

    // Optional: Sandbox support (usually disabled for overlays)
    void* sandbox_info = nullptr;
#if defined(CEF_USE_SANDBOX)
    // Note: Sandbox typically not used for overlay applications due to complexity
    // If needed, implement sandbox initialization here
    // CefScopedSandboxInfo scoped_sandbox;
    // sandbox_info = scoped_sandbox.sandbox_info();
#endif

    // FIXED: Using C API function instead of C++ wrapper
    // This handles subprocess execution for CEF's multi-process architecture
    int exit_code = cef_execute_process(&main_args, nullptr, sandbox_info);
    if (exit_code >= 0) {
        // This is a CEF subprocess (renderer, GPU, etc.), exit immediately
        return exit_code;
    }

    // If we reach here, this is the main browser process
    try {
        // FIXED: Create and run the main application
        // NexileApp constructor now handles CEF C API initialization internally
        auto app = std::make_unique<Nexile::NexileApp>(hInstance);

        // Run the application - this includes the CEF message loop integration
        int result = app->Run(nCmdShow);

        // FIXED: Explicit cleanup before CEF shutdown
        // This ensures proper destruction order for CEF C API
        app.reset();

        return result;
    }
    catch (const std::exception& e) {
        // FIXED: Enhanced error handling for CEF C API issues
        std::string errorMsg = "Nexile - Critical Error:\n\n";
        errorMsg += e.what();
        errorMsg += "\n\nCEF C API Error Details:\n";
        errorMsg += "This error occurred during CEF initialization or runtime.\n\n";
        errorMsg += "Troubleshooting steps:\n";
        errorMsg += "1. Ensure CEF binaries are present in application directory\n";
        errorMsg += "2. Check system has sufficient memory (>1GB available)\n";
        errorMsg += "3. Verify no other CEF applications are conflicting\n";
        errorMsg += "4. Try running as administrator\n";
        errorMsg += "5. Check Windows compatibility (Windows 10+ required)\n\n";
        errorMsg += "If the problem persists, check the log files in:\n";
        errorMsg += "%APPDATA%\\Nexile\\";

        MessageBoxA(NULL, errorMsg.c_str(), "Nexile - Startup Failed", MB_OK | MB_ICONERROR);

        // Also log to console if available
        std::cerr << "Nexile Error: " << e.what() << std::endl;
        std::cerr << "This appears to be a CEF C API initialization failure." << std::endl;

        return 1;
    }
    catch (...) {
        // FIXED: Catch-all handler for unknown exceptions
        const char* errorMsg =
            "Nexile - Unknown Error\n\n"
            "An unknown error occurred during application startup.\n"
            "This may be related to:\n\n"
            "• CEF C API initialization failure\n"
            "• Missing system dependencies\n"
            "• Insufficient system resources\n"
            "• Antivirus software interference\n\n"
            "Please try:\n"
            "1. Restarting your computer\n"
            "2. Running as administrator\n"
            "3. Temporarily disabling antivirus\n"
            "4. Checking Windows Event Viewer for details\n\n"
            "If the problem persists, please report this issue\n"
            "with your system specifications.";

        MessageBoxA(NULL, errorMsg, "Nexile - Unknown Error", MB_OK | MB_ICONERROR);

        return 2;
    }
}