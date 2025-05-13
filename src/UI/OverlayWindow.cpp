#include "UI/OverlayWindow.h"
#include "Core/NexileApp.h"
#include "Modules/ModuleInterface.h"
#include "Utils/Utils.h"
#include "Utils/Logger.h"

#include <functional>
#include <string>
#include <sstream>
#include <fstream>
#include <ShlObj.h>
#include <Shlwapi.h>    
#include <wrl.h>   
#include <wil/com.h>    
#include <algorithm>
#include <nlohmann/json.hpp>

#pragma comment(lib, "Shlwapi.lib")

namespace Nexile {

    namespace {
        void EnsureDirectoryExists(const std::wstring& path) {
            if (PathFileExistsW(path.c_str())) return;
            if (SHCreateDirectoryExW(nullptr, path.c_str(), nullptr) != ERROR_SUCCESS) {
                LOG_WARNING("Failed to create folder: {}", Utils::WideStringToString(path));
            }
        }

        template<typename T>
        std::string HResultToHex(T hr) {
            char buf[11]{};
            sprintf_s(buf, "0x%08X", static_cast<unsigned int>(hr));
            return buf;
        }
    }

    LRESULT CALLBACK OverlayWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        return self ? self->HandleMessage(hwnd, msg, wp, lp) : DefWindowProc(hwnd, msg, wp, lp);
    }

    OverlayWindow::OverlayWindow(NexileApp* app)
        : m_app(app), m_hwnd(nullptr), m_visible(false), m_clickThrough(true) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to initialize COM, HRESULT: {}", HResultToHex(hr));
        }

        InitializeWindow();
        InitializeWebView();
    }

    OverlayWindow::~OverlayWindow() {
        if (m_hwnd) DestroyWindow(m_hwnd);
        CoUninitialize();
    }

    void OverlayWindow::RegisterWindowClass() {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof wc;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = m_app->GetInstanceHandle();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"NexileOverlayClass";
        RegisterClassExW(&wc);
    }

    void OverlayWindow::InitializeWindow() {
        RegisterWindowClass();

        m_hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"NexileOverlayClass", L"Nexile Overlay", WS_POPUP,
            0, 0, 1920, 1080,
            nullptr, nullptr, m_app->GetInstanceHandle(), nullptr);

        if (!m_hwnd) throw std::runtime_error("Failed to create overlay window");

        SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        SetLayeredWindowAttributes(m_hwnd, 0, 200, LWA_ALPHA);
    }

    void OverlayWindow::CenterWindow() {
        if (!m_hwnd) return;

        // Always use primary monitor for window positioning
        HMONITOR hPrimaryMon = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { 0 };
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(hPrimaryMon, &mi);

        // Calculate size and position based on the primary monitor's bounds
        int monWidth = mi.rcMonitor.right - mi.rcMonitor.left;
        int monHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;

        // Get window dimensions
        RECT rc;
        GetWindowRect(m_hwnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        // Ensure window size fits within monitor bounds
        if (width > monWidth) width = monWidth - 40; // Leave small margin
        if (height > monHeight) height = monHeight - 40;

        // Center within the monitor
        int x = mi.rcMonitor.left + (monWidth - width) / 2;
        int y = mi.rcMonitor.top + (monHeight - height) / 2;

        // Ensure the window is fully contained within the monitor bounds
        if (x + width > mi.rcMonitor.right) x = mi.rcMonitor.right - width;
        if (y + height > mi.rcMonitor.bottom) y = mi.rcMonitor.bottom - height;
        if (x < mi.rcMonitor.left) x = mi.rcMonitor.left;
        if (y < mi.rcMonitor.top) y = mi.rcMonitor.top;

        LOG_INFO("Centering window on primary monitor ({}, {}, {}, {}), window ({}, {}, {}, {})",
            mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom,
            x, y, width, height);

        // Set the position
        SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    }

    void OverlayWindow::InitializeWebView() {
        wchar_t appData[MAX_PATH]{};
        std::wstring userDataFolder;
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
            userDataFolder = std::wstring(appData) + L"\\Nexile\\WebView2Data";
            EnsureDirectoryExists(userDataFolder);
            LOG_INFO("WebView2 user data folder: {}", Utils::WideStringToString(userDataFolder));
        }
        else {
            userDataFolder = L"WebView2Data";
            LOG_WARNING("Could not resolve %%APPDATA%%, using local WebView2Data folder");
        }

        std::wstring runtimeDir = Utils::StringToWideString(
            Utils::CombinePath(Utils::GetModulePath(), "webview2_runtime"));

        bool bundleExists = PathFileExistsW((runtimeDir + L"\\msedgewebview2.exe").c_str());
        if (!bundleExists) {
            LOG_ERROR("Bundled WebView2 runtime not found at {}", Utils::WideStringToString(runtimeDir));
            MessageBoxW(nullptr,
                L"Bundled WebView2 runtime not found.\n"
                L"Please ensure the application was installed correctly.",
                L"Nexile – WebView2 error", MB_ICONERROR);
            return;
        }

        LOG_INFO("Using bundled WebView2 runtime at {}", Utils::WideStringToString(runtimeDir));

        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            runtimeDir.c_str(), userDataFolder.c_str(), nullptr,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT hrEnv, ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(hrEnv) || !env) {
                        LOG_ERROR("Failed to create WebView2 environment, HRESULT: {}", HResultToHex(hrEnv));
                        MessageBoxW(nullptr,
                            L"Microsoft Edge WebView2 Runtime could not be initialised.\n"
                            L"Please ensure the application was installed correctly.",
                            L"Nexile – WebView2 error", MB_ICONERROR);
                        return S_OK;
                    }
                    LOG_INFO("WebView2 environment created successfully");
                    m_webViewEnvironment = env;

                    env->CreateCoreWebView2Controller(
                        m_hwnd,
                        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT hrCtl, ICoreWebView2Controller* ctl) -> HRESULT {
                                if (FAILED(hrCtl) || !ctl) {
                                    LOG_ERROR("Failed to create WebView2 controller, HRESULT: {}", HResultToHex(hrCtl));
                                    return S_OK;
                                }
                                m_webViewController = ctl;
                                m_webViewController->get_CoreWebView2(&m_webView);

                                Microsoft::WRL::ComPtr<ICoreWebView2Controller2> ctl2;
                                if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
                                    COREWEBVIEW2_COLOR black{ 0xFF,0x00,0x00,0x00 };
                                    ctl2->put_DefaultBackgroundColor(black);
                                }

                                RECT rc{}; GetClientRect(m_hwnd, &rc);
                                m_webViewController->put_Bounds(rc);

                                SetupWebViewEventHandlers();

                                m_webViewController->put_IsVisible(TRUE);

                                LoadWelcomePage();

                                return S_OK;
                            }).Get());
                    return S_OK;
                }).Get());

        if (FAILED(hr)) {
            LOG_ERROR("Failed to create WebView2 environment with bundled runtime (HRESULT {})", HResultToHex(hr));
            MessageBoxW(nullptr,
                L"Failed to initialize bundled WebView2 runtime.\n"
                L"Please ensure no files are missing from installation.",
                L"Nexile – WebView2 error", MB_ICONERROR);
        }
    }

    void OverlayWindow::LoadWelcomePage() {
        if (!m_webView) {
            LOG_ERROR("Cannot load welcome page: WebView2 not initialized");
            return;
        }

        // Center the window on screen
        CenterWindow();

        // Set window to 70% opacity for welcome screen
        SetLayeredWindowAttributes(m_hwnd, 0, 180, LWA_ALPHA);

        // Set click-through based on currently stored value
        SetClickThrough(m_clickThrough);

        // Using simplified welcome page with functional buttons
        const wchar_t* welcomeHTML = LR"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Welcome to Nexile</title>
    <style>
        html, body {
            margin: 0;
            padding: 0;
            width: 100%;
            height: 100%;
            background-color: rgba(0, 0, 0, 0.75);
            color: white;
            font-family: 'Segoe UI', Arial, sans-serif;
            overflow: hidden;
        }

        .container {
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            height: 100%;
            padding: 20px;
            box-sizing: border-box;
            max-width: 800px;
            margin: 0 auto;
        }

        .logo {
            font-size: 48px;
            font-weight: bold;
            margin-bottom: 20px;
            color: #4a90e2;
        }

        .subtitle {
            font-size: 18px;
            margin-bottom: 40px;
            text-align: center;
        }

        .hotkeys {
            background-color: rgba(40, 40, 40, 0.7);
            border-radius: 8px;
            padding: 15px 25px;
            margin: 10px 0;
            width: 80%;
            max-width: 600px;
        }

        .hotkeys h2 {
            color: #4a90e2;
            margin-top: 0;
            border-bottom: 1px solid rgba(255, 255, 255, 0.2);
            padding-bottom: 10px;
        }

        .hotkey-item {
            display: flex;
            justify-content: space-between;
            margin: 10px 0;
            padding: 5px 0;
        }

        .hotkey-combo {
            background-color: rgba(30, 30, 30, 0.8);
            padding: 5px 10px;
            border-radius: 4px;
            min-width: 80px;
            text-align: center;
            margin-left: 20px;
        }

        .footer {
            position: absolute;
            bottom: 20px;
            color: rgba(255, 255, 255, 0.5);
            font-size: 14px;
        }

        .controls {
            margin-top: 20px;
        }

        .button {
            display: inline-block;
            background-color: #4a90e2;
            color: white;
            padding: 10px 20px;
            border-radius: 4px;
            text-decoration: none;
            margin: 0 10px;
            cursor: pointer;
            transition: background-color 0.2s;
            border: none;
            font-size: 16px;
        }

        .button:hover {
            background-color: #3a80d2;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">NEXILE</div>
        <div class="subtitle">Game Overlay Assistant</div>

        <div class="hotkeys">
            <h2>Default Hotkeys</h2>
            <div class="hotkey-item">
                <span>Toggle Overlay</span>
                <span class="hotkey-combo">Alt+Shift+O</span>
            </div>
            <div class="hotkey-item">
                <span>Price Check (PoE)</span>
                <span class="hotkey-combo">Alt+P</span>
            </div>
            <div class="hotkey-item">
                <span>Open Settings</span>
                <span class="hotkey-combo">Alt+Shift+S</span>
            </div>
            <div class="hotkey-item">
                <span>Open Browser</span>
                <span class="hotkey-combo">Alt+Shift+B</span>
            </div>
        </div>

        <div class="controls">
            <button class="button" id="settings-button">Settings</button>
            <button class="button" id="browser-button">Open Browser</button>
            <button class="button" id="close-button">Close Overlay</button>
        </div>

        <div class="footer">Nexile v0.1.0 | Press Alt+Shift+O to toggle overlay</div>
    </div>

    <script>
        // Make buttons work by using proper message format
        document.addEventListener('DOMContentLoaded', function() {
            document.getElementById('settings-button').addEventListener('click', function() {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'open_settings'
                }));
            });

            document.getElementById('browser-button').addEventListener('click', function() {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'open_browser'
                }));
            });

            document.getElementById('close-button').addEventListener('click', function() {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'toggle_overlay'
                }));
            });
        });
    </script>
