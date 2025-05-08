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

namespace Nexile {

    // Window procedure for overlay window
    LRESULT CALLBACK OverlayWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        // Get OverlayWindow instance from user data
        OverlayWindow* window = reinterpret_cast<OverlayWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

        // Forward message to instance if available
        if (window) {
            return window->HandleMessage(hwnd, uMsg, wParam, lParam);
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    OverlayWindow::OverlayWindow(NexileApp* app)
        : m_app(app),
        m_hwnd(NULL),
        m_visible(false),
        m_clickThrough(true) {

        // Initialize window
        InitializeWindow();

        // Initialize WebView
        InitializeWebView();
    }

    OverlayWindow::~OverlayWindow() {
        // WebView resources will be released automatically
        // Destroy window if needed
        if (m_hwnd) {
            DestroyWindow(m_hwnd);
            m_hwnd = NULL;
        }
    }

    void OverlayWindow::RegisterWindowClass() {
        WNDCLASSEXW wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEXW);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WindowProc;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = 0;
        wcex.hInstance = m_app->GetInstanceHandle();
        wcex.hIcon = NULL;
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszMenuName = NULL;
        wcex.lpszClassName = L"NexileOverlayClass";
        wcex.hIconSm = NULL;

        RegisterClassExW(&wcex);
    }

    void OverlayWindow::InitializeWindow() {
        // Register window class
        RegisterWindowClass();

        // Create layered window (transparent)
        m_hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"NexileOverlayClass",
            L"Nexile Overlay",
            WS_POPUP,
            0, 0, 1920, 1080,
            NULL,
            NULL,
            m_app->GetInstanceHandle(),
            NULL
        );

        if (!m_hwnd) {
            throw std::runtime_error("Failed to create overlay window");
        }

        // Store this pointer with the window
        SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        // Set transparency
        SetLayeredWindowAttributes(m_hwnd, 0, 230, LWA_ALPHA);
    }

    void OverlayWindow::InitializeWebView() {
        // Get WebView2 environment
        std::wstring userDataFolder;

        // Create user data folder in AppData
        wchar_t appDataPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
            userDataFolder = std::wstring(appDataPath) + L"\\Nexile\\WebView2Data";
            CreateDirectoryW(userDataFolder.c_str(), NULL);
            LOG_INFO("WebView2 user data folder: {}", Utils::WideStringToString(userDataFolder));
        }
        else {
            userDataFolder = L"WebView2Data";
            LOG_WARNING("Failed to get AppData path, using local folder for WebView2 data");
        }

        // Create WebView2 environment
        LOG_INFO("Creating WebView2 environment...");
        CreateCoreWebView2EnvironmentWithOptions(
            NULL,
            userDataFolder.c_str(),
            NULL,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                    if (SUCCEEDED(result) && environment) {
                        LOG_INFO("WebView2 environment created successfully");

                        // Store environment
                        m_webViewEnvironment = environment;

                        // Create WebView controller
                        environment->CreateCoreWebView2Controller(
                            m_hwnd,
                            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                                    if (SUCCEEDED(result) && controller) {
                                        LOG_INFO("WebView2 controller created successfully");

                                        // Store controller
                                        m_webViewController = controller;

                                        // Get WebView
                                        m_webViewController->get_CoreWebView2(&m_webView);

                                        if (m_webView) {
                                            // Setup event handlers
                                            SetupWebViewEventHandlers();

                                            // Set bounds
                                            RECT bounds;
                                            GetClientRect(m_hwnd, &bounds);
                                            m_webViewController->put_Bounds(bounds);

                                            // Navigate to initial HTML page
                                            LoadMainOverlayUI();
                                        }
                                        else {
                                            LOG_ERROR("Failed to get CoreWebView2 interface");
                                        }
                                    }
                                    else {
                                        LOG_ERROR("Failed to create WebView2 controller, HRESULT: 0x{:X}", static_cast<unsigned int>(result));
                                    }
                                    return S_OK;
                                }
                            ).Get()
                        );
                    }
                    else {
                        LOG_ERROR("Failed to create WebView2 environment, HRESULT: 0x{:X}", static_cast<unsigned int>(result));
                    }
                    return S_OK;
                }
            ).Get()
        );
    }

    void OverlayWindow::LoadMainOverlayUI() {
        if (!m_webView) {
            LOG_ERROR("Cannot load main overlay UI: WebView2 not initialized");
            return;
        }

        // Get path to HTML file
        std::string htmlPath = Utils::CombinePath(Utils::GetModulePath(), "HTML\\main_overlay.html");
        LOG_INFO("Loading main overlay HTML from: {}", htmlPath);

        if (!Utils::FileExists(htmlPath)) {
            LOG_ERROR("Main overlay HTML file not found: {}", htmlPath);

            // Try alternative path
            htmlPath = Utils::CombinePath(Utils::GetModulePath(), "..\\HTML\\main_overlay.html");
            LOG_INFO("Trying alternative path: {}", htmlPath);

            if (!Utils::FileExists(htmlPath)) {
                LOG_ERROR("Alternative path also failed, creating default HTML content");
                // Create simple HTML content
                std::wstring defaultHTML = L"<html><head><title>Nexile Overlay</title>"
                    L"<style>body { background-color: rgba(30, 30, 30, 0.8); color: white; "
                    L"font-family: Arial; padding: 20px; }</style></head>"
                    L"<body><h1>Nexile Overlay</h1>"
                    L"<p>Press Alt+D to check item prices in Path of Exile</p></body></html>";
                m_webView->NavigateToString(defaultHTML.c_str());
                return;
            }
        }

        // Convert to wide string for WebView2
        std::wstring wHtmlPath = Utils::StringToWideString(htmlPath);

        // Create file URL
        std::wstring fileUrl = L"file:///" + wHtmlPath;

        // Replace backslashes with forward slashes
        std::replace(fileUrl.begin(), fileUrl.end(), L'\\', L'/');

        LOG_INFO("Navigating to: {}", Utils::WideStringToString(fileUrl));
        m_webView->Navigate(fileUrl.c_str());
    }

    void OverlayWindow::SetupWebViewEventHandlers() {
        if (!m_webView) {
            LOG_ERROR("Cannot setup WebView event handlers: WebView2 not initialized");
            return;
        }

        // Register message handler
        m_webView->add_WebMessageReceived(
            Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    wil::unique_cotaskmem_string messageRaw;
                    args->get_WebMessageAsJson(&messageRaw);

                    if (messageRaw) {
                        std::wstring message = messageRaw.get();
                        LOG_DEBUG("Received web message: {}", Utils::WideStringToString(message));
                        HandleWebMessage(message);
                    }
                    return S_OK;
                }
            ).Get(),
            &m_webMessageReceivedToken
        );

        // Add script to allow communication from WebView to host
        m_webView->AddScriptToExecuteOnDocumentCreated(
            L"window.chrome.webview.addEventListener('message', event => { window.postMessage(event.data, '*'); });",
            nullptr
        );

        // Enable dev tools in debug mode
