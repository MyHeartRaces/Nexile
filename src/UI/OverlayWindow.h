#pragma once

#include <Windows.h>
#include <WebView2.h>
#include <wil/com.h>

#include <string>
#include <functional>
#include <vector>
#include <mutex>
#include <memory>

namespace Nexile {

    class NexileApp;
    class IModule;

    using WebMessageCallback = std::function<void(const std::wstring&)>;

    class OverlayWindow {
    public:
        explicit OverlayWindow(NexileApp* app);
        ~OverlayWindow();

        // ------------------ public API ------------------
        void Show();
        void Hide();
        void SetPosition(const RECT& rect);

        void Navigate(const std::wstring& uri);
        void ExecuteScript(const std::wstring& script);
        void SetClickThrough(bool clickThrough);

        void RegisterWebMessageCallback(WebMessageCallback cb);
        void LoadModuleUI(const std::shared_ptr<IModule>& module);
        std::wstring CreateModuleLoaderHTML();

        // Made public to be accessible from SettingsModule
        void LoadMainOverlayUI();

        // Returns true if the WebView is initialized and ready
        bool IsWebViewReady() const { return m_webView != nullptr; }

    private:
        // ------------- core window plumbing -------------
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

        void RegisterWindowClass();
        void InitializeWindow();
        void InitializeWebView();

        // ------------- navigation helpers ---------------
        void NavigateWelcomePage();

        // ------------- WebView wiring -------------------
        void SetupWebViewEventHandlers();
        void HandleWebMessage(const std::wstring& message);

        // ------------- members --------------------------
        NexileApp* m_app;
        HWND       m_hwnd;
        bool       m_visible;
        bool       m_clickThrough;

        wil::com_ptr<ICoreWebView2Environment> m_webViewEnvironment;
        wil::com_ptr<ICoreWebView2Controller>  m_webViewController;
        wil::com_ptr<ICoreWebView2>            m_webView;

        EventRegistrationToken m_webMessageReceivedToken{};

        std::mutex                       m_callbackMutex;
        std::vector<WebMessageCallback>  m_webMessageCallbacks;
    };

} // namespace Nexile