</body>
</html>
)";

        // Set HTML content directly
        m_webView->NavigateToString(welcomeHTML);
        LOG_INFO("Welcome page loaded");
    }

    void OverlayWindow::LoadBrowserPage() {
        if (!m_webView) {
            LOG_ERROR("Cannot load browser page: WebView2 not initialized");
            return;
        }

        // Make window fully opaque for browser
        SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);

        // Center the window on screen
        CenterWindow();

        // Turn off click-through for browser
        SetClickThrough(false);

        const wchar_t* browserHTML_part1 = LR"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Nexile Browser</title>
    <style>
        html, body {
            margin: 0;
            padding: 0;
            width: 100%;
            height: 100%;
            background-color: rgba(20, 20, 20, 0.0);
            color: white;
            font-family: 'Segoe UI', Arial, sans-serif;
            overflow: hidden;
        })";

        const wchar_t* browserHTML_part2 = LR"(
        .browser-container {
            display: flex;
            flex-direction: column;
            height: 100%;
            padding: 10px;
            box-sizing: border-box;
            max-width: 90%;
            margin: 0 auto;
        }

        .address-bar {
            display: flex;
            padding: 10px;
            background-color: rgba(40, 40, 40, 1.0);
            border-radius: 5px;
            margin-bottom: 10px;
            width: 100%;
        })";

        const wchar_t* browserHTML_part3 = LR"(
        .address-input {
            flex-grow: 1;
            padding: 8px 12px;
            border: none;
            border-radius: 3px;
            background-color: rgba(60, 60, 60, 1.0);
            color: white;
            margin-right: 10px;
            font-size: 14px;
        }

        .address-input:focus {
            outline: none;
            background-color: rgba(70, 70, 70, 1.0);
        })";

        const wchar_t* browserHTML_part4 = LR"(
        .navigation-buttons {
            display: flex;
            gap: 5px;
        }

        .nav-button {
            background-color: #4a90e2;
            border: none;
            border-radius: 3px;
            color: white;
            padding: 8px 12px;
            cursor: pointer;
            font-size: 14px;
        }

        .nav-button:hover {
            background-color: #3a80d2;
        })";

        const wchar_t* browserHTML_part5 = LR"(
        .bookmarks {
            display: flex;
            gap: 10px;
            padding: 10px;
            background-color: rgba(40, 40, 40, 1.0);
            border-radius: 5px;
            margin-bottom: 10px;
            overflow-x: auto;
            white-space: nowrap;
            width: 100%;
        }

        .bookmark {
            background-color: #333;
            color: #ddd;
            padding: 6px 10px;
            border-radius: 3px;
            cursor: pointer;
            font-size: 13px;
        })";

        const wchar_t* browserHTML_part6 = LR"(
        .bookmark:hover {
            background-color: #444;
        }
        
        .webview-frame {
            flex-grow: 1;
            width: 100%;
            height: calc(100vh - 130px);
            background-color: white;
            border-radius: 5px;
            overflow: hidden;
        }

        .browser-content {
            flex-grow: 1;
            width: 100%;
            height: calc(100vh - 130px);
            border: none;
            border-radius: 5px;
        })";

        const wchar_t* browserHTML_part7 = LR"(
        .status-bar {
            padding: 8px;
            background-color: rgba(40, 40, 40, 1.0);
            border-radius: 3px;
            margin-top: 10px;
            font-size: 12px;
            color: #aaa;
            width: 100%;
        }
    </style>
