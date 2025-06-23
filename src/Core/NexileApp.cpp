// NexileApp.cpp - ONLY the sections that need changes for C API migration
// Most of your file remains unchanged - only these specific changes needed:

// ================== INCLUDE CHANGES ==================
// REMOVE these lines from the top of your NexileApp.cpp:
/*
#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_command_line.h>
#include <include/wrapper/cef_helpers.h>
*/

// ADD this line instead:
#include "include/cef_app.h"  // Only for cef_do_message_loop_work

// ================== RUN METHOD CHANGES ==================
// In your NexileApp::Run() method, find this section:

// OLD C++ wrapper approach:
/*
    // Message loop with CEF work
    MSG msg = {};
    while (true) {
        // Process CEF work
        CefDoMessageLoopWork();

        // Process Windows messages
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                break;
            }// NexileApp.cpp - ONLY the sections that need changes for C API migration
// Most of your file remains unchanged - only these specific changes needed:

// ================== INCLUDE CHANGES ==================
// REMOVE these lines from the top of your NexileApp.cpp:
/*
#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_command_line.h>
#include <include/wrapper/cef_helpers.h>
*/

// ADD this line instead:
#include "include/cef_app.h"  // Only for cef_do_message_loop_work

// ================== RUN METHOD CHANGES ==================
// In your NexileApp::Run() method, find this section:

// OLD C++ wrapper approach:
/*
    // Message loop with CEF work
    MSG msg = {};
    while (true) {
        // Process CEF work
        CefDoMessageLoopWork();

        // Process Windows messages
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Small sleep to prevent high CPU usage
        Sleep(1);
    }
*/

// REPLACE with C API approach:
    // Message loop with CEF work
    MSG msg = {};
    while (true) {
        // Process CEF work - CRITICAL CHANGE: C API function
        cef_do_message_loop_work();

        // Process Windows messages (unchanged)
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Small sleep to prevent high CPU usage
        Sleep(1);
    }

// ================== CONSTRUCTOR CHANGES ==================
// In your NexileApp constructor, find this section:

// OLD logging:
/*
        LOG_INFO("Nexile initialized successfully with CEF");
*/

// REPLACE with:
        LOG_INFO("Nexile initialized successfully with CEF C API");

// ================== MEMORY OPTIMIZATION ADDITIONS ==================
// ADD this method to your NexileApp class for memory monitoring:

void NexileApp::LogMemoryUsage(const std::string& context) {
    #ifdef NEXILE_MEMORY_DEBUGGING
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        LOG_INFO("Memory [{}]: Working Set = {}MB, Peak = {}MB", 
                 context,
                 pmc.WorkingSetSize / 1024 / 1024,
                 pmc.PeakWorkingSetSize / 1024 / 1024);
    }
    #endif
}

// ADD these calls in strategic locations for memory monitoring:
// In constructor after overlay creation:
        LogMemoryUsage("Post-Overlay-Init");

// In destructor before overlay destruction:
        LogMemoryUsage("Pre-Overlay-Destroy");

// In ToggleOverlay when showing:
        LogMemoryUsage("Overlay-Show");

// In ToggleOverlay when hiding:
        LogMemoryUsage("Overlay-Hide");

// ================== INCLUDE ADDITIONS FOR MEMORY MONITORING ==================
// ADD to your includes at the top of NexileApp.cpp:
#include <Psapi.h>

// ADD to your NexileApp.h class declaration:
private:
    // Memory monitoring for C API optimization
    void LogMemoryUsage(const std::string& context);

// ================== THAT'S IT! ==================
// All other parts of your NexileApp.cpp remain exactly the same.
// The CEF integration is now handled entirely by OverlayWindow using C API.