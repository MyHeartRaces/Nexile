#include "Core/NexileApp.h"
#include <Windows.h>
#include <iostream>
#include <memory>

// CEF includes
#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_command_line.h>
#include <include/wrapper/cef_helpers.h>
#include <include/cef_sandbox_win.h>


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    // Enable heap corruption detection
    HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);

    // CEF main args
    CefMainArgs main_args(hInstance);

    // Optional: Use sandbox for better isolation
    void* sandbox_info = nullptr;
#if defined(CEF_USE_SANDBOX)
    CefScopedSandboxInfo scoped_sandbox;
    sandbox_info = scoped_sandbox.sandbox_info();
#endif

    // Check if this is a subprocess
    int exit_code = CefExecuteProcess(main_args, nullptr, sandbox_info);
    if (exit_code >= 0) {
        return exit_code;
    }

    try {
        // Create and run the application
        auto app = std::make_unique<Nexile::NexileApp>(hInstance);
        return app->Run(nCmdShow);
    }
    catch (const std::exception& e) {
        MessageBoxA(NULL, e.what(), "Nexile - Unhandled Exception", MB_OK | MB_ICONERROR);
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}