</head>
<body>
    <div class="browser-container">
        <div class="address-bar">
            <input type="text" class="address-input" id="urlInput" placeholder="Enter URL or search term..." />
            <div class="navigation-buttons">
                <button class="nav-button" id="goButton">Go</button>
                <button class="nav-button" id="backButton">←</button>
                <button class="nav-button" id="homeButton">Home</button>
                <button class="nav-button" id="closeButton">Close</button>
            </div>
        </div>)";

        const wchar_t* browserHTML_part8 = LR"(
        <div class="bookmarks">
            <div class="bookmark" data-url="https://www.google.com">Google</div>
            <div class="bookmark" data-url="https://www.reddit.com/r/pathofexile">PoE Reddit</div>
            <div class="bookmark" data-url="https://www.poelab.com">PoE Lab</div>
            <div class="bookmark" data-url="https://www.poe.ninja">poe.ninja</div>
            <div class="bookmark" data-url="https://www.poedb.tw">PoeDB</div>
        </div>

        <iframe id="browserFrame" class="browser-content" src="about:blank"></iframe>

        <div class="status-bar" id="statusBar">Ready</div>
    </div>)";

        const wchar_t* browserHTML_part9 = LR"(
    <script>
        // Custom postMessage polyfill to ensure reliable communication
        window.originalPostMessage = window.postMessage;
        window.postMessage = function(message) {
            try {
                if (window.chrome && window.chrome.webview) {
                    const strMessage = typeof message === 'string' ? message : JSON.stringify(message);
                    window.chrome.webview.postMessage(strMessage);
                } else {
                    window.originalPostMessage(message, '*');
                }
            } catch (e) {
                console.error('Error in postMessage:', e);
            }
        };
    
        document.addEventListener('DOMContentLoaded', function() {
            const urlInput = document.getElementById('urlInput');
            const goButton = document.getElementById('goButton');
            const backButton = document.getElementById('backButton');
            const homeButton = document.getElementById('homeButton');
            const closeButton = document.getElementById('closeButton');
            const statusBar = document.getElementById('statusBar');
            const browserFrame = document.getElementById('browserFrame');
            const bookmarks = document.querySelectorAll('.bookmark');)";

        const wchar_t* browserHTML_part10 = LR"(
            // Function to navigate to a URL
            function navigateToUrl(url) {
                if (!url.startsWith('http://') && !url.startsWith('https://')) {
                    url = 'https://' + url;
                }
                
                try {
                    // Update iframe source
                    browserFrame.src = url;
                    urlInput.value = url;
                    statusBar.textContent = 'Loading: ' + url;
                } catch (error) {
                    statusBar.textContent = 'Error: ' + error.message;
                }
            })";

        const wchar_t* browserHTML_part11 = LR"(
            // Handle Go button and Enter key
            goButton.addEventListener('click', function() {
                if (urlInput.value.trim()) {
                    navigateToUrl(urlInput.value.trim());
                }
            });

            urlInput.addEventListener('keypress', function(e) {
                if (e.key === 'Enter' && urlInput.value.trim()) {
                    navigateToUrl(urlInput.value.trim());
                }
            });)";

        const wchar_t* browserHTML_part12 = LR"(
            // Handle back button
            backButton.addEventListener('click', function() {
                try {
                    browserFrame.contentWindow.history.back();
                } catch (e) {
                    statusBar.textContent = 'Cannot go back: ' + e.message;
                }
            });

            // Handle home button
            homeButton.addEventListener('click', function() {
                navigateToUrl('https://www.google.com');
            });)";

        const wchar_t* browserHTML_part13 = LR"(
            // Handle close button
            closeButton.addEventListener('click', function() {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'close_browser'
                }));
            });

            // Handle bookmark clicks
            bookmarks.forEach(bookmark => {
                bookmark.addEventListener('click', function() {
                    const url = this.getAttribute('data-url');
                    if (url) {
                        navigateToUrl(url);
                    }
                });
            });)";

        const wchar_t* browserHTML_part14 = LR"(
            // Handle iframe load events
            browserFrame.addEventListener('load', function() {
                try {
                    statusBar.textContent = 'Loaded: ' + browserFrame.contentWindow.location.href;
                    urlInput.value = browserFrame.contentWindow.location.href;
                } catch (e) {
                    // Catch cross-origin errors
                    statusBar.textContent = 'Loaded: ' + urlInput.value;
                }
            });

            // Set initial focus
            urlInput.focus();
            
            // Navigate to default page
            navigateToUrl('https://www.google.com');
        });
    </script>
