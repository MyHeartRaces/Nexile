#include "Core/NexileApp.h"
#include "UI/OverlayWindow.h"
#include "Game/GameDetector.h"
#include "Input/HotkeyManager.h"
#include "Config/ProfileManager.h"
#include "Modules/PriceCheckModule.h"
#include "Modules/SettingsModule.h"
#include "Utils/Utils.h"
#include "Utils/Logger.h"

// FIXED: CEF C API include (only what we need)
#include "include/cef_app.h"  // Only for cef_do_message_loop_work
#include <Psapi.h>           // For memory monitoring
#include <nlohmann/json.hpp>

#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>

#pragma comment(lib, "Psapi.lib")

namespace Nexile {

    // FIXED: Initialize static instance
    NexileApp* NexileApp::s_instance = nullptr;

    NexileApp::NexileApp(HINSTANCE hInstance)
        : m_hInstance(hInstance),
          m_mainWindow(nullptr),
          m_activeGame(GameID::None),
          m_overlayVisible(false),
          m_inSettingsMode(false),
          m_browserOpen(false),
          m_stopIdleTimer(false),
          m_idleTimerStarted(false) {

        // Set global instance
        s_instance = this;

        // Initialize logging
        Logger::GetInstance().SetLogLevel(LogLevel::Info);
        Logger::GetInstance().SetLogToConsole(true);

        std::string logPath = Utils::CombinePath(Utils::GetAppDataPath(), "nexile.log");
        Logger::GetInstance().SetLogToFile(true, logPath);

        LOG_INFO("=================================================================");
        LOG_INFO("Nexile v0.1.0 Starting Up (CEF C API Version)");
        LOG_INFO("=================================================================");

        LogMemoryUsage("App-Constructor-Start");

        // Ensure application directories exist
        std::string appDataPath = Utils::GetAppDataPath();
        Utils::CreateDirectory(appDataPath);

        // Initialize window class and main window
        RegisterWindowClass();
        InitializeWindow();

        // Initialize managers
        m_profileManager = std::make_unique<ProfileManager>();
        m_hotkeyManager = std::make_unique<HotkeyManager>(this);
        m_gameDetector = std::make_unique<GameDetector>();

        // Set up manager relationships
        m_profileManager->SetHotkeyManager(std::unique_ptr<HotkeyManager>());  // ProfileManager owns HotkeyManager

        // Create overlay window - FIXED: This now handles CEF C API internally
        m_overlayWindow = std::make_unique<OverlayWindow>(this);
        m_profileManager->SetOverlayWindow(m_overlayWindow.get());

        LogMemoryUsage("Post-Overlay-Init");

        // Register overlay web message callback for communication
        m_overlayWindow->RegisterWebMessageCallback([this](const std::wstring& message) {
            try {
                std::string msgStr = Utils::WideStringToString(message);
                nlohmann::json msg = nlohmann::json::parse(msgStr);

                std::string action = msg.value("action", "");

                if (action == "toggle_overlay") {
                    ToggleOverlay();
                } else if (action == "open_settings") {
                    OnHotkeyPressed(HotkeyManager::HOTKEY_GAME_SETTINGS);
                } else if (action == "open_browser") {
                    OnHotkeyPressed(HotkeyManager::HOTKEY_BROWSER);
                } else if (action == "show_module") {
                    std::string moduleId = msg.value("moduleId", "");
                    if (!moduleId.empty()) {
                        auto module = GetModule(moduleId);
                        if (module) {
                            m_overlayWindow->LoadModuleUI(module);
                            SetOverlayVisible(true);
                        }
                    }
                } else if (action == "close_browser") {
                    m_browserOpen = false;
                    m_overlayWindow->LoadMainOverlayUI();
                } else if (action == "module_closed") {
                    // Module was closed, return to main overlay
                    if (m_overlayVisible) {
                        m_overlayWindow->LoadMainOverlayUI();
                    }
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Error processing web message: {}", e.what());
            }
        });

        // Initialize modules
        InitializeModules();

        // Setup system tray
        AddTrayIcon();

        // Start game detection
        m_gameDetector->StartDetection([this](GameID gameId) {
            OnGameChanged(gameId);
        });

        // Start hotkey registration
        m_hotkeyManager->RegisterGlobalHotkeys();

        // Start idle timer
        m_lastActivityTime = std::chrono::steady_clock::now();
        m_stopIdleTimer = false;
        m_idleTimerThread = std::thread(&NexileApp::IdleTimerThreadFunc, this);
        m_idleTimerStarted = true;

        LOG_INFO("Nexile initialized successfully with CEF C API");
        LogMemoryUsage("App-Constructor-End");
    }

    NexileApp::~NexileApp() {
        LOG_INFO("Shutting down Nexile application");
        LogMemoryUsage("Pre-Overlay-Destroy");

        // Stop idle timer
        if (m_idleTimerStarted) {
            m_stopIdleTimer = true;
            if (m_idleTimerThread.joinable()) {
                m_idleTimerThread.join();
            }
        }

        // Stop game detection
        if (m_gameDetector) {
            m_gameDetector->StopDetection();
        }

        // Unregister hotkeys
        if (m_hotkeyManager) {
            m_hotkeyManager->UnregisterAllHotkeys();
        }

        // Remove tray icon
        RemoveTrayIcon();

        // Clean up overlay window (this also shuts down CEF)
        m_overlayWindow.reset();

        // Clean up other managers
        m_profileManager.reset();
        m_hotkeyManager.reset();
        m_gameDetector.reset();

        // Clear static instance
        s_instance = nullptr;

        LOG_INFO("Nexile shutdown completed");
    }

    int NexileApp::Run(int nCmdShow) {
        LOG_INFO("Starting Nexile message loop");

        // Show main window (hidden tray application)
        ShowWindow(m_mainWindow, SW_HIDE);

        // FIXED: Message loop with CEF C API work
        MSG msg = {};
        while (true) {
            // CRITICAL CHANGE: Process CEF work using C API function
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

        LOG_INFO("Nexile message loop ended");
        return static_cast<int>(msg.wParam);
    }

    void NexileApp::RegisterWindowClass() {
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = m_hInstance;
        wc.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(101)); // IDI_NEXILE_ICON
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"NexileMainWindow";
        wc.hIconSm = LoadIcon(m_hInstance, MAKEINTRESOURCE(102)); // IDI_NEXILE_ICON_SMALL

        if (!RegisterClassEx(&wc)) {
            throw std::runtime_error("Failed to register main window class");
        }
    }

    void NexileApp::InitializeWindow() {
        m_mainWindow = CreateWindowEx(
            0,
            L"NexileMainWindow",
            L"Nexile",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            800, 600,
            nullptr, nullptr,
            m_hInstance,
            this
        );

        if (!m_mainWindow) {
            throw std::runtime_error("Failed to create main window");
        }
    }

    LRESULT CALLBACK NexileApp::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        NexileApp* app = nullptr;

        if (uMsg == WM_NCCREATE) {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            app = static_cast<NexileApp*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            app = reinterpret_cast<NexileApp*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        if (app) {
            return app->HandleMessage(hwnd, uMsg, wParam, lParam);
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    LRESULT NexileApp::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_HOTKEY: {
                int hotkeyId = static_cast<int>(wParam);
                OnHotkeyPressed(hotkeyId);
                return 0;
            }

            case WM_TRAYICON: {
                ProcessTrayMessage(wParam, lParam);
                return 0;
            }

            case WM_CLOSE:
                // Hide to tray instead of closing
                ShowWindow(hwnd, SW_HIDE);
                return 0;

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;

            default:
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }

    void NexileApp::ProcessTrayMessage(WPARAM wParam, LPARAM lParam) {
        if (wParam == 1) { // Our tray icon ID
            switch (lParam) {
                case WM_RBUTTONUP: {
                    // Show context menu
                    POINT pt;
                    GetCursorPos(&pt);

                    HMENU hMenu = CreatePopupMenu();
                    AppendMenu(hMenu, MF_STRING, 1001, L"Toggle Overlay");
                    AppendMenu(hMenu, MF_STRING, 1002, L"Settings");
                    AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
                    AppendMenu(hMenu, MF_STRING, 1003, L"Exit");

                    SetForegroundWindow(m_mainWindow);
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                                           pt.x, pt.y, 0, m_mainWindow, nullptr);

                    switch (cmd) {
                        case 1001: ToggleOverlay(); break;
                        case 1002: ShowSettingsDialog(); break;
                        case 1003: PostQuitMessage(0); break;
                    }

                    DestroyMenu(hMenu);
                    break;
                }

                case WM_LBUTTONDBLCLK:
                    ToggleOverlay();
                    break;
            }
        }
    }

    void NexileApp::AddTrayIcon() {
        memset(&m_trayIconData, 0, sizeof(NOTIFYICONDATA));
        m_trayIconData.cbSize = sizeof(NOTIFYICONDATA);
        m_trayIconData.hWnd = m_mainWindow;
        m_trayIconData.uID = 1;
        m_trayIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        m_trayIconData.uCallbackMessage = WM_TRAYICON;
        m_trayIconData.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(101));
        wcscpy_s(m_trayIconData.szTip, L"Nexile - Game Overlay Assistant");

        Shell_NotifyIcon(NIM_ADD, &m_trayIconData);
    }

