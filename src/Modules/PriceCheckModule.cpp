#include "PriceCheckModule.h"
#include "../Core/NexileApp.h"
#include "../Input/HotkeyManager.h"
#include "../UI/OverlayWindow.h"

#include <Windows.h>
#include <regex>
#include <sstream>
#include <thread>
#include <chrono>
#include <iostream>

namespace Nexile {

    PriceCheckModule::PriceCheckModule() {
        // Initialize here
    }

    PriceCheckModule::~PriceCheckModule() {
        // Cleanup
        if (m_priceCheckOperation.valid()) {
            try {
                m_priceCheckOperation.wait();
            }
            catch (...) {
                // Ignore exceptions during cleanup
            }
        }
    }

    std::string PriceCheckModule::GetModuleID() const {
        return "price_check";
    }

    std::string PriceCheckModule::GetModuleName() const {
        return "Price Check";
    }

    std::string PriceCheckModule::GetModuleDescription() const {
        return "Checks prices for items in Path of Exile";
    }

    std::string PriceCheckModule::GetModuleVersion() const {
        return "1.0.0";
    }

    std::string PriceCheckModule::GetModuleAuthor() const {
        return "Nexile Team";
    }

    bool PriceCheckModule::SupportsGame(GameID gameId) const {
        // This module only supports Path of Exile and Path of Exile 2
        return gameId == GameID::PathOfExile || gameId == GameID::PathOfExile2;
    }