</body>
</html>)";

        // Concatenate browser HTML parts
        std::wstring browserHTML = std::wstring(browserHTML_part1) +
            std::wstring(browserHTML_part2) +
            std::wstring(browserHTML_part3) +
            std::wstring(browserHTML_part4) +
            std::wstring(browserHTML_part5) +
            std::wstring(browserHTML_part6) +
            std::wstring(browserHTML_part7) +
            std::wstring(browserHTML_part8) +
            std::wstring(browserHTML_part9) +
            std::wstring(browserHTML_part10) +
            std::wstring(browserHTML_part11) +
            std::wstring(browserHTML_part12) +
            std::wstring(browserHTML_part13) +
            std::wstring(browserHTML_part14);

        // Set the browser HTML
        m_webView->NavigateToString(browserHTML.c_str());
        LOG_INFO("Browser page loaded");
    }

    void OverlayWindow::LoadMainOverlayUI() {
        if (!m_webView) {
            LOG_ERROR("Cannot load main overlay UI: WebView2 not initialized");
            return;
        }

        // Set window to 70% opacity for main overlay
        SetLayeredWindowAttributes(m_hwnd, 0, 180, LWA_ALPHA);

        // Restore click-through setting
        SetClickThrough(m_clickThrough);

        std::string htmlPath = Utils::CombinePath(Utils::GetModulePath(), "HTML\\main_overlay.html");
        LOG_INFO("Loading main overlay HTML from: {}", htmlPath);

        if (!Utils::FileExists(htmlPath)) {
            LOG_ERROR("Main overlay HTML file not found: {}", htmlPath);
            htmlPath = Utils::CombinePath(Utils::GetModulePath(), "..\\HTML\\main_overlay.html");
            LOG_INFO("Trying alternative path: {}", htmlPath);

            if (!Utils::FileExists(htmlPath)) {
                LOG_ERROR("Alternative path also failed, creating default HTML content");
                std::wstring fallback = L"<html><head><title>Nexile Overlay</title><style>body{background:#1e1e1ecc;color:#fff;font-family:Arial;padding:20px}</style></head><body><h1>Nexile Overlay</h1><p>Press Alt+P to check item prices in Path of Exile</p></body></html>";
                m_webView->NavigateToString(fallback.c_str());
                return;
            }
        }

        std::wstring url = L"file:///" + Utils::StringToWideString(htmlPath);
        std::replace(url.begin(), url.end(), L'\\', L'/');
        m_webView->Navigate(url.c_str());
    }

    void OverlayWindow::SetupWebViewEventHandlers() {
        if (!m_webView) {
            LOG_ERROR("Cannot setup WebView event handlers: WebView2 not initialized");
            return;
        }

        // Configure WebView settings first
        Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(m_webView->get_Settings(&settings))) {
            settings->put_IsScriptEnabled(TRUE);
            settings->put_AreDefaultScriptDialogsEnabled(TRUE);
            settings->put_IsWebMessageEnabled(TRUE);

            // Try to get advanced settings if available
            Microsoft::WRL::ComPtr<ICoreWebView2Settings3> settings3;
            if (SUCCEEDED(settings.As(&settings3))) {
                // Allow iframe navigation to any domain
                settings3->put_AreBrowserAcceleratorKeysEnabled(TRUE);
            }

            // Try to get additional settings
            Microsoft::WRL::ComPtr<ICoreWebView2Settings4> settings4;
            if (SUCCEEDED(settings.As(&settings4))) {
                // Ensure iframes can be properly shown
                settings4->put_IsGeneralAutofillEnabled(TRUE);
            }
        }

        // Configure WebView permissions
        m_webView->add_PermissionRequested(
            Microsoft::WRL::Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [](ICoreWebView2* /*sender*/, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                    COREWEBVIEW2_PERMISSION_KIND kind;
                    args->get_PermissionKind(&kind);

                    // Allow all permissions that might be needed for iframe content
                    args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);
                    return S_OK;
                }).Get(), nullptr);

        m_webView->add_WebMessageReceived(
            Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2* /*sender*/, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    wil::unique_cotaskmem_string raw;
                    args->get_WebMessageAsJson(&raw);
                    if (raw) {
                        std::wstring msg = raw.get();
                        LOG_DEBUG("Received web message: {}", Utils::WideStringToString(msg));
                        HandleWebMessage(msg);
                    }
                    return S_OK;
                }).Get(),
                    &m_webMessageReceivedToken);

        m_webView->add_NavigationCompleted(
            Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this](ICoreWebView2* /*sender*/, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                    BOOL success;
                    args->get_IsSuccess(&success);
                    if (success) {
                        LOG_INFO("Navigation completed successfully");

                        wil::unique_cotaskmem_string url;
                        if (m_webView && SUCCEEDED(m_webView->get_Source(&url)) && url) {
                            std::wstring wUrl = url.get();
                            LOG_INFO("Current URL: {}", Utils::WideStringToString(wUrl));

                            std::wstringstream script;
                            script << L"window.postMessage({";
                            script << L"action: 'navigate_complete',";
                            script << L"url: '" << wUrl << L"'";
                            script << L"}, '*');";

                            ExecuteScript(script.str());
                        }
                    }
                    else {
                        COREWEBVIEW2_WEB_ERROR_STATUS status;
                        args->get_WebErrorStatus(&status);
                        LOG_ERROR("Navigation failed with error: {}", status);
                    }
                    return S_OK;
                }).Get(),
                    &m_navigationCompletedToken);

        // Configure proper WebView-to-JS communication
        const wchar_t* initScript = LR"(
            window.chrome = window.chrome || {};
            window.chrome.webview = {
                postMessage: function(data) {
                    window.chrome.webview.hostObjects.sync.bridge.postMessage(data);
                }
            };
            
            // Set up message listener that works in both directions
            window.addEventListener('message', function(e) {
                if (!e.data) return;
                try {
                    // Handle both string and object messages
                    const data = typeof e.data === 'string' ? e.data : JSON.stringify(e.data);
                    window.chrome.webview.postMessage(data);
                } catch(ex) {
                    console.error('Failed to post message:', ex);
                }
            });
            
            // Create a global event listener for settings
            window.applySettings = function(settings) {
                document.dispatchEvent(new CustomEvent('applySettings', { detail: settings }));
            };
        )";

        m_webView->AddScriptToExecuteOnDocumentCreated(initScript, nullptr);

        // Add direct message handler - simpler approach without host objects
        m_webView->AddScriptToExecuteOnDocumentCreated(
            L"window.chrome = window.chrome || {};"
            L"window.chrome.webview = {"
            L"  postMessage: function(data) {"
            L"    window.external.notify(data);"
            L"  }"
            L"};"
            L"window.addEventListener('message', function(e) {"
            L"  if (e.data) {"
            L"    try {"
            L"      const message = typeof e.data === 'string' ? e.data : JSON.stringify(e.data);"
            L"      window.chrome.webview.postMessage(message);"
            L"    } catch(ex) { console.error('Message error:', ex); }"
            L"  }"
            L"});", nullptr);

