#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "../Game/GameTypes.h"

namespace Nexile {

    // Forward declarations
    class HotkeyManager;
    class OverlayWindow;

    // Profile settings structure
    struct ProfileSettings {
        // General settings
        bool overlayEnabled;
        bool clickThrough;
        float overlayOpacity;

        // Enabled modules
        std::unordered_map<std::string, bool> enabledModules;

        // Hotkey overrides for this game
        std::unordered_map<int, std::pair<int, int>> hotkeyOverrides;  // hotkeyId -> (modifiers, virtualKey)

        // UI positioning
        int overlayX;
        int overlayY;
        int overlayWidth;
        int overlayHeight;

        ProfileSettings()
            : overlayEnabled(true),
            clickThrough(true),
            overlayOpacity(0.8f),
            overlayX(0),
            overlayY(0),
            overlayWidth(0),
            overlayHeight(0) {
        }
    };

    class ProfileManager {
    public:
        ProfileManager();
        ~ProfileManager();

        // Load profile for a specific game
        void LoadProfile(GameID gameId);

        // Save current profile
        void SaveProfile();

        // Get current profile settings
        ProfileSettings& GetCurrentProfile();

        // Get profile for a specific game
        ProfileSettings& GetProfile(GameID gameId);

        // Enable or disable a module in the current profile
        void SetModuleEnabled(const std::string& moduleId, bool enabled);

        // Check if a module is enabled in the current profile
        bool IsModuleEnabled(const std::string& moduleId);

        // Set a hotkey override for the current profile
        void SetHotkeyOverride(int hotkeyId, int modifiers, int virtualKey);

        // Clear a hotkey override for the current profile
        void ClearHotkeyOverride(int hotkeyId);

        // Get hotkey manager
        HotkeyManager* GetHotkeyManager() { return m_hotkeyManager.get(); }

        // Set hotkey manager
        void SetHotkeyManager(std::unique_ptr<HotkeyManager> hotkeyManager);

        // Get overlay window
        OverlayWindow* GetOverlayWindow() { return m_overlayWindow; }

        // Set overlay window
        void SetOverlayWindow(OverlayWindow* overlayWindow);

    private:
        // Initialize default profiles
        void InitializeDefaultProfiles();

        // Load profiles from disk
        void LoadProfiles();

        // Save profiles to disk
        void SaveProfiles();

        // Get profile file path
        std::string GetProfileFilePath();

    private:
        // Current game ID
        GameID m_currentGameId;

        // Profiles for each game
        std::unordered_map<GameID, ProfileSettings> m_profiles;

        // Hotkey manager
        std::unique_ptr<HotkeyManager> m_hotkeyManager;

        // Overlay window (not owned)
        OverlayWindow* m_overlayWindow;

        // Flag to track if profiles have been modified
        bool m_profilesModified;
    };

} // namespace Nexile