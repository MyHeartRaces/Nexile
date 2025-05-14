# Nexile

Nexile is a powerful game overlay assistant designed specifically for action RPG games like Path of Exile and Last Epoch. It provides seamless in-game access to useful tools and information without alt-tabbing, helping to enhance your gaming experience.

## Features

### General Features
- **Customizable Overlay System**: Transparent overlay that can be toggled with hotkeys
- **Click-Through Mode**: Interact with your game while the overlay is visible
- **Game Auto-Detection**: Automatically detects supported games and loads appropriate profiles
- **Hotkey System**: Fully customizable global hotkeys for all features
- **Profile System**: Different settings profiles for each supported game
- **System Tray Integration**: Easy access to Nexile features from the system tray

### Game-Specific Modules
- **Price Checker** (Path of Exile): Quickly check the value of items with a simple hotkey
- **In-Game Browser**: Access websites, build guides, and trading sites without leaving your game
- **Settings Manager**: Customize all aspects of Nexile to suit your preferences


## Getting Started

### System Requirements
- Windows 10 or newer
- Microsoft Edge WebView2 Runtime (included in the installation)
- Supported game client

### Installation

1. Download the latest release from the official website
2. Run the installer and follow the on-screen instructions
3. Launch Nexile from the Start menu or desktop shortcut
4. Nexile will start minimized in the system tray

## Usage

### Default Hotkeys

| Function | Hotkey |
|----------|--------|
| Toggle Overlay | Alt+Shift+O |
| Price Check (PoE) | Alt+P |
| Open Settings | Alt+Shift+S |
| Open Browser | Alt+Shift+B |

### Using the Price Checker (Path of Exile)

1. Hover your cursor over an item in Path of Exile
2. Press Alt+P
3. The item details and estimated price will appear in the overlay

### Customizing Settings

1. Press Alt+Shift+S or right-click the system tray icon and select "Settings"
2. Adjust overlay opacity, click-through behavior, and other options
3. Configure module-specific settings
4. Customize hotkeys for different functions
5. Click "Save" to apply your changes

### Using the In-Game Browser

1. Press Alt+Shift+B to open the browser
2. Navigate to websites using the address bar or bookmarks
3. Access build guides, wiki pages, or trading sites without leaving your game

## Configuration

### Profile System

Nexile automatically creates separate configuration profiles for each supported game. When you change settings while a specific game is running, those settings are saved to that game's profile.

### Customizing the Overlay

- **Opacity**: Adjust how transparent the overlay appears
- **Click-Through**: Toggle whether you can interact with the game through the overlay
- **Position**: The overlay automatically centers on your game window

## Troubleshooting

- If hotkeys aren't working, check for conflicts with other applications
- Ensure your game is running in supported mode (fullscreen windowed recommended)
- Check the application logs in %APPDATA%\Nexile for error messages

## Credits

Nexile is developed by the Nexile Team and utilizes the following technologies:
- Microsoft WebView2
- nlohmann/json for configuration handling