#ifdef _DEBUG
        m_webView->OpenDevToolsWindow();
#endif
        LOG_INFO("WebView2 event handlers setup complete");
    }

    void OverlayWindow::HandleWebMessage(const std::wstring& message) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);

        std::string msgStr = Utils::WideStringToString(message);

        try {
            nlohmann::json j = nlohmann::json::parse(msgStr);

            if (j.contains("action")) {
                std::string action = j["action"];

                if (action == "open_browser") {
                    LoadBrowserPage();
                    return;
                }
                else if (action == "close_browser") {
                    LoadWelcomePage();
                    return;
                }
                else if (action == "navigate_to" && j.contains("url")) {
                    std::string url = j["url"];
                    if (m_webView) {
                        m_webView->Navigate(Utils::StringToWideString(url).c_str());
                        LOG_INFO("Navigating to: {}", url);
                    }
                    return;
                }
                else if (action == "go_back") {
                    if (m_webView) {
                        BOOL canGoBack;
                        if (SUCCEEDED(m_webView->get_CanGoBack(&canGoBack)) && canGoBack) {
                            m_webView->GoBack();
                        }
                    }
                    return;
                }
                else if (action == "open_settings") {
                    if (m_app) {
                        auto settingsModule = m_app->GetModule("settings");
                        if (settingsModule) {
                            LoadModuleUI(settingsModule);
                            SetClickThrough(false);
                            m_app->OnHotkeyPressed(HotkeyManager::HOTKEY_GAME_SETTINGS);
                        }
                    }
                    return;
                }
                else if (action == "toggle_overlay") {
                    if (m_app) {
                        m_app->ToggleOverlay();
                    }
                    return;
                }
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("Error parsing message: {}", e.what());
        }

        for (auto& cb : m_webMessageCallbacks) cb(message);
    }

    void OverlayWindow::RegisterWebMessageCallback(WebMessageCallback cb) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_webMessageCallbacks.emplace_back(std::move(cb));
    }

    void OverlayWindow::Show() {
        if (!m_hwnd) {
            LOG_ERROR("Cannot show overlay: window not initialized");
            return;
        }

        m_visible = true;

        // Ensure the window appears on the same monitor as the active game
        CenterWindow();

        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        LOG_INFO("Overlay window shown");

        if (m_webViewController) {
            RECT rc{}; GetClientRect(m_hwnd, &rc);
            m_webViewController->put_Bounds(rc);
        }
    }

    void OverlayWindow::Hide() {
        if (!m_hwnd) {
            LOG_ERROR("Cannot hide overlay: window not initialized");
            return;
        }

        m_visible = false;
        ShowWindow(m_hwnd, SW_HIDE);
        LOG_INFO("Overlay window hidden");
    }

    void OverlayWindow::SetPosition(const RECT& rect) {
        if (!m_hwnd || !m_webViewController) {
            LOG_ERROR("Cannot set position: window or WebView controller not initialized");
            return;
        }

        // Always use primary monitor to ensure the window is fully visible
        HMONITOR hPrimaryMon = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { 0 };
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(hPrimaryMon, &mi);

        // Calculate size and position based on the primary monitor's bounds
        int monWidth = mi.rcMonitor.right - mi.rcMonitor.left;
        int monHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;

        // Set a fixed size that fits within the primary monitor
        int width = static_cast<int>(min(monWidth * 0.8, 1280.0));  // Max width 1280 or 80% of monitor
        int height = static_cast<int>(min(monHeight * 0.8, 960.0)); // Max height 960 or 80% of monitor

        // Center within the primary monitor - ensure it stays fully on screen
        int x = mi.rcMonitor.left + (monWidth - width) / 2;
        int y = mi.rcMonitor.top + (monHeight - height) / 2;

        // Ensure the window is fully contained within the monitor bounds
        if (x + width > mi.rcMonitor.right) x = mi.rcMonitor.right - width;
        if (y + height > mi.rcMonitor.bottom) y = mi.rcMonitor.bottom - height;
        if (x < mi.rcMonitor.left) x = mi.rcMonitor.left;
        if (y < mi.rcMonitor.top) y = mi.rcMonitor.top;

        // Log the positioning information
        LOG_INFO("Setting overlay window position: primary monitor ({}, {}, {}, {}), window ({}, {}, {}, {})",
            mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom,
            x, y, width, height);

        // Set the window position and size
        SetWindowPos(
            m_hwnd, HWND_TOPMOST,
            x, y, width, height,
            SWP_SHOWWINDOW | SWP_NOACTIVATE);

        // Update the WebView bounds to match the window
        m_webViewController->put_Bounds({ 0, 0, width, height });
    }

    void OverlayWindow::Navigate(const std::wstring& uri) {
        if (m_webView) {
            LOG_INFO("Navigating WebView to: {}", Utils::WideStringToString(uri));
            m_webView->Navigate(uri.c_str());
        }
        else LOG_ERROR("Cannot navigate: WebView not initialized");
    }

    void OverlayWindow::ExecuteScript(const std::wstring& script) {
        if (!m_webView) {
            LOG_ERROR("Cannot execute script: WebView not initialized");
            return;
        }

        m_webView->ExecuteScript(
            script.c_str(),
            Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [](HRESULT hr, LPCWSTR) -> HRESULT {
                    if (FAILED(hr))
                        LOG_ERROR("Script execution failed, HRESULT: 0x%08X", static_cast<unsigned int>(hr));
                    return S_OK;
                }).Get());
    }

    void OverlayWindow::SetClickThrough(bool clickThrough) {
        if (!m_hwnd) {
            LOG_ERROR("Cannot set click-through: window not initialized");
            return;
        }

        m_clickThrough = clickThrough;
        LONG exStyle = GetWindowLong(m_hwnd, GWL_EXSTYLE);
        exStyle = clickThrough ? (exStyle | WS_EX_TRANSPARENT)
            : (exStyle & ~WS_EX_TRANSPARENT);
        SetWindowLong(m_hwnd, GWL_EXSTYLE, exStyle);
        LOG_INFO("Overlay click-through set to: {}", clickThrough);
    }

    void OverlayWindow::LoadModuleUI(const std::shared_ptr<IModule>& module) {
        if (!m_webView || !module) {
            LOG_ERROR("Cannot load module UI: WebView or module not initialized");
            return;
        }

        // Set window to 70% opacity for module UI
        SetLayeredWindowAttributes(m_hwnd, 0, 180, LWA_ALPHA);

        // Turn off click-through for module UI
        SetClickThrough(false);

        std::string moduleId = module->GetModuleID();
        LOG_INFO("Loading UI for module: {}", moduleId);

        // For settings module, use our simplified settings page
        if (moduleId == "settings") {
            // Break up large HTML string into smaller parts to avoid compiler limits
            const wchar_t* settingsHTML_part1 = LR"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Nexile Settings</title>
    <style>
        :root {
            --bg-color: rgba(30, 30, 30, 0.85);
            --primary-color: #4a90e2;
            --border-color: #555;
            --text-color: #fff;
            --secondary-bg: rgba(50, 50, 50, 0.7);
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        })";

            const wchar_t* settingsHTML_part2 = LR"(
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            color: var(--text-color);
            background-color: var(--bg-color);
            padding: 20px;
        }

        .settings-container {
            width: 100%;
            max-width: 800px;
            margin: 0 auto;
        }

        h1 {
            color: var(--primary-color);
            margin-bottom: 20px;
            border-bottom: 1px solid var(--border-color);
            padding-bottom: 10px;
        }

        h2 {
            color: var(--primary-color);
            margin: 25px 0 15px 0;
        })";

            const wchar_t* settingsHTML_part3 = LR"(
        .settings-section {
            background-color: var(--secondary-bg);
            border-radius: 8px;
            padding: 20px;
            margin-bottom: 20px;
        }

        .setting-item {
            margin-bottom: 15px;
            display: flex;
            align-items: center;
            justify-content: space-between;
        }

        .setting-label {
            font-size: 16px;
            flex: 1;
        }

        .setting-control {
            display: flex;
            align-items: center;
            min-width: 150px;
        })";

            const wchar_t* settingsHTML_part4 = LR"(
        /* Checkbox styling */
        .checkbox-container {
            display: block;
            position: relative;
            padding-left: 35px;
            margin-bottom: 12px;
            cursor: pointer;
            font-size: 16px;
            user-select: none;
        }

            .checkbox-container input {
                position: absolute;
                opacity: 0;
                cursor: pointer;
                height: 0;
                width: 0;
            }

        .checkmark {
            position: absolute;
            top: 0;
            left: 0;
            height: 25px;
            width: 25px;
            background-color: #333;
            border-radius: 4px;
        })";

            const wchar_t* settingsHTML_part5 = LR"(
        .checkbox-container:hover input ~ .checkmark {
            background-color: #444;
        }

        .checkbox-container input:checked ~ .checkmark {
            background-color: var(--primary-color);
        }

        .checkmark:after {
            content: "";
            position: absolute;
            display: none;
        }

        .checkbox-container input:checked ~ .checkmark:after {
            display: block;
        }

        .checkbox-container .checkmark:after {
            left: 9px;
            top: 5px;
            width: 5px;
            height: 10px;
            border: solid white;
            border-width: 0 3px 3px 0;
            transform: rotate(45deg);
        })";

            const wchar_t* settingsHTML_part6 = LR"(
        /* Slider styling */
        .slider-container {
            width: 100%;
        }

        .slider {
            -webkit-appearance: none;
            width: 100%;
            height: 10px;
            border-radius: 5px;
            background: #333;
            outline: none;
        }

            .slider::-webkit-slider-thumb {
                -webkit-appearance: none;
                appearance: none;
                width: 20px;
                height: 20px;
                border-radius: 50%;
                background: var(--primary-color);
                cursor: pointer;
            })";

            const wchar_t* settingsHTML_part7 = LR"(
            .slider::-moz-range-thumb {
                width: 20px;
                height: 20px;
                border-radius: 50%;
                background: var(--primary-color);
                cursor: pointer;
            }

        .slider-value {
            margin-left: 10px;
            min-width: 40px;
            text-align: right;
        }

        /* Hotkey display */
        .hotkey-display {
            display: inline-block;
            background-color: #333;
            padding: 5px 10px;
            border-radius: 4px;
            min-width: 100px;
            text-align: center;
            margin-right: 10px;
        })";

            const wchar_t* settingsHTML_part8 = LR"(
        .button {
            display: inline-block;
            background-color: var(--primary-color);
            color: white;
            padding: 5px 15px;
            border-radius: 4px;
            cursor: pointer;
            border: none;
            font-size: 14px;
        }

            .button:hover {
                background-color: #3a80d2;
            }

        .button-small {
            padding: 3px 10px;
            font-size: 12px;
        }

        .buttons-container {
            display: flex;
            justify-content: flex-end;
            margin-top: 20px;
        })";

            const wchar_t* settingsHTML_part9 = LR"(
            .buttons-container button {
                margin-left: 10px;
            }

        /* Recording state */
        .recording {
            background-color: #ff4d4d;
            animation: blink 1s infinite;
        }

        @keyframes blink {
            50% {
                background-color: #ff8080;
            }
        }
    </style>
