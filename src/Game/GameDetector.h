#pragma once

#include <Windows.h>
#include <unordered_map>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>

#include "GameTypes.h"

namespace Nexile {

    class GameDetector {
    public:
        using GameChangeCallback = std::function<void(GameID)>;

        GameDetector();
        ~GameDetector();

        // Start the game detection process
        void StartDetection(GameChangeCallback callback);

        // Stop the game detection process
        void StopDetection();

        // Check for running games immediately
        GameID DetectRunningGame();

        // Get the window handle of the current game
        HWND GetGameWindowHandle() const;

        // Get the window rectangle of the current game
        RECT GetGameWindowRect() const;

        // Check if game is in fullscreen mode
        bool IsGameFullscreen() const;

        // Manually set the current game (for testing or forced override)
        void SetManualGameOverride(GameID gameId);

        // Clear manual game override
        void ClearManualGameOverride();

    private:
        // The detection thread function
        void DetectionThreadFunc();

        // Check if a specific process is running
        bool IsProcessRunning(const std::wstring& processName, DWORD& outProcessId);

        // Find window handle by process ID
        HWND FindMainWindowByProcessId(DWORD processId);

        // Initialize game process info map
        void InitializeGameProcessMap();

        // Structure for callback info
        struct CallbackInfo {
            bool callCallback;
            GameID gameId;
        };

        // Helper to process callback
        void ProcessCallback(GameID newGameId);

    private:
        // Detection thread
        std::thread m_detectionThread;

        // Thread control
        std::atomic<bool> m_stopDetection;

        // Game process information map
        std::unordered_map<GameID, GameProcessInfo> m_gameProcessMap;

        // Current game info
        GameID m_currentGameId;
        HWND m_currentGameWindow;

        // Manual override
        bool m_manualOverride;
        GameID m_manualGameId;

        // Thread synchronization
        mutable std::mutex m_mutex;

        // Game change callback
        GameChangeCallback m_gameChangeCallback;
    };

} // namespace Nexile