    void NexileApp::RemoveTrayIcon() {
        Shell_NotifyIcon(NIM_DELETE, &m_trayIconData);
    }

    void NexileApp::ShowSettingsDialog() {
        if (m_inSettingsMode) {
            // Already in settings, just show overlay
            SetOverlayVisible(true);
            return;
        }

        m_inSettingsMode = true;

        // Load settings module UI
        auto settingsModule = GetModule("settings");
        if (settingsModule) {
            m_overlayWindow->LoadModuleUI(settingsModule);
            SetOverlayVisible(true);
        } else {
            // Fallback to basic settings
            m_overlayWindow->Navigate(L"nexile://settings.html");
            SetOverlayVisible(true);
        }
    }

    void NexileApp::ToggleOverlay() {
        SetOverlayVisible(!m_overlayVisible);
        UpdateActivityTimestamp();

        if (m_overlayVisible) {
            LogMemoryUsage("Overlay-Show");
        } else {
            LogMemoryUsage("Overlay-Hide");
        }
    }

    void NexileApp::SetOverlayVisible(bool visible) {
        m_overlayVisible = visible;

        if (m_overlayWindow) {
            if (visible) {
                // If in settings mode, keep settings UI
                if (!m_inSettingsMode && !m_browserOpen) {
                    m_overlayWindow->LoadMainOverlayUI();
                }
                m_overlayWindow->Show();
            } else {
                m_overlayWindow->Hide();
                // Exit settings mode when hiding overlay
                m_inSettingsMode = false;
                m_browserOpen = false;
            }
        }
    }

