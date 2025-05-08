#include "ProfileManager.h"
#include "../Input/HotkeyManager.h"
#include "../UI/OverlayWindow.h"

#include <Windows.h>
#include <ShlObj.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

// Use the nlohmann/json library for JSON parsing
using json = nlohmann::json;

namespace Nexile {

    ProfileManager::ProfileManager()
        : m_currentGameId(GameID::None),
        m_overlayWindow(nullptr),
        m_profilesModified(false) {

        // Initialize default profiles
        InitializeDefaultProfiles();

        // Load profiles from disk
        LoadProfiles();
    }

    ProfileManager::~ProfileManager() {
        // Save profiles if modified
        if (m_profilesModified) {
            SaveProfiles();
        }
    }

    void ProfileManager::InitializeDefaultProfiles() {
        // Path of Exile profile
        ProfileSettings poeProfile;
        poeProfile.overlayEnabled = true;
        poeProfile.clickThrough = true;
        poeProfile.overlayOpacity = 0.8f;
        poeProfile.enabledModules["price_check"] = true;
        m_profiles[GameID::PathOfExile] = poeProfile;

        // Path of Exile 2 profile (copy from PoE for now)
        m_profiles[GameID::PathOfExile2] = poeProfile;

        // Last Epoch profile
        ProfileSettings leProfile;
        leProfile.overlayEnabled = true;
        leProfile.clickThrough = true;
        leProfile.overlayOpacity = 0.8f;
        leProfile.enabledModules["price_check"] = false;  // Disable price check for LE
        m_profiles[GameID::LastEpoch] = leProfile;

        // Default profile (no game)
        ProfileSettings defaultProfile;
        defaultProfile.overlayEnabled = false;
        defaultProfile.clickThrough = true;
        defaultProfile.overlayOpacity = 0.8f;
        m_profiles[GameID::None] = defaultProfile;
    }

    void ProfileManager::LoadProfile(GameID gameId) {
        // Set current game ID
        m_currentGameId = gameId;

        // Ensure profile exists
        if (m_profiles.find(gameId) == m_profiles.end()) {
            m_profiles[gameId] = ProfileSettings();
        }

        // Apply profile settings
        const ProfileSettings& profile = m_profiles[gameId];

        // Apply hotkey overrides
        if (m_hotkeyManager) {
            // First unregister all hotkeys
            m_hotkeyManager->UnregisterAllHotkeys();

            // Then register with overrides
            for (const auto& [hotkeyId, override] : profile.hotkeyOverrides) {
                m_hotkeyManager->RegisterHotkey(override.first, override.second, hotkeyId);
            }

            // Register any other hotkeys not overridden
            m_hotkeyManager->RegisterGlobalHotkeys();
        }

        // Apply overlay settings
        if (m_overlayWindow) {
            m_overlayWindow->SetClickThrough(profile.clickThrough);
        }
    }

    void ProfileManager::SaveProfile() {
        m_profilesModified = true;
        SaveProfiles();
    }

    ProfileSettings& ProfileManager::GetCurrentProfile() {
        return m_profiles[m_currentGameId];
    }

    ProfileSettings& ProfileManager::GetProfile(GameID gameId) {
        if (m_profiles.find(gameId) == m_profiles.end()) {
            m_profiles[gameId] = ProfileSettings();
        }

        return m_profiles[gameId];
    }

    void ProfileManager::SetModuleEnabled(const std::string& moduleId, bool enabled) {
        m_profiles[m_currentGameId].enabledModules[moduleId] = enabled;
        m_profilesModified = true;
    }

    bool ProfileManager::IsModuleEnabled(const std::string& moduleId) {
        const auto& profile = m_profiles[m_currentGameId];
        auto it = profile.enabledModules.find(moduleId);
        if (it != profile.enabledModules.end()) {
            return it->second;
        }

        return false;
    }

    void ProfileManager::SetHotkeyOverride(int hotkeyId, int modifiers, int virtualKey) {
        m_profiles[m_currentGameId].hotkeyOverrides[hotkeyId] = std::make_pair(modifiers, virtualKey);
        m_profilesModified = true;

        // Update hotkey if manager is available
        if (m_hotkeyManager) {
            m_hotkeyManager->UnregisterHotkey(hotkeyId);
            m_hotkeyManager->RegisterHotkey(modifiers, virtualKey, hotkeyId);
        }
    }

    void ProfileManager::ClearHotkeyOverride(int hotkeyId) {
        auto& overrides = m_profiles[m_currentGameId].hotkeyOverrides;
        auto it = overrides.find(hotkeyId);
        if (it != overrides.end()) {
            overrides.erase(it);
            m_profilesModified = true;

            // Update hotkey if manager is available
            if (m_hotkeyManager) {
                m_hotkeyManager->UnregisterHotkey(hotkeyId);
                m_hotkeyManager->RegisterGlobalHotkeys();
            }
        }
    }

