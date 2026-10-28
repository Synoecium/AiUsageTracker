#pragma once

#include <deque>
#include <cstdint>
#include <functional>
#include <string>

#include <wx/frame.h>
#include <wx/timer.h>

class wxWebView;
class wxWebViewEvent;

// Owns a hidden, persistent embedded-browser (WebView2/Edge) session used to
// talk to a same-origin service. Because it's a real Chromium-based browser engine, its
// requests carry a genuine browser TLS fingerprint and cookie jar, so they
// pass Cloudflare's bot protection the same way a normal browser tab would -
// unlike a plain WinHTTP/curl client, which gets blocked regardless of
// headers or cookies supplied.
//
// The user logs into the configured service once inside this embedded browser (ShowLogin),
// after which the session persists (via a dedicated WebView2 user-data
// folder under %APPDATA%) across app restarts, same as a normal browser
// profile.
//
// Fetches are implemented by kicking off a fetch() in the page and polling a
// plain JS variable for the result, rather than relying on RunScriptAsync to
// await the fetch's promise directly - that turned out to not reliably wait
// for the promise to settle on this backend, so every RunScriptAsync call
// here is a trivial synchronous script instead.
class WebSession : public wxFrame {
public:
    using JsonCallback = std::function<void(bool ok, const std::string& body, const std::string& error)>;

    explicit WebSession(wxWindow* parent,
                        std::string origin = "https://claude.ai",
                        std::string windowTitle = "Log in to claude.ai",
                        std::string bearerToken = {});

    // Makes the embedded browser window visible, navigated to the configured service, so
    // the user can log in (or check they're still logged in).
    void ShowLogin();
    void SetBearerToken(std::string bearerToken);

    // Fetches JSON from a path (e.g. "/api/organizations") using
    // the embedded browser's own cookies via a same-origin fetch(). Calls
    // back on the UI thread. May queue the request until the browser has
    // finished its initial navigation.
    void FetchJson(const std::string& path, JsonCallback callback);
    void FetchJsonPost(const std::string& path, const std::string& body, JsonCallback callback);
    void FetchConnectJson(const std::string& path, const std::string& body, JsonCallback callback);

private:
    enum class Phase { Idle, Kickoff, Polling };

    void OnWebViewLoaded(wxWebViewEvent& event);
    void OnWebViewNewWindow(wxWebViewEvent& event);
    void OnScriptResult(wxWebViewEvent& event);
    void OnPollTimer(wxTimerEvent& event);
    void OnTimeout(wxTimerEvent& event);
    void PumpQueue();
    void FailCurrentRequest(const std::string& error);
    void RunRequestScript(const wxString& script);

    struct PendingRequest {
        std::string path;
        std::string method = "GET";
        std::string body;
        bool connectJson = false;
        JsonCallback callback;
    };

    wxWebView* m_webView = nullptr;
    std::string m_origin;
    std::string m_bearerToken;
    bool m_navigated = false;
    bool m_atOrigin = false;
    Phase m_phase = Phase::Idle;
    wxTimer m_pollTimer;
    wxTimer m_timeoutTimer;
    uintptr_t m_requestId = 0;
    std::deque<PendingRequest> m_queue;
};