    void NexileApp::OnHotkeyPressed(int hotkeyId) {
        UpdateActivityTimestamp();

        switch (hotkeyId) {
            case HotkeyManager::HOTKEY_TOGGLE_OVERLAY:
                ToggleOverlay();
                break;

            case HotkeyManager::HOTKEY_GAME_SETTINGS:
                if (m_inSettingsMode) {
                    m_inSettingsMode = false;
                    if (m_overlayVisible) {
                        m_overlayWindow->LoadMainOverlayUI();
                    } else {
                        SetOverlayVisible(false);
                    }
                } else {
                    ShowSettingsDialog();
                }
                break;

            case HotkeyManager::HOTKEY_BROWSER:
                if (m_browserOpen) {
                    m_browserOpen = false;
                    if (m_overlayVisible) {
                        m_overlayWindow->LoadMainOverlayUI();
                    }
                } else {
                    m_browserOpen = true;
                    m_overlayWindow->LoadBrowserPage();
                    SetOverlayVisible(true);
                }
                break;

            case HotkeyManager::HOTKEY_PRICE_CHECK: {
                auto priceCheckModule = GetModule("price_check");
                if (priceCheckModule && priceCheckModule->IsEnabled()) {
                    priceCheckModule->OnHotkeyPressed(hotkeyId);
                }
                break;
            }

            default:
                // Forward to modules
                for (auto& [moduleId, module] : m_modules) {
                    if (module && module->IsEnabled()) {
                        module->OnHotkeyPressed(hotkeyId);
                    }
                }
                break;
        }
    }

    void NexileApp::OnGameChanged(GameID gameId) {
        if (m_activeGame == gameId) {
            return; // No change
        }

        LOG_INFO("Game changed: {} -> {}", GameIDToString(m_activeGame), GameIDToString(gameId));

        m_activeGame = gameId;

        // Load profile for new game
        if (m_profileManager) {
            m_profileManager->LoadProfile(gameId);
        }

        // Notify modules of game change
        for (auto& [moduleId, module] : m_modules) {
            if (module) {
                module->OnGameChange(gameId);
            }
        }

        // Load game-specific modules
        LoadModulesForGame(gameId);
    }

    void NexileApp::InitializeModules() {
        LOG_INFO("Initializing built-in modules");

        // Create built-in modules
        auto priceCheckModule = std::make_shared<PriceCheckModule>();
        auto settingsModule = std::make_shared<SettingsModule>();

        // Register modules
        m_modules[priceCheckModule->GetModuleID()] = priceCheckModule;
        m_modules[settingsModule->GetModuleID()] = settingsModule;

        // Initialize modules with current game
        for (auto& [moduleId, module] : m_modules) {
            module->OnModuleLoad(m_activeGame);
        }

        // Load external modules from directory
        std::string modulesDir = Utils::CombinePath(Utils::GetModulePath(), "Modules");
        if (Utils::DirectoryExists(modulesDir)) {
            LoadModulesFromDirectory(modulesDir);
        }

        LOG_INFO("Module initialization completed. Loaded {} modules", m_modules.size());
    }

