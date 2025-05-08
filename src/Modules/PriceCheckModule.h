#pragma once

#include "ModuleInterface.h"
#include <string>
#include <vector>
#include <mutex>
#include <future>

namespace Nexile {

    // Price check module for Path of Exile
    class PriceCheckModule : public ModuleBase {
    public:
        PriceCheckModule();
        ~PriceCheckModule() override;

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
        // Perform a price check
        void PerformPriceCheck();

        // Parse item text from clipboard
        bool ParseItemTextFromClipboard(std::string& itemText);

        // Send Ctrl+C to the game to copy item data
        void SendCopyCommand();

        // Query price API
        void QueryPriceAPI(const std::string& itemText);

        // Update UI with price results
        void UpdateUI(const std::string& results);

        // Simulates pressing a key
        void SimulateKeyPress(int virtualKey);

    private:
        // Item data structure
        struct ItemData {
            std::string name;
            std::string baseType;
            std::string rarity;
            std::string itemLevel;
            std::vector<std::string> mods;
        };

        // Parse Path of Exile item text
        bool ParsePoEItem(const std::string& text, ItemData& item);

        // Current item data
        ItemData m_currentItem;

        // Mutex for thread safety
        std::mutex m_mutex;

        // Current price check operation
        std::future<void> m_priceCheckOperation;
    };

} // namespace Nexile