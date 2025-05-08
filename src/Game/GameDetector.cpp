#include "GameDetector.h"
#include <TlHelp32.h>
#include <iostream>

namespace Nexile {

    // Helper for FindMainWindowByProcessId
    struct FindWindowData {
        DWORD processId;
        HWND resultWindow;
    };

    // Window enumeration callback
    BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
        FindWindowData* data = reinterpret_cast<FindWindowData*>(lParam);

        // Skip invisible windows
        if (!IsWindowVisible(hwnd)) {
            return TRUE; // Continue enumeration
        }

        // Get process ID for this window
        DWORD windowProcessId = 0;
        GetWindowThreadProcessId(hwnd, &windowProcessId);

        if (windowProcessId == data->processId) {
            // Check if it's a main window (no owner)
            if (!GetWindow(hwnd, GW_OWNER) && GetWindowTextLength(hwnd) > 0) {
                data->resultWindow = hwnd;
                return FALSE; // Stop enumeration
            }
        }

        return TRUE; // Continue enumeration
    }

    GameDetector::GameDetector()
        : m_stopDetection(false),
        m_currentGameId(GameID::None),
        m_currentGameWindow(NULL),
        m_manualOverride(false),
        m_manualGameId(GameID::None) {

        // Initialize game process map
        InitializeGameProcessMap();
    }

    GameDetector::~GameDetector() {
        // Stop detection thread
        StopDetection();
    }

    void GameDetector::InitializeGameProcessMap() {
        // Path of Exile
        m_gameProcessMap[GameID::PathOfExile] = GameProcessInfo(
            L"PathOfExile_x64.exe",
            L"POEWindowClass",
            L"Path of Exile"
        );

        // Path of Exile 2 (placeholder, adjust when released)
        m_gameProcessMap[GameID::PathOfExile2] = GameProcessInfo(
            L"PathOfExile2.exe",
            L"",
            L"Path of Exile 2"
        );

        // Last Epoch
        m_gameProcessMap[GameID::LastEpoch] = GameProcessInfo(
            L"LastEpoch.exe",
            L"UnityWndClass",
            L"Last Epoch"
        );
    }

    void GameDetector::StartDetection(GameChangeCallback callback) {
        // Store callback
        m_gameChangeCallback = callback;

        // Check for games immediately once
        GameID initialGame = DetectRunningGame();

        if (initialGame != GameID::None) {
            ProcessCallback(initialGame);
        }

        // Reset stop flag
        m_stopDetection = false;

        // Start detection thread
        m_detectionThread = std::thread(&GameDetector::DetectionThreadFunc, this);
    }

    void GameDetector::StopDetection() {
        // Set stop flag
        m_stopDetection = true;

        // Wait for thread to finish
        if (m_detectionThread.joinable()) {
            m_detectionThread.join();
        }
    }

    void GameDetector::DetectionThreadFunc() {
        // Detection loop
        while (!m_stopDetection) {
            // Detect running game
            GameID detectedGame = DetectRunningGame();

            // If game changed, notify
            std::unique_lock<std::mutex> lock(m_mutex);
            if (detectedGame != m_currentGameId) {
                m_currentGameId = detectedGame;

                // Find game window if a game is running
                m_currentGameWindow = NULL;
                if (detectedGame != GameID::None) {
                    DWORD processId = 0;
                    if (IsProcessRunning(m_gameProcessMap[detectedGame].processName, processId)) {
                        m_currentGameWindow = FindMainWindowByProcessId(processId);
                    }
                }

                // Call callback outside the lock
                lock.unlock();
                ProcessCallback(detectedGame);
            }

            // Sleep to avoid high CPU usage (check every 2 seconds)
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    void GameDetector::ProcessCallback(GameID newGameId) {
        if (m_gameChangeCallback) {
            m_gameChangeCallback(newGameId);
        }
    }

    GameID GameDetector::DetectRunningGame() {
        // If manual override is set, return that
        if (m_manualOverride) {
            return m_manualGameId;
        }

        // Check for each supported game
        for (const auto& [gameId, processInfo] : m_gameProcessMap) {
            DWORD processId = 0;
            if (IsProcessRunning(processInfo.processName, processId)) {
                return gameId;
            }
        }

        // No supported game found
        return GameID::None;
    }

    bool GameDetector::IsProcessRunning(const std::wstring& processName, DWORD& outProcessId) {
        outProcessId = 0;

        // Take snapshot of processes
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            return false;
        }

        // Set up process entry structure
        PROCESSENTRY32W pe32 = {};
        pe32.dwSize = sizeof(PROCESSENTRY32W);

        // Get first process
        if (!Process32FirstW(hSnapshot, &pe32)) {
            CloseHandle(hSnapshot);
            return false;
        }

        // Iterate through processes
        do {
            // Case-insensitive comparison
            if (_wcsicmp(pe32.szExeFile, processName.c_str()) == 0) {
                outProcessId = pe32.th32ProcessID;
                CloseHandle(hSnapshot);
                return true;
            }
        } while (Process32NextW(hSnapshot, &pe32));

        // Clean up
        CloseHandle(hSnapshot);
        return false;
    }

    HWND GameDetector::FindMainWindowByProcessId(DWORD processId) {
        FindWindowData data = {};
        data.processId = processId;
        data.resultWindow = NULL;

        // Enumerate windows
        EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&data));

        return data.resultWindow;
    }

    HWND GameDetector::GetGameWindowHandle() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_currentGameWindow;
    }

    RECT GameDetector::GetGameWindowRect() const {
        RECT result = {};

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_currentGameWindow != NULL) {
            GetWindowRect(m_currentGameWindow, &result);
        }
        else {
            // If no game window, return primary monitor rectangle
            result.left = 0;
            result.top = 0;
            result.right = GetSystemMetrics(SM_CXSCREEN);
            result.bottom = GetSystemMetrics(SM_CYSCREEN);
        }

        return result;
    }

    bool GameDetector::IsGameFullscreen() const {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_currentGameWindow == NULL) {
            return false;
        }

        // Get window rectangle
        RECT windowRect;
        GetWindowRect(m_currentGameWindow, &windowRect);

        // Get monitor information
        HMONITOR hMonitor = MonitorFromWindow(m_currentGameWindow, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
        GetMonitorInfo(hMonitor, &monitorInfo);

        // Check if window covers the entire monitor
        return windowRect.left == monitorInfo.rcMonitor.left &&
            windowRect.top == monitorInfo.rcMonitor.top &&
            windowRect.right == monitorInfo.rcMonitor.right &&
            windowRect.bottom == monitorInfo.rcMonitor.bottom;
    }

    void GameDetector::SetManualGameOverride(GameID gameId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_manualOverride = true;
        m_manualGameId = gameId;

        // Immediately process the change
        ProcessCallback(gameId);
    }

    void GameDetector::ClearManualGameOverride() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_manualOverride = false;

        // Detect current game
        GameID currentGame = DetectRunningGame();

        // Only process callback if the game has changed
        if (currentGame != m_currentGameId) {
            m_currentGameId = currentGame;
            ProcessCallback(currentGame);
        }
    }

} // namespace Nexile
