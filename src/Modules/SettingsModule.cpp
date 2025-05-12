#include "SettingsModule.h"
#include "../Core/NexileApp.h"
#include "../Utils/Utils.h"
#include "../Utils/Logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace Nexile {

    SettingsModule::SettingsModule()
        : m_recordingHotkeyId(-1) {
        // Initialize here
    }

    SettingsModule::~SettingsModule() {
        // Cleanup
    }

    std::string SettingsModule::GetModuleID() const {
        return "settings";
    }

    std::string SettingsModule::GetModuleName() const {
        return "Settings";
    }

    std::string SettingsModule::GetModuleDescription() const {
        return "Configure Nexile settings";
    }

    std::string SettingsModule::GetModuleVersion() const {
        return "1.0.0";
    }

    std::string SettingsModule::GetModuleAuthor() const {
        return "Nexile Team";
    }

    bool SettingsModule::SupportsGame(GameID gameId) const {
        // Settings module works with all games
        return true;
    }

    std::string SettingsModule::GetModuleUIHTML() const {
        // Read settings HTML file
        std::string htmlPath = Utils::CombinePath(Utils::GetModulePath(), "HTML\\settings.html");
        std::string htmlContent = Utils::ReadTextFile(htmlPath);

        if (!htmlContent.empty()) {
            LOG_INFO("Loaded settings HTML from file: {}", htmlPath);
            return htmlContent;
        }

        LOG_WARNING("Settings HTML file not found at {}. Using default settings HTML.", htmlPath);

        // Return a basic settings page if we couldn't load the file
        return R"(
            <!DOCTYPE html>
            <html>
            <head>
                <title>Nexile Settings</title>
                <style>
                    body { 
                        background-color: rgba(30, 30, 30, 0.85);
                        color: white;
                        font-family: 'Segoe UI', sans-serif;
                        padding: 20px;
                    }
                    h1 { color: #4a90e2; }
                    
                    .settings-container {
                        margin: 20px 0;
                    }
                    
                    .setting-item {
                        display: flex;
                        justify-content: space-between;
                        align-items: center;
                        padding: 10px;
                        border-bottom: 1px solid rgba(255, 255, 255, 0.1);
                    }
                    
                    .setting-label {
                        font-weight: bold;
                    }
                    
                    .button-container {
                        margin-top: 20px;
                        text-align: right;
                    }
                    
                    .button {
                        background-color: #4a90e2;
                        color: white;
                        border: none;
                        padding: 8px 16px;
                        border-radius: 4px;
                        cursor: pointer;
                        margin-left: 10px;
                    }
                    
                    .button:hover {
                        background-color: #3a80d2;
                    }
                </style>
            </head>
            <body>
                <h1>Nexile Settings</h1>
                
                <div class="settings-container">
                    <h2>General Settings</h2>
                    <div class="setting-item">
                        <span class="setting-label">Overlay Opacity</span>
                        <input type="range" id="opacity-slider" min="10" max="100" value="80">
                        <span id="opacity-value">80%</span>
                    </div>
                    
                    <div class="setting-item">
                        <span class="setting-label">Click-Through</span>
                        <input type="checkbox" id="click-through" checked>
                    </div>
                    
                    <h2>Modules</h2>
                    <div class="setting-item">
                        <span class="setting-label">Price Check</span>
                        <input type="checkbox" id="module-pricecheck" checked>
                    </div>
                    
                    <h2>Hotkeys</h2>
                    <div class="setting-item">
                        <span class="setting-label">Toggle Overlay</span>
                        <span>Ctrl+F1</span>
                    </div>
                    <div class="setting-item">
                        <span class="setting-label">Price Check</span>
                        <span>Alt+D</span>
                    </div>
                    <div class="setting-item">
                        <span class="setting-label">Settings</span>
                        <span>Ctrl+F2</span>
                    </div>
                </div>
                
                <div class="button-container">
                    <button id="btn-cancel" class="button">Cancel</button>
                    <button id="btn-save" class="button">Save</button>
                </div>
                
                <script>
                    // Initialize settings
                    document.addEventListener('DOMContentLoaded', function() {
                        // Request current settings
                        window.chrome.webview.postMessage(JSON.stringify({
                            action: "get_settings"
                        }));
                        
                        // Handle opacity slider
                        const opacitySlider = document.getElementById('opacity-slider');
                        const opacityValue = document.getElementById('opacity-value');
                        
                        opacitySlider.addEventListener('input', function() {
                            opacityValue.textContent = this.value + '%';
                        });
                        
                        // Handle save button
                        document.getElementById('btn-save').addEventListener('click', function() {
                            const settings = {
                                general: {
                                    opacity: parseInt(opacitySlider.value),
                                    clickThrough: document.getElementById('click-through').checked
                                },
                                modules: {
                                    priceCheck: document.getElementById('module-pricecheck').checked
                                }
                            };
                            
                            window.chrome.webview.postMessage(JSON.stringify({
                                action: "save_settings",
                                settings: settings
                            }));
                        });
                        
                        // Handle cancel button
                        document.getElementById('btn-cancel').addEventListener('click', function() {
                            window.chrome.webview.postMessage(JSON.stringify({
                                action: "cancel_settings"
                            }));
                        });
                        
                        // Listen for messages from C++
                        window.chrome.webview.addEventListener('message', function(event) {
                            try {
                                const message = JSON.parse(event.data);
                                console.log("Received settings message:", message);
                                
                                if (message.action === 'load_settings' && message.settings) {
                                    const settings = message.settings;
                                    
                                    // Update general settings
                                    if (settings.general) {
                                        if (settings.general.opacity) {
                                            opacitySlider.value = settings.general.opacity;
                                            opacityValue.textContent = settings.general.opacity + '%';
                                        }
                                        
                                        if (settings.general.hasOwnProperty('clickThrough')) {
                                            document.getElementById('click-through').checked = 
                                                settings.general.clickThrough;
                                        }
                                    }
                                    
                                    // Update module settings
                                    if (settings.modules) {
                                        if (settings.modules.hasOwnProperty('price_check')) {
                                            document.getElementById('module-pricecheck').checked = 
                                                settings.modules.price_check;
                                        }
                                    }
                                }
                            } catch (e) {
                                console.error("Error processing settings message:", e);
                            }
                        });
                    });
                </script>
            </body>
            </html>
        )";
    }

    void SettingsModule::OnLoad() {
        LOG_INFO("Settings module loaded");

        // Register message handler for settings page
        NexileApp* app = NexileApp::GetInstance();
        if (app) {
            OverlayWindow* overlay = app->GetProfileManager()->GetOverlayWindow();
            if (overlay) {
                overlay->RegisterWebMessageCallback([this](const std::wstring& message) {
                    ProcessSettingsMessage(Utils::WideStringToString(message));
                    });
            }
        }

        // Load initial settings
        LoadSettings();
    }

    void SettingsModule::OnUnload() {
        LOG_INFO("Settings module unloaded");

        // Save settings on unload
        SaveSettings();
    }

    void SettingsModule::OnGameChanged() {
        // Update settings for the current game
        LOG_INFO("Settings module: Game changed to {}", GameIDToString(m_currentGame));

        // Update UI if visible
        UpdateSettingsUI();
    }

    void SettingsModule::OnHotkeyPressed(int hotkeyId) {
        // The NexileApp now handles the settings hotkey directly, so this method doesn't need to do anything
        // The code here is deliberately left empty to avoid conflicting with NexileApp's settings handling
    }

    void SettingsModule::SaveSettings() {
        NexileApp* app = NexileApp::GetInstance();
        if (!app) return;

        // Save profile settings
        app->GetProfileManager()->SaveProfile();

        LOG_INFO("Settings saved");
    }

    void SettingsModule::LoadSettings() {
        // Settings are loaded from profile manager
        LOG_INFO("Settings loaded");
    }

    void SettingsModule::UpdateSettingsUI() {
        NexileApp* app = NexileApp::GetInstance();
        if (!app) return;

        OverlayWindow* overlay = app->GetProfileManager()->GetOverlayWindow();
        if (!overlay) return;

        // Create settings JSON object
        json settings;

        // Get current profile
        ProfileSettings& profile = app->GetProfileManager()->GetCurrentProfile();

        // General settings
        settings["general"]["opacity"] = static_cast<int>(profile.overlayOpacity * 100);
        settings["general"]["clickThrough"] = profile.clickThrough;
        settings["general"]["autostart"] = false; // TODO: Implement
        settings["general"]["autodetect"] = true;  // TODO: Implement
        settings["general"]["currentGame"] = GameIDToString(app->GetActiveGameID());

        // Module settings
        for (const auto& [moduleId, enabled] : profile.enabledModules) {
            settings["modules"][moduleId] = enabled;
        }

        // Hotkey settings
        HotkeyManager* hotkeyManager = app->GetProfileManager()->GetHotkeyManager();
        if (hotkeyManager) {
            // Global hotkeys
            settings["hotkeys"][std::to_string(HotkeyManager::HOTKEY_TOGGLE_OVERLAY)] =
                hotkeyManager->GetHotkeyString(HotkeyManager::HOTKEY_TOGGLE_OVERLAY);

            settings["hotkeys"][std::to_string(HotkeyManager::HOTKEY_GAME_SETTINGS)] =
                hotkeyManager->GetHotkeyString(HotkeyManager::HOTKEY_GAME_SETTINGS);

            // Module hotkeys
            settings["hotkeys"][std::to_string(HotkeyManager::HOTKEY_PRICE_CHECK)] =
                hotkeyManager->GetHotkeyString(HotkeyManager::HOTKEY_PRICE_CHECK);

            settings["hotkeys"][std::to_string(HotkeyManager::HOTKEY_BUILD_GUIDE)] =
                hotkeyManager->GetHotkeyString(HotkeyManager::HOTKEY_BUILD_GUIDE);

            settings["hotkeys"][std::to_string(HotkeyManager::HOTKEY_MAP_OVERLAY)] =
                hotkeyManager->GetHotkeyString(HotkeyManager::HOTKEY_MAP_OVERLAY);
        }

        // Send settings to UI
        std::wstringstream script;
        script << L"window.postMessage({";
        script << L"action: 'load_settings',";
        script << L"settings: " << Utils::StringToWideString(json(settings).dump());
        script << L"}, '*');";

        overlay->ExecuteScript(script.str());
    }

    void SettingsModule::ProcessSettingsMessage(const std::string& message) {
        try {
            json msg = json::parse(message);

            // Check if this is a settings message
            if (!msg.contains("action")) {
                return;
            }

            std::string action = msg["action"];

            if (action == "get_settings") {
                // Send current settings to UI
                UpdateSettingsUI();
            }
            else if (action == "save_settings") {
                NexileApp* app = NexileApp::GetInstance();
                if (!app) return;

                ProfileManager* profileManager = app->GetProfileManager();
                if (!profileManager) return;

                // Get current profile
                ProfileSettings& profile = profileManager->GetCurrentProfile();

                // Update general settings
                if (msg.contains("settings") && msg["settings"].contains("general")) {
                    json general = msg["settings"]["general"];

                    if (general.contains("opacity")) {
                        profile.overlayOpacity = static_cast<float>(general["opacity"].get<int>()) / 100.0f;
                    }

                    if (general.contains("clickThrough")) {
                        profile.clickThrough = general["clickThrough"].get<bool>();

                        // Update overlay click-through setting
                        OverlayWindow* overlay = profileManager->GetOverlayWindow();
                        if (overlay) {
                            overlay->SetClickThrough(profile.clickThrough);
                        }
                    }

                    // TODO: Handle other general settings
                }

                // Update module settings
                if (msg.contains("settings") && msg["settings"].contains("modules")) {
                    json modules = msg["settings"]["modules"];

                    if (modules.contains("priceCheck")) {
                        profileManager->SetModuleEnabled("price_check", modules["priceCheck"].get<bool>());
                    }

                    // TODO: Handle other module settings
                }

                // Save settings
                SaveSettings();

                // Close settings UI and return to overlay
                OverlayWindow* overlay = profileManager->GetOverlayWindow();
                if (overlay) {
                    overlay->LoadMainOverlayUI();
                }

                // Exit settings mode in NexileApp
                app->OnHotkeyPressed(HotkeyManager::HOTKEY_GAME_SETTINGS);
            }
            else if (action == "cancel_settings") {
                // Close settings UI without saving
                NexileApp* app = NexileApp::GetInstance();
                if (app) {
                    // Exit settings mode in NexileApp
                    app->OnHotkeyPressed(HotkeyManager::HOTKEY_GAME_SETTINGS);
                }
            }
            else if (action == "reset_settings") {
                // Reset settings to default
                NexileApp* app = NexileApp::GetInstance();
                if (app) {
                    // TODO: Implement reset to default

                    // Update UI
                    UpdateSettingsUI();
                }
            }
            else if (action == "hotkey_recording_start") {
                if (msg.contains("hotkeyId")) {
                    int hotkeyId = msg["hotkeyId"].get<int>();
                    StartHotkeyRecording(hotkeyId);
                }
            }
            else if (action == "hotkey_recording_stop") {
                StopHotkeyRecording();
            }
            else if (action == "hotkey_update") {
                if (msg.contains("hotkeyId") && msg.contains("ctrl") &&
                    msg.contains("alt") && msg.contains("shift") && msg.contains("key")) {

                    int hotkeyId = msg["hotkeyId"].get<int>();
                    bool ctrl = msg["ctrl"].get<bool>();
                    bool alt = msg["alt"].get<bool>();
                    bool shift = msg["shift"].get<bool>();
                    int key = msg["key"].get<int>();

                    // Calculate modifiers
                    int modifiers = 0;
                    if (ctrl) modifiers |= MOD_CONTROL;
                    if (alt) modifiers |= MOD_ALT;
                    if (shift) modifiers |= MOD_SHIFT;

                    UpdateHotkey(hotkeyId, modifiers, key);
                }
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("Error processing settings message: {}", e.what());
        }
    }

    void SettingsModule::StartHotkeyRecording(int hotkeyId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_recordingHotkeyId = hotkeyId;
        LOG_INFO("Started recording hotkey for ID: {}", hotkeyId);
    }

    void SettingsModule::StopHotkeyRecording() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_recordingHotkeyId = -1;
        LOG_INFO("Stopped recording hotkey");
    }

    void SettingsModule::UpdateHotkey(int hotkeyId, int modifiers, int virtualKey) {
        NexileApp* app = NexileApp::GetInstance();
        if (!app) return;

        ProfileManager* profileManager = app->GetProfileManager();
        if (!profileManager) return;

        // Update hotkey in profile
        profileManager->SetHotkeyOverride(hotkeyId, modifiers, virtualKey);

        LOG_INFO("Updated hotkey for ID {}: modifiers={}, key={}", hotkeyId, modifiers, virtualKey);
    }

} // namespace Nexile