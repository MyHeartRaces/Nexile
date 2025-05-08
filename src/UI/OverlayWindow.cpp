#include "OverlayWindow.h"
#include "../Core/NexileApp.h"
#include "../Modules/ModuleInterface.h"
#include "Resources.h"

#include <shlwapi.h>
#include <shlobj_core.h>
#include <WebView2EnvironmentOptions.h>
#include <stdexcept>
#include <filesystem>

#pragma comment(lib, "shlwapi.lib")

namespace Nexile {

    // Static map to store window instances for window procedure
    static std::unordered_map<HWND, OverlayWindow*> s_windowMap;

    OverlayWindow::OverlayWindow(NexileApp* app)
        : m_app(app),
        m_hwnd(NULL),
        m_visible(false),
        m_clickThrough(true) {

        // Initialize the overlay window
        InitializeWindow();

        // Initialize WebView
        InitializeWebView();
    }

    OverlayWindow::~OverlayWindow() {
        // Remove event handlers
        if (m_webView) {
            m_webView->remove_WebMessageReceived(m_webMessageReceivedToken);
        }

        // Remove from window map
        if (m_hwnd && s_windowMap.count(m_hwnd)) {
            s_windowMap.erase(m_hwnd);
        }

        // Destroy window
        if (m_hwnd) {
            DestroyWindow(m_hwnd);
            m_hwnd = NULL;
        }
    }

    void OverlayWindow::RegisterWindowClass() {
        WNDCLASSEX wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WindowProc;
        wcex.cbClsExtra = 0;
        wcex.cbWndExtra = 0;
        wcex.hInstance = m_app->GetInstanceHandle();
        wcex.hIcon = NULL;
        wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszMenuName = nullptr;
        wcex.lpszClassName = L"NexileOverlayClass";
        wcex.hIconSm = NULL;

        if (!RegisterClassEx(&wcex)) {
            throw std::runtime_error("Failed to register overlay window class");
        }
    }

    void OverlayWindow::InitializeWindow() {
        // Register window class
        RegisterWindowClass();

        // Get primary monitor dimensions
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);

        // Create overlay window (initially hidden)
        DWORD exStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
        m_hwnd = CreateWindowEx(
            exStyle,
            L"NexileOverlayClass",
            L"Nexile Overlay",
            WS_POPUP,
            0, 0, screenWidth, screenHeight,
            NULL, NULL,
            m_app->GetInstanceHandle(),
            NULL
        );

        if (!m_hwnd) {
            throw std::runtime_error("Failed to create overlay window");
        }

        // Store this instance in the map for the window procedure
        s_windowMap[m_hwnd] = this;