    void ProfileManager::SetHotkeyManager(std::unique_ptr<HotkeyManager> hotkeyManager) {
        m_hotkeyManager = std::move(hotkeyManager);
    }

    void ProfileManager::SetOverlayWindow(OverlayWindow* overlayWindow) {
        m_overlayWindow = overlayWindow;
    }

    void ProfileManager::LoadProfiles() {
        std::string filePath = GetProfileFilePath();

        // Check if file exists
        if (!std::filesystem::exists(filePath)) {
            return;
        }

        try {
            // Open file
            std::ifstream file(filePath);
            if (!file.is_open()) {
                return;
            }

            // Parse JSON
            json profilesJson;
            file >> profilesJson;
            file.close();

            // Process each game profile
            for (const auto& [gameStr, profileJson] : profilesJson.items()) {
                GameID gameId = StringToGameID(gameStr);
                ProfileSettings profile;

                // General settings
                profile.overlayEnabled = profileJson.value("overlayEnabled", true);
                profile.clickThrough = profileJson.value("clickThrough", true);
                profile.overlayOpacity = profileJson.value("overlayOpacity", 0.8f);
                profile.overlayX = profileJson.value("overlayX", 0);
                profile.overlayY = profileJson.value("overlayY", 0);
                profile.overlayWidth = profileJson.value("overlayWidth", 0);
                profile.overlayHeight = profileJson.value("overlayHeight", 0);

                // Enabled modules
                if (profileJson.contains("enabledModules")) {
                    for (const auto& [moduleId, enabled] : profileJson["enabledModules"].items()) {
                        profile.enabledModules[moduleId] = enabled;
                    }
                }

                // Hotkey overrides
                if (profileJson.contains("hotkeyOverrides")) {
                    for (const auto& [hotkeyIdStr, overrideJson] : profileJson["hotkeyOverrides"].items()) {
                        int hotkeyId = std::stoi(hotkeyIdStr);
                        int modifiers = overrideJson[0];
                        int virtualKey = overrideJson[1];
                        profile.hotkeyOverrides[hotkeyId] = std::make_pair(modifiers, virtualKey);
                    }
                }

                // Store profile
                m_profiles[gameId] = profile;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error loading profiles: " << e.what() << std::endl;
        }
    }

    void ProfileManager::SaveProfiles() {
        std::string filePath = GetProfileFilePath();

        try {
            // Create directory if it doesn't exist
            std::filesystem::path dirPath = std::filesystem::path(filePath).parent_path();
            std::filesystem::create_directories(dirPath);

            // Create JSON object
            json profilesJson;

            // Add each profile
            for (const auto& [gameId, profile] : m_profiles) {
                json profileJson;

                // General settings
                profileJson["overlayEnabled"] = profile.overlayEnabled;
                profileJson["clickThrough"] = profile.clickThrough;
                profileJson["overlayOpacity"] = profile.overlayOpacity;
                profileJson["overlayX"] = profile.overlayX;
                profileJson["overlayY"] = profile.overlayY;
                profileJson["overlayWidth"] = profile.overlayWidth;
                profileJson["overlayHeight"] = profile.overlayHeight;

                // Enabled modules
                json enabledModulesJson;
                for (const auto& [moduleId, enabled] : profile.enabledModules) {
                    enabledModulesJson[moduleId] = enabled;
                }
                profileJson["enabledModules"] = enabledModulesJson;

                // Hotkey overrides
                json hotkeyOverridesJson;
                for (const auto& [hotkeyId, override] : profile.hotkeyOverrides) {
                    json overrideJson = { override.first, override.second };
                    hotkeyOverridesJson[std::to_string(hotkeyId)] = overrideJson;
                }
                profileJson["hotkeyOverrides"] = hotkeyOverridesJson;

                // Add to profiles
                profilesJson[GameIDToString(gameId)] = profileJson;
            }

            // Write to file
            std::ofstream file(filePath);
            if (file.is_open()) {
                file << profilesJson.dump(4);
                file.close();
                m_profilesModified = false;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error saving profiles: " << e.what() << std::endl;
        }
    }

    std::string ProfileManager::GetProfileFilePath() {
        // Get AppData folder
        wchar_t appDataPath[MAX_PATH];
        SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, appDataPath);

        // Create path to profiles.json
        std::wstring wpath = std::wstring(appDataPath) + L"\\Nexile\\profiles.json";

        // Convert to UTF-8
        std::string path(wpath.begin(), wpath.end());

        return path;
    }

} // namespace Nexile
