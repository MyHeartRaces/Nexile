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

#pragma comment(lib, "Shlwapi.lib")

namespace Nexile {

    // =============================================================
    // Helper – ensure a full directory tree exists
    // =============================================================
    namespace {
        void EnsureDirectoryExists(const std::wstring& path) {
            if (PathFileExistsW(path.c_str())) return;
            if (SHCreateDirectoryExW(nullptr, path.c_str(), nullptr) != ERROR_SUCCESS) {
                LOG_WARNING("Failed to create folder: {}", Utils::WideStringToString(path));
            }
        }

        // Tiny wrapper because fmt expects braces not printf codes
        template<typename T>
        std::string HResultToHex(T hr) {
            char buf[11]{}; // 0xFFFFFFFF\0
            sprintf_s(buf, "0x%08X", static_cast<unsigned int>(hr));
            return buf;
        }
    }

    // =============================================================
    // Static window procedure → instance handler
    // =============================================================
    LRESULT CALLBACK OverlayWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        return self ? self->HandleMessage(hwnd, msg, wp, lp) : DefWindowProc(hwnd, msg, wp, lp);
    }

    // =============================================================
    // Construction / Destruction
    // =============================================================
    OverlayWindow::OverlayWindow(NexileApp* app)
        : m_app(app), m_hwnd(nullptr), m_visible(false), m_clickThrough(true) {
        // Initialize COM for this thread
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to initialize COM, HRESULT: {}", HResultToHex(hr));
        }

        InitializeWindow();
        InitializeWebView();
    }

    OverlayWindow::~OverlayWindow() {
        if (m_hwnd) DestroyWindow(m_hwnd);

        // Uninitialize COM
        CoUninitialize();
    }

    // =============================================================
    // Window class + creation
    // =============================================================
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

        // Use a black background with proper transparency (alpha: 200)
        SetLayeredWindowAttributes(m_hwnd, 0, 200, LWA_ALPHA);
    }

    // =============================================================
    // WebView2 initialisation (with bundled runtime)
    // =============================================================
    void OverlayWindow::InitializeWebView() {
        // --- user-data folder (%APPDATA%\Nexile\WebView2Data)
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

        // --- use bundled WebView2 runtime ---------------------------------------------------
        std::wstring runtimeDir = Utils::StringToWideString(
            Utils::CombinePath(Utils::GetModulePath(), "..\\..\\..\\..\\third_party\\webview2"));

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

        // --- lambda that creates the environment with bundled runtime -----------------------
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

                    // -------- Create controller ------------------------------------
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

                                // black background before first navigate
                                Microsoft::WRL::ComPtr<ICoreWebView2Controller2> ctl2;
                                if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
                                    COREWEBVIEW2_COLOR black{ 0xFF,0x00,0x00,0x00 };
                                    ctl2->put_DefaultBackgroundColor(black);
                                }

                                RECT rc{}; GetClientRect(m_hwnd, &rc);
                                m_webViewController->put_Bounds(rc);

                                // Set up event handlers before navigation
                                SetupWebViewEventHandlers();

                                // Enable the WebView2 visual - this is critical to make it display content
                                m_webViewController->put_IsVisible(TRUE);

                                // Show welcome page on startup
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

    // =============================================================
    // Load the welcome page from HTML file or fallback to embedded
    // =============================================================
    void OverlayWindow::LoadWelcomePage() {
        if (!m_webView) {
            LOG_ERROR("Cannot load welcome page: WebView2 not initialized");
            return;
        }

        // Try to load welcome.html file
        std::string welcomePath = Utils::CombinePath(Utils::GetModulePath(), "HTML\\welcome.html");
        LOG_INFO("Attempting to load welcome page from: {}", welcomePath);

        if (Utils::FileExists(welcomePath)) {
            std::string htmlContent = Utils::ReadTextFile(welcomePath);
            if (!htmlContent.empty()) {
                m_webView->NavigateToString(Utils::StringToWideString(htmlContent).c_str());
                LOG_INFO("Welcome page loaded from file");
                return;
            }
        }

        LOG_INFO("Welcome file not found, using built-in welcome page");

        // Fallback to embedded welcome page
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
            <a href="#" class="button" id="settings-button">Settings</a>
            <a href="#" class="button" id="browser-button">Open Browser</a>
            <a href="#" class="button" id="close-button">Close Overlay</a>
        </div>

        <div class="footer">Nexile v0.1.0 | Press Alt+Shift+O to toggle overlay</div>
    </div>

    <script>
        document.addEventListener('DOMContentLoaded', function() {
            // Setup button event handlers
            document.getElementById('settings-button').addEventListener('click', function() {
                // Send message to C++ host to open settings
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'open_settings'
                }));
            });

            document.getElementById('browser-button').addEventListener('click', function() {
                // Send message to C++ host to open browser
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'open_browser'
                }));
            });

            document.getElementById('close-button').addEventListener('click', function() {
                // Send message to C++ host to close/hide overlay
                window.chrome.webview.postMessage(JSON.stringify({
                    action: 'toggle_overlay'
                }));
            });

            // Listen for messages from the C++ host
            window.chrome.webview.addEventListener('message', function(event) {
                const message = JSON.parse(event.data);
                // Handle messages from C++ host if needed
            });
        });
    </script>
