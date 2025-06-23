#pragma once

#include <Windows.h>
#include <cef_app.h>
#include <cef_client.h>
#include <cef_browser.h>
#include <cef_frame.h>
#include <cef_render_handler.h>
#include <cef_load_handler.h>
#include <cef_display_handler.h>
#include <cef_life_span_handler.h>
#include <cef_request_handler.h>
#include <cef_resource_handler.h>
#include <cef_scheme.h>
#include <wrapper/cef_helpers.h>

#include <string>
#include <functional>
#include <vector>
#include <mutex>
#include <memory>

namespace Nexile {

    class NexileApp;
    class IModule;

    using WebMessageCallback = std::function<void(const std::wstring&)>;

    // CEF Client implementation
    class NexileClient : public CefClient,
                        public CefLifeSpanHandler,
                        public CefLoadHandler,
                        public CefDisplayHandler,
                        public CefRequestHandler {
    public:
        explicit NexileClient(class OverlayWindow* overlay);

        // CefClient methods
        virtual CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
            return this;
        }

        virtual CefRefPtr<CefLoadHandler> GetLoadHandler() override {
            return this;
        }

        virtual CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
            return this;
        }

        virtual CefRefPtr<CefRequestHandler> GetRequestHandler() override {
            return this;
        }

        // CefLifeSpanHandler methods
        virtual void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
        virtual bool DoClose(CefRefPtr<CefBrowser> browser) override;
        virtual void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

        // CefLoadHandler methods
        virtual void OnLoadStart(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               TransitionType transition_type) override;
        virtual void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefFrame> frame,
                             int httpStatusCode) override;
        virtual void OnLoadError(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               ErrorCode errorCode,
                               const CefString& errorText,
                               const CefString& failedUrl) override;

        // CefDisplayHandler methods
        virtual void OnTitleChange(CefRefPtr<CefBrowser> browser,
                                 const CefString& title) override;

        // CefRequestHandler methods
        virtual CefRefPtr<CefResourceHandler> GetResourceHandler(
            CefRefPtr<CefBrowser> browser,
            CefRefPtr<CefFrame> frame,
            CefRefPtr<CefRequest> request) override;

        // Custom methods
        void SetBrowser(CefRefPtr<CefBrowser> browser) { m_browser = browser; }
        CefRefPtr<CefBrowser> GetBrowser() const { return m_browser; }

    private:
        OverlayWindow* m_overlay;
        CefRefPtr<CefBrowser> m_browser;

        IMPLEMENT_REFCOUNTING(NexileClient);
    };

    // JavaScript Bridge for communication
    class NexileV8Handler : public CefV8Handler {
    public:
        explicit NexileV8Handler(OverlayWindow* overlay);

        virtual bool Execute(const CefString& name,
                           CefRefPtr<CefV8Value> object,
                           const CefV8ValueList& arguments,
                           CefRefPtr<CefV8Value>& retval,
                           CefString& exception) override;

    private:
        OverlayWindow* m_overlay;

        IMPLEMENT_REFCOUNTING(NexileV8Handler);
    };

    // CEF App implementation for renderer process
    class NexileApp_CEF : public CefApp, public CefRenderProcessHandler {
    public:
        NexileApp_CEF();

        // CefApp methods
        virtual CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
            return this;
        }

        // CefRenderProcessHandler methods
        virtual void OnContextCreated(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    CefRefPtr<CefV8Context> context) override;

        virtual bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                            CefRefPtr<CefFrame> frame,
                                            CefProcessId source_process,
                                            CefRefPtr<CefProcessMessage> message) override;

    private:
        IMPLEMENT_REFCOUNTING(NexileApp_CEF);
    };

    // Resource handler for custom protocols
    class NexileResourceHandler : public CefResourceHandler {
    public:
        NexileResourceHandler();

        virtual bool ProcessRequest(CefRefPtr<CefRequest> request,
                                  CefRefPtr<CefCallback> callback) override;

        virtual void GetResponseHeaders(CefRefPtr<CefResponse> response,
                                      int64& response_length,
                                      CefString& redirectUrl) override;

        virtual bool ReadResponse(void* data_out,
                                int bytes_to_read,
                                int& bytes_read,
                                CefRefPtr<CefCallback> callback) override;

        virtual void Cancel() override;

    private:
        std::string m_data;
        size_t m_offset;
        std::string m_mime_type;

        IMPLEMENT_REFCOUNTING(NexileResourceHandler);
    };

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
        void LoadMainOverlayUI();
        void LoadWelcomePage();
        void LoadBrowserPage();

        // New method to center the window on screen
        void CenterWindow();

        // Get click-through state
        bool GetClickThrough() const { return m_clickThrough; }

        // CEF message handling
        void HandleWebMessage(const std::wstring& message);
        void OnBrowserCreated(CefRefPtr<CefBrowser> browser);
        void OnBrowserClosing();

    private:
        // ------------- core window plumbing -------------
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

        void RegisterWindowClass();
        void InitializeWindow();
        void InitializeCEF();
        void ShutdownCEF();

        // ------------- CEF helpers -------------------
        std::string LoadHTMLResource(const std::string& filename);
        std::string CreateDataURL(const std::string& html);

        // ------------- members --------------------------
        NexileApp* m_app;
        HWND       m_hwnd;
        bool       m_visible;
        bool       m_clickThrough;
        bool       m_cefInitialized;

        CefRefPtr<NexileClient> m_client;
        CefRefPtr<CefBrowser>   m_browser;

        std::mutex                       m_callbackMutex;
        std::vector<WebMessageCallback>  m_webMessageCallbacks;

        // Window settings
        RECT m_windowRect;
    };

} // namespace Nexile