#ifdef _DEBUG
        m_webView->OpenDevToolsWindow();
        LOG_INFO("DevTools window opened for WebView2");
#endif

        LOG_INFO("WebView2 event handlers setup complete");
    }

    void OverlayWindow::HandleWebMessage(const std::wstring& message) {
        // Process message from WebView
        LOG_DEBUG("Processing web message: {}", Utils::WideStringToString(message));

        std::lock_guard<std::mutex> lock(m_callbackMutex);
        for (const auto& callback : m_webMessageCallbacks) {
            callback(message);
        }
    }

    void OverlayWindow::RegisterWebMessageCallback(WebMessageCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_webMessageCallbacks.push_back(callback);
    }

    void OverlayWindow::Show() {
        if (!m_hwnd) {
            LOG_ERROR("Cannot show overlay: window not initialized");
            return;
        }

        m_visible = true;
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        LOG_INFO("Overlay window shown");

        // Ensure WebView is properly sized
        if (m_webViewController) {
            RECT bounds;
            GetClientRect(m_hwnd, &bounds);
            m_webViewController->put_Bounds(bounds);
            LOG_DEBUG("WebView bounds updated: {}x{}", bounds.right, bounds.bottom);
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

        // Set window position
        SetWindowPos(
            m_hwnd,
            HWND_TOPMOST,
            rect.left,
            rect.top,
            rect.right - rect.left,
            rect.bottom - rect.top,
            SWP_SHOWWINDOW | SWP_NOACTIVATE
        );

        // Set WebView position
        m_webViewController->put_Bounds({ 0, 0, rect.right - rect.left, rect.bottom - rect.top });
        LOG_DEBUG("Overlay position set to: {},{} - {}x{}", rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
    }

    void OverlayWindow::Navigate(const std::wstring& uri) {
        if (m_webView) {
            LOG_INFO("Navigating WebView to: {}", Utils::WideStringToString(uri));
            m_webView->Navigate(uri.c_str());
        }
        else {
            LOG_ERROR("Cannot navigate: WebView not initialized");
        }
    }

    // Function for navigating to URL

    void OverlayWindow::ExecuteScript(const std::wstring& script) {
        if (!m_webView) {
            LOG_ERROR("Cannot execute script: WebView not initialized");
            return;
        }

        LOG_DEBUG("Executing script in WebView");
        m_webView->ExecuteScript(
            script.c_str(),
            Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [](HRESULT result, LPCWSTR resultObject) -> HRESULT {
                    if (FAILED(result)) {
                        LOG_ERROR("Script execution failed, HRESULT: 0x{:X}", static_cast<unsigned int>(result));
                    }
                    return S_OK;
                }
            ).Get()
        );
    }

    void OverlayWindow::SetClickThrough(bool clickThrough) {
        if (!m_hwnd) {
            LOG_ERROR("Cannot set click-through: window not initialized");
            return;
        }

        m_clickThrough = clickThrough;

        // Get current style
        LONG exStyle = GetWindowLong(m_hwnd, GWL_EXSTYLE);

        if (clickThrough) {
            // Add transparent flag (ignore mouse)
            exStyle |= WS_EX_TRANSPARENT;
        }
        else {
            // Remove transparent flag (capture mouse)
            exStyle &= ~WS_EX_TRANSPARENT;
        }

        // Apply new style
        SetWindowLong(m_hwnd, GWL_EXSTYLE, exStyle);
        LOG_INFO("Overlay click-through set to: {}", clickThrough);
    }

    void OverlayWindow::LoadModuleUI(const std::shared_ptr<IModule>& module) {
        if (!m_webView || !module) {
            LOG_ERROR("Cannot load module UI: WebView or module not initialized");
            return;
        }

        std::string moduleId = module->GetModuleID();
        LOG_INFO("Loading UI for module: {}", moduleId);

        // First, try to load from file
        std::string htmlFileName = moduleId + "_module.html";
        std::string htmlPath = Utils::CombinePath(Utils::GetModulePath(), "HTML\\" + htmlFileName);

        if (Utils::FileExists(htmlPath)) {
            LOG_INFO("Loading module HTML from file: {}", htmlPath);
            std::string htmlContent = Utils::ReadTextFile(htmlPath);

            if (!htmlContent.empty()) {
                // Convert to wide string for WebView2
                std::wstring wHtmlContent = Utils::StringToWideString(htmlContent);

                // Use NavigateToString instead of data URL
                if (m_webView) {
                    LOG_DEBUG("Using NavigateToString for module HTML content");
                    m_webView->NavigateToString(wHtmlContent.c_str());
                    return;
                }
            }
            else {
                LOG_ERROR("Failed to read HTML content from file: {}", htmlPath);
            }
        }
        else {
            LOG_WARNING("Module HTML file not found: {}", htmlPath);
        }

        // If file loading failed, use module's GetModuleUIHTML method
        LOG_INFO("Using module's GetModuleUIHTML method");
        std::string htmlContent = module->GetModuleUIHTML();

        if (htmlContent.empty()) {
            // Create default HTML
            LOG_WARNING("Module returned empty HTML, creating default content");
            std::stringstream ss;
            ss << "<html><head><title>" << module->GetModuleName() << "</title>";
            ss << "<style>body { background-color: rgba(30, 30, 30, 0.8); color: white; font-family: Arial; padding: 20px; }</style>";
            ss << "</head><body>";
            ss << "<h1>" << module->GetModuleName() << "</h1>";
            ss << "<p>" << module->GetModuleDescription() << "</p>";
            ss << "</body></html>";
            htmlContent = ss.str();
        }

        // Convert to wide string and load
        std::wstring wHtmlContent = Utils::StringToWideString(htmlContent);

        if (m_webView) {
            LOG_DEBUG("Using NavigateToString for module HTML content");
            m_webView->NavigateToString(wHtmlContent.c_str());
        }
        else {
            LOG_ERROR("WebView is nullptr, cannot load module UI");
        }
    }

    std::wstring OverlayWindow::CreateModuleLoaderHTML() {
        return L"";  // Placeholder - to be implemented
    }

    LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_SIZE:
            // Resize WebView if it exists
            if (m_webViewController) {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                m_webViewController->put_Bounds(bounds);
                LOG_DEBUG("Resized WebView to: {}x{}", bounds.right, bounds.bottom);
            }
            return 0;

        case WM_DESTROY:
            // Clean up WebView
            if (m_webView) {
                if (m_webMessageReceivedToken.value) {
                    m_webView->remove_WebMessageReceived(m_webMessageReceivedToken);
                    m_webMessageReceivedToken.value = 0;
                }
            }
            LOG_INFO("Overlay window destroyed");
            return 0;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

} // namespace Nexile