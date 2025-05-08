#pragma once

#include <string>

namespace Nexile {

    // Supported games
    enum class GameID {
        None,
        PathOfExile,
        PathOfExile2,
        LastEpoch
    };

    // Game process information
    struct GameProcessInfo {
        std::wstring processName;
        std::wstring windowClassName;
        std::wstring windowTitlePattern;

        GameProcessInfo() = default;

        GameProcessInfo(const std::wstring& process, const std::wstring& className = L"",
            const std::wstring& titlePattern = L"")
            : processName(process),
            windowClassName(className),
            windowTitlePattern(titlePattern) {
        }
    };

    // Convert GameID to string
    inline std::string GameIDToString(GameID gameId) {
        switch (gameId) {
        case GameID::PathOfExile: return "PathOfExile";
        case GameID::PathOfExile2: return "PathOfExile2";
        case GameID::LastEpoch: return "LastEpoch";
        case GameID::None:
        default: return "None";
        }
    }

    // Convert string to GameID
    inline GameID StringToGameID(const std::string& str) {
        if (str == "PathOfExile") return GameID::PathOfExile;
        if (str == "PathOfExile2") return GameID::PathOfExile2;
        if (str == "LastEpoch") return GameID::LastEpoch;
        return GameID::None;
    }

} // namespace Nexile