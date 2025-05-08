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

    // Only the InitializeWebView method with HRESULT conversion fix
    void OverlayWindow::InitializeWebView() {
        // Get WebView2 environment
        std::wstring userDataFolder;

        // Create user data folder in AppData
        wchar_t appDataPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
            userDataFolder = std::wstring(appDataPath) + L"\\Nexile\\WebView2Data";
            CreateDirectoryW(userDataFolder.c_str(), NULL);
        }
        else {
            userDataFolder = L"WebView2Data";
        }

        // Create WebView2 environment - without storing the HRESULT
        // This prevents the "cannot convert from 'void' to 'HRESULT'" error
        CreateCoreWebView2EnvironmentWithOptions(
            NULL,
            userDataFolder.c_str(),
            NULL,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                    if (SUCCEEDED(result) && environment) {
                        // Store environment
                        m_webViewEnvironment = environment;

                        // Create WebView controller
                        environment->CreateCoreWebView2Controller(
                            m_hwnd,
                            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                                    if (SUCCEEDED(result) && controller) {
                                        // Store controller
                                        m_webViewController = controller;

                                        // Get WebView
                                        m_webViewController->get_CoreWebView2(&m_webView);

                                        if (m_webView) {
                                            // Setup event handlers
                                            SetupWebViewEventHandlers();

                                            // Navigate to initial page
                                            Navigate(L"about:blank");
                                        }
                                    }
                                    return S_OK;
                                }
                            ).Get()
                        );
                    }
                    return S_OK;
                }
            ).Get()
        );
    }

    void OverlayWindow::SetupWebViewEventHandlers() {
        if (!m_webView) {
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
    }

    void OverlayWindow::HandleWebMessage(const std::wstring& message) {
        // Process message from WebView
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
            return;
        }

        m_visible = true;
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    }

    void OverlayWindow::Hide() {
        if (!m_hwnd) {
            return;
        }

        m_visible = false;
        ShowWindow(m_hwnd, SW_HIDE);
    }

    void OverlayWindow::SetPosition(const RECT& rect) {
        if (!m_hwnd || !m_webViewController) {
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

    void OverlayWindow::SetClickThrough(bool clickThrough) {
        if (!m_hwnd) {
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
    }

    void OverlayWindow::LoadModuleUI(const std::shared_ptr<IModule>& module) {
        if (!m_webView || !module) {
            return;
        }

        // Get HTML from module
        std::string htmlContent = module->GetModuleUIHTML();
        if (htmlContent.empty()) {
            // Create default HTML
            std::stringstream ss;
            ss << "<html><head><title>" << module->GetModuleName() << "</title>";
            ss << "<style>body { background-color: rgba(30, 30, 30, 0.8); color: white; font-family: Arial; padding: 20px; }</style>";
            ss << "</head><body>";
            ss << "<h1>" << module->GetModuleName() << "</h1>";
            ss << "<p>" << module->GetModuleDescription() << "</p>";
            ss << "</body></html>";
            htmlContent = ss.str();
        }

        // Convert to wide string
        std::wstring wHtml = Utils::StringToWideString(htmlContent);

        // Navigate to data URL
        std::wstring encodedHtml;
        for (wchar_t c : wHtml) {
            if (c <= 127) {
                if (c == L'"') encodedHtml += L"%22";
                else if (c == L' ') encodedHtml += L"%20";
                else if (c == L'<') encodedHtml += L"%3C";
                else if (c == L'>') encodedHtml += L"%3E";
                else if (c == L'#') encodedHtml += L"%23";
                else if (c == L'%') encodedHtml += L"%25";
                else if (c == L'{') encodedHtml += L"%7B";
                else if (c == L'}') encodedHtml += L"%7D";
                else if (c == L'|') encodedHtml += L"%7C";
                else if (c == L'\\') encodedHtml += L"%5C";
                else if (c == L'^') encodedHtml += L"%5E";
                else if (c == L'~') encodedHtml += L"%7E";
                else if (c == L'[') encodedHtml += L"%5B";
                else if (c == L']') encodedHtml += L"%5D";
                else if (c == L'`') encodedHtml += L"%60";
                else if (c == L';') encodedHtml += L"%3B";
                else if (c == L'/') encodedHtml += L"%2F";
                else if (c == L'?') encodedHtml += L"%3F";
                else if (c == L':') encodedHtml += L"%3A";
                else if (c == L'@') encodedHtml += L"%40";
                else if (c == L'=') encodedHtml += L"%3D";
                else if (c == L'&') encodedHtml += L"%26";
                else if (c == L'+') encodedHtml += L"%2B";
                else encodedHtml += c;
            }
            else {
                wchar_t buffer[8];
                swprintf_s(buffer, L"%%%04X", static_cast<int>(c));
                encodedHtml += buffer;
            }
        }

        // Navigate to data URL
        std::wstring dataUrl = L"data:text/html;charset=utf-8," + encodedHtml;
        Navigate(dataUrl);
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
            return 0;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

} // namespace Nexile