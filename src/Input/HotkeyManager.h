#pragma once

#include <Windows.h>
#include <unordered_map>
#include <string>
#include <memory>
#include <functional>

namespace Nexile {

    // Forward declarations
    class NexileApp;

    // Hotkey configuration
    struct HotkeyConfig {
        int modifiers;     // MOD_ALT, MOD_CONTROL, etc.
        int virtualKey;    // Virtual key code (e.g., 'A', VK_F1, etc.)
        int hotkeyId;      // Identifier for this hotkey
        std::string description;  // Description of the hotkey

        HotkeyConfig() : modifiers(0), virtualKey(0), hotkeyId(0) {}

        HotkeyConfig(int mods, int vk, int id, const std::string& desc = "")
            : modifiers(mods), virtualKey(vk), hotkeyId(id), description(desc) {
        }

        // Create a unique string identifier for this hotkey
        std::string GetIdentifier() const {
            return std::to_string(modifiers) + "_" + std::to_string(virtualKey);
        }
    };

    class HotkeyManager {
    public:
        // Predefined hotkey IDs
        static const int HOTKEY_TOGGLE_OVERLAY = 1000;
        static const int HOTKEY_PRICE_CHECK = 1001;
        static const int HOTKEY_BUILD_GUIDE = 1002;
        static const int HOTKEY_MAP_OVERLAY = 1003;
        static const int HOTKEY_GAME_SETTINGS = 1004;

        HotkeyManager(NexileApp* app);
        ~HotkeyManager();

        // Register global hotkeys
        void RegisterGlobalHotkeys();

        // Unregister all hotkeys
        void UnregisterAllHotkeys();

        // Register a specific hotkey
        bool RegisterHotkey(int modifiers, int virtualKey, int hotkeyId);

        // Unregister a specific hotkey
        bool UnregisterHotkey(int hotkeyId);

        // Get hotkey by ID
        HotkeyConfig GetHotkeyById(int hotkeyId) const;

        // Get next available hotkey ID
        int GetNextHotkeyId();

        // Convert modifiers to string (e.g., "Ctrl+Alt+")
        static std::string ModifiersToString(int modifiers);

        // Convert virtual key to string (e.g., "F1")
        static std::string VirtualKeyToString(int virtualKey);

        // Get a human-readable description of a hotkey
        std::string GetHotkeyString(int hotkeyId) const;

    private:
        // Initialize default hotkeys
        void InitializeDefaultHotkeys();

        // Application instance
        NexileApp* m_app;

        // Map of hotkey ID to hotkey config
        std::unordered_map<int, HotkeyConfig> m_hotkeys;

        // Map of hotkey identifiers to hotkey IDs (for duplicate checking)
        std::unordered_map<std::string, int> m_hotkeyIdentifiers;

        // Next available hotkey ID for custom hotkeys
        int m_nextHotkeyId;
    };

} // namespace Nexile