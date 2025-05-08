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
#include <Shlwapi.h>          // SHCreateDirectoryExW, PathFileExistsW
#include <wrl.h>              // Microsoft::WRL::Callback
#include <wil/com.h>          // wil::com_ptr helpers
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
        InitializeWindow();
        InitializeWebView();
    }

    OverlayWindow::~OverlayWindow() {
        if (m_hwnd) DestroyWindow(m_hwnd);
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
        SetLayeredWindowAttributes(m_hwnd, 0, 230, LWA_ALPHA);
    }

    // =============================================================
    // WebView2 initialisation (with bundled fallback runtime)
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

        // --- decide which runtime to use ---------------------------------------------------
        wil::unique_cotaskmem_string sysVersion;
        HRESULT hrProbe = GetAvailableCoreWebView2BrowserVersionString(nullptr, &sysVersion);

        std::wstring runtimeDir = Utils::StringToWideString(
            Utils::CombinePath(Utils::GetModulePath(), "webview2_runtime"));

        bool bundleExists = PathFileExistsW((runtimeDir + L"\\msedgewebview2.exe").c_str());
        const wchar_t* browserFolder = nullptr;
        if (hrProbe == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && bundleExists) {
            browserFolder = runtimeDir.c_str();
            LOG_INFO("System WebView2 runtime not found – falling back to bundled runtime at {}", Utils::WideStringToString(runtimeDir));
        }

        // --- lambda that actually creates the environment ----------------------------------
        auto createEnvironment = [&](const wchar_t* browserExeFolder, const wchar_t* dataFolder) -> HRESULT {
            return CreateCoreWebView2EnvironmentWithOptions(
                browserExeFolder, dataFolder, nullptr,
                Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                    [this](HRESULT hrEnv, ICoreWebView2Environment* env) -> HRESULT {
                        if (FAILED(hrEnv) || !env) {
                            LOG_ERROR("Failed to create WebView2 environment, HRESULT: {}", HResultToHex(hrEnv));
                            MessageBoxW(nullptr,
                                L"Microsoft Edge WebView2 Runtime could not be initialised.\n"
                                L"Please install the Evergreen runtime and restart Nexile.",
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

                                    SetupWebViewEventHandlers();
                                    NavigateWelcomePage();
                                    return S_OK;
                                }).Get());
                        return S_OK;
                    }).Get());
            };

        // attempt with runtime decision
        HRESULT hr = createEnvironment(browserFolder, userDataFolder.c_str());
        if (FAILED(hr) && browserFolder != nullptr) {
            // one more try: maybe the bundled runtime also fails – try system default
            LOG_WARNING("Retrying WebView2 environment creation with system runtime (HRESULT {} )", HResultToHex(hr));
            createEnvironment(nullptr, userDataFolder.c_str());
        }
    }

    // =============================================================
    // Show the black welcome page
    // =============================================================
    void OverlayWindow::NavigateWelcomePage() {
        if (!m_webView) return;
        std::string htmlPath = Utils::CombinePath(Utils::GetModulePath(), "UI/HTML/welcome.html");
        if (!Utils::FileExists(htmlPath)) {
            LOG_WARNING("welcome.html not found, falling back to inline welcome page");
            m_webView->NavigateToString(L"<html><body style='background:#000;color:#fff;display:flex;align-items:center;justify-content:center;font-family:Segoe UI'><h1>Welcome to Nexile Overlay</h1></body></html>");
            return;
        }
        std::wstring url = L"file:///" + Utils::StringToWideString(htmlPath);
        std::replace(url.begin(), url.end(), L'\\', L'/');
        m_webView->Navigate(url.c_str());
    }

    // =============================================================
    // Remaining existing methods (unchanged from original) --------
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
                std::wstring fallback = L"<html><head><title>Nexile Overlay</title><style>body{background:#1e1e1ecc;color:#fff;font-family:Arial;padding:20px}</style></head><body><h1>Nexile Overlay</h1><p>Press Alt+D to check item prices in Path of Exile</p></body></html>";
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

        m_webView->AddScriptToExecuteOnDocumentCreated(
            L"window.chrome.webview.addEventListener('message', e=>window.postMessage(e.data,'*'));", nullptr);
#ifdef _DEBUG
        m_webView->OpenDevToolsWindow();
#endif
        LOG_INFO("WebView2 event handlers setup complete");
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
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        LOG_INFO("Overlay window shown");

        if (m_webViewController)
        {
            RECT rc{}; GetClientRect(m_hwnd, &rc);
            m_webViewController->put_Bounds(rc);
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

        SetWindowPos(
            m_hwnd, HWND_TOPMOST,
            rect.left, rect.top,
            rect.right - rect.left,
            rect.bottom - rect.top,
            SWP_SHOWWINDOW | SWP_NOACTIVATE);

        m_webViewController->put_Bounds({ 0,0, rect.right - rect.left, rect.bottom - rect.top });
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
        exStyle = clickThrough ? (exStyle | WS_EX_TRANSPARENT)
            : (exStyle & ~WS_EX_TRANSPARENT);
        SetWindowLong(m_hwnd, GWL_EXSTYLE, exStyle);
        LOG_INFO("Overlay click-through set to: {}", clickThrough);
    }

    // =============================================================
    //  Module UI loader (unchanged apart from logging)
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

        std::string htmlFileName = moduleId + "_module.html";
        std::string htmlPath = Utils::CombinePath(Utils::GetModulePath(), "HTML\\" + htmlFileName);

        if (Utils::FileExists(htmlPath))
        {
            LOG_INFO("Loading module HTML from file: {}", htmlPath);
            std::string htmlContent = Utils::ReadTextFile(htmlPath);
            if (!htmlContent.empty())
            {
                m_webView->NavigateToString(Utils::StringToWideString(htmlContent).c_str());
                return;
            }
            LOG_ERROR("Failed to read HTML content from file: {}", htmlPath);
        }
        else LOG_WARNING("Module HTML file not found: {}", htmlPath);

        std::string htmlContent = module->GetModuleUIHTML();
        if (htmlContent.empty())
        {
            LOG_WARNING("Module returned empty HTML, creating default content");
            std::stringstream ss;
            ss << "<html><head><title>" << module->GetModuleName() << "</title>";
            ss << "<style>body{background-color:rgba(30,30,30,0.8);color:#fff;font-family:Arial;padding:20px}</style></head><body>";
            ss << "<h1>" << module->GetModuleName() << "</h1><p>" << module->GetModuleDescription() << "</p></body></html>";
            htmlContent = ss.str();
        }
        m_webView->NavigateToString(Utils::StringToWideString(htmlContent).c_str());
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
