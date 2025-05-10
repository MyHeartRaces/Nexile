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

        // Create standard window instead of layered window for better WebView compatibility
        m_hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,  // Removed WS_EX_LAYERED and WS_EX_TRANSPARENT
            L"NexileOverlayClass", L"Nexile Overlay", WS_OVERLAPPEDWINDOW,  // Use standard window style
            100, 100, 800, 600,  // Position away from corner for better visibility during testing
            nullptr, nullptr, m_app->GetInstanceHandle(), nullptr);

        if (!m_hwnd) throw std::runtime_error("Failed to create overlay window");

        SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        // Set window background to dark color
        SetClassLongPtr(m_hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)CreateSolidBrush(RGB(30, 30, 30)));
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

                                // Use FULLY OPAQUE background rather than transparent for reliability
                                Microsoft::WRL::ComPtr<ICoreWebView2Controller2> ctl2;
                                if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
                                    // Make WebView background fully opaque with a dark color
                                    // Using fully opaque background is much more reliable for rendering
                                    COREWEBVIEW2_COLOR darkBackground{ 0xFF, 0x1E, 0x1E, 0x1E };
                                    ctl2->put_DefaultBackgroundColor(darkBackground);
                                }

                                // Important: Set specific WebView settings
                                ICoreWebView2Settings* settings = nullptr;
                                m_webView->get_Settings(&settings);
                                if (settings) {
                                    // Enable JavaScript
                                    settings->put_IsScriptEnabled(TRUE);
                                    // Disable default context menus
                                    settings->put_AreDefaultContextMenusEnabled(FALSE);
                                    // Enable status bar
                                    settings->put_IsStatusBarEnabled(FALSE);

                                    // For WebView2 SDK version 1.0.1072 or higher
                                    Microsoft::WRL::ComPtr<ICoreWebView2Settings3> settings3;
                                    if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&settings3)))) {
                                        // Enable dedicated GPU acceleration if available
                                        settings3->put_AreBrowserAcceleratorKeysEnabled(TRUE);
                                    }
                                }

                                // Set WebView bounds to match window size
                                RECT webViewRect{};
                                GetClientRect(m_hwnd, &webViewRect);
                                LOG_INFO("Setting WebView bounds: {},{} - {},{}",
                                    webViewRect.left, webViewRect.top, webViewRect.right, webViewRect.bottom);
                                m_webViewController->put_Bounds(webViewRect);

                                // Set bounds to match window
                                RECT rc{};
                                GetClientRect(m_hwnd, &rc);
                                m_webViewController->put_Bounds(rc);

                                // Set up event handlers and navigate to welcome page
                                SetupWebViewEventHandlers();
                                NavigateWelcomePage();
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
    // Show the black welcome page
    // =============================================================
    void OverlayWindow::NavigateWelcomePage() {
        if (!m_webView) return;

        // Create an extremely simple HTML page that will definitely display
        const wchar_t* welcomeHTML = LR"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Nexile Overlay</title>
    <style>
        body {
            margin: 0;
            padding: 0;
            background-color: #1e1e1e;
            color: white;
            font-family: Arial, sans-serif;
        }
        .content {
            width: 100%;
            padding: 20px;
            box-sizing: border-box;
        }
        h1 {
            color: #4a90e2;
            text-align: center;
            margin-top: 20px;
        }
        .box {
            background-color: #333;
            border: 1px solid #555;
            border-radius: 5px;
            padding: 20px;
            margin: 20px auto;
            max-width: 600px;
        }
        ul {
            list-style: none;
            padding-left: 0;
        }
        li {
            padding: 10px;
            margin-bottom: 5px;
            border-bottom: 1px solid #444;
        }
        .button {
            background-color: #4a90e2;
            color: white;
            border: none;
            padding: 10px 20px;
            border-radius: 4px;
            cursor: pointer;
            display: inline-block;
            margin: 10px;
        }
        .button:hover {
            background-color: #3a80d2;
        }
        .buttons {
            text-align: center;
        }
    </style>
</head>
<body>
    <div class="content">
        <h1>NEXILE</h1>
        
        <div class="box">
            <h2>Available Hotkeys</h2>
            <ul>
                <li>Toggle Overlay: <strong>Ctrl+F1</strong></li>
                <li>Price Check: <strong>Alt+D</strong></li>
                <li>Settings: <strong>Ctrl+F2</strong></li>
            </ul>
        </div>
        
        <div class="buttons">
            <button class="button" id="settings-button">Open Settings</button>
            <button class="button" id="close-button">Close Overlay</button>
        </div>
    </div>

    <script>
        // Verbose logging to diagnose WebView issues
        console.log('Welcome page script executing');
        
        try {
            // Mark elements with visual changes to test rendering
            document.body.style.border = "5px solid red";
            console.log('Added red border to body');
            
            // Setup event handlers
            document.getElementById('settings-button').onclick = function() {
                console.log('Settings button clicked');
                try {
                    window.chrome.webview.postMessage(JSON.stringify({
                        action: 'open_settings'
                    }));
                } catch(e) {
                    console.error('Error sending settings message:', e);
                    alert('Error: ' + e.message);
                }
            };
            
            document.getElementById('close-button').onclick = function() {
                console.log('Close button clicked');
                try {
                    window.chrome.webview.postMessage(JSON.stringify({
                        action: 'toggle_overlay'
                    }));
                } catch(e) {
                    console.error('Error sending close message:', e);
                    alert('Error: ' + e.message);
                }
            };
            
            // Listen for events from native host
            window.chrome.webview.addEventListener('message', function(event) {
                console.log('Received message from native host:', event.data);
            });
            
            // Send test message to verify WebView communication
            window.chrome.webview.postMessage(JSON.stringify({
                action: 'test',
                message: 'WebView communication test'
            }));
            console.log('Test message sent to native code');
            
        } catch(e) {
            console.error('Error in welcome page script:', e);
            // Show error directly on page
            var errorDiv = document.createElement('div');
            errorDiv.style.backgroundColor = 'red';
            errorDiv.style.color = 'white';
            errorDiv.style.padding = '20px';
            errorDiv.style.margin = '20px';
            errorDiv.innerText = 'Error: ' + e.message;
            document.body.appendChild(errorDiv);
        }
    </script>
</body>
</html>
)";

        // Directly set HTML content and verify it was sent
        LOG_INFO("Setting welcome page HTML content");
        HRESULT hr = m_webView->NavigateToString(welcomeHTML);
        if (SUCCEEDED(hr)) {
            LOG_INFO("Welcome page HTML set successfully");
        }
        else {
            LOG_ERROR("Failed to set welcome page HTML, HRESULT: {}", HResultToHex(hr));
        }

        // Execute a simple script to verify WebView is working
        std::wstring testScript = L"document.body.style.backgroundColor = '#1e1e1e'; console.log('Test script executed successfully'); true;";
        m_webView->ExecuteScript(testScript.c_str(), nullptr);

        LOG_INFO("Loading built-in welcome page");
    }

    // =============================================================
    // Load the main overlay UI
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
                std::wstring fallback = L"<html><head><title>Nexile Overlay</title><style>body{background:rgba(30,30,30,0.8);color:#fff;font-family:Arial;padding:20px}</style></head><body><h1>Nexile Overlay</h1><p>Press Alt+D to check item prices in Path of Exile</p></body></html>";
                m_webView->NavigateToString(fallback.c_str());
                return;
            }
        }
        std::wstring url = L"file:///" + Utils::StringToWideString(htmlPath);
        std::replace(url.begin(), url.end(), L'\\', L'/');
        m_webView->Navigate(url.c_str());
    }

    // =============================================================
    // Setup WebView event handlers
    // =============================================================
    void OverlayWindow::SetupWebViewEventHandlers() {
        if (!m_webView) {
            LOG_ERROR("Cannot setup WebView event handlers: WebView2 not initialized");
            return;
        }
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

        // Add this script to all pages to enable messaging
        m_webView->AddScriptToExecuteOnDocumentCreated(
            L"window.chrome.webview.addEventListener('message', e=>window.postMessage(e.data,'*'));", nullptr);

#ifdef _DEBUG
        // Open developer tools in debug mode
        m_webView->OpenDevToolsWindow();
#endif
        LOG_INFO("WebView2 event handlers setup complete");
    }

    // =============================================================
    // Load module UI - updated to support multiple filename patterns
    // =============================================================
    void OverlayWindow::LoadModuleUI(const std::shared_ptr<IModule>& module)
    {
        if (!m_webView || !module)
        {
            LOG_ERROR("Cannot load module UI: WebView or module not initialized");
            return;
        }

        std::string moduleId = module->GetModuleID();
        LOG_INFO("Loading UI for module: {}", moduleId);

        // Try multiple possible filenames
        std::string htmlPath;
        std::string htmlContent;

        // First try moduleId_module.html
        std::string htmlFileName = moduleId + "_module.html";
        htmlPath = Utils::CombinePath(Utils::GetModulePath(), "HTML\\" + htmlFileName);

        if (Utils::FileExists(htmlPath))
        {
            LOG_INFO("Loading module HTML from file: {}", htmlPath);
            htmlContent = Utils::ReadTextFile(htmlPath);
        }
        else
        {
            // Then try just moduleId.html
            htmlFileName = moduleId + ".html";
            htmlPath = Utils::CombinePath(Utils::GetModulePath(), "HTML\\" + htmlFileName);

            if (Utils::FileExists(htmlPath))
            {
                LOG_INFO("Loading module HTML from file: {}", htmlPath);
                htmlContent = Utils::ReadTextFile(htmlPath);
            }
            else
            {
                LOG_WARNING("Module HTML files not found: tried {}_module.html and {}.html", moduleId, moduleId);
            }
        }

        // If we couldn't load from file, get HTML from the module
        if (htmlContent.empty())
        {
            LOG_INFO("Using HTML content from module");
            htmlContent = module->GetModuleUIHTML();
        }

        if (htmlContent.empty())
        {
            LOG_WARNING("Module returned empty HTML, creating default content");
            std::stringstream ss;
            ss << "<html><head><title>" << module->GetModuleName() << "</title>";
            ss << "<style>body{background-color:rgba(30,30,30,0.8);color:#fff;font-family:Arial;padding:20px}</style></head><body>";
            ss << "<h1>" << module->GetModuleName() << "</h1><p>" << module->GetModuleDescription() << "</p></body></html>";
            htmlContent = ss.str();
        }

        LOG_INFO("Setting HTML content for module: {} (content length: {})", moduleId, htmlContent.length());
        m_webView->NavigateToString(Utils::StringToWideString(htmlContent).c_str());
    }

    // =============================================================
    //  Message dispatch from WebView to native callbacks
    // =============================================================
    void OverlayWindow::HandleWebMessage(const std::wstring& message)
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        for (auto& cb : m_webMessageCallbacks) cb(message);
    }

    void OverlayWindow::RegisterWebMessageCallback(WebMessageCallback cb)
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_webMessageCallbacks.emplace_back(std::move(cb));
    }

    // =============================================================
    //  Show / hide overlay window
    // =============================================================
    void OverlayWindow::Show()
    {
        if (!m_hwnd)
        {
            LOG_ERROR("Cannot show overlay: window not initialized");
            return;
        }

        m_visible = true;

        // Important: Use SW_SHOWNORMAL for proper visibility
        ShowWindow(m_hwnd, SW_SHOWNORMAL);
        LOG_INFO("Overlay window shown");

        // Make sure we're the topmost window
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        if (m_webViewController)
        {
            RECT bounds{};
            GetClientRect(m_hwnd, &bounds);
            LOG_INFO("Window client rect: {}x{}", bounds.right, bounds.bottom);
            m_webViewController->put_Bounds(bounds);

            // Force a complete repaint - critical for visibility
            InvalidateRect(m_hwnd, NULL, TRUE);
            UpdateWindow(m_hwnd);

            // Attempt to refresh WebView content
            if (m_webView) {
                m_webView->Reload();
            }
        }
    }

    void OverlayWindow::Hide()
    {
        if (!m_hwnd)
        {
            LOG_ERROR("Cannot hide overlay: window not initialized");
            return;
        }
        m_visible = false;
        ShowWindow(m_hwnd, SW_HIDE);
        LOG_INFO("Overlay window hidden");
    }

    // =============================================================
    //  Move/resize (called by main app)
    // =============================================================
    void OverlayWindow::SetPosition(const RECT& rect)
    {
        if (!m_hwnd || !m_webViewController)
        {
            LOG_ERROR("Cannot set position: window or WebView controller not initialized");
            return;
        }

        // Use SWP_SHOWWINDOW to ensure window is visible
        SetWindowPos(
            m_hwnd, HWND_TOPMOST,
            rect.left, rect.top,
            rect.right - rect.left,
            rect.bottom - rect.top,
            SWP_SHOWWINDOW);

        // Update WebView bounds to match window size
        m_webViewController->put_Bounds({ 0, 0, rect.right - rect.left, rect.bottom - rect.top });

        // Force repaint to ensure content is displayed
        InvalidateRect(m_hwnd, NULL, TRUE);
        UpdateWindow(m_hwnd);
    }

    // =============================================================
    //  Navigation / scripting helpers
    // =============================================================
    void OverlayWindow::Navigate(const std::wstring& uri)
    {
        if (m_webView)
        {
            LOG_INFO("Navigating WebView to: {}", Utils::WideStringToString(uri));
            m_webView->Navigate(uri.c_str());
        }
        else LOG_ERROR("Cannot navigate: WebView not initialized");
    }

    void OverlayWindow::ExecuteScript(const std::wstring& script)
    {
        if (!m_webView)
        {
            LOG_ERROR("Cannot execute script: WebView not initialized");
            return;
        }

        m_webView->ExecuteScript(
            script.c_str(),
            Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [](HRESULT hr, LPCWSTR) -> HRESULT
                {
                    if (FAILED(hr))
                        LOG_ERROR("Script execution failed, HRESULT: 0x%08X", static_cast<unsigned int>(hr));
                    return S_OK;
                }).Get());
    }

    // =============================================================
    //  Mouse transparency toggle
    // =============================================================
    void OverlayWindow::SetClickThrough(bool clickThrough)
    {
        if (!m_hwnd)
        {
            LOG_ERROR("Cannot set click-through: window not initialized");
            return;
        }

        m_clickThrough = clickThrough;
        LONG exStyle = GetWindowLong(m_hwnd, GWL_EXSTYLE);

        if (clickThrough) {
            // Add WS_EX_TRANSPARENT flag to make window click-through
            exStyle |= WS_EX_TRANSPARENT;
        }
        else {
            // Remove WS_EX_TRANSPARENT flag to make window capture clicks
            exStyle &= ~WS_EX_TRANSPARENT;
        }

        SetWindowLong(m_hwnd, GWL_EXSTYLE, exStyle);
        LOG_INFO("Overlay click-through set to: {}", clickThrough);
    }

    std::wstring OverlayWindow::CreateModuleLoaderHTML()
    {
        return L""; // placeholder 
    }

    // =============================================================
    //  Window message handler (resize & destroy)
    // =============================================================
    LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_SIZE:
            if (m_webViewController)
            {
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