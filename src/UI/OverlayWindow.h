#pragma once

#include <Windows.h>
#include <wrl.h>
#include <wil/com.h>
#include <WebView2.h>
#include <vector>
#include <string>
#include <functional>
#include <mutex>

namespace Nexile {

    // Forward declarations
    class NexileApp;
    class IModule;

    // Callback for web message received
    using WebMessageCallback = std::function<void(const std::wstring& message)>;

    class OverlayWindow {
    public:
        OverlayWindow(NexileApp* app);
        ~OverlayWindow();

        // Show the overlay window
        void Show();

        // Hide the overlay window
        void Hide();

        // Set the position and size of the overlay
        void SetPosition(const RECT& rect);

        // Navigate WebView to URL or local file
        void Navigate(const std::wstring& uri);

        // Execute JavaScript in the WebView
        void ExecuteScript(const std::wstring& script);

        // Register callback for web messages
        void RegisterWebMessageCallback(WebMessageCallback callback);

        // Get the window handle
        HWND GetWindowHandle() const { return m_hwnd; }

        // Toggle click-through mode (transparent to mouse input or not)
        void SetClickThrough(bool clickThrough);

        // Load a module's UI into the overlay
        void LoadModuleUI(const std::shared_ptr<IModule>& module);

        // Window procedure for overlay window
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

        // Process window messages
        LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    private:
        // Initialize the overlay window
        void InitializeWindow();

        // Register window class
        void RegisterWindowClass();

        // Initialize WebView2 control
        void InitializeWebView();

        // Set up event handlers for WebView
        void SetupWebViewEventHandlers();

        // Handle web message received
        void HandleWebMessage(const std::wstring& message);

        // Create HTML for module loader interface
        std::wstring CreateModuleLoaderHTML();

    private:
        // Parent application
        NexileApp* m_app;

        // Overlay window handle
        HWND m_hwnd;

        // WebView environment
        wil::com_ptr<ICoreWebView2Environment> m_webViewEnvironment;

        // WebView controller
        wil::com_ptr<ICoreWebView2Controller> m_webViewController;

        // WebView
        wil::com_ptr<ICoreWebView2> m_webView;

        // Web message callbacks
        std::vector<WebMessageCallback> m_webMessageCallbacks;

        // Synchronization for callbacks
        std::mutex m_callbackMutex;

        // Current overlay state
        bool m_visible;
        bool m_clickThrough;

        // Event token for web message received
        EventRegistrationToken m_webMessageReceivedToken;
    };

} // namespace Nexile