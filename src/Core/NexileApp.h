#pragma once

#include <Windows.h>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <chrono>

#include "UI/OverlayWindow.h"
#include "Modules/ModuleInterface.h"
#include "Game/GameDetector.h"
#include "Input/HotkeyManager.h"
#include "Config/ProfileManager.h"

namespace Nexile {

    class NexileApp {
    public:
        NexileApp(HINSTANCE hInstance);
        ~NexileApp();

        int Run(int nCmdShow);

        // Static window procedure for main window
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

        // Process messages from the message loop
        LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

        // Get instance of the application
        static NexileApp* GetInstance() { return s_instance; }

        // Get handle to main window
        HWND GetMainWindowHandle() const { return m_mainWindow; }

        // Get instance handle
        HINSTANCE GetInstanceHandle() const { return m_hInstance; }

        // Toggle overlay visibility
        void ToggleOverlay();

        // Show or hide overlay
        void SetOverlayVisible(bool visible);

        // Handle hotkey press
        void OnHotkeyPressed(int hotkeyId);

        // Handle game change detection
        void OnGameChanged(GameID gameId);

        // Initialize modules
        void InitializeModules();

        // Load module by ID
        bool LoadModule(const std::string& moduleId);

        // Unload module by ID
        bool UnloadModule(const std::string& moduleId);

        // Get module by ID
        std::shared_ptr<IModule> GetModule(const std::string& moduleId);

        // Get active game ID
        GameID GetActiveGameID() const;

        // Get profile manager
        ProfileManager* GetProfileManager() { return m_profileManager.get(); }

        // Update activity timestamp to prevent idle mode
        void UpdateActivityTimestamp();

    private:
        // Initialize the application window
        void InitializeWindow();

        // Register window class
        void RegisterWindowClass();

        // Process system tray messages
        void ProcessTrayMessage(WPARAM wParam, LPARAM lParam);

        // Add icon to system tray
        void AddTrayIcon();

        // Remove icon from system tray
        void RemoveTrayIcon();

        // Show settings dialog
        void ShowSettingsDialog();

        // Load modules from a directory
        void LoadModulesFromDirectory(const std::string& directory);

        // Load a module from a DLL file
        bool LoadModuleFromDLL(const std::string& dllPath);

        // Load modules for a specific game
        void LoadModulesForGame(GameID gameId);

        // Idle timer thread function
        void IdleTimerThreadFunc();

    private:
        // Static instance for window procedure
        static NexileApp* s_instance;

        // Application instance handle
        HINSTANCE m_hInstance;

        // Main application window handle
        HWND m_mainWindow;

        // Overlay window
        std::unique_ptr<OverlayWindow> m_overlayWindow;

        // Hotkey manager
        std::unique_ptr<HotkeyManager> m_hotkeyManager;

        // Game detector
        std::unique_ptr<GameDetector> m_gameDetector;

        // Profile manager
        std::unique_ptr<ProfileManager> m_profileManager;

        // Loaded modules
        std::unordered_map<std::string, std::shared_ptr<IModule>> m_modules;

        // Loaded module DLL handles
        std::unordered_map<std::string, HMODULE> m_moduleHandles;

        // Current active game
        GameID m_activeGame = GameID::None;

        // Overlay visible flag
        bool m_overlayVisible = false;

        // Tray icon data
        NOTIFYICONDATA m_trayIconData = {};

        // Idle timer thread and related variables
        std::thread m_idleTimerThread;
        std::atomic<bool> m_stopIdleTimer;
        std::chrono::steady_clock::time_point m_lastActivityTime;
        bool m_idleTimerStarted;

        // Custom tray message ID
        static const UINT WM_TRAYICON = WM_USER + 1;
    };

} // namespace Nexile