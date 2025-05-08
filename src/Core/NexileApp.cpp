#include "NexileApp.h"
#include <stdexcept>

#include "Modules/PriceCheckModule.h"
#include "UI/Resources.h"

namespace Nexile {

    // Initialize static instance
    NexileApp* NexileApp::s_instance = nullptr;

    NexileApp::NexileApp(HINSTANCE hInstance) : m_hInstance(hInstance) {
        // Store instance for window procedure
        s_instance = this;

        // Initialize application window
        InitializeWindow();

        // Create managers and components
        m_profileManager = std::make_unique<ProfileManager>();
        m_hotkeyManager = std::make_unique<HotkeyManager>(this);
        m_gameDetector = std::make_unique<GameDetector>();
        m_overlayWindow = std::make_unique<OverlayWindow>(this);

        // Initialize modules
        InitializeModules();

        // Register hotkeys
        m_hotkeyManager->RegisterGlobalHotkeys();

        // Add system tray icon
        AddTrayIcon();
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
    }

    void NexileApp::RegisterWindowClass() {
        WNDCLASSEX wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WindowProc;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = 0;
        wcex.hInstance = m_hInstance;
        wcex.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_NEXILE_ICON));
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszMenuName = nullptr;
        wcex.lpszClassName = L"NexileMainClass";
        wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_NEXILE_ICON_SMALL));

        if (!RegisterClassEx(&wcex)) {
            throw std::runtime_error("Failed to register window class");
        }
    }

    void NexileApp::InitializeWindow() {
        // Register window class
        RegisterWindowClass();

        // Create main window (hidden, just for messages)
        m_mainWindow = CreateWindow(
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
            throw std::runtime_error("Failed to create main window");
        }
    }

    int NexileApp::Run(int nCmdShow) {
        // Start game detection
        m_gameDetector->StartDetection([this](GameID gameId) {
            OnGameChanged(gameId);
            });

        // Message loop
        MSG msg = {};
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
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
            OnHotkeyPressed((int)wParam);
            return 0;

        case WM_TRAYICON:
            ProcessTrayMessage(wParam, lParam);
            return 0;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    void NexileApp::ProcessTrayMessage(WPARAM wParam, LPARAM lParam) {
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
            POINT curPoint;
            GetCursorPos(&curPoint);

            HMENU hMenu = CreatePopupMenu();
            if (hMenu) {
                // Add menu items
                InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, 1001, L"Toggle Overlay");
                InsertMenu(hMenu, 1, MF_BYPOSITION | MF_STRING, 1002, L"Exit");

                // Set default item
                SetMenuDefaultItem(hMenu, 1001, FALSE);

                // Show menu
                SetForegroundWindow(m_mainWindow);
                TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                    curPoint.x, curPoint.y, 0, m_mainWindow, NULL);
                DestroyMenu(hMenu);
            }
        }
    }

    void NexileApp::AddTrayIcon() {
        m_trayIconData = {};
        m_trayIconData.cbSize = sizeof(NOTIFYICONDATA);
        m_trayIconData.hWnd = m_mainWindow;
        m_trayIconData.uID = 1;
        m_trayIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        m_trayIconData.uCallbackMessage = WM_TRAYICON;
        m_trayIconData.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_NEXILE_ICON_SMALL));
        wcscpy_s(m_trayIconData.szTip, L"Nexile - Game Overlay");

        Shell_NotifyIcon(NIM_ADD, &m_trayIconData);
    }

    void NexileApp::RemoveTrayIcon() {
        if (m_trayIconData.cbSize > 0) {
            Shell_NotifyIcon(NIM_DELETE, &m_trayIconData);
        }
    }

    void NexileApp::ToggleOverlay() {
        SetOverlayVisible(!m_overlayVisible);
    }

    void NexileApp::SetOverlayVisible(bool visible) {
        m_overlayVisible = visible;

        if (m_overlayWindow) {
            if (visible) {
                m_overlayWindow->Show();
            }
            else {
                m_overlayWindow->Hide();
            }
        }
    }

    void NexileApp::OnHotkeyPressed(int hotkeyId) {
        // Handle core hotkeys
        if (hotkeyId == HotkeyManager::HOTKEY_TOGGLE_OVERLAY) {
            ToggleOverlay();
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
    }

    void NexileApp::InitializeModules() {
        // Create and register built-in modules
        // These could later be moved to a plugin system
        auto priceCheckModule = std::make_shared<PriceCheckModule>();
        m_modules["price_check"] = priceCheckModule;

        // Initialize built-in modules
        for (auto& [id, module] : m_modules) {
            if (module) {
                module->OnModuleLoad(m_activeGame);
            }
        }
    }

    bool NexileApp::LoadModule(const std::string& moduleId) {
        auto it = m_modules.find(moduleId);
        if (it != m_modules.end() && it->second) {
            // Module already loaded, just initialize it
            it->second->OnModuleLoad(m_activeGame);
            return true;
        }

        // Later this will load dynamic modules from DLLs
        return false;
    }

    bool NexileApp::UnloadModule(const std::string& moduleId) {
        auto it = m_modules.find(moduleId);
        if (it != m_modules.end() && it->second) {
            // Call module unload
            it->second->OnModuleUnload();
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

} // namespace Nexile
