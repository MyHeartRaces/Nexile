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
#include <algorithm>
#include <nlohmann/json.hpp>

#include <include/cef_browser.h>
#include <include/cef_command_line.h>
#include <include/wrapper/cef_helpers.h>
#include <include/base/cef_bind.h>
#include <include/wrapper/cef_closure_task.h>

#pragma comment(lib, "Shlwapi.lib")

namespace Nexile {

    namespace {
        void EnsureDirectoryExists(const std::wstring& path) {
            if (PathFileExistsW(path.c_str())) return;
            if (SHCreateDirectoryExW(nullptr, path.c_str(), nullptr) != ERROR_SUCCESS) {
                LOG_WARNING("Failed to create folder: {}", Utils::WideStringToString(path));
            }
        }
    }

    // CEF Client Implementation
    NexileClient::NexileClient(OverlayWindow* overlay) : m_overlay(overlay) {
    }

    void NexileClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
        CEF_REQUIRE_UI_THREAD();
        m_browser = browser;
        if (m_overlay) {
            m_overlay->OnBrowserCreated(browser);
        }
        LOG_INFO("CEF browser created successfully");
    }

    bool NexileClient::DoClose(CefRefPtr<CefBrowser> browser) {
        CEF_REQUIRE_UI_THREAD();
        return false;
    }

    void NexileClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
        CEF_REQUIRE_UI_THREAD();
        m_browser = nullptr;
        if (m_overlay) {
            m_overlay->OnBrowserClosing();
        }
        LOG_INFO("CEF browser closed");
    }

    void NexileClient::OnLoadStart(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  TransitionType transition_type) {
        CEF_REQUIRE_UI_THREAD();
        if (frame->IsMain()) {
            LOG_DEBUG("CEF load started");
        }
    }

    void NexileClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                int httpStatusCode) {
        CEF_REQUIRE_UI_THREAD();
        if (frame->IsMain()) {
            LOG_INFO("CEF load completed with status: {}", httpStatusCode);

            // Inject JavaScript bridge
            std::string bridgeScript = R"(
                window.nexile = window.nexile || {};
                window.nexile.postMessage = function(data) {
                    if (window.nexileBridge) {
                        window.nexileBridge.postMessage(JSON.stringify(data));
                    }
                };

                // Compatibility layer for existing code
                window.chrome = window.chrome || {};
                window.chrome.webview = {
                    postMessage: function(data) {
                        window.nexile.postMessage(data);
                    }
                };

                console.log('Nexile JavaScript bridge initialized');
            )";

            frame->ExecuteJavaScript(bridgeScript, "", 0);
        }
    }

    void NexileClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  ErrorCode errorCode,
                                  const CefString& errorText,
                                  const CefString& failedUrl) {
        CEF_REQUIRE_UI_THREAD();
        if (frame->IsMain()) {
            LOG_ERROR("CEF load error: {} ({})", errorText.ToString(), static_cast<int>(errorCode));
        }
    }

    void NexileClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                    const CefString& title) {
        CEF_REQUIRE_UI_THREAD();
        LOG_DEBUG("CEF title changed: {}", title.ToString());
    }

    CefRefPtr<CefResourceHandler> NexileClient::GetResourceHandler(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request) {

        std::string url = request->GetURL();

        // Handle nexile:// protocol
        if (url.find("nexile://") == 0) {
            return new NexileResourceHandler();
        }

        return nullptr;
    }

    // V8 Handler Implementation
    NexileV8Handler::NexileV8Handler(OverlayWindow* overlay) : m_overlay(overlay) {
    }

    bool NexileV8Handler::Execute(const CefString& name,
                                 CefRefPtr<CefV8Value> object,
                                 const CefV8ValueList& arguments,
                                 CefRefPtr<CefV8Value>& retval,
                                 CefString& exception) {

        if (name == "postMessage") {
            if (arguments.size() == 1 && arguments[0]->IsString()) {
                std::string message = arguments[0]->GetStringValue();

                // Send message to browser process
                CefRefPtr<CefBrowser> browser = CefV8Context::GetCurrentContext()->GetBrowser();
                CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("nexile_message");
                CefRefPtr<CefListValue> args = msg->GetArgumentList();
                args->SetString(0, message);
                browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);

                retval = CefV8Value::CreateBool(true);
                return true;
            }
        }

        return false;
    }

    // CEF App Implementation
    NexileApp_CEF::NexileApp_CEF() {
    }

    void NexileApp_CEF::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame,
                                        CefRefPtr<CefV8Context> context) {

        // Create JavaScript bridge object
        CefRefPtr<CefV8Value> window = context->GetGlobal();
        CefRefPtr<CefV8Value> nexileBridge = CefV8Value::CreateObject(nullptr, nullptr);

        // Create V8 handler for the bridge
        CefRefPtr<NexileV8Handler> handler = new NexileV8Handler(nullptr);

        // Add postMessage function
        CefRefPtr<CefV8Value> postMessageFunc = CefV8Value::CreateFunction("postMessage", handler);
        nexileBridge->SetValue("postMessage", postMessageFunc, V8_PROPERTY_ATTRIBUTE_NONE);

        // Attach to window
        window->SetValue("nexileBridge", nexileBridge, V8_PROPERTY_ATTRIBUTE_NONE);

        LOG_DEBUG("CEF V8 context created and bridge attached");
    }

    bool NexileApp_CEF::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                                CefRefPtr<CefFrame> frame,
                                                CefProcessId source_process,
                                                CefRefPtr<CefProcessMessage> message) {

        const std::string& name = message->GetName();
        if (name == "nexile_execute_script") {
            CefRefPtr<CefListValue> args = message->GetArgumentList();
            if (args->GetSize() > 0) {
                std::string script = args->GetString(0);
                frame->ExecuteJavaScript(script, "", 0);
                return true;
            }
        }

        return false;
    }

    // Resource Handler Implementation
    NexileResourceHandler::NexileResourceHandler() : m_offset(0) {
    }

    bool NexileResourceHandler::ProcessRequest(CefRefPtr<CefRequest> request,
                                              CefRefPtr<CefCallback> callback) {

        std::string url = request->GetURL();
        LOG_DEBUG("Processing resource request: {}", url);

        // Extract filename from nexile:// URL
        std::string filename;
        if (url.find("nexile://") == 0) {
            filename = url.substr(9); // Remove "nexile://"
        }

        // Load HTML content
        std::string htmlPath = Utils::CombinePath(Utils::GetModulePath(), "HTML\\" + filename);
        if (Utils::FileExists(htmlPath)) {
            m_data = Utils::ReadTextFile(htmlPath);
            m_mime_type = "text/html";
        } else {
            // Return 404
            m_data = "<html><body><h1>404 Not Found</h1></body></html>";
            m_mime_type = "text/html";
        }

        m_offset = 0;
        callback->Continue();
        return true;
    }

    void NexileResourceHandler::GetResponseHeaders(CefRefPtr<CefResponse> response,
                                                  int64& response_length,
                                                  CefString& redirectUrl) {

        response->SetMimeType(m_mime_type);
        response->SetStatus(200);
        response_length = m_data.length();
    }

    bool NexileResourceHandler::ReadResponse(void* data_out,
                                            int bytes_to_read,
                                            int& bytes_read,
                                            CefRefPtr<CefCallback> callback) {

        bool has_data = false;
        bytes_read = 0;

        if (m_offset < m_data.length()) {
            int transfer_size = std::min(bytes_to_read, static_cast<int>(m_data.length() - m_offset));
            memcpy(data_out, m_data.c_str() + m_offset, transfer_size);
            m_offset += transfer_size;
            bytes_read = transfer_size;
            has_data = true;
        }

        return has_data;
    }

    void NexileResourceHandler::Cancel() {
        // Nothing to cancel
    }

    // OverlayWindow Implementation
    LRESULT CALLBACK OverlayWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        return self ? self->HandleMessage(hwnd, msg, wp, lp) : DefWindowProc(hwnd, msg, wp, lp);
    }

    OverlayWindow::OverlayWindow(NexileApp* app)
        : m_app(app), m_hwnd(nullptr), m_visible(false), m_clickThrough(true), m_cefInitialized(false) {

        m_windowRect = {0, 0, 1280, 960};

        InitializeWindow();
        InitializeCEF();
    }

    OverlayWindow::~OverlayWindow() {
        if (m_browser) {
            m_browser->GetHost()->CloseBrowser(true);
            m_browser = nullptr;
        }

        if (m_hwnd) {
            DestroyWindow(m_hwnd);
        }

        ShutdownCEF();
    }

    void OverlayWindow::RegisterWindowClass() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
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
            0, 0, 1280, 960,
            nullptr, nullptr, m_app->GetInstanceHandle(), nullptr);

        if (!m_hwnd) {
            throw std::runtime_error("Failed to create overlay window");
        }

        SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        SetLayeredWindowAttributes(m_hwnd, 0, 200, LWA_ALPHA);

        LOG_INFO("Overlay window created successfully");
    }

    void OverlayWindow::InitializeCEF() {
        // CEF settings
        CefSettings settings;
        settings.no_sandbox = true;
        settings.single_process = true;
        settings.multi_threaded_message_loop = false;

        // Set paths
        std::string cef_cache = Utils::CombinePath(Utils::GetAppDataPath(), "cef_cache");
        Utils::CreateDirectory(cef_cache);
        CefString(&settings.cache_path) = cef_cache;

        std::string cef_log = Utils::CombinePath(Utils::GetAppDataPath(), "cef.log");
        CefString(&settings.log_file) = cef_log;
        settings.log_severity = LOGSEVERITY_INFO;

        // Resource path
        std::string resource_dir = Utils::CombinePath(Utils::GetModulePath(), "cef_resources");
        CefString(&settings.resources_dir_path) = resource_dir;

        std::string locales_dir = Utils::CombinePath(Utils::GetModulePath(), "locales");
        CefString(&settings.locales_dir_path) = locales_dir;

        // Initialize CEF
        CefRefPtr<NexileApp_CEF> app = new NexileApp_CEF();

        if (!CefInitialize(CefMainArgs(GetModuleHandle(nullptr)), settings, app, nullptr)) {
            throw std::runtime_error("Failed to initialize CEF");
        }

        m_cefInitialized = true;
        LOG_INFO("CEF initialized successfully");

        // Create browser client
        m_client = new NexileClient(this);

        // Browser settings
        CefBrowserSettings browserSettings;
        browserSettings.web_security = STATE_DISABLED;
        browserSettings.javascript_close_windows = STATE_DISABLED;

        // Window info
        CefWindowInfo windowInfo;
        windowInfo.SetAsChild(m_hwnd, CefRect(0, 0, 1280, 960));

        // Create browser
        if (!CefBrowserHost::CreateBrowser(windowInfo, m_client, "about:blank", browserSettings, nullptr, nullptr)) {
            throw std::runtime_error("Failed to create CEF browser");
        }

        LOG_INFO("CEF browser creation initiated");
    }

    void OverlayWindow::ShutdownCEF() {
        if (m_cefInitialized) {
            CefShutdown();
            m_cefInitialized = false;
            LOG_INFO("CEF shutdown completed");
        }
    }

    void OverlayWindow::Show() {
        if (!m_hwnd) {
            LOG_ERROR("Cannot show overlay: window not initialized");
            return;
        }

        m_visible = true;
        CenterWindow();
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        LOG_INFO("Overlay window shown");
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

    void OverlayWindow::CenterWindow() {
        if (!m_hwnd) return;

        HMONITOR hPrimaryMon = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {0};
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(hPrimaryMon, &mi);

        int monWidth = mi.rcMonitor.right - mi.rcMonitor.left;
        int monHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;

        GetWindowRect(m_hwnd, &m_windowRect);
        int width = m_windowRect.right - m_windowRect.left;
        int height = m_windowRect.bottom - m_windowRect.top;

        if (width > monWidth) width = monWidth - 40;
        if (height > monHeight) height = monHeight - 40;

        int x = mi.rcMonitor.left + (monWidth - width) / 2;
        int y = mi.rcMonitor.top + (monHeight - height) / 2;

        if (x + width > mi.rcMonitor.right) x = mi.rcMonitor.right - width;
        if (y + height > mi.rcMonitor.bottom) y = mi.rcMonitor.bottom - height;
        if (x < mi.rcMonitor.left) x = mi.rcMonitor.left;
        if (y < mi.rcMonitor.top) y = mi.rcMonitor.top;

        SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);

        // Update browser size
        if (m_browser) {
            m_browser->GetHost()->WasResized();
        }
    }

    void OverlayWindow::SetPosition(const RECT& rect) {
        if (!m_hwnd) {
            LOG_ERROR("Cannot set position: window not initialized");
            return;
        }

        HMONITOR hPrimaryMon = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {0};
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(hPrimaryMon, &mi);

        int monWidth = mi.rcMonitor.right - mi.rcMonitor.left;
        int monHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;

        int width = static_cast<int>(min(monWidth * 0.8, 1280.0));
        int height = static_cast<int>(min(monHeight * 0.8, 960.0));

        int x = mi.rcMonitor.left + (monWidth - width) / 2;
        int y = mi.rcMonitor.top + (monHeight - height) / 2;

        if (x + width > mi.rcMonitor.right) x = mi.rcMonitor.right - width;
        if (y + height > mi.rcMonitor.bottom) y = mi.rcMonitor.bottom - height;
        if (x < mi.rcMonitor.left) x = mi.rcMonitor.left;
        if (y < mi.rcMonitor.top) y = mi.rcMonitor.top;

        SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE);

        // Update browser size
        if (m_browser) {
            m_browser->GetHost()->WasResized();
        }
    }

    void OverlayWindow::Navigate(const std::wstring& uri) {
        if (m_browser && m_browser->GetMainFrame()) {
            std::string url = Utils::WideStringToString(uri);
            LOG_INFO("Navigating to: {}", url);
            m_browser->GetMainFrame()->LoadURL(url);
        } else {
            LOG_ERROR("Cannot navigate: browser not initialized");
        }
    }

    void OverlayWindow::ExecuteScript(const std::wstring& script) {
        if (!m_browser || !m_browser->GetMainFrame()) {
            LOG_ERROR("Cannot execute script: browser not initialized");
            return;
        }

        std::string scriptStr = Utils::WideStringToString(script);

        // Send to renderer process
        CefRefPtr<CefProcessMessage> msg = CefProcessMessage::Create("nexile_execute_script");
        CefRefPtr<CefListValue> args = msg->GetArgumentList();
        args->SetString(0, scriptStr);
        m_browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);
    }

    void OverlayWindow::SetClickThrough(bool clickThrough) {
        if (!m_hwnd) {
            LOG_ERROR("Cannot set click-through: window not initialized");
            return;
        }

        m_clickThrough = clickThrough;
        LONG exStyle = GetWindowLong(m_hwnd, GWL_EXSTYLE);
        exStyle = clickThrough ? (exStyle | WS_EX_TRANSPARENT) : (exStyle & ~WS_EX_TRANSPARENT);
        SetWindowLong(m_hwnd, GWL_EXSTYLE, exStyle);
        LOG_INFO("Overlay click-through set to: {}", clickThrough);
    }

    void OverlayWindow::LoadWelcomePage() {
        std::string welcomeHTML = LoadHTMLResource("welcome.html");
        if (welcomeHTML.empty()) {
            // Fallback HTML
            welcomeHTML = R"(
                <!DOCTYPE html>
                <html><head><title>Nexile</title>
                <style>body{background:rgba(30,30,30,0.85);color:#fff;font-family:Arial;padding:20px;text-align:center}</style>
                </head><body><h1>Welcome to Nexile</h1><p>Game overlay ready</p></body></html>
            )";
        }

        std::string dataURL = CreateDataURL(welcomeHTML);
        Navigate(Utils::StringToWideString(dataURL));

        // Set appropriate opacity and click-through
        SetLayeredWindowAttributes(m_hwnd, 0, 180, LWA_ALPHA);
        SetClickThrough(m_clickThrough);
        CenterWindow();
    }

    void OverlayWindow::LoadBrowserPage() {
        std::string browserHTML = LoadHTMLResource("browser.html");
        if (browserHTML.empty()) {
            browserHTML = R"(
                <!DOCTYPE html>
                <html><head><title>Nexile Browser</title>
                <style>body{background:rgba(30,30,30,0.85);color:#fff;font-family:Arial;padding:20px}</style>
                </head><body><h1>Browser Mode</h1><p>Browser functionality would go here</p></body></html>
            )";
        }

        std::string dataURL = CreateDataURL(browserHTML);
        Navigate(Utils::StringToWideString(dataURL));

        // Make fully opaque and non-click-through for browser
        SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
        SetClickThrough(false);
        CenterWindow();
    }

    void OverlayWindow::LoadMainOverlayUI() {
        std::string overlayHTML = LoadHTMLResource("main_overlay.html");
        if (overlayHTML.empty()) {
            overlayHTML = R"(
                <!DOCTYPE html>
                <html><head><title>Nexile Overlay</title>
                <style>body{background:rgba(30,30,30,0.85);color:#fff;font-family:Arial;padding:20px}</style>
                </head><body><h1>Nexile Overlay</h1><p>Main overlay UI</p></body></html>
            )";
        }

        std::string dataURL = CreateDataURL(overlayHTML);
        Navigate(Utils::StringToWideString(dataURL));

        // Set appropriate opacity and restore click-through setting
        SetLayeredWindowAttributes(m_hwnd, 0, 180, LWA_ALPHA);
        SetClickThrough(m_clickThrough);
    }

    void OverlayWindow::LoadModuleUI(const std::shared_ptr<IModule>& module) {
        if (!module) {
            LOG_ERROR("Cannot load module UI: module is null");
            return;
        }

        std::string moduleId = module->GetModuleID();
        LOG_INFO("Loading UI for module: {}", moduleId);

        std::string moduleHTML;

        // Try to load from file first
        std::string htmlFileName = moduleId + "_module.html";
        moduleHTML = LoadHTMLResource(htmlFileName);

        // Fall back to module's built-in HTML
        if (moduleHTML.empty()) {
            moduleHTML = module->GetModuleUIHTML();
        }

        // Final fallback
        if (moduleHTML.empty()) {
            std::stringstream ss;
            ss << "<!DOCTYPE html><html><head><title>" << module->GetModuleName() << "</title>";
            ss << "<style>body{background:rgba(30,30,30,0.85);color:#fff;font-family:Arial;padding:20px}</style></head><body>";
            ss << "<h1>" << module->GetModuleName() << "</h1>";
            ss << "<p>" << module->GetModuleDescription() << "</p></body></html>";
            moduleHTML = ss.str();
        }

        std::string dataURL = CreateDataURL(moduleHTML);
        Navigate(Utils::StringToWideString(dataURL));

        // Set appropriate properties for module UI
        SetLayeredWindowAttributes(m_hwnd, 0, 180, LWA_ALPHA);
        SetClickThrough(false); // Modules need interaction
    }

    void OverlayWindow::RegisterWebMessageCallback(WebMessageCallback cb) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_webMessageCallbacks.emplace_back(std::move(cb));
    }

    void OverlayWindow::HandleWebMessage(const std::wstring& message) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);

        std::string msgStr = Utils::WideStringToString(message);
        LOG_DEBUG("Received web message: {}", msgStr);

        try {
            nlohmann::json j = nlohmann::json::parse(msgStr);

            if (j.contains("action")) {
                std::string action = j["action"];

                if (action == "open_browser") {
                    LoadBrowserPage();
                    return;
                } else if (action == "close_browser") {
                    LoadWelcomePage();
                    return;
                } else if (action == "open_settings") {
                    if (m_app) {
                        auto settingsModule = m_app->GetModule("settings");
                        if (settingsModule) {
                            LoadModuleUI(settingsModule);
                            SetClickThrough(false);
                            m_app->OnHotkeyPressed(HotkeyManager::HOTKEY_GAME_SETTINGS);
                        }
                    }
                    return;
                } else if (action == "toggle_overlay") {
                    if (m_app) {
                        m_app->ToggleOverlay();
                    }
                    return;
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Error parsing web message: {}", e.what());
        }

        // Forward to registered callbacks
        for (auto& cb : m_webMessageCallbacks) {
            cb(message);
        }
    }

    void OverlayWindow::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
        m_browser = browser;

        // Process messages from renderer
        if (browser) {
            // Set up message handler in the browser process
            class MessageHandler {
            public:
                static void HandleMessage(OverlayWindow* overlay, const std::string& message) {
                    overlay->HandleWebMessage(Utils::StringToWideString(message));
                }
            };

            // Register process message handler
            // This would be handled in the client's OnProcessMessageReceived
        }

        // Load welcome page by default
        LoadWelcomePage();
    }

    void OverlayWindow::OnBrowserClosing() {
        m_browser = nullptr;
    }

    std::string OverlayWindow::LoadHTMLResource(const std::string& filename) {
        std::string htmlPath = Utils::CombinePath(Utils::GetModulePath(), "HTML\\" + filename);

        if (Utils::FileExists(htmlPath)) {
            std::string content = Utils::ReadTextFile(htmlPath);
            LOG_INFO("Loaded HTML resource: {}", filename);
            return content;
        }

        LOG_WARNING("HTML resource not found: {}", filename);
        return "";
    }

    std::string OverlayWindow::CreateDataURL(const std::string& html) {
        // Create a data URL for the HTML content
        std::string encoded = "data:text/html;charset=utf-8,";

        // Simple URL encoding for basic characters
        for (char c : html) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded += c;
            } else {
                char buffer[4];
                sprintf_s(buffer, "%%%02X", static_cast<unsigned char>(c));
                encoded += buffer;
            }
        }

        return encoded;
    }

    LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_SIZE:
            if (m_browser) {
                m_browser->GetHost()->WasResized();
            }
            return 0;

        case WM_DESTROY:
            if (m_browser) {
                m_browser->GetHost()->CloseBrowser(true);
            }
            return 0;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

} // namespace Nexile