        // Set window transparency (50% opacity)
        SetLayeredWindowAttributes(m_hwnd, 0, 200, LWA_ALPHA);
    }

    void OverlayWindow::InitializeWebView() {
        // Create WebView options
        auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        options->put_AdditionalBrowserArguments(L"--disable-web-security --allow-file-access-from-files");

        // Get or create user data folder
        wchar_t appDataPath[MAX_PATH];
        SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appDataPath);
        std::wstring userDataPath = std::wstring(appDataPath) + L"\\Nexile\\WebView2Data";

        // Ensure the directory exists
        std::filesystem::create_directories(userDataPath);

        // Create WebView environment
        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr,  // Use default Edge runtime
            userDataPath.c_str(),
            options.Get(),
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                    // Store environment
                    m_webViewEnvironment = environment;

                    // Create WebView controller
                    environment->CreateCoreWebView2Controller(
                        m_hwnd,
                        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                                // Store controller
                                m_webViewController = controller;

                                // Get WebView
                                m_webViewController->get_CoreWebView2(&m_webView);

                                // Set up event handlers
                                SetupWebViewEventHandlers();

                                // Set WebView bounds
                                RECT bounds;
                                GetClientRect(m_hwnd, &bounds);
                                m_webViewController->put_Bounds(bounds);

                                // Set default background color (transparent)
                                COREWEBVIEW2_COLOR color = { 0, 0, 0, 0 };
                                m_webViewController->put_DefaultBackgroundColor(color);

                                // Navigate to initial HTML content
                                std::wstring initialHTML = CreateModuleLoaderHTML();
                                m_webView->NavigateToString(initialHTML.c_str());

                                return S_OK;
                            }
                        ).Get()
                    );

                    return S_OK;
                }
            ).Get()
        );

        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create WebView2 environment");
        }
    }

    void OverlayWindow::SetupWebViewEventHandlers() {
        // Set up web message handler
        m_webView->add_WebMessageReceived(
            Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    // Get message
                    wil::unique_cotaskmem_string message;
                    args->get_WebMessageAsJson(&message);

                    // Handle message
                    HandleWebMessage(message.get());

                    return S_OK;
                }
            ).Get(),
            &m_webMessageReceivedToken
        );

        // Add any other event handlers here
    }

    void OverlayWindow::HandleWebMessage(const std::wstring& message) {
        // Call registered callbacks
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        for (const auto& callback : m_webMessageCallbacks) {
            if (callback) {
                callback(message);
            }
        }
    }

    std::wstring OverlayWindow::CreateModuleLoaderHTML() {
        // Create a basic HTML template for the overlay
        return L"<!DOCTYPE html>"
            L"<html>"
            L"<head>"
            L"    <meta charset=\"UTF-8\">"
            L"    <title>Nexile Overlay</title>"
            L"    <style>"
            L"        body, html {"
            L"            margin: 0;"
            L"            padding: 0;"
            L"            background-color: transparent;"
            L"            overflow: hidden;"
            L"            width: 100%;"
            L"            height: 100%;"
            L"            font-family: Arial, sans-serif;"
            L"        }"
            L"        #overlay-container {"
            L"            position: absolute;"
            L"            top: 0;"
            L"            left: 0;"
            L"            width: 100%;"
            L"            height: 100%;"
            L"            pointer-events: none;"
            L"            display: flex;"
            L"            flex-direction: column;"
            L"        }"
            L"        .module-container {"
            L"            background-color: rgba(0, 0, 0, 0.7);"
            L"            color: white;"
            L"            border-radius: 5px;"
            L"            padding: 10px;"
            L"            margin: 10px;"
            L"            max-width: 400px;"
            L"            box-shadow: 0 0 10px rgba(0, 0, 0, 0.5);"
            L"            display: none;"
            L"            pointer-events: auto;"
            L"        }"
            L"        .module-header {"
            L"            display: flex;"
            L"            justify-content: space-between;"
            L"            align-items: center;"
            L"            margin-bottom: 10px;"
            L"            pointer-events: auto;"
            L"        }"
            L"        .module-title {"
            L"            font-weight: bold;"
            L"        }"
            L"        .module-close {"
            L"            cursor: pointer;"
            L"        }"
            L"        .module-content {"
            L"            pointer-events: auto;"
            L"        }"
            L"    </style>"
            L"    <script>"
            L"        window.addEventListener('DOMContentLoaded', function() {"
            L"            // Setup communication with native app"
            L"            window.addEventListener('message', function(event) {"
            L"                try {"
            L"                    const message = JSON.parse(event.data);"
            L"                    handleNativeMessage(message);"
            L"                } catch (e) {"
            L"                    console.error('Error parsing message:', e);"
            L"                }"
            L"            });"
            L"            "
            L"            // Send ready message to native code"
            L"            sendToNative({ action: 'overlay_ready' });"
            L"        });"
            L"        "
            L"        function handleNativeMessage(message) {"
            L"            switch (message.action) {"
            L"                case 'show_module':"
            L"                    showModule(message.moduleId, message.title, message.content);"
            L"                    break;"
            L"                case 'hide_module':"
            L"                    hideModule(message.moduleId);"
            L"                    break;"
            L"                case 'update_module':"
            L"                    updateModule(message.moduleId, message.content);"
            L"                    break;"
            L"                case 'execute_script':"
            L"                    executeScript(message.script);"
            L"                    break;"
            L"            }"
            L"        }"
            L"        "
            L"        function showModule(moduleId, title, content) {"
            L"            let moduleContainer = document.getElementById('module-' + moduleId);"
            L"            "
            L"            if (!moduleContainer) {"
            L"                moduleContainer = document.createElement('div');"
            L"                moduleContainer.id = 'module-' + moduleId;"
            L"                moduleContainer.className = 'module-container';"
            L"                moduleContainer.innerHTML = `"
            L"                    <div class=\"module-header\">"
            L"                        <div class=\"module-title\">${title}</div>"
            L"                        <div class=\"module-close\" onclick=\"hideModule('${moduleId}')\">×</div>"
            L"                    </div>"
            L"                    <div id=\"module-content-${moduleId}\" class=\"module-content\">${content}</div>"
            L"                `;"
            L"                document.getElementById('overlay-container').appendChild(moduleContainer);"
            L"            }"
            L"            "
            L"            moduleContainer.style.display = 'block';"
            L"        }"
            L"        "
            L"        function hideModule(moduleId) {"
            L"            const moduleContainer = document.getElementById('module-' + moduleId);"
            L"            if (moduleContainer) {"
            L"                moduleContainer.style.display = 'none';"
            L"            }"
            L"            "
            L"            // Notify native code"
            L"            sendToNative({"
            L"                action: 'module_closed',"
            L"                moduleId: moduleId"
            L"            });"
            L"        }"
            L"        "
            L"        function updateModule(moduleId, content) {"
            L"            const contentElement = document.getElementById('module-content-' + moduleId);"
            L"            if (contentElement) {"
            L"                contentElement.innerHTML = content;"
            L"            }"
            L"        }"
            L"        "
            L"        function executeScript(script) {"
            L"            try {"
            L"                eval(script);"
            L"            } catch (e) {"
            L"                console.error('Error executing script:', e);"
            L"            }"
            L"        }"
            L"        "
            L"        function sendToNative(message) {"
            L"            window.chrome.webview.postMessage(JSON.stringify(message));"
            L"        }"
            L"    </script>"
            L"</head>"
            L"<body>"
            L"    <div id=\"overlay-container\"></div>"
            L"</body>"
            L"</html>";
    }

    LRESULT CALLBACK OverlayWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        // Find the window instance
        auto it = s_windowMap.find(hwnd);
        if (it != s_windowMap.end() && it->second) {
            return it->second->HandleMessage(hwnd, uMsg, wParam, lParam);
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_SIZE:
            // Resize WebView
            if (m_webViewController) {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                m_webViewController->put_Bounds(bounds);
            }
            break;

        case WM_NCHITTEST:
            // Make the window click-through if needed
            if (m_clickThrough) {
                return HTTRANSPARENT;
            }
            break;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    void OverlayWindow::Show() {
        if (!m_visible) {
            m_visible = true;

            // Show the window (no activation)
            ShowWindow(m_hwnd, SW_SHOWNA);

            // Set window to be topmost
            SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }

    void OverlayWindow::Hide() {
        if (m_visible) {
            m_visible = false;

            // Hide the window
            ShowWindow(m_hwnd, SW_HIDE);
        }
    }

    void OverlayWindow::SetPosition(const RECT& rect) {
        // Set window position and size
        SetWindowPos(
            m_hwnd,
            HWND_TOPMOST,
            rect.left, rect.top,
            rect.right - rect.left, rect.bottom - rect.top,
            SWP_NOACTIVATE | (m_visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW)
        );

        // Update WebView bounds
        if (m_webViewController) {
            RECT bounds;
            GetClientRect(m_hwnd, &bounds);
            m_webViewController->put_Bounds(bounds);
        }
    }

    void OverlayWindow::Navigate(const std::wstring& uri) {
        if (m_webView) {
            m_webView->Navigate(uri.c_str());
        }
    }

    void OverlayWindow::ExecuteScript(const std::wstring& script) {
        if (m_webView) {
            m_webView->ExecuteScript(
                script.c_str(),
                Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                    [](HRESULT result, LPCWSTR resultObject) -> HRESULT {
                        return S_OK;
                    }
                ).Get()
            );
        }
    }

    void OverlayWindow::RegisterWebMessageCallback(WebMessageCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_webMessageCallbacks.push_back(callback);
    }

    void OverlayWindow::SetClickThrough(bool clickThrough) {
        if (m_clickThrough != clickThrough) {
            m_clickThrough = clickThrough;

            // Update window styles
            LONG exStyle = GetWindowLong(m_hwnd, GWL_EXSTYLE);

            if (clickThrough) {
                exStyle |= WS_EX_TRANSPARENT;
            }
            else {
                exStyle &= ~WS_EX_TRANSPARENT;
            }

            SetWindowLong(m_hwnd, GWL_EXSTYLE, exStyle);
        }
    }

    void OverlayWindow::LoadModuleUI(const std::shared_ptr<IModule>& module) {
        if (!module) {
            return;
        }

        // Get module information
        std::string moduleId = module->GetModuleID();
        std::string moduleTitle = module->GetModuleName();
        std::string moduleContent = module->GetModuleUIHTML();

        // Convert to wide strings
        std::wstring wModuleId(moduleId.begin(), moduleId.end());
        std::wstring wModuleTitle(moduleTitle.begin(), moduleTitle.end());
        std::wstring wModuleContent(moduleContent.begin(), moduleContent.end());

        // Create JSON message to send to WebView
        std::wstring script = L"handleNativeMessage({" +
            L"action: 'show_module', " +
            L"moduleId: '" + wModuleId + L"', " +
            L"title: '" + wModuleTitle + L"', " +
            L"content: `" + wModuleContent + L"`" +
            L"});";

        // Execute the script
        ExecuteScript(script);
    }

} // namespace Nexile