#pragma once

#include <string>
#include "../Game/GameTypes.h"

namespace Nexile {

    // Interface for Nexile modules
    class IModule {
    public:
        virtual ~IModule() = default;

        // Called when module is loaded
        virtual void OnModuleLoad(GameID currentGame) = 0;

        // Called when module is unloaded
        virtual void OnModuleUnload() = 0;

        // Called when game changes
        virtual void OnGameChange(GameID newGame) = 0;

        // Called when a hotkey assigned to this module is pressed
        virtual void OnHotkeyPressed(int hotkeyId) = 0;

        // Get module identifier
        virtual std::string GetModuleID() const = 0;

        // Get module name (display name)
        virtual std::string GetModuleName() const = 0;

        // Get module description
        virtual std::string GetModuleDescription() const = 0;

        // Get module version
        virtual std::string GetModuleVersion() const = 0;

        // Get module author
        virtual std::string GetModuleAuthor() const = 0;

        // Get supported games
        virtual bool SupportsGame(GameID gameId) const = 0;

        // Get HTML content for the module UI
        virtual std::string GetModuleUIHTML() const = 0;

        // Check if module is enabled
        virtual bool IsEnabled() const = 0;

        // Enable or disable the module
        virtual void SetEnabled(bool enabled) = 0;
    };

    // Base class for modules
    class ModuleBase : public IModule {
    public:
        ModuleBase() : m_enabled(false), m_currentGame(GameID::None) {}
        virtual ~ModuleBase() = default;

        // IModule implementation
        void OnModuleLoad(GameID currentGame) override {
            m_currentGame = currentGame;
            m_enabled = SupportsGame(currentGame);
            OnLoad();
        }

        void OnModuleUnload() override {
            m_enabled = false;
            OnUnload();
        }

        void OnGameChange(GameID newGame) override {
            m_currentGame = newGame;
            m_enabled = SupportsGame(newGame);
            OnGameChanged();
        }

        bool IsEnabled() const override {
            return m_enabled;
        }

        void SetEnabled(bool enabled) override {
            m_enabled = enabled;
        }

    protected:
        // Virtual methods for derived classes to override
        virtual void OnLoad() {}
        virtual void OnUnload() {}
        virtual void OnGameChanged() {}

        // Current state
        bool m_enabled;
        GameID m_currentGame;
    };

} // namespace Nexile