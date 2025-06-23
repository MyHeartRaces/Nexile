#pragma once

#include <Windows.h>
#include <string>
#include <functional>
#include <vector>
#include <mutex>
#include <memory>

// CEF C API includes - CRITICAL CHANGE: Different include structure
#include "include/capi/cef_app_capi.h"
#include "include/capi/cef_browser_capi.h"
#include "include/capi/cef_client_capi.h"
#include "include/capi/cef_frame_capi.h"
#include "include/capi/cef_load_handler_capi.h"
#include "include/capi/cef_life_span_handler_capi.h"
#include "include/capi/cef_display_handler_capi.h"
#include "include/capi/cef_request_handler_capi.h"
#include "include/capi/cef_resource_handler_capi.h"
#include "include/capi/cef_render_process_handler_capi.h"
#include "include/capi/cef_v8_capi.h"
#include "include/cef_version.h"
#include "include/cef_app.h"  // For main functions

namespace Nexile {

    class NexileApp;
    class IModule;

    using WebMessageCallback = std::function<void(const std::wstring&)>;

    // Data structures for C API callback context
    struct NexileClientData {
        class OverlayWindow* overlay;
        std::string currentResourceData;
        size_t resourceOffset;
        std::string resourceMimeType;
    };

    class OverlayWindow {
    public:
        explicit OverlayWindow(NexileApp* app);
        ~OverlayWindow();

        // ================== Public API (unchanged) ==================
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
        void CenterWindow();
        bool GetClickThrough() const { return m_clickThrough; }

        // ================== CEF C API Integration ==================
        void HandleWebMessage(const std::wstring& message);
        void OnBrowserCreated(cef_browser_t* browser);
        void OnBrowserClosing();

        // ================== Static CEF C API Callbacks ==================

        // Life Span Handler Callbacks
        static void CEF_CALLBACK OnAfterCreated(cef_life_span_handler_t* self, cef_browser_t* browser);
        static int CEF_CALLBACK DoClose(cef_life_span_handler_t* self, cef_browser_t* browser);
        static void CEF_CALLBACK OnBeforeClose(cef_life_span_handler_t* self, cef_browser_t* browser);

        // Load Handler Callbacks
        static void CEF_CALLBACK OnLoadStart(cef_load_handler_t* self, cef_browser_t* browser,
                                            cef_frame_t* frame, cef_transition_type_t transition_type);
        static void CEF_CALLBACK OnLoadEnd(cef_load_handler_t* self, cef_browser_t* browser,
                                          cef_frame_t* frame, int httpStatusCode);
        static void CEF_CALLBACK OnLoadError(cef_load_handler_t* self, cef_browser_t* browser,
                                            cef_frame_t* frame, cef_errorcode_t errorCode,
                                            const cef_string_t* errorText, const cef_string_t* failedUrl);

        // Display Handler Callbacks
        static void CEF_CALLBACK OnTitleChange(cef_display_handler_t* self, cef_browser_t* browser,
                                              const cef_string_t* title);

        // Request Handler Callbacks
        static cef_resource_handler_t* CEF_CALLBACK GetResourceHandler(cef_request_handler_t* self,
                                                                       cef_browser_t* browser,
                                                                       cef_frame_t* frame,
                                                                       cef_request_t* request);

        // Render Process Handler Callbacks
        static void CEF_CALLBACK OnContextCreated(cef_render_process_handler_t* self,
                                                 cef_browser_t* browser, cef_frame_t* frame,
                                                 cef_v8context_t* context);
        static int CEF_CALLBACK OnProcessMessageReceived(cef_render_process_handler_t* self,
                                                         cef_browser_t* browser, cef_frame_t* frame,
                                                         cef_process_id_t source_process,
                                                         cef_process_message_t* message);

        // Resource Handler Callbacks
        static int CEF_CALLBACK ResourceProcessRequest(cef_resource_handler_t* self,
                                                      cef_request_t* request, cef_callback_t* callback);
        static void CEF_CALLBACK ResourceGetResponseHeaders(cef_resource_handler_t* self,
                                                           cef_response_t* response, int64* response_length,
                                                           cef_string_t* redirectUrl);
        static int CEF_CALLBACK ResourceReadResponse(cef_resource_handler_t* self, void* data_out,
                                                    int bytes_to_read, int* bytes_read,
                                                    cef_callback_t* callback);
        static void CEF_CALLBACK ResourceCancel(cef_resource_handler_t* self);

        // V8 Handler Callbacks
        static int CEF_CALLBACK V8Execute(cef_v8handler_t* self, const cef_string_t* name,
                                         cef_v8value_t* object, size_t argumentsCount,
                                         cef_v8value_t* const* arguments, cef_v8value_t** retval,
                                         cef_string_t* exception);

        // Client Handler Callbacks
        static cef_life_span_handler_t* CEF_CALLBACK GetLifeSpanHandler(cef_client_t* self);
        static cef_load_handler_t* CEF_CALLBACK GetLoadHandler(cef_client_t* self);
        static cef_display_handler_t* CEF_CALLBACK GetDisplayHandler(cef_client_t* self);
        static cef_request_handler_t* CEF_CALLBACK GetRequestHandler(cef_client_t* self);

        // App Handler Callbacks
        static cef_render_process_handler_t* CEF_CALLBACK GetRenderProcessHandler(cef_app_t* self);

    private:
        // ================== Window Management ==================
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        void RegisterWindowClass();
        void InitializeWindow();

        // ================== CEF C API Management ==================
        void InitializeCEF();
        void ShutdownCEF();
        void CreateCEFHandlers();
        void ReleaseCEFHandlers();

        // ================== Helper Functions ==================
        std::string LoadHTMLResource(const std::string& filename);
        std::string CreateDataURL(const std::string& html);

        // CEF String Conversion Helpers
        static std::string CefStringToStdString(const cef_string_t* cef_str);
        static void StdStringToCefString(const std::string& std_str, cef_string_t* cef_str);
        static void FreeCefString(cef_string_t* cef_str);

        // CEF Reference Counting Helper
        static void InitializeCefBase(cef_base_ref_counted_t* base, size_t size);

        // ================== Member Variables ==================

        // Core application
        NexileApp* m_app;
        HWND m_hwnd;
        bool m_visible;
        bool m_clickThrough;
        bool m_cefInitialized;

        // CEF C API structures - MAJOR CHANGE: Direct C structs
        cef_browser_t* m_browser;
        cef_client_t* m_client;
        cef_life_span_handler_t* m_life_span_handler;
        cef_load_handler_t* m_load_handler;
        cef_display_handler_t* m_display_handler;
        cef_request_handler_t* m_request_handler;
        cef_render_process_handler_t* m_render_process_handler;
        cef_app_t* m_app_handler;

        // Callback context data
        NexileClientData* m_clientData;

        // Message handling
        std::mutex m_callbackMutex;
        std::vector<WebMessageCallback> m_webMessageCallbacks;

        // Window properties
        RECT m_windowRect;

        // Memory optimization flag
        bool m_memoryOptimizationEnabled;

        // Resource tracking for memory management
        std::vector<cef_base_ref_counted_t*> m_cefResources;
    };

} // namespace Nexile