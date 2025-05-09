#include "HotkeyManager.h"
#include "../Core/NexileApp.h"
#include "../Utils/Logger.h"
#include <iostream>
#include <sstream>

namespace Nexile {

    HotkeyManager::HotkeyManager(NexileApp* app)
        : m_app(app), m_nextHotkeyId(2000) {

        // Initialize with default hotkeys
        InitializeDefaultHotkeys();
    }

    HotkeyManager::~HotkeyManager() {
        // Unregister all hotkeys
        UnregisterAllHotkeys();
    }

    void HotkeyManager::InitializeDefaultHotkeys() {
        // Default hotkey for toggling overlay (Ctrl+F1)
        RegisterHotkey(MOD_CONTROL, VK_F1, HOTKEY_TOGGLE_OVERLAY);

        // Default hotkey for settings (Ctrl+F2)
        RegisterHotkey(MOD_CONTROL, VK_F2, HOTKEY_GAME_SETTINGS);

        // Default hotkey for price check (Alt+D) - registered by the module

        // Add more default hotkeys as needed
    }

    void HotkeyManager::RegisterGlobalHotkeys() {
        // Register all hotkeys in our map
        for (const auto& [id, config] : m_hotkeys) {
            RegisterHotkey(config.modifiers, config.virtualKey, id);
        }
    }

    void HotkeyManager::UnregisterAllHotkeys() {
        // Unregister all hotkeys
        for (const auto& [id, config] : m_hotkeys) {
            UnregisterHotkey(id);
        }
    }

    bool HotkeyManager::RegisterHotkey(int modifiers, int virtualKey, int hotkeyId) {
        if (!m_app) {
            return false;
        }

        // Create hotkey config
        HotkeyConfig config(modifiers, virtualKey, hotkeyId);

        // Check for duplicates
        std::string identifier = config.GetIdentifier();
        auto it = m_hotkeyIdentifiers.find(identifier);
        if (it != m_hotkeyIdentifiers.end() && it->second != hotkeyId) {
            // Same key combination used for a different hotkey
            LOG_WARNING("Hotkey combination already in use: {}", identifier);
            return false;
        }

        // Register with Windows
        HWND hwnd = m_app->GetMainWindowHandle();
        if (!RegisterHotKey(hwnd, hotkeyId, modifiers, virtualKey)) {
            DWORD error = GetLastError();
            LOG_ERROR("Failed to register hotkey: {}", error);

            // Error 1409 means hotkey is already registered
            if (error == ERROR_HOTKEY_ALREADY_REGISTERED) {
                LOG_WARNING("Hotkey already registered by another application");

                // Try with a fallback method - unregister first then register again
                UnregisterHotKey(hwnd, hotkeyId);
                if (!RegisterHotKey(hwnd, hotkeyId, modifiers, virtualKey)) {
                    error = GetLastError();
                    LOG_ERROR("Failed to register hotkey after unregister attempt: {}", error);
                    return false;
                }
                LOG_INFO("Successfully registered hotkey after unregister attempt: {}", GetHotkeyString(hotkeyId));
            }
            else {
                return false;
            }
        }

        // Store in our maps
        m_hotkeys[hotkeyId] = config;
        m_hotkeyIdentifiers[identifier] = hotkeyId;

        LOG_INFO("Successfully registered hotkey: {} ({})", hotkeyId, GetHotkeyString(hotkeyId));
        return true;
    }

    bool HotkeyManager::UnregisterHotkey(int hotkeyId) {
        if (!m_app) {
            return false;
        }

        // Find hotkey config
        auto it = m_hotkeys.find(hotkeyId);
        if (it == m_hotkeys.end()) {
            return false;
        }

        // Unregister with Windows
        HWND hwnd = m_app->GetMainWindowHandle();
        if (!UnregisterHotKey(hwnd, hotkeyId)) {
            DWORD error = GetLastError();
            LOG_ERROR("Failed to unregister hotkey: {}", error);
            return false;
        }

        // Remove from identifier map
        std::string identifier = it->second.GetIdentifier();
        m_hotkeyIdentifiers.erase(identifier);

        // Remove from hotkey map
        m_hotkeys.erase(it);

        LOG_INFO("Unregistered hotkey: {}", hotkeyId);
        return true;
    }

    HotkeyConfig HotkeyManager::GetHotkeyById(int hotkeyId) const {
        auto it = m_hotkeys.find(hotkeyId);
        if (it != m_hotkeys.end()) {
            return it->second;
        }

        return HotkeyConfig();
    }

    int HotkeyManager::GetNextHotkeyId() {
        return m_nextHotkeyId++;
    }

    std::string HotkeyManager::ModifiersToString(int modifiers) {
        std::string result;

        if (modifiers & MOD_CONTROL) {
            result += "Ctrl+";
        }

        if (modifiers & MOD_SHIFT) {
            result += "Shift+";
        }

        if (modifiers & MOD_ALT) {
            result += "Alt+";
        }

        if (modifiers & MOD_WIN) {
            result += "Win+";
        }

        return result;
    }

    std::string HotkeyManager::VirtualKeyToString(int virtualKey) {
        // Handle special keys
        switch (virtualKey) {
        case VK_F1: return "F1";
        case VK_F2: return "F2";
        case VK_F3: return "F3";
        case VK_F4: return "F4";
        case VK_F5: return "F5";
        case VK_F6: return "F6";
        case VK_F7: return "F7";
        case VK_F8: return "F8";
        case VK_F9: return "F9";
        case VK_F10: return "F10";
        case VK_F11: return "F11";
        case VK_F12: return "F12";
        case VK_ESCAPE: return "Esc";
        case VK_TAB: return "Tab";
        case VK_RETURN: return "Enter";
        case VK_SPACE: return "Space";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_PRIOR: return "Page Up";
        case VK_NEXT: return "Page Down";
        case VK_UP: return "Up";
        case VK_DOWN: return "Down";
        case VK_LEFT: return "Left";
        case VK_RIGHT: return "Right";
        }

        // Handle printable characters
        if (virtualKey >= 'A' && virtualKey <= 'Z') {
            return std::string(1, static_cast<char>(virtualKey));
        }

        if (virtualKey >= '0' && virtualKey <= '9') {
            return std::string(1, static_cast<char>(virtualKey));
        }

        // For other keys, return the virtual key code as a number
        return "Key(" + std::to_string(virtualKey) + ")";
    }

    std::string HotkeyManager::GetHotkeyString(int hotkeyId) const {
        auto it = m_hotkeys.find(hotkeyId);
        if (it != m_hotkeys.end()) {
            return ModifiersToString(it->second.modifiers) +
                VirtualKeyToString(it->second.virtualKey);
        }

        return "Not Set";
    }

} // namespace Nexile