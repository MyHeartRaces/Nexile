#include "UI/OverlayWindow.h"
#include "Core/NexileApp.h"
#include "Modules/ModuleInterface.h"
#include "Input/HotkeyManager.h"
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
#include <Psapi.h>

// CEF C API includes
#include "include/cef_app.h"

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Psapi.lib")

namespace Nexile {

    // FIXED: Initialize static members
    std::unordered_map<void*, OverlayWindow*> OverlayWindow::s_handlerRegistry;
    std::mutex OverlayWindow::s_registryMutex;

    namespace {
        void EnsureDirectoryExists(const std::wstring& path) {
            if (PathFileExistsW(path.c_str())) return;
            if (SHCreateDirectoryExW(nullptr, path.c_str(), nullptr) != ERROR_SUCCESS) {
                LOG_WARNING("Failed to create folder: {}", Utils::WideStringToString(path));
            }
        }

        // Memory usage logging for debugging
        void LogMemoryUsage(const std::string& context) {
            #ifdef NEXILE_MEMORY_DEBUGGING
            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
                LOG_INFO("Memory [{}]: Working Set = {}MB, Peak = {}MB",
                         context,
                         pmc.WorkingSetSize / 1024 / 1024,
                         pmc.PeakWorkingSetSize / 1024 / 1024);
            }
            #endif
        }
    }

    // ================== CEF Base Reference Counting Helper ==================
    void OverlayWindow::InitializeCefBase(cef_base_ref_counted_t* base, size_t size) {
        memset(base, 0, size);
        base->size = size;
        base->ref_count = 1;

        base->add_ref = [](cef_base_ref_counted_t* self) -> int {
            return ++self->ref_count;
        };

        base->release = [](cef_base_ref_counted_t* self) -> int {
            if (--self->ref_count == 0) {
                delete[] reinterpret_cast<char*>(self);
                return 1;
            }
            return 0;
        };

        base->has_one_ref = [](cef_base_ref_counted_t* self) -> int {
            return self->ref_count == 1;
        };

        base->has_at_least_one_ref = [](cef_base_ref_counted_t* self) -> int {
            return self->ref_count >= 1;
        };
    }

    // FIXED: Helper to get context from extended handler structures
    NexileHandlerContext* OverlayWindow::GetHandlerContext(void* handler_ptr) {
        // Cast to the appropriate extended structure based on context
        // This assumes we know which type of handler we're dealing with
        // In practice, we can use the handler's function pointers to identify type
        return nullptr; // Will be properly implemented in specific callbacks
    }

    // ================== Constructor/Destructor ==================
    OverlayWindow::OverlayWindow(NexileApp* app)
        : m_app(app), m_hwnd(nullptr), m_visible(false), m_clickThrough(true),
          m_cefInitialized(false), m_browser(nullptr), m_client(nullptr),
          m_life_span_handler(nullptr), m_load_handler(nullptr), m_display_handler(nullptr),
          m_request_handler(nullptr), m_render_process_handler(nullptr), m_app_handler(nullptr),
          m_context(nullptr), m_memoryOptimizationEnabled(true) {

        m_windowRect = {0, 0, 1280, 960};

        // FIXED: Create shared context
        m_context = new NexileHandlerContext{this, "", 0, ""};

        LOG_INFO("Initializing Nexile Overlay with CEF C API");
        LogMemoryUsage("Pre-Init");

        RegisterWindowClass();
        InitializeWindow();
        InitializeCEF();

        LogMemoryUsage("Post-Init");
    }

    OverlayWindow::~OverlayWindow() {
        LOG_INFO("Shutting down Nexile Overlay");
        LogMemoryUsage("Pre-Shutdown");

        if (m_browser) {
            auto host = m_browser->get_host(m_browser);
            if (host) {
                host->close_browser(host, 1);
                host->base.release((cef_base_ref_counted_t*)host);
            }
            m_browser->base.release((cef_base_ref_counted_t*)m_browser);
            m_browser = nullptr;
        }

        if (m_hwnd) {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }

        ReleaseCEFHandlers();
        ShutdownCEF();

        // FIXED: Clean up context
        delete m_context;
        m_context = nullptr;

        LogMemoryUsage("Post-Shutdown");
    }

    // ================== Window Management ==================
    void OverlayWindow::RegisterWindowClass() {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = m_app->GetInstanceHandle();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
        wc.lpszClassName = L"NexileOverlayWindow";

        if (!RegisterClassExW(&wc)) {
            throw std::runtime_error("Failed to register overlay window class");
        }
    }

    void OverlayWindow::InitializeWindow() {
        DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
        DWORD style = WS_POPUP | WS_VISIBLE;

        m_hwnd = CreateWindowExW(
            exStyle,
            L"NexileOverlayWindow",
            L"Nexile Overlay",
            style,
            0, 0, 1280, 960,
            nullptr, nullptr,
            m_app->GetInstanceHandle(),
            this
        );

        if (!m_hwnd) {
            throw std::runtime_error("Failed to create overlay window");
        }

        SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 200, LWA_ALPHA);
        SetClickThrough(m_clickThrough);
    }

    LRESULT CALLBACK OverlayWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        OverlayWindow* window = nullptr;

        if (msg == WM_NCCREATE) {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lp);
            window = static_cast<OverlayWindow*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        } else {
            window = reinterpret_cast<OverlayWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        if (window) {
            return window->HandleMessage(hwnd, msg, wp, lp);
        }

        return DefWindowProc(hwnd, msg, wp, lp);
    }

    LRESULT OverlayWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_SIZE: {
                if (m_browser) {
                    RECT rect;
                    GetClientRect(hwnd, &rect);
                    auto host = m_browser->get_host(m_browser);
                    if (host) {
                        host->was_resized(host);
                        host->base.release((cef_base_ref_counted_t*)host);
                    }
                }
                return 0;
            }

            case WM_CLOSE:
                Hide();
                return 0;

            case WM_DESTROY:
                return 0;

            default:
                return DefWindowProc(hwnd, msg, wp, lp);
        }
    }

    void OverlayWindow::CenterWindow() {
        if (!m_hwnd) return;

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int x = (screenWidth - 1280) / 2;
        int y = (screenHeight - 960) / 2;

        SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, 1280, 960, SWP_NOACTIVATE);
    }

    void OverlayWindow::SetPosition(const RECT& rect) {
        m_windowRect = rect;
        if (m_hwnd) {
            SetWindowPos(m_hwnd, nullptr, rect.left, rect.top,
                        rect.right - rect.left, rect.bottom - rect.top,
                        SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    void OverlayWindow::SetClickThrough(bool clickThrough) {
        m_clickThrough = clickThrough;
        if (m_hwnd) {
            LONG_PTR exStyle = GetWindowLongPtr(m_hwnd, GWL_EXSTYLE);
            if (clickThrough) {
                exStyle |= WS_EX_TRANSPARENT;
            } else {
                exStyle &= ~WS_EX_TRANSPARENT;
            }
            SetWindowLongPtr(m_hwnd, GWL_EXSTYLE, exStyle);
        }
    }

    // ================== CEF Initialization ==================
    void OverlayWindow::InitializeCEF() {
        cef_main_args_t main_args = {};
        main_args.instance = m_app->GetInstanceHandle();

        // Check if this is a subprocess
        int exit_code = cef_execute_process(&main_args, nullptr, nullptr);
        if (exit_code >= 0) {
            return; // This is a subprocess
        }

        // CEF settings with memory optimization
        cef_settings_t settings = {};
        settings.size = sizeof(cef_settings_t);
        settings.no_sandbox = 1;
        settings.single_process = 1;
        settings.multi_threaded_message_loop = 0;
        settings.uncaught_exception_stack_size = 10;
        settings.persist_session_cookies = 0;
        settings.persist_user_preferences = 0;
        settings.log_severity = LOGSEVERITY_WARNING;

        // Set paths
        std::string cef_cache = Utils::CombinePath(Utils::GetAppDataPath(), "cef_cache");
        Utils::CreateDirectory(cef_cache);
        cef_string_from_utf8(cef_cache.c_str(), cef_cache.length(), &settings.cache_path);

        std::string cef_log = Utils::CombinePath(Utils::GetAppDataPath(), "cef.log");
        cef_string_from_utf8(cef_log.c_str(), cef_log.length(), &settings.log_file);

        std::string resource_dir = Utils::GetModulePath();
        cef_string_from_utf8(resource_dir.c_str(), resource_dir.length(), &settings.resources_dir_path);

        std::string locales_dir = Utils::CombinePath(Utils::GetModulePath(), "locales");
        cef_string_from_utf8(locales_dir.c_str(), locales_dir.length(), &settings.locales_dir_path);

        CreateCEFHandlers();

        if (!cef_initialize(&main_args, &settings, &m_app_handler->handler, nullptr)) {
            throw std::runtime_error("Failed to initialize CEF C API");
        }

        m_cefInitialized = true;
        LOG_INFO("CEF C API initialized with memory constraints");

        // Create browser
        cef_window_info_t windowInfo = {};
        windowInfo.style = WS_CHILD | WS_VISIBLE;
        windowInfo.parent_window = m_hwnd;
        windowInfo.x = 0;
        windowInfo.y = 0;
        windowInfo.width = 1280;
        windowInfo.height = 960;

        cef_browser_settings_t browserSettings = {};
        browserSettings.size = sizeof(cef_browser_settings_t);
        browserSettings.web_security = STATE_DISABLED;
        browserSettings.javascript_close_windows = STATE_DISABLED;
        browserSettings.plugins = STATE_DISABLED;
        browserSettings.java = STATE_DISABLED;
        browserSettings.javascript_access_clipboard = STATE_DISABLED;
        browserSettings.javascript_dom_paste = STATE_DISABLED;
        browserSettings.image_loading = STATE_ENABLED;
        browserSettings.image_shrink_standalone_to_fit = STATE_ENABLED;
        browserSettings.text_area_resize = STATE_DISABLED;
        browserSettings.tab_to_links = STATE_DISABLED;
        browserSettings.local_storage = STATE_DISABLED;
        browserSettings.databases = STATE_DISABLED;
        browserSettings.application_cache = STATE_DISABLED;
        browserSettings.webgl = STATE_DISABLED;
        browserSettings.background_color = 0; // Transparent

        cef_string_t url = {};
        std::string dataURL = "data:text/html;charset=utf-8,<html><body style='background:transparent;margin:0;padding:0;'></body></html>";
        cef_string_from_utf8(dataURL.c_str(), dataURL.length(), &url);

        if (!cef_browser_host_create_browser(&windowInfo, &m_client->client, &url, &browserSettings, nullptr, nullptr)) {
            throw std::runtime_error("Failed to create CEF browser");
        }

        cef_string_clear(&url);
        LOG_INFO("CEF browser creation initiated");
    }

    void OverlayWindow::CreateCEFHandlers() {
        // FIXED: Create extended handlers with proper context embedding

        // Life Span Handler
        m_life_span_handler = new NexileLifeSpanHandler;
        memset(m_life_span_handler, 0, sizeof(NexileLifeSpanHandler));
        InitializeCefBase((cef_base_ref_counted_t*)&m_life_span_handler->handler, sizeof(cef_life_span_handler_t));
        m_life_span_handler->handler.on_after_created = OnAfterCreated;
        m_life_span_handler->handler.do_close = DoClose;
        m_life_span_handler->handler.on_before_close = OnBeforeClose;
        m_life_span_handler->context = m_context;

        // Load Handler
        m_load_handler = new NexileLoadHandler;
        memset(m_load_handler, 0, sizeof(NexileLoadHandler));
        InitializeCefBase((cef_base_ref_counted_t*)&m_load_handler->handler, sizeof(cef_load_handler_t));
        m_load_handler->handler.on_load_start = OnLoadStart;
        m_load_handler->handler.on_load_end = OnLoadEnd;
        m_load_handler->handler.on_load_error = OnLoadError;
        m_load_handler->context = m_context;

        // Display Handler
        m_display_handler = new NexileDisplayHandler;
        memset(m_display_handler, 0, sizeof(NexileDisplayHandler));
        InitializeCefBase((cef_base_ref_counted_t*)&m_display_handler->handler, sizeof(cef_display_handler_t));
        m_display_handler->handler.on_title_change = OnTitleChange;
        m_display_handler->context = m_context;

        // Request Handler
        m_request_handler = new NexileRequestHandler;
        memset(m_request_handler, 0, sizeof(NexileRequestHandler));
        InitializeCefBase((cef_base_ref_counted_t*)&m_request_handler->handler, sizeof(cef_request_handler_t));
        m_request_handler->handler.get_resource_handler = GetResourceHandler;
        m_request_handler->context = m_context;

        // Render Process Handler
        m_render_process_handler = new NexileRenderProcessHandler;
        memset(m_render_process_handler, 0, sizeof(NexileRenderProcessHandler));
        InitializeCefBase((cef_base_ref_counted_t*)&m_render_process_handler->handler, sizeof(cef_render_process_handler_t));
        m_render_process_handler->handler.on_context_created = OnContextCreated;
        m_render_process_handler->handler.on_process_message_received = OnProcessMessageReceived;
        m_render_process_handler->context = m_context;

        // App Handler
        m_app_handler = new NexileAppHandler;
        memset(m_app_handler, 0, sizeof(NexileAppHandler));
        InitializeCefBase((cef_base_ref_counted_t*)&m_app_handler->handler, sizeof(cef_app_t));
        m_app_handler->handler.get_render_process_handler = GetRenderProcessHandler;
        m_app_handler->context = m_context;

        // Client Handler
        m_client = new NexileClient;
        memset(m_client, 0, sizeof(NexileClient));
        InitializeCefBase((cef_base_ref_counted_t*)&m_client->client, sizeof(cef_client_t));
        m_client->client.get_life_span_handler = GetLifeSpanHandler;
        m_client->client.get_load_handler = GetLoadHandler;
        m_client->client.get_display_handler = GetDisplayHandler;
        m_client->client.get_request_handler = GetRequestHandler;
        m_client->context = m_context;

        // FIXED: Register handlers in global registry for cleanup tracking
        std::lock_guard<std::mutex> lock(s_registryMutex);
        s_handlerRegistry[m_life_span_handler] = this;
        s_handlerRegistry[m_load_handler] = this;
        s_handlerRegistry[m_display_handler] = this;
        s_handlerRegistry[m_request_handler] = this;
        s_handlerRegistry[m_render_process_handler] = this;
        s_handlerRegistry[m_app_handler] = this;
        s_handlerRegistry[m_client] = this;
    }

    void OverlayWindow::ReleaseCEFHandlers() {
        // FIXED: Unregister from global registry
        {
            std::lock_guard<std::mutex> lock(s_registryMutex);
            s_handlerRegistry.erase(m_life_span_handler);
            s_handlerRegistry.erase(m_load_handler);
            s_handlerRegistry.erase(m_display_handler);
            s_handlerRegistry.erase(m_request_handler);
            s_handlerRegistry.erase(m_render_process_handler);
            s_handlerRegistry.erase(m_app_handler);
            s_handlerRegistry.erase(m_client);
        }

        auto releaseHandler = [](auto** handler) {
            if (*handler) {
                (*handler)->handler.base.release((cef_base_ref_counted_t*)&(*handler)->handler);
                *handler = nullptr;
            }
        };

        releaseHandler(&m_client);
        releaseHandler(&m_life_span_handler);
        releaseHandler(&m_load_handler);
        releaseHandler(&m_display_handler);
        releaseHandler(&m_request_handler);
        releaseHandler(&m_render_process_handler);
        releaseHandler(&m_app_handler);
    }

    void OverlayWindow::ShutdownCEF() {
        if (m_cefInitialized) {
            cef_shutdown();
            m_cefInitialized = false;
            LOG_INFO("CEF C API shutdown completed");
        }
    }

    // ================== CEF Callback Implementations ==================

    void CEF_CALLBACK OverlayWindow::OnAfterCreated(cef_life_span_handler_t* self, cef_browser_t* browser) {
        // FIXED: Get context from extended structure
        NexileLifeSpanHandler* handler = reinterpret_cast<NexileLifeSpanHandler*>(self);
        if (handler && handler->context && handler->context->overlay) {
            handler->context->overlay->OnBrowserCreated(browser);
        }
    }

    int CEF_CALLBACK OverlayWindow::DoClose(cef_life_span_handler_t* self, cef_browser_t* browser) {
        return 0; // Allow close
    }

    void CEF_CALLBACK OverlayWindow::OnBeforeClose(cef_life_span_handler_t* self, cef_browser_t* browser) {
        NexileLifeSpanHandler* handler = reinterpret_cast<NexileLifeSpanHandler*>(self);
        if (handler && handler->context && handler->context->overlay) {
            handler->context->overlay->OnBrowserClosing();
        }
    }

    void CEF_CALLBACK OverlayWindow::OnLoadStart(cef_load_handler_t* self, cef_browser_t* browser,
                                                cef_frame_t* frame, cef_transition_type_t transition_type) {
        if (frame && frame->is_main(frame)) {
            LOG_DEBUG("CEF load started");
        }
    }

    void CEF_CALLBACK OverlayWindow::OnLoadEnd(cef_load_handler_t* self, cef_browser_t* browser,
                                              cef_frame_t* frame, int httpStatusCode) {
        if (frame && frame->is_main(frame)) {
            LOG_INFO("CEF load completed with status: {}", httpStatusCode);

            // Inject JavaScript bridge
            cef_string_t script = {};
            std::string bridgeScript = R"(
                window.nexile = window.nexile || {};
                window.nexile.postMessage = function(data) {
                    if (window.nexileBridge) {
                        window.nexileBridge.postMessage(JSON.stringify(data));
                    }
                };
                window.chrome = window.chrome || {};
                window.chrome.webview = {
                    postMessage: function(data) {
                        window.nexile.postMessage(data);
                    }
                };
                console.log('Nexile bridge initialized');
            )";

            cef_string_from_utf8(bridgeScript.c_str(), bridgeScript.length(), &script);
            cef_string_t url = {};
            frame->execute_java_script(frame, &script, &url, 0);
            cef_string_clear(&script);
        }
    }

    void CEF_CALLBACK OverlayWindow::OnLoadError(cef_load_handler_t* self, cef_browser_t* browser,
                                                cef_frame_t* frame, cef_errorcode_t errorCode,
                                                const cef_string_t* errorText, const cef_string_t* failedUrl) {
        if (frame && frame->is_main(frame)) {
            std::string error = CefStringToStdString(errorText);
            LOG_ERROR("CEF load error: {} ({})", error, static_cast<int>(errorCode));
        }
    }

    void CEF_CALLBACK OverlayWindow::OnTitleChange(cef_display_handler_t* self, cef_browser_t* browser,
                                                  const cef_string_t* title) {
        std::string titleStr = CefStringToStdString(title);
        LOG_DEBUG("CEF title changed: {}", titleStr);
    }

    cef_resource_handler_t* CEF_CALLBACK OverlayWindow::GetResourceHandler(cef_request_handler_t* self,
                                                                           cef_browser_t* browser,
                                                                           cef_frame_t* frame,
                                                                           cef_request_t* request) {
        cef_string_userfree_t url_ptr = request->get_url(request);
        std::string url = CefStringToStdString(url_ptr);
        cef_string_userfree_free(url_ptr);

        if (url.find("nexile://") == 0) {
            // FIXED: Create resource handler with context
            NexileRequestHandler* reqHandler = reinterpret_cast<NexileRequestHandler*>(self);

            auto* resHandler = new NexileResourceHandler;
            memset(resHandler, 0, sizeof(NexileResourceHandler));
            InitializeCefBase((cef_base_ref_counted_t*)&resHandler->handler, sizeof(cef_resource_handler_t));

            resHandler->handler.process_request = ResourceProcessRequest;
            resHandler->handler.get_response_headers = ResourceGetResponseHeaders;
            resHandler->handler.read_response = ResourceReadResponse;
            resHandler->handler.cancel = ResourceCancel;

            // Share context
            resHandler->context = reqHandler->context;

            return &resHandler->handler;
        }

        return nullptr;
    }

    // ================== Resource Handler Implementation ==================

    int CEF_CALLBACK OverlayWindow::ResourceProcessRequest(cef_resource_handler_t* self,
                                                          cef_request_t* request, cef_callback_t* callback) {
        NexileResourceHandler* handler = reinterpret_cast<NexileResourceHandler*>(self);
        if (!handler || !handler->context) return 0;

        cef_string_userfree_t url_ptr = request->get_url(request);
        std::string url = CefStringToStdString(url_ptr);
        cef_string_userfree_free(url_ptr);

        std::string filename;
        if (url.find("nexile://") == 0) {
            filename = url.substr(9);
        }

        // Load HTML content
        std::string htmlPath = Utils::CombinePath(Utils::GetModulePath(), "HTML\\" + filename);
        if (Utils::FileExists(htmlPath)) {
            handler->context->currentResourceData = Utils::ReadTextFile(htmlPath);
            handler->context->resourceMimeType = "text/html";
        } else {
            handler->context->currentResourceData = "<html><body><h1>404 Not Found</h1></body></html>";
            handler->context->resourceMimeType = "text/html";
        }

        handler->context->resourceOffset = 0;
        callback->cont(callback);
        return 1;
    }

    void CEF_CALLBACK OverlayWindow::ResourceGetResponseHeaders(cef_resource_handler_t* self,
                                                               cef_response_t* response, int64* response_length,
                                                               cef_string_t* redirectUrl) {
        NexileResourceHandler* handler = reinterpret_cast<NexileResourceHandler*>(self);
        if (!handler || !handler->context) return;

        cef_string_t mimeType = {};
        cef_string_from_utf8(handler->context->resourceMimeType.c_str(),
                           handler->context->resourceMimeType.length(), &mimeType);
        response->set_mime_type(response, &mimeType);
        cef_string_clear(&mimeType);

        response->set_status(response, 200);
        *response_length = handler->context->currentResourceData.length();
    }

    int CEF_CALLBACK OverlayWindow::ResourceReadResponse(cef_resource_handler_t* self, void* data_out,
                                                        int bytes_to_read, int* bytes_read,
                                                        cef_callback_t* callback) {
        NexileResourceHandler* handler = reinterpret_cast<NexileResourceHandler*>(self);
        if (!handler || !handler->context) {
            *bytes_read = 0;
            return 0;
        }

        *bytes_read = 0;
        if (handler->context->resourceOffset < handler->context->currentResourceData.length()) {
            int transfer_size = std::min(bytes_to_read,
                                       static_cast<int>(handler->context->currentResourceData.length() - handler->context->resourceOffset));
            memcpy(data_out, handler->context->currentResourceData.c_str() + handler->context->resourceOffset, transfer_size);
            handler->context->resourceOffset += transfer_size;
            *bytes_read = transfer_size;
            return 1;
        }
        return 0;
    }

    void CEF_CALLBACK OverlayWindow::ResourceCancel(cef_resource_handler_t* self) {
        NexileResourceHandler* handler = reinterpret_cast<NexileResourceHandler*>(self);
        if (handler && handler->context) {
            handler->context->currentResourceData.clear();
            handler->context->currentResourceData.shrink_to_fit();
        }
    }

    // ================== Client Handler Callbacks ==================

    cef_life_span_handler_t* CEF_CALLBACK OverlayWindow::GetLifeSpanHandler(cef_client_t* self) {
        NexileClient* client = reinterpret_cast<NexileClient*>(self);
        if (client && client->context && client->context->overlay) {
            client->context->overlay->m_life_span_handler->handler.base.add_ref(
                (cef_base_ref_counted_t*)&client->context->overlay->m_life_span_handler->handler);
            return &client->context->overlay->m_life_span_handler->handler;
        }
        return nullptr;
    }

    cef_load_handler_t* CEF_CALLBACK OverlayWindow::GetLoadHandler(cef_client_t* self) {
        NexileClient* client = reinterpret_cast<NexileClient*>(self);
        if (client && client->context && client->context->overlay) {
            client->context->overlay->m_load_handler->handler.base.add_ref(
                (cef_base_ref_counted_t*)&client->context->overlay->m_load_handler->handler);
            return &client->context->overlay->m_load_handler->handler;
        }
        return nullptr;
    }

    cef_display_handler_t* CEF_CALLBACK OverlayWindow::GetDisplayHandler(cef_client_t* self) {
        NexileClient* client = reinterpret_cast<NexileClient*>(self);
        if (client && client->context && client->context->overlay) {
            client->context->overlay->m_display_handler->handler.base.add_ref(
                (cef_base_ref_counted_t*)&client->context->overlay->m_display_handler->handler);
            return &client->context->overlay->m_display_handler->handler;
        }
        return nullptr;
    }

    cef_request_handler_t* CEF_CALLBACK OverlayWindow::GetRequestHandler(cef_client_t* self) {
        NexileClient* client = reinterpret_cast<NexileClient*>(self);
        if (client && client->context && client->context->overlay) {
            client->context->overlay->m_request_handler->handler.base.add_ref(
                (cef_base_ref_counted_t*)&client->context->overlay->m_request_handler->handler);
            return &client->context->overlay->m_request_handler->handler;
        }
        return nullptr;
    }

    cef_render_process_handler_t* CEF_CALLBACK OverlayWindow::GetRenderProcessHandler(cef_app_t* self) {
        NexileAppHandler* appHandler = reinterpret_cast<NexileAppHandler*>(self);
        if (appHandler && appHandler->context && appHandler->context->overlay) {
            appHandler->context->overlay->m_render_process_handler->handler.base.add_ref(
                (cef_base_ref_counted_t*)&appHandler->context->overlay->m_render_process_handler->handler);
            return &appHandler->context->overlay->m_render_process_handler->handler;
        }
        return nullptr;
    }

    // ================== V8 and Process Message Handling ==================

    void CEF_CALLBACK OverlayWindow::OnContextCreated(cef_render_process_handler_t* self,
                                                     cef_browser_t* browser, cef_frame_t* frame,
                                                     cef_v8context_t* context) {
        // Create JavaScript bridge
        cef_v8value_t* window = context->get_global(context);
        cef_v8value_t* nexileBridge = cef_v8value_create_object(nullptr, nullptr);

        auto* v8Handler = new NexileV8Handler;
        memset(v8Handler, 0, sizeof(NexileV8Handler));
        InitializeCefBase((cef_base_ref_counted_t*)&v8Handler->handler, sizeof(cef_v8handler_t));
        v8Handler->handler.execute = V8Execute;

        NexileRenderProcessHandler* renderHandler = reinterpret_cast<NexileRenderProcessHandler*>(self);
        v8Handler->context = renderHandler->context;

        cef_string_t funcName = {};
        cef_string_from_utf8("postMessage", 11, &funcName);
        cef_v8value_t* postMessageFunc = cef_v8value_create_function(&funcName, &v8Handler->handler);
        cef_string_clear(&funcName);

        cef_string_t bridgeProperty = {};
        cef_string_from_utf8("postMessage", 11, &bridgeProperty);
        nexileBridge->set_value_bykey(nexileBridge, &bridgeProperty, postMessageFunc, V8_PROPERTY_ATTRIBUTE_NONE);
        cef_string_clear(&bridgeProperty);

        cef_string_t windowProperty = {};
        cef_string_from_utf8("nexileBridge", 12, &windowProperty);
        window->set_value_bykey(window, &windowProperty, nexileBridge, V8_PROPERTY_ATTRIBUTE_NONE);
        cef_string_clear(&windowProperty);

        // Cleanup references
        postMessageFunc->base.release((cef_base_ref_counted_t*)postMessageFunc);
        nexileBridge->base.release((cef_base_ref_counted_t*)nexileBridge);
        window->base.release((cef_base_ref_counted_t*)window);
    }

    int CEF_CALLBACK OverlayWindow::OnProcessMessageReceived(cef_render_process_handler_t* self,
                                                            cef_browser_t* browser, cef_frame_t* frame,
                                                            cef_process_id_t source_process,
                                                            cef_process_message_t* message) {
        cef_string_userfree_t name_ptr = message->get_name(message);
        std::string name = CefStringToStdString(name_ptr);
        cef_string_userfree_free(name_ptr);

        if (name == "nexile_message") {
            cef_list_value_t* args = message->get_argument_list(message);
            if (args->get_size(args) > 0) {
                cef_string_userfree_t msg_ptr = args->get_string(args, 0);
                std::string msg = CefStringToStdString(msg_ptr);
                cef_string_userfree_free(msg_ptr);

                // Handle message in overlay
                NexileRenderProcessHandler* handler = reinterpret_cast<NexileRenderProcessHandler*>(self);
                if (handler && handler->context && handler->context->overlay) {
                    std::wstring wmsg = Utils::StringToWideString(msg);
                    handler->context->overlay->HandleWebMessage(wmsg);
                }

                args->base.release((cef_base_ref_counted_t*)args);
                return 1;
            }
            args->base.release((cef_base_ref_counted_t*)args);
        }
        return 0;
    }

    int CEF_CALLBACK OverlayWindow::V8Execute(cef_v8handler_t* self, const cef_string_t* name,
                                             cef_v8value_t* object, size_t argumentsCount,
                                             cef_v8value_t* const* arguments, cef_v8value_t** retval,
                                             cef_string_t* exception) {
        std::string nameStr = CefStringToStdString(name);

        if (nameStr == "postMessage" && argumentsCount == 1 && arguments[0]->is_string(arguments[0])) {
            cef_string_userfree_t message_ptr = arguments[0]->get_string_value(arguments[0]);
            std::string message = CefStringToStdString(message_ptr);
            cef_string_userfree_free(message_ptr);

            // Send message to browser process
            cef_v8context_t* context = cef_v8context_get_current_context();
            cef_browser_t* browser = context->get_browser(context);
            cef_frame_t* frame = context->get_frame(context);

            cef_string_t msgName = {};
            cef_string_from_utf8("nexile_message", 14, &msgName);
            cef_process_message_t* msg = cef_process_message_create(&msgName);
            cef_string_clear(&msgName);

            cef_list_value_t* args = msg->get_argument_list(msg);
            cef_string_t messageStr = {};
            cef_string_from_utf8(message.c_str(), message.length(), &messageStr);
            args->set_string(args, 0, &messageStr);
            cef_string_clear(&messageStr);

            frame->send_process_message(frame, PID_BROWSER, msg);

            // Cleanup
            args->base.release((cef_base_ref_counted_t*)args);
            msg->base.release((cef_base_ref_counted_t*)msg);
            frame->base.release((cef_base_ref_counted_t*)frame);
            browser->base.release((cef_base_ref_counted_t*)browser);
            context->base.release((cef_base_ref_counted_t*)context);

            *retval = cef_v8value_create_bool(1);
            return 1;
        }
        return 0;
    }

    // ================== String Conversion Helpers ==================

    std::string OverlayWindow::CefStringToStdString(const cef_string_t* cef_str) {
        if (!cef_str || !cef_str->str) return "";
        std::wstring wstr(cef_str->str, cef_str->length);
        return Utils::WideStringToString(wstr);
    }

    void OverlayWindow::StdStringToCefString(const std::string& std_str, cef_string_t* cef_str) {
        std::wstring wstr = Utils::StringToWideString(std_str);
        cef_string_from_wide(wstr.c_str(), wstr.length(), cef_str);
    }

    void OverlayWindow::FreeCefString(cef_string_t* cef_str) {
        cef_string_clear(cef_str);
    }

    // ================== Public API Implementation ==================

    void OverlayWindow::Navigate(const std::wstring& uri) {
        if (m_browser) {
            auto frame = m_browser->get_main_frame(m_browser);
            if (frame) {
                cef_string_t url = {};
                std::string urlStr = Utils::WideStringToString(uri);
                cef_string_from_utf8(urlStr.c_str(), urlStr.length(), &url);

                frame->load_url(frame, &url);
                cef_string_clear(&url);

                frame->base.release((cef_base_ref_counted_t*)frame);
                LOG_INFO("Navigating to: {}", urlStr);
            }
        } else {
            LOG_ERROR("Cannot navigate: browser not initialized");
        }
    }

    void OverlayWindow::ExecuteScript(const std::wstring& script) {
        if (!m_browser) {
            LOG_ERROR("Cannot execute script: browser not initialized");
            return;
        }

        auto frame = m_browser->get_main_frame(m_browser);
        if (frame) {
            cef_string_t scriptStr = {};
            std::string scriptUtf8 = Utils::WideStringToString(script);
            cef_string_from_utf8(scriptUtf8.c_str(), scriptUtf8.length(), &scriptStr);

            cef_string_t url = {};
            frame->execute_java_script(frame, &scriptStr, &url, 0);

            cef_string_clear(&scriptStr);
            frame->base.release((cef_base_ref_counted_t*)frame);
        }
    }

    void OverlayWindow::OnBrowserCreated(cef_browser_t* browser) {
        m_browser = browser;
        m_browser->base.add_ref((cef_base_ref_counted_t*)m_browser);

        LoadWelcomePage();
        LOG_INFO("CEF browser created successfully via C API");
        LogMemoryUsage("Browser-Created");
    }

    void OverlayWindow::OnBrowserClosing() {
        if (m_browser) {
            m_browser->base.release((cef_base_ref_counted_t*)m_browser);
            m_browser = nullptr;
        }
        LOG_INFO("CEF browser closed via C API");
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
        LogMemoryUsage("Window-Shown");
    }

    void OverlayWindow::Hide() {
        if (!m_hwnd) {
            LOG_ERROR("Cannot hide overlay: window not initialized");
            return;
        }

        m_visible = false;
        ShowWindow(m_hwnd, SW_HIDE);

        // Memory cleanup on hide
        if (m_memoryOptimizationEnabled) {
            ExecuteScript(L"if(window.gc) window.gc();");
        }

        LOG_INFO("Overlay window hidden");
        LogMemoryUsage("Window-Hidden");
    }

    // ================== Helper Functions ==================

    std::string OverlayWindow::LoadHTMLResource(const std::string& filename) {
        std::string htmlPath = Utils::CombinePath(Utils::GetModulePath(), "HTML\\" + filename);
        if (Utils::FileExists(htmlPath)) {
            return Utils::ReadTextFile(htmlPath);
        }
        return "";
    }

    std::string OverlayWindow::CreateDataURL(const std::string& html) {
        return "data:text/html;charset=utf-8," + html;
    }

    void OverlayWindow::RegisterWebMessageCallback(WebMessageCallback cb) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_webMessageCallbacks.push_back(cb);
    }

    void OverlayWindow::HandleWebMessage(const std::wstring& message) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        for (auto& callback : m_webMessageCallbacks) {
            try {
                callback(message);
            } catch (const std::exception& e) {
                LOG_ERROR("Error in web message callback: {}", e.what());
            }
        }
    }

    void OverlayWindow::LoadWelcomePage() {
        Navigate(L"nexile://welcome.html");
    }

    void OverlayWindow::LoadBrowserPage() {
        Navigate(L"nexile://browser.html");
    }

    void OverlayWindow::LoadMainOverlayUI() {
        Navigate(L"nexile://main_overlay.html");
    }

    void OverlayWindow::LoadModuleUI(const std::shared_ptr<IModule>& module) {
        if (!module) return;

        std::string moduleId = module->GetModuleID();
        if (moduleId == "price_check") {
            Navigate(L"nexile://price_check_module.html");
        } else if (moduleId == "settings") {
            Navigate(L"nexile://settings.html");
        } else {
            // Use module's HTML content
            std::string html = module->GetModuleUIHTML();
            if (!html.empty()) {
                std::string dataURL = CreateDataURL(html);
                Navigate(Utils::StringToWideString(dataURL));
            }
        }
    }

} // namespace Nexile