    std::string PriceCheckModule::GetModuleUIHTML() const {
        // Basic HTML for the price check UI
        return R"(
        <div id="price-check-container">
            <div id="price-check-status">Hover over an item and press Alt+D to check price</div>
            <div id="price-check-result" style="display: none;">
                <h3 id="item-name"></h3>
                <div id="item-details"></div>
                <div id="price-info"></div>
            </div>
            <div id="price-check-loading" style="display: none;">
                <p>Checking price...</p>
            </div>
            <div id="price-check-error" style="display: none;">
                <p>Error checking price. Please try again.</p>
            </div>
        </div>
        <script>
            // Function to update UI with price check results
            function updatePriceCheck(data) {
                const container = document.getElementById('price-check-container');
                const status = document.getElementById('price-check-status');
                const result = document.getElementById('price-check-result');
                const loading = document.getElementById('price-check-loading');
                const error = document.getElementById('price-check-error');
                const itemName = document.getElementById('item-name');
                const itemDetails = document.getElementById('item-details');
                const priceInfo = document.getElementById('price-info');
                
                // Reset UI
                status.style.display = 'none';
                result.style.display = 'none';
                loading.style.display = 'none';
                error.style.display = 'none';
                
                try {
                    const itemData = JSON.parse(data);
                    
                    if (itemData.error) {
                        error.textContent = itemData.error;
                        error.style.display = 'block';
                        return;
                    }
                    
                    if (itemData.loading) {
                        loading.style.display = 'block';
                        return;
                    }
                    
                    // Show results
                    itemName.textContent = itemData.name || 'Unknown Item';
                    
                    let detailsHtml = '';
                    if (itemData.rarity) {
                        detailsHtml += `<div>Rarity: ${itemData.rarity}</div>`;
                    }
                    if (itemData.baseType) {
                        detailsHtml += `<div>Base Type: ${itemData.baseType}</div>`;
                    }
                    if (itemData.itemLevel) {
                        detailsHtml += `<div>Item Level: ${itemData.itemLevel}</div>`;
                    }
                    
                    itemDetails.innerHTML = detailsHtml;
                    
                    // Display price information
                    if (itemData.price) {
                        priceInfo.innerHTML = `<div>Estimated Price: ${itemData.price}</div>`;
                        if (itemData.confidence) {
                            priceInfo.innerHTML += `<div>Confidence: ${itemData.confidence}</div>`;
                        }
                    } else {
                        priceInfo.innerHTML = '<div>No price data available</div>';
                    }
                    
                    result.style.display = 'block';
                } catch (e) {
                    console.error('Error parsing price data:', e);
                    error.style.display = 'block';
                }
            }
            
            // Register message handler
            window.addEventListener('message', function(event) {
                const message = event.data;
                if (message && message.module === 'price_check') {
                    updatePriceCheck(message.data);
                }
            });
        </script>
    )";
    }

    void PriceCheckModule::OnLoad() {
        // Register hotkey for price check (Alt+D)
        NexileApp* app = NexileApp::GetInstance();
        if (app) {
            HotkeyManager* hotkeyManager = app->GetProfileManager()->GetHotkeyManager();
            if (hotkeyManager) {
                hotkeyManager->RegisterHotkey(MOD_ALT, 'D', HotkeyManager::HOTKEY_PRICE_CHECK);
            }
        }
    }

    void PriceCheckModule::OnUnload() {
        // Unregister hotkeys
        NexileApp* app = NexileApp::GetInstance();
        if (app) {
            HotkeyManager* hotkeyManager = app->GetProfileManager()->GetHotkeyManager();
            if (hotkeyManager) {
                hotkeyManager->UnregisterHotkey(HotkeyManager::HOTKEY_PRICE_CHECK);
            }
        }
    }

    void PriceCheckModule::OnGameChanged() {
        // Update enabled state based on game
        m_enabled = SupportsGame(m_currentGame);
    }

    void PriceCheckModule::OnHotkeyPressed(int hotkeyId) {
        if (!m_enabled) {
            return;
        }

        if (hotkeyId == HotkeyManager::HOTKEY_PRICE_CHECK) {
            // Start price check in a separate thread
            std::lock_guard<std::mutex> lock(m_mutex);

            // Cancel any existing operation
            if (m_priceCheckOperation.valid()) {
                try {
                    m_priceCheckOperation.wait_for(std::chrono::milliseconds(100));
                }
                catch (...) {
                    // Ignore
                }
            }

            // Start new operation
            m_priceCheckOperation = std::async(std::launch::async, [this]() {
                PerformPriceCheck();
                });

            // Show the module UI
            NexileApp* app = NexileApp::GetInstance();
            if (app && app->GetModule("price_check")) {
                app->SetOverlayVisible(true);
                app->GetProfileManager()->GetOverlayWindow()->LoadModuleUI(app->GetModule("price_check"));

                // Show loading state
                UpdateUI(R"({"loading": true})");
            }
        }
    }

    void PriceCheckModule::PerformPriceCheck() {
        // Send Ctrl+C to copy item under cursor
        SendCopyCommand();

        // Wait a bit for the clipboard to be updated
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Get item text from clipboard
        std::string itemText;
        if (!ParseItemTextFromClipboard(itemText)) {
            UpdateUI(R"({"error": "No item data found in clipboard"})");
            return;
        }

        // Parse item data
        ItemData item;
        if (!ParsePoEItem(itemText, item)) {
            UpdateUI(R"({"error": "Failed to parse item data"})");
            return;
        }

        // Store current item
        m_currentItem = item;

        // Query price API
        QueryPriceAPI(itemText);
    }

    bool PriceCheckModule::ParseItemTextFromClipboard(std::string& itemText) {
        itemText.clear();

        // Try to open clipboard
        if (!OpenClipboard(NULL)) {
            return false;
        }

        // Get clipboard data
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData == NULL) {
            CloseClipboard();
            return false;
        }

        // Lock clipboard data
        char* pszText = static_cast<char*>(GlobalLock(hData));
        if (pszText == NULL) {
            CloseClipboard();
            return false;
        }

        // Copy data
        itemText = pszText;

        // Unlock and close
        GlobalUnlock(hData);
        CloseClipboard();

        return !itemText.empty();
    }

    void PriceCheckModule::SendCopyCommand() {
        // Send Ctrl+C to the game
        // Note: This is allowed as it's one action per hotkey press

        // Press Ctrl down
        SimulateKeyPress(VK_CONTROL | 0x100);

        // Press C down
        SimulateKeyPress('C' | 0x100);

        // Wait a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Release C
        SimulateKeyPress('C');

        // Release Ctrl
        SimulateKeyPress(VK_CONTROL);

        // Wait for clipboard to update
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void PriceCheckModule::SimulateKeyPress(int virtualKey) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = virtualKey & 0xFF;
        input.ki.dwFlags = (virtualKey & 0x100) ? 0 : KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
    }

    bool PriceCheckModule::ParsePoEItem(const std::string& text, ItemData& item) {
        // Reset item
        item = ItemData();

        // Split text into lines
        std::istringstream stream(text);
        std::string line;
        std::vector<std::string> lines;

        while (std::getline(stream, line)) {
            lines.push_back(line);
        }

        if (lines.empty()) {
            return false;
        }

        // First line is the rarity
        if (lines[0].find("Rarity:") == 0) {
            item.rarity = lines[0].substr(8);

            // Remove leading/trailing whitespace
            item.rarity.erase(0, item.rarity.find_first_not_of(" \t"));
            item.rarity.erase(item.rarity.find_last_not_of(" \t") + 1);
        }

        // Second line is item name (if present)
        if (lines.size() > 1) {
            item.name = lines[1];
        }

        // Third line might be base type (for rare/magic items)
        if (lines.size() > 2 &&
            (item.rarity == "Rare" || item.rarity == "Magic" || item.rarity == "Unique")) {
            item.baseType = lines[2];
        }

        // Look for item level
        for (const auto& line : lines) {
            if (line.find("Item Level:") == 0) {
                item.itemLevel = line.substr(11);

                // Remove leading/trailing whitespace
                item.itemLevel.erase(0, item.itemLevel.find_first_not_of(" \t"));
                item.itemLevel.erase(item.itemLevel.find_last_not_of(" \t") + 1);
            }
        }

        // Extract mods (lines after a separator)
        bool inMods = false;
        for (const auto& line : lines) {
            if (line.find("--------") == 0) {
                inMods = true;
                continue;
            }

            if (inMods && !line.empty()) {
                item.mods.push_back(line);
            }
        }

        return !item.name.empty() && !item.rarity.empty();
    }

    void PriceCheckModule::QueryPriceAPI(const std::string& itemText) {
        // TODO: Implement actual API query
        // For now, simulate a delay and return mock data

        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Create a JSON response
        std::ostringstream json;
        json << "{";

        if (!m_currentItem.name.empty()) {
            json << "\"name\": \"" << m_currentItem.name << "\",";
        }

        if (!m_currentItem.baseType.empty()) {
            json << "\"baseType\": \"" << m_currentItem.baseType << "\",";
        }

        if (!m_currentItem.rarity.empty()) {
            json << "\"rarity\": \"" << m_currentItem.rarity << "\",";
        }

        if (!m_currentItem.itemLevel.empty()) {
            json << "\"itemLevel\": \"" << m_currentItem.itemLevel << "\",";
        }

        // Mock price data
        json << "\"price\": \"5-10 chaos\",";
        json << "\"confidence\": \"medium\"";

        json << "}";

        // Update UI with results
        UpdateUI(json.str());
    }

    void PriceCheckModule::UpdateUI(const std::string& results) {
        // Send results to overlay
        NexileApp* app = NexileApp::GetInstance();
        if (app) {
            OverlayWindow* overlay = app->GetProfileManager()->GetOverlayWindow();
            if (overlay) {
                // Create message for JavaScript
                std::ostringstream script;
                script << "window.postMessage({";
                script << "module: 'price_check',";
                script << "data: " << results;
                script << "}, '*');";

                // Convert to wide string
                std::wstring wScript(script.str().begin(), script.str().end());

                // Execute in overlay
                overlay->ExecuteScript(wScript);
            }
        }
    }

} // namespace Nexile
