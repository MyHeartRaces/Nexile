#include "NexileApp.h"
#include <stdexcept>
#include <filesystem>
#include <thread>
#include <chrono>

#include "../Modules/PriceCheckModule.h"
#include "../Modules/SettingsModule.h"
#include "../UI/Resources.h"
#include "../Utils/Utils.h"
#include "../Utils/Logger.h"

namespace Nexile {

    // Initialize static instance
    NexileApp* NexileApp::s_instance = nullptr;

    NexileApp::NexileApp(HINSTANCE hInstance) : m_hInstance(hInstance), m_activeGame(GameID::None),
        m_overlayVisible(false), m_inSettingsMode(false), m_browserOpen(false) {
        // Store instance for window procedure
        s_instance = this;

        // Initialize application window
        InitializeWindow();

        // Create managers and components
        m_profileManager = std::make_unique<ProfileManager>();
        m_hotkeyManager = std::make_unique<HotkeyManager>(this);
        m_gameDetector = std::make_unique<GameDetector>();
        m_overlayWindow = std::make_unique<OverlayWindow>(this);

        // Set overlay window in profile manager
        m_profileManager->SetOverlayWindow(m_overlayWindow.get());

        // Setup hotkey manager
        m_profileManager->SetHotkeyManager(std::move(m_hotkeyManager));
        m_hotkeyManager.reset(m_profileManager->GetHotkeyManager());

        // Initialize modules
        InitializeModules();

        // Register hotkeys
        if (m_hotkeyManager) {
            m_hotkeyManager->RegisterGlobalHotkeys();
        }

        // Add system tray icon
        AddTrayIcon();

        // Initialize idle timer
        m_lastActivityTime = std::chrono::steady_clock::now();
        m_idleTimerStarted = false;

        LOG_INFO("Nexile initialized successfully");
    }

    NexileApp::~NexileApp() {
        // Remove tray icon
        RemoveTrayIcon();

        // Unregister hotkeys
        m_hotkeyManager.reset();

        // Destroy overlay first
        m_overlayWindow.reset();

        // Clear all modules
        m_modules.clear();

        // Reset static instance
        s_instance = nullptr;

        LOG_INFO("Nexile shutdown complete");
    }

    void NexileApp::RegisterWindowClass() {
        WNDCLASSEXW wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WindowProc;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = 0;
        wcex.hInstance = m_hInstance;
        // Use standard application icon instead of custom icon
        wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszMenuName = nullptr;
        wcex.lpszClassName = L"NexileMainClass";
        wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

        if (!RegisterClassExW(&wcex)) {
            DWORD error = GetLastError();
            LOG_ERROR("Failed to register window class, error code: {}", error);
            throw std::runtime_error("Failed to register window class");
        }
    }

    void NexileApp::InitializeWindow() {
        // Register window class
        RegisterWindowClass();

        // Create main window (hidden, just for messages)
        m_mainWindow = CreateWindowW(
            L"NexileMainClass",
            L"Nexile Controller",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            400, 300,
            nullptr, nullptr,
            m_hInstance,
            nullptr
        );

        if (!m_mainWindow) {
            DWORD error = GetLastError();
            LOG_ERROR("Failed to create main window, error code: {}", error);
            throw std::runtime_error("Failed to create main window");
        }
    }

    int NexileApp::Run(int nCmdShow) {
        // Start idle timer thread
        m_stopIdleTimer = false;
        m_idleTimerThread = std::thread(&NexileApp::IdleTimerThreadFunc, this);

        // Start game detection
        m_gameDetector->StartDetection([this](GameID gameId) {
            OnGameChanged(gameId);
            });

        LOG_INFO("Nexile running, starting message loop");

        // Message loop
        MSG msg = {};
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Stop idle timer thread
        m_stopIdleTimer = true;
        if (m_idleTimerThread.joinable()) {
            m_idleTimerThread.join();
        }

        return (int)msg.wParam;
    }

    LRESULT CALLBACK NexileApp::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        // Get app instance from static variable
        if (s_instance) {
            return s_instance->HandleMessage(hwnd, uMsg, wParam, lParam);
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    LRESULT NexileApp::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_HOTKEY:
            UpdateActivityTimestamp();
            OnHotkeyPressed((int)wParam);
            return 0;

        case WM_TRAYICON:
            ProcessTrayMessage(wParam, lParam);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case IDM_EXIT:
                PostQuitMessage(0);
                return 0;
            case IDM_TOGGLE_OVERLAY:
                ToggleOverlay();
                return 0;
            case IDM_SETTINGS:
                ShowSettingsDialog();
                return 0;
            default:
                break;
            }
            break;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    void NexileApp::ProcessTrayMessage(WPARAM wParam, LPARAM lParam) {
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
            POINT curPoint;
            GetCursorPos(&curPoint);

            HMENU hMenu = CreatePopupMenu();
            if (hMenu) {
                UpdateActivityTimestamp();

                // Add menu items - use InsertMenuW for Unicode support
                InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, IDM_TOGGLE_OVERLAY, L"Toggle Overlay");
                InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_STRING, IDM_SETTINGS, L"Settings");
                InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
                InsertMenuW(hMenu, 3, MF_BYPOSITION | MF_STRING, IDM_EXIT, L"Exit");

