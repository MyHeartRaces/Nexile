#include "Core/NexileApp.h"
#include <Windows.h>
#include <iostream>
#include <memory>

// CEF includes
#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_command_line.h>
#include <include/wrapper/cef_helpers.h>

// Entry point for Windows application
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Enable console for debugging (can be removed in release)
#ifdef _DEBUG
    AllocConsole();
    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
#endif

    // CEF expects main args
    CefMainArgs main_args(hInstance);

    // Check if this is a subprocess
    int exit_code = CefExecuteProcess(main_args, nullptr, nullptr);
    if (exit_code >= 0) {
        // This was a subprocess, exit
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