</body>
</html>
)";

        // Directly set HTML content
        m_webView->NavigateToString(welcomeHTML);
    }

    // =============================================================
    // Load browser page
    // =============================================================
    void OverlayWindow::LoadBrowserPage() {
        if (!m_webView) {
            LOG_ERROR("Cannot load browser page: WebView2 not initialized");
            return;
        }

        // Create a browser UI with search box
        const wchar_t* browserHTML = LR"(
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
            background-color: rgba(20, 20, 20, 0.9);
            color: white;
            font-family: 'Segoe UI', Arial, sans-serif;
            overflow: hidden;
        }

        .browser-container {
            display: flex;
            flex-direction: column;
            height: 100%;
            padding: 10px;
            box-sizing: border-box;
        }

        .address-bar {
            display: flex;
            padding: 10px;
            background-color: rgba(40, 40, 40, 0.8);
            border-radius: 5px;
            margin-bottom: 10px;
        }

        .address-input {
            flex-grow: 1;
            padding: 8px 12px;
            border: none;
            border-radius: 3px;
            background-color: rgba(60, 60, 60, 0.8);
            color: white;
            margin-right: 10px;
            font-size: 14px;
        }

        .address-input:focus {
            outline: none;
            background-color: rgba(70, 70, 70, 0.8);
        }

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
        }

        .content-frame {
            flex-grow: 1;
            border: none;
            background-color: white;
            border-radius: 5px;
        }

        .status-bar {
            padding: 8px;
            background-color: rgba(40, 40, 40, 0.8);
            border-radius: 3px;
            margin-top: 10px;
            font-size: 12px;
            color: #aaa;
        }

        .bookmarks {
            display: flex;
            gap: 10px;
            padding: 10px;
            background-color: rgba(40, 40, 40, 0.8);
            border-radius: 5px;
            margin-bottom: 10px;
            overflow-x: auto;
            white-space: nowrap;
        }

        .bookmark {
            background-color: #333;
            color: #ddd;
            padding: 6px 10px;
            border-radius: 3px;
            cursor: pointer;
            font-size: 13px;
        }

        .bookmark:hover {
            background-color: #444;
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
        </div>

        <div class="bookmarks">
            <div class="bookmark" data-url="https://www.google.com">Google</div>
            <div class="bookmark" data-url="https://www.reddit.com/r/pathofexile">PoE Reddit</div>
            <div class="bookmark" data-url="https://www.poelab.com">PoE Lab</div>
            <div class="bookmark" data-url="https://www.poe.ninja">poe.ninja</div>
            <div class="bookmark" data-url="https://www.poedb.tw">PoeDB</div>
        </div>

        <iframe id="contentFrame" class="content-frame" src="about:blank"></iframe>

        <div class="status-bar" id="statusBar">Ready</div>
    </div>

    <script>
        document.addEventListener('DOMContentLoaded', function() {
            const urlInput = document.getElementById('urlInput');
            const goButton = document.getElementById('goButton');
            const backButton = document.getElementById('backButton');
            const homeButton = document.getElementById('homeButton');
            const closeButton = document.getElementById('closeButton');
            const contentFrame = document.getElementById('contentFrame');
            const statusBar = document.getElementById('statusBar');
            const bookmarks = document.querySelectorAll('.bookmark');

            // Function to navigate to a URL
            function navigateToUrl(url) {
                if (!url.startsWith('http://') && !url.startsWith('https://')) {
                    url = 'https://' + url;
                }
                
                try {
                    contentFrame.src = url;
                    urlInput.value = url;
                    statusBar.textContent = 'Loading: ' + url;
                } catch (error) {
                    statusBar.textContent = 'Error: ' + error.message;
                }
            }

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
            });

            // Handle back button
            backButton.addEventListener('click', function() {
                contentFrame.contentWindow.history.back();
            });

            // Handle home button
            homeButton.addEventListener('click', function() {
                navigateToUrl('https://www.google.com');
            });

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
            });

            // Handle iframe load events
            contentFrame.addEventListener('load', function() {
                statusBar.textContent = 'Loaded: ' + contentFrame.src;
                urlInput.value = contentFrame.src;
            });

            // Set initial page
            navigateToUrl('https://www.google.com');
        });
    </script>