                // Set default item
                SetMenuDefaultItem(hMenu, IDM_TOGGLE_OVERLAY, FALSE);

                // Show menu
                SetForegroundWindow(m_mainWindow);
                TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                    curPoint.x, curPoint.y, 0, m_mainWindow, NULL);
                DestroyMenu(hMenu);
            }
        }
    }

    void NexileApp::AddTrayIcon() {
        // Determine whether we're compiling with UNICODE
#ifdef UNICODE
        NOTIFYICONDATAW nid = {}; // Use the Unicode version
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = m_mainWindow;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_NEXILE_ICON_SMALL));
        wcscpy_s(nid.szTip, L"Nexile - Game Overlay");

        if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
            DWORD error = GetLastError();
            LOG_WARNING("Failed to add tray icon, error code: {}", error);
        }
        m_trayIconData = nid;
#else
        NOTIFYICONDATAA nid = {}; // Use the ANSI version
        nid.cbSize = sizeof(NOTIFYICONDATAA);
        nid.hWnd = m_mainWindow;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_NEXILE_ICON_SMALL));
        strcpy_s(nid.szTip, "Nexile - Game Overlay");

        if (!Shell_NotifyIconA(NIM_ADD, &nid)) {
            DWORD error = GetLastError();
            LOG_WARNING("Failed to add tray icon, error code: {}", error);
        }
        m_trayIconData = nid;