    bool NexileApp::LoadModule(const std::string& moduleId) {
        // Check if already loaded
        if (m_modules.find(moduleId) != m_modules.end()) {
            LOG_WARNING("Module {} is already loaded", moduleId);
            return true;
        }

        // Try to load from DLL
        std::string dllPath = Utils::CombinePath(Utils::GetModulePath(), "Modules\\" + moduleId + ".dll");
        if (Utils::FileExists(dllPath)) {
            return LoadModuleFromDLL(dllPath);
        }

        LOG_ERROR("Module {} not found", moduleId);
        return false;
    }

    bool NexileApp::UnloadModule(const std::string& moduleId) {
        auto it = m_modules.find(moduleId);
        if (it == m_modules.end()) {
            return false;
        }

        // Unload module
        it->second->OnModuleUnload();
        m_modules.erase(it);

        // Unload DLL if it was loaded from DLL
        auto dllIt = m_moduleHandles.find(moduleId);
        if (dllIt != m_moduleHandles.end()) {
            FreeLibrary(dllIt->second);
            m_moduleHandles.erase(dllIt);
        }

        LOG_INFO("Module {} unloaded", moduleId);
        return true;
    }

    std::shared_ptr<IModule> NexileApp::GetModule(const std::string& moduleId) {
        auto it = m_modules.find(moduleId);
        if (it != m_modules.end()) {
            return it->second;
        }
        return nullptr;
    }

    GameID NexileApp::GetActiveGameID() const {
        return m_activeGame;
    }

    void NexileApp::UpdateActivityTimestamp() {
        m_lastActivityTime = std::chrono::steady_clock::now();
    }

    void NexileApp::LoadModulesFromDirectory(const std::string& directory) {
        std::vector<std::string> dllFiles = Utils::GetFilesInDirectory(directory, ".dll");
        for (const std::string& dllPath : dllFiles) {
            LoadModuleFromDLL(dllPath);
        }
    }

    bool NexileApp::LoadModuleFromDLL(const std::string& dllPath) {
        HMODULE hModule = LoadLibraryA(dllPath.c_str());
        if (!hModule) {
            LOG_ERROR("Failed to load module DLL: {}", dllPath);
            return false;
        }

        // Look for module creation function
        typedef IModule* (*CreateModuleFunc)();
        CreateModuleFunc createModule = reinterpret_cast<CreateModuleFunc>(
            GetProcAddress(hModule, "CreateModule"));

        if (!createModule) {
            LOG_ERROR("Module DLL {} does not export CreateModule function", dllPath);
            FreeLibrary(hModule);
            return false;
        }

        // Create module instance
        IModule* modulePtr = createModule();
        if (!modulePtr) {
            LOG_ERROR("Failed to create module instance from {}", dllPath);
            FreeLibrary(hModule);
            return false;
        }

        std::shared_ptr<IModule> module(modulePtr);
        std::string moduleId = module->GetModuleID();

        // Store module and DLL handle
        m_modules[moduleId] = module;
        m_moduleHandles[moduleId] = hModule;

        // Initialize module
        module->OnModuleLoad(m_activeGame);

        LOG_INFO("Loaded external module: {} v{} by {}",
                module->GetModuleName(),
                module->GetModuleVersion(),
                module->GetModuleAuthor());

        return true;
    }

    void NexileApp::LoadModulesForGame(GameID gameId) {
        // Enable/disable modules based on game compatibility
        for (auto& [moduleId, module] : m_modules) {
            if (module) {
                bool shouldEnable = module->SupportsGame(gameId);
                if (m_profileManager) {
                    shouldEnable = shouldEnable && m_profileManager->IsModuleEnabled(moduleId);
                }
                module->SetEnabled(shouldEnable);
            }
        }
    }

    void NexileApp::IdleTimerThreadFunc() {
        const auto idleThreshold = std::chrono::minutes(30); // 30 minutes idle threshold

        while (!m_stopIdleTimer) {
            std::this_thread::sleep_for(std::chrono::minutes(1)); // Check every minute

            if (m_stopIdleTimer) break;

            auto now = std::chrono::steady_clock::now();
            auto timeSinceActivity = now - m_lastActivityTime;

            if (timeSinceActivity > idleThreshold) {
                // System is idle, hide overlay to save resources
                if (m_overlayVisible) {
                    LOG_INFO("Auto-hiding overlay due to inactivity");
                    SetOverlayVisible(false);
                }

                // Trigger memory cleanup
                LogMemoryUsage("Idle-Cleanup");
            }
        }
    }

    // FIXED: Memory monitoring implementation
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

} // namespace Nexile