</head>)";

            const wchar_t* settingsHTML_part10 = LR"(
<body>
    <div class="settings-container">
        <h1>Nexile Settings</h1>

        <div class="settings-section">
            <h2>General Settings</h2>

            <div class="setting-item">
                <span class="setting-label">Overlay Opacity</span>
                <div class="setting-control">
                    <div class="slider-container">
                        <input type="range" min="20" max="100" value="80" class="slider" id="opacity-slider">
                    </div>
                    <span class="slider-value" id="opacity-value">80%</span>
                </div>
            </div>)";

            const wchar_t* settingsHTML_part11 = LR"(
            <div class="setting-item">
                <span class="setting-label">Click-through Overlay</span>
                <div class="setting-control">
                    <label class="checkbox-container">
                        <input type="checkbox" id="click-through-checkbox" checked>
                        <span class="checkmark"></span>
                    </label>
                </div>
            </div>

            <div class="setting-item">
                <span class="setting-label">Start with Windows</span>
                <div class="setting-control">
                    <label class="checkbox-container">
                        <input type="checkbox" id="autostart-checkbox">
                        <span class="checkmark"></span>
                    </label>
                </div>
            </div>)";

            const wchar_t* settingsHTML_part12 = LR"(
            <div class="setting-item">
                <span class="setting-label">Auto-detect Games</span>
                <div class="setting-control">
                    <label class="checkbox-container">
                        <input type="checkbox" id="autodetect-checkbox" checked>
                        <span class="checkmark"></span>
                    </label>
                </div>
            </div>
        </div>

        <div class="settings-section">
            <h2>Hotkey Settings</h2>

            <div class="setting-item">
                <span class="setting-label">Toggle Overlay</span>
                <div class="setting-control">
                    <span class="hotkey-display" id="toggle-overlay-hotkey">Alt+Shift+O</span>
                    <button class="button button-small" id="toggle-overlay-button">Change</button>
                </div>
            </div>)";

            const wchar_t* settingsHTML_part13 = LR"(
            <div class="setting-item">
                <span class="setting-label">Open Settings</span>
                <div class="setting-control">
                    <span class="hotkey-display" id="settings-hotkey">Alt+Shift+S</span>
                    <button class="button button-small" id="settings-button">Change</button>
                </div>
            </div>

            <div class="setting-item">
                <span class="setting-label">Open Browser</span>
                <div class="setting-control">
                    <span class="hotkey-display" id="browser-hotkey">Alt+Shift+B</span>
                    <button class="button button-small" id="browser-button">Change</button>
                </div>
            </div>
        </div>)";

            const wchar_t* settingsHTML_part14 = LR"(
        <div class="buttons-container">
            <button class="button" id="defaults-button">Reset to Defaults</button>
            <button class="button" id="cancel-button">Cancel</button>
            <button class="button" id="save-button">Save</button>
        </div>
    </div>)";

            const wchar_t* settingsHTML_part15 = LR"(
    <script>
        // Initialize UI
        document.addEventListener('DOMContentLoaded', function () {
            // Setup sliders
            const opacitySlider = document.getElementById('opacity-slider');
            const opacityValue = document.getElementById('opacity-value');

            opacitySlider.addEventListener('input', function () {
                opacityValue.textContent = this.value + '%';
            });)";

            const wchar_t* settingsHTML_part16 = LR"(
            // Setup buttons
            document.getElementById('save-button').addEventListener('click', function() {
                const settings = {
                    general: {
                        opacity: parseInt(opacitySlider.value),
                        clickThrough: document.getElementById('click-through-checkbox').checked,
                        autostart: document.getElementById('autostart-checkbox').checked,
                        autodetect: document.getElementById('autodetect-checkbox').checked
                    }
                };

                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage(JSON.stringify({
                        action: 'save_settings',
                        settings: settings
                    }));
                } else {
                    // Fallback method
                    window.postMessage({
                        action: 'save_settings',
                        settings: settings
                    }, '*');
                }
            });)";

            const wchar_t* settingsHTML_part17 = LR"(
            document.getElementById('cancel-button').addEventListener('click', function() {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'cancel_settings'
                }));
            });

            document.getElementById('defaults-button').addEventListener('click', function() {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'reset_settings'
                }));
            });

            // Request current settings
            setTimeout(function() {
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'get_settings'
                }));
            }, 500);)";

            const wchar_t* settingsHTML_part18 = LR"(
            // Listen for settings from C++ host
            window.addEventListener('message', function(event) {
                try {
                    const data = event.data;
                    const message = typeof data === 'string' ? JSON.parse(data) : data;
                    
                    if (message.action === 'load_settings') {
                        const settings = message.settings;
                        
                        // Update UI with settings
                        if (settings.general) {
                            if (settings.general.opacity !== undefined) {
                                document.getElementById('opacity-slider').value = settings.general.opacity;
                                document.getElementById('opacity-value').textContent = settings.general.opacity + '%';
                            })";

            const wchar_t* settingsHTML_part19 = LR"(                            
                            if (settings.general.clickThrough !== undefined) {
                                document.getElementById('click-through-checkbox').checked = settings.general.clickThrough;
                            }
                            
                            if (settings.general.autostart !== undefined) {
                                document.getElementById('autostart-checkbox').checked = settings.general.autostart;
                            }
                            
                            if (settings.general.autodetect !== undefined) {
                                document.getElementById('autodetect-checkbox').checked = settings.general.autodetect;
                            }
                        })";

            const wchar_t* settingsHTML_part20 = LR"(                        
                        // Update hotkeys
                        if (settings.hotkeys) {
                            if (settings.hotkeys['1000']) {
                                document.getElementById('toggle-overlay-hotkey').textContent = settings.hotkeys['1000'];
                            }
                            
                            if (settings.hotkeys['1004']) {
                                document.getElementById('settings-hotkey').textContent = settings.hotkeys['1004'];
                            }
                            
                            if (settings.hotkeys['1005']) {
                                document.getElementById('browser-hotkey').textContent = settings.hotkeys['1005'];
                            }
                        }
                    }
                } catch (error) {
                    console.error('Error processing message:', error);
                }
            });
        });
    </script>