</body>
</html>
)";

        // Directly set HTML content
        m_webView->NavigateToString(browserHTML);

        // Make overlay interactive for browser use
        SetClickThrough(false);
        LOG_INFO("Browser page loaded");
    }

    // =============================================================
    // Load main overlay UI
    // =============================================================
    void OverlayWindow::LoadMainOverlayUI() {
        if (!m_webView) {
            LOG_ERROR("Cannot load main overlay UI: WebView2 not initialized");
            return;
        }

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

    // =============================================================
    // Setup WebView2 event handlers
    // =============================================================
    void OverlayWindow::SetupWebViewEventHandlers() {
        if (!m_webView) {
            LOG_ERROR("Cannot setup WebView event handlers: WebView2 not initialized");
            return;
        }

        // Add web message received handler
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

        // Add navigation completed handler
        m_webView->add_NavigationCompleted(
            Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this](ICoreWebView2* /*sender*/, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                    BOOL success;
                    args->get_IsSuccess(&success);
                    if (success) {
                        LOG_INFO("Navigation completed successfully");
                    }
                    else {
                        COREWEBVIEW2_WEB_ERROR_STATUS status;
                        args->get_WebErrorStatus(&status);
                        LOG_ERROR("Navigation failed with error: {}", status);
                    }
                    return S_OK;
                }).Get(),
                    &m_navigationCompletedToken);

        m_webView->AddScriptToExecuteOnDocumentCreated(
            L"window.chrome.webview.addEventListener('message', e=>window.postMessage(e.data,'*'));", nullptr);
#ifdef _DEBUG
        m_webView->OpenDevToolsWindow();
#endif
        LOG_INFO("WebView2 event handlers setup complete");
    }

    // =============================================================
    // Message dispatch from WebView to native callbacks
    // =============================================================
    void OverlayWindow::HandleWebMessage(const std::wstring& message) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);

        // First, check if we need to handle browser-related messages
        std::string msgStr = Utils::WideStringToString(message);
        if (msgStr.find("open_browser") != std::string::npos) {
            LoadBrowserPage();
            return;
        }
        else if (msgStr.find("close_browser") != std::string::npos) {
            LoadWelcomePage();
            return;
        }

        // Forward message to registered callbacks
        for (auto& cb : m_webMessageCallbacks) cb(message);
    }

    // =============================================================
    // Register callback for web messages
    // =============================================================
    void OverlayWindow::RegisterWebMessageCallback(WebMessageCallback cb) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_webMessageCallbacks.emplace_back(std::move(cb));
    }

    // =============================================================
    // Show/hide overlay window
    // =============================================================
    void OverlayWindow::Show() {
        if (!m_hwnd) {
            LOG_ERROR("Cannot show overlay: window not initialized");
            return;
        }

        m_visible = true;
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

    // =============================================================
    // Set window position and size
    // =============================================================
    void OverlayWindow::SetPosition(const RECT& rect) {
        if (!m_hwnd || !m_webViewController) {
            LOG_ERROR("Cannot set position: window or WebView controller not initialized");
            return;
        }

        SetWindowPos(
            m_hwnd, HWND_TOPMOST,
            rect.left, rect.top,
            rect.right - rect.left,
            rect.bottom - rect.top,
            SWP_SHOWWINDOW | SWP_NOACTIVATE);

        m_webViewController->put_Bounds({ 0,0, rect.right - rect.left, rect.bottom - rect.top });
    }

    // =============================================================
    // Navigation helpers
    // =============================================================
    void OverlayWindow::Navigate(const std::wstring& uri) {
        if (m_webView) {
            LOG_INFO("Navigating WebView to: {}", Utils::WideStringToString(uri));
            m_webView->Navigate(uri.c_str());
        }
        else LOG_ERROR("Cannot navigate: WebView not initialized");
    }

    // =============================================================
    // Execute JavaScript in WebView
    // =============================================================
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

    // =============================================================
    // Set click-through mode
    // =============================================================
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

    // =============================================================
    // Load a module's UI
    // =============================================================
    void OverlayWindow::LoadModuleUI(const std::shared_ptr<IModule>& module) {
        if (!m_webView || !module) {
            LOG_ERROR("Cannot load module UI: WebView or module not initialized");
            return;
        }

        std::string moduleId = module->GetModuleID();
        LOG_INFO("Loading UI for module: {}", moduleId);

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

    // =============================================================
    // Create module loader HTML
    // =============================================================
    std::wstring OverlayWindow::CreateModuleLoaderHTML() {
        return L""; // Not implemented - would be used for module UI wrapper
    }

    // =============================================================
    // Window message handler
    // =============================================================
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