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

## Technical Architecture

### CEF Integration
Nexile has been migrated from Microsoft WebView2 to Chromium Embedded Framework (CEF) for better cross-platform compatibility and enhanced performance. The CEF integration provides:

- **Enhanced JavaScript Bridge**: Improved communication between C++ and JavaScript
- **Better Resource Management**: Custom resource handlers for HTML assets
- **Single Process Mode**: Simplified deployment and debugging
- **Custom Protocol Support**: Support for `nexile://` protocol for internal resources

## Getting Started

### System Requirements
- Windows 10 or newer (64-bit)
- Visual C++ Redistributable 2019 or newer
- CEF Runtime (included in the installation)
- Supported game client

### Development Setup

#### Prerequisites
1. **Visual Studio 2022** with C++ development tools
2. **CMake 3.20** or newer
3. **vcpkg** for dependency management
4. **CEF Binary Distribution** (see below)

#### CEF Setup
1. Download the CEF Binary Distribution for Windows 64-bit from [CEF Downloads](https://cef-builds.spotifycdn.com/index.html)
    - Recommended: Standard Distribution (not Minimal)
    - Version: 120.x or newer
2. Extract CEF to `third_party/cef/` in the project root
3. The directory structure should look like:
   ```
   third_party/cef/
   ├── cmake/
   ├── include/
   ├── Resources/
   ├── windows64/
   │   ├── libcef.dll
   │   ├── libcef_dll_wrapper.lib
   │   └── ...
   └── README.txt
   ```

#### Building
1. Clone the repository:
   ```bash
   git clone <repository-url>
   cd nexile
   ```

2. Initialize vcpkg dependencies:
   ```bash
   vcpkg install nlohmann-json
   ```

3. Configure with CMake:
   ```bash
   mkdir build
   cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
   ```

4. Build the project:
   ```bash
   cmake --build . --config Release
   ```

#### CEF Distribution Notes
- The CEF binaries are **not included** in the repository due to size constraints
- You must download and extract CEF manually before building
- Make sure to use the correct architecture (x64) and version compatibility

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

## Development

### Project Structure
```
src/
├── Core/           # Application core and main loop
├── UI/             # User interface components (CEF integration)
├── Modules/        # Game-specific modules
├── Game/           # Game detection and window management
├── Input/          # Hotkey management
├── Config/         # Configuration and profile management
└── Utils/          # Utility functions and logging
```

### CEF Integration Details

#### JavaScript Bridge
The CEF integration uses a custom JavaScript bridge for communication:
```javascript
// Send message to C++
window.nexile.postMessage({
    action: 'example_action',
    data: 'example_data'
});

// Receive messages from C++
window.handleNexileMessage = function(messageData) {
    // Process message from C++
};
```

#### Resource Loading
HTML resources are loaded using a custom resource handler that supports the `nexile://` protocol for internal assets.

#### Single Process Mode
CEF runs in single-process mode for simplified deployment and debugging. This may be changed to multi-process in future versions for better stability.

### Module Development

Modules can be developed as either:
1. **Built-in modules**: Compiled into the main executable
2. **Plugin modules**: Separate DLLs loaded at runtime

Each module must implement the `IModule` interface and provide:
- Module identification and metadata
- Game compatibility information
- HTML-based user interface
- Hotkey handling

## Troubleshooting

### CEF Issues
- **CEF initialization failed**: Ensure CEF binaries are in the correct location
- **Blank overlay**: Check that HTML resources are being loaded correctly
- **JavaScript errors**: Use CEF developer tools (F12) for debugging

### General Issues
- If hotkeys aren't working, check for conflicts with other applications
- Ensure your game is running in supported mode (fullscreen windowed recommended)
- Check the application logs in %APPDATA%\Nexile for error messages

### Known Limitations
- CEF requires additional memory compared to WebView2
- Single-process mode may be less stable than multi-process
- Some websites may not render correctly in the embedded browser

## Migration from WebView2

This version represents a complete migration from Microsoft WebView2 to CEF. Key changes include:

### Breaking Changes
- **CEF Dependency**: CEF binaries must be distributed with the application
- **JavaScript API**: Updated message passing interface
- **Resource Loading**: Changed from file:// URLs to custom protocol
- **Build Requirements**: Additional CEF setup steps required

### Benefits
- **Better Cross-Platform Support**: CEF works on multiple operating systems
- **Enhanced Control**: More control over browser behavior and rendering
- **Improved Performance**: Better resource management and caching
- **Modern Web Features**: Support for latest web standards

### Migration Notes
- All HTML files have been updated for CEF compatibility
- JavaScript bridge functions maintain backward compatibility where possible
- Configuration files and user data remain compatible

## Credits

Nexile is developed by the Nexile Team and utilizes the following technologies:
- Chromium Embedded Framework (CEF)
- nlohmann/json for configuration handling
- Windows API for system integration

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Contributing

Contributions are welcome! Please read the contributing guidelines before submitting pull requests.

For bug reports and feature requests, please use the GitHub issue tracker.