#endif
    }

    void NexileApp::RemoveTrayIcon() {
        if (m_trayIconData.cbSize > 0) {
#ifdef UNICODE
            Shell_NotifyIconW(NIM_DELETE, &m_trayIconData);
#else
            Shell_NotifyIconA(NIM_DELETE, &m_trayIconData);
#endif
        }
    }

    void NexileApp::ToggleOverlay() {
        SetOverlayVisible(!m_overlayVisible);
    }

    void NexileApp::SetOverlayVisible(bool visible) {
        m_overlayVisible = visible;

        if (m_overlayWindow) {
            if (visible) {
                UpdateActivityTimestamp();
                m_overlayWindow->Show();
                LOG_INFO("Overlay shown");
            }
            else {
                m_overlayWindow->Hide();
                LOG_INFO("Overlay hidden");
            }
        }
    }

    void NexileApp::ShowSettingsDialog() {
        // Show settings module UI
        auto settingsModule = GetModule("settings");
        if (settingsModule) {
            settingsModule->OnHotkeyPressed(HotkeyManager::HOTKEY_GAME_SETTINGS);
        }
        else {
            LOG_ERROR("Settings module not found");
        }
    }

    void NexileApp::OnHotkeyPressed(int hotkeyId) {
        LOG_DEBUG("Hotkey pressed: {}", hotkeyId);

        // Update activity timestamp
        UpdateActivityTimestamp();

        // Handle core hotkeys
        if (hotkeyId == HotkeyManager::HOTKEY_TOGGLE_OVERLAY) {
            ToggleOverlay();
            return;
        }
        else if (hotkeyId == HotkeyManager::HOTKEY_GAME_SETTINGS) {
            // Toggle settings mode
            m_inSettingsMode = !m_inSettingsMode;

            if (m_inSettingsMode) {
                // Show settings
                if (m_overlayWindow) {
                    auto settingsModule = GetModule("settings");
                    if (settingsModule) {
                        m_overlayWindow->LoadModuleUI(settingsModule);
                        m_overlayWindow->SetClickThrough(false); // Make interactive for settings
                        SetOverlayVisible(true);
                    }
                    else {
                        LOG_ERROR("Settings module not found");
                        m_inSettingsMode = false;
                    }
                }
            }
            else {
                // Exit settings mode, return to welcome screen
                if (m_overlayWindow) {
                    // Restore click-through based on profile
                    bool clickThrough = m_profileManager ?
                        m_profileManager->GetCurrentProfile().clickThrough : true;
                    m_overlayWindow->SetClickThrough(clickThrough);

                    if (m_browserOpen) {
                        // If browser was open, return to browser
                        m_overlayWindow->LoadBrowserPage();
                    }
                    else {
                        // Otherwise return to welcome screen
                        m_overlayWindow->LoadWelcomePage();
                    }
                }
            }
            return;
        }
        else if (hotkeyId == HotkeyManager::HOTKEY_BROWSER) {
            // Toggle browser mode
            m_browserOpen = !m_browserOpen;

            if (m_browserOpen) {
                // Open browser
                if (m_overlayWindow) {
                    m_overlayWindow->LoadBrowserPage();
                    // Browser automatically sets click-through to false
                    SetOverlayVisible(true);
                }
            }
            else {
                // Close browser, return to welcome screen
                if (m_overlayWindow) {
                    // Restore click-through based on profile
                    bool clickThrough = m_profileManager ?
                        m_profileManager->GetCurrentProfile().clickThrough : true;
                    m_overlayWindow->SetClickThrough(clickThrough);
                    m_overlayWindow->LoadWelcomePage();
                }
            }
            return;
        }

        // Let modules handle their hotkeys
        for (auto& [id, module] : m_modules) {
            if (module && module->IsEnabled()) {
                module->OnHotkeyPressed(hotkeyId);
            }
        }
    }

    void NexileApp::OnGameChanged(GameID gameId) {
        LOG_INFO("Game changed to: {}", GameIDToString(gameId));

        // Update current game
        m_activeGame = gameId;

        // Load appropriate profile
        if (m_profileManager) {
            m_profileManager->LoadProfile(gameId);
        }

        // Adjust overlay size to match game window if needed
        if (m_overlayWindow && m_overlayVisible) {
            RECT gameRect = m_gameDetector->GetGameWindowRect();
            if (gameRect.right > gameRect.left && gameRect.bottom > gameRect.top) {
                m_overlayWindow->SetPosition(gameRect);
            }
        }

        // Notify modules of game change
        for (auto& [id, module] : m_modules) {
            if (module) {
                module->OnGameChange(gameId);
            }
        }

        // If we switched to a game, ensure needed modules are loaded
        if (gameId != GameID::None) {
            LoadModulesForGame(gameId);
        }
    }

    void NexileApp::InitializeModules() {
        // Create and register built-in modules
        auto priceCheckModule = std::make_shared<PriceCheckModule>();
        m_modules["price_check"] = priceCheckModule;

        // Create settings module
        auto settingsModule = std::make_shared<SettingsModule>();
        m_modules["settings"] = settingsModule;

        // Register hotkey for settings and browser
        if (m_hotkeyManager) {
            m_hotkeyManager->RegisterHotkey(MOD_ALT | MOD_SHIFT, 'S', HotkeyManager::HOTKEY_GAME_SETTINGS);
            m_hotkeyManager->RegisterHotkey(MOD_ALT | MOD_SHIFT, 'B', HotkeyManager::HOTKEY_BROWSER);
        }

        // Scan modules directory for plugin DLLs
        std::string modulesPath = Utils::CombinePath(Utils::GetModulePath(), "modules");
        if (Utils::DirectoryExists(modulesPath)) {
            LoadModulesFromDirectory(modulesPath);
        }

        // Initialize built-in modules
        for (auto& [id, module] : m_modules) {
            if (module) {
                module->OnModuleLoad(m_activeGame);
            }
        }

        LOG_INFO("Modules initialized: {} modules loaded", m_modules.size());
    }

    void NexileApp::LoadModulesFromDirectory(const std::string& directory) {
        LOG_INFO("Scanning for plugin modules in: {}", directory);

        std::vector<std::string> dllFiles = Utils::GetFilesInDirectory(directory, ".dll");

        for (const auto& dllPath : dllFiles) {
            if (LoadModuleFromDLL(dllPath)) {
                LOG_INFO("Loaded module from: {}", dllPath);
            }
        }
    }

    bool NexileApp::LoadModuleFromDLL(const std::string& dllPath) {
        // Convert to wide string for Windows API
        std::wstring wDllPath = Utils::StringToWideString(dllPath);

        // Load the DLL
        HMODULE hModule = LoadLibraryW(wDllPath.c_str());
        if (!hModule) {
            DWORD error = GetLastError();
            LOG_ERROR("Failed to load DLL {}, error code: {}", dllPath, error);
            return false;
        }

        // Find the create module function
        typedef std::shared_ptr<IModule>(*CreateModuleFn)();
        CreateModuleFn createModule = reinterpret_cast<CreateModuleFn>(
            GetProcAddress(hModule, "CreateModule"));

        if (!createModule) {
            DWORD error = GetLastError();
            LOG_ERROR("Failed to find CreateModule function in {}, error code: {}", dllPath, error);
            FreeLibrary(hModule);
            return false;
        }

        // Create the module
        std::shared_ptr<IModule> module;
        try {
            module = createModule();
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception while creating module from {}: {}", dllPath, e.what());
            FreeLibrary(hModule);
            return false;
        }

        if (!module) {
            LOG_ERROR("CreateModule returned nullptr in {}", dllPath);
            FreeLibrary(hModule);
            return false;
        }

        // Store the module
        std::string moduleId = module->GetModuleID();
        if (moduleId.empty()) {
            LOG_ERROR("Module from {} returned empty ID", dllPath);
            FreeLibrary(hModule);
            return false;
        }

        // Check for duplicate module ID
        if (m_modules.find(moduleId) != m_modules.end()) {
            LOG_WARNING("Module with ID '{}' already exists, skipping {}", moduleId, dllPath);
            FreeLibrary(hModule);
            return false;
        }

        // Store the module and DLL handle
        m_modules[moduleId] = module;
        m_moduleHandles[moduleId] = hModule;

        LOG_INFO("Successfully loaded module '{}' from {}", moduleId, dllPath);
        return true;
    }

    void NexileApp::LoadModulesForGame(GameID gameId) {
        // If no game or no profile manager, do nothing
        if (gameId == GameID::None || !m_profileManager) {
            return;
        }

        // Get profile for this game
        ProfileSettings& profile = m_profileManager->GetProfile(gameId);

        // Enable/disable modules based on profile
        for (auto& [id, module] : m_modules) {
            if (module) {
                // Check if this module supports the current game
                bool supportsGame = module->SupportsGame(gameId);

                // Check if this module is enabled in the profile
                bool enabledInProfile = profile.enabledModules.count(id) > 0 ?
                    profile.enabledModules[id] : false;

                // Set module enabled state based on both conditions
                module->SetEnabled(supportsGame && enabledInProfile);

                LOG_DEBUG("Module '{}' supports game: {}, enabled in profile: {}, final state: {}",
                    id, supportsGame, enabledInProfile, module->IsEnabled());
            }
        }
    }

    bool NexileApp::LoadModule(const std::string& moduleId) {
        auto it = m_modules.find(moduleId);
        if (it != m_modules.end() && it->second) {
            // Module already loaded, just initialize it
            it->second->OnModuleLoad(m_activeGame);
            LOG_INFO("Initialized existing module: {}", moduleId);
            return true;
        }

        // Check for a module DLL with this name in the modules directory
        std::string modulesPath = Utils::CombinePath(Utils::GetModulePath(), "modules");
        std::string moduleDllPath = Utils::CombinePath(modulesPath, moduleId + ".dll");

        if (Utils::FileExists(moduleDllPath)) {
            LOG_INFO("Found module DLL: {}", moduleDllPath);
            return LoadModuleFromDLL(moduleDllPath);
        }

        LOG_WARNING("Failed to load module: {}, module not found", moduleId);
        return false;
    }

    bool NexileApp::UnloadModule(const std::string& moduleId) {
        auto it = m_modules.find(moduleId);
        if (it != m_modules.end() && it->second) {
            // Call module unload
            it->second->OnModuleUnload();

            // Check if we need to free a DLL
            auto handleIt = m_moduleHandles.find(moduleId);
            if (handleIt != m_moduleHandles.end()) {
                // Free the DLL
                FreeLibrary(handleIt->second);
                m_moduleHandles.erase(handleIt);
            }

            // Remove from modules map
            m_modules.erase(it);

            LOG_INFO("Unloaded module: {}", moduleId);
            return true;
        }

        return false;
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

        // If we're in idle state, wake up
        if (m_idleTimerStarted) {
            m_idleTimerStarted = false;
            LOG_DEBUG("Activity detected, exiting idle state");
        }
    }

    void NexileApp::IdleTimerThreadFunc() {
        const auto idleThreshold = std::chrono::minutes(5); // 5 minutes of inactivity

        while (!m_stopIdleTimer) {
            auto now = std::chrono::steady_clock::now();
            auto idleTime = now - m_lastActivityTime;

            // Check if we've been idle long enough
            if (!m_idleTimerStarted && idleTime > idleThreshold) {
                m_idleTimerStarted = true;
                LOG_INFO("Idle timeout reached, entering idle state");

                // If overlay is visible and no game is running, hide it
                if (m_overlayVisible && m_activeGame == GameID::None) {
                    SetOverlayVisible(false);
                }
            }

            // Sleep for a bit before checking again
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    }

} // namespace Nexile