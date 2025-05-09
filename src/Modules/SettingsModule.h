#pragma once

#include "ModuleInterface.h"
#include <string>
#include <vector>
#include <mutex>
#include <future>

namespace Nexile {

    // Settings module for Nexile
    class SettingsModule : public ModuleBase {
    public:
        SettingsModule();
        ~SettingsModule() override;

        // IModule implementation
        std::string GetModuleID() const override;
        std::string GetModuleName() const override;
        std::string GetModuleDescription() const override;
        std::string GetModuleVersion() const override;
        std::string GetModuleAuthor() const override;
        bool SupportsGame(GameID gameId) const override;
        std::string GetModuleUIHTML() const override;
        void OnHotkeyPressed(int hotkeyId) override;

    protected:
        // ModuleBase overrides
        void OnLoad() override;
        void OnUnload() override;
        void OnGameChanged() override;

    private:
        // Save settings to file
        void SaveSettings();

        // Load settings from file
        void LoadSettings();

        // Update UI with current settings
        void UpdateSettingsUI();

        // Process settings message from UI
        void ProcessSettingsMessage(const std::string& message);

        // Handle hotkey recording
        void StartHotkeyRecording(int hotkeyId);
        void StopHotkeyRecording();
        void UpdateHotkey(int hotkeyId, int modifiers, int virtualKey);

    private:
        // Mutex for thread safety
        std::mutex m_mutex;

        // Currently recording hotkey
        int m_recordingHotkeyId;
    };

} // namespace Nexile