</body>
</html>)";

            // Concatenate all HTML parts
            std::wstring fullHTML = std::wstring(settingsHTML_part1) +
                std::wstring(settingsHTML_part2) +
                std::wstring(settingsHTML_part3) +
                std::wstring(settingsHTML_part4) +
                std::wstring(settingsHTML_part5) +
                std::wstring(settingsHTML_part6) +
                std::wstring(settingsHTML_part7) +
                std::wstring(settingsHTML_part8) +
                std::wstring(settingsHTML_part9) +
                std::wstring(settingsHTML_part10) +
                std::wstring(settingsHTML_part11) +
                std::wstring(settingsHTML_part12) +
                std::wstring(settingsHTML_part13) +
                std::wstring(settingsHTML_part14) +
                std::wstring(settingsHTML_part15) +
                std::wstring(settingsHTML_part16) +
                std::wstring(settingsHTML_part17) +
                std::wstring(settingsHTML_part18) +
                std::wstring(settingsHTML_part19) +
                std::wstring(settingsHTML_part20);

            m_webView->NavigateToString(fullHTML.c_str());
            return;
        }

        // For other modules, use the original method
        std::string htmlFileName = moduleId + "_module.html";
        std::string htmlPath = Utils::CombinePath(Utils::GetModulePath(), "HTML\\" + htmlFileName);

        if (Utils::FileExists(htmlPath)) {
            LOG_INFO("Loading module HTML from file: {}", htmlPath);
            std::string htmlContent = Utils::ReadTextFile(htmlPath);
            if (!htmlContent.empty()) {
                m_webView->NavigateToString(Utils::StringToWideString(htmlContent).c_str());
                return;
            }
            LOG_ERROR("Failed to read HTML content from file: {}", htmlPath);
        }
        else LOG_WARNING("Module HTML file not found: {}", htmlPath);

        std::string htmlContent = module->GetModuleUIHTML();
        if (htmlContent.empty()) {
            LOG_WARNING("Module returned empty HTML, creating default content");
            std::stringstream ss;
            ss << "<html><head><title>" << module->GetModuleName() << "</title>";
            ss << "<style>body{background-color:rgba(30,30,30,0.8);color:#fff;font-family:Arial;padding:20px}</style></head><body>";
            ss << "<h1>" << module->GetModuleName() << "</h1><p>" << module->GetModuleDescription() << "</p></body></html>";
            htmlContent = ss.str();
        }
        m_webView->NavigateToString(Utils::StringToWideString(htmlContent).c_str());
    }

    std::wstring OverlayWindow::CreateModuleLoaderHTML() {
        return L"";
    }

    LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_SIZE:
            if (m_webViewController) {
                RECT rc{}; GetClientRect(hwnd, &rc);
                m_webViewController->put_Bounds(rc);
            }
            return 0;

        case WM_DESTROY:
            if (m_webView && m_webMessageReceivedToken.value)
                m_webView->remove_WebMessageReceived(m_webMessageReceivedToken);
            return 0;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

} // namespace Nexile