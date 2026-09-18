#include "WebSession.h"

#include <wx/webview.h>
#include <wx/sizer.h>
#include <wx/msgdlg.h>
#include <nlohmann/json.hpp>

#include "Logger.h"

using json = nlohmann::json;

namespace {
constexpr int kPollIntervalMs = 150;

// Always leaves window.__cuTResult holding a JSON string once the fetch
// settles (success or failure) - never throws/rejects up to RunScriptAsync,
// and its own completion value is the literal `true`, not a Promise, so it
// resolves immediately regardless of whether this backend awaits promises.
const wxString& KickoffScriptTemplate() {
    static const wxString tpl =
        "window.__cuTResult = null;"
        "const opts = %s;"
        "const token = localStorage.getItem('access_token');"
        "if (token) opts.headers['Authorization'] = 'Bearer ' + token;"
        "try {"
        "  const ti = JSON.parse(localStorage.getItem('volcano-token-info') || '{}');"
        "  if (ti.userId) opts.headers['X-Traffic-Id'] = ti.userId;"
        "  if (ti.webId) opts.headers['x-msh-device-id'] = ti.webId;"
        "  if (ti.ssid) opts.headers['x-msh-session-id'] = ti.ssid;"
        "} catch (_) {}"
        "fetch(%s, opts)"
        "  .then(r => r.text().then(body => {"
        "    window.__cuTResult = JSON.stringify({status: r.status, body: body, href: location.href});"
        "  }))"
        "  .catch(e => {"
        "    window.__cuTResult = JSON.stringify({"
        "      status: 0, body: (e && e.message) ? e.message : String(e), href: location.href"
        "    });"
        "  });"
        "true;";
    return tpl;
}

const wxString& PollScript() {
    static const wxString script =
        "(function() {"
        "  var r = window.__cuTResult;"
        "  if (r === null || r === undefined) return 'PENDING';"
        "  window.__cuTResult = null;"
        "  return r;"
        "})()";
    return script;
}

const wxString& ConnectKickoffScriptTemplate() {
    static const wxString tpl =
        "window.__cuTResult = null;"
        "const p = new TextEncoder().encode(%s);"
        "const b = new Uint8Array(5 + p.length);"
        "b[0] = 0;"
        "new DataView(b.buffer).setUint32(1, p.length, false);"
        "b.set(p, 5);"
        "const token = localStorage.getItem('access_token');"
        "const headers = {'accept': 'application/json', "
        "'content-type': 'application/connect+json; charset=utf-8', "
        "'connect-protocol-version': '1', 'x-msh-platform': 'web', "
        "'x-msh-version': '2.2.0', 'R-Timezone': Intl.DateTimeFormat().resolvedOptions().timeZone};"
        "if (token) headers['Authorization'] = 'Bearer ' + token;"
        "fetch(%s, {method: 'POST', credentials: 'include', headers: headers, body: b})"
        ".then(r => r.arrayBuffer().then(a => {"
        "  const bytes = new Uint8Array(a);"
        "  let body;"
        "  if (bytes.length >= 5 && (bytes[0] & 0x80) === 0) {"
        "    const len = new DataView(a).getUint32(1, false);"
        "    body = new TextDecoder().decode(bytes.slice(5, 5 + len));"
        "  } else { body = new TextDecoder().decode(bytes); }"
        "  window.__cuTResult = JSON.stringify({status: r.status, body: body, href: location.href});"
        "}))"
        ".catch(e => { window.__cuTResult = JSON.stringify({status: 0, body: (e && e.message) ? e.message : String(e), href: location.href}); });"
        "true;";
    return tpl;
}

} // namespace

WebSession::WebSession(wxWindow* parent, std::string origin, std::string windowTitle, std::string bearerToken)
    : wxFrame(parent, wxID_ANY, windowTitle, wxDefaultPosition, wxSize(480, 720)),
      m_origin(std::move(origin)),
      m_bearerToken(std::move(bearerToken)),
      m_pollTimer(this) {
    if (!wxWebView::IsBackendAvailable(wxWebViewBackendEdge)) {
        Logger::LogError("WebView2 (Edge) backend is not available on this system.");
        wxMessageBox(
            "This app needs the Microsoft Edge WebView2 Runtime to talk to claude.ai.\n"
            "It's preinstalled on most Windows 10/11 systems; if this keeps appearing, "
            "search for \"WebView2 Runtime\" to install it from Microsoft.",
            "WebView2 not available", wxOK | wxICON_ERROR);
    }

    m_webView = wxWebView::New(this, wxID_ANY, wxString::FromUTF8(m_origin.c_str()) + "/", wxDefaultPosition, wxDefaultSize,
                                wxWebViewBackendEdge);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_webView, 1, wxEXPAND);
    SetSizer(sizer);

    m_webView->Bind(wxEVT_WEBVIEW_LOADED, &WebSession::OnWebViewLoaded, this);
    m_webView->Bind(wxEVT_WEBVIEW_SCRIPT_RESULT, &WebSession::OnScriptResult, this);
    // Some login flows (e.g. Cursor's "Sign in") open the target URL in a new
    // window/tab (target="_blank" or window.open()) rather than navigating
    // in place. wxWebView has nowhere to put that new window by default, so
    // it's silently dropped - navigate this same view there instead, which
    // is all we need since we only care about ending up authenticated in
    // this session's persisted profile.
    m_webView->Bind(wxEVT_WEBVIEW_NEWWINDOW, &WebSession::OnWebViewNewWindow, this);
    Bind(wxEVT_TIMER, &WebSession::OnPollTimer, this, m_pollTimer.GetId());

    // Hide instead of destroying when the user closes the login window - the
    // WebView2 instance (and its logged-in session) stays alive in the
    // background for future fetches.
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (event.CanVeto()) {
            event.Veto();
            Hide();
        } else {
            event.Skip();
        }
    });
}

void WebSession::ShowLogin() {
    m_atOrigin = false;
    if (m_navigated) {
        m_webView->LoadURL(wxString::FromUTF8(m_origin.c_str()) + "/");
    }
    Show();
    Raise();
}

void WebSession::SetBearerToken(std::string bearerToken) {
    m_bearerToken = std::move(bearerToken);
}

void WebSession::FetchJson(const std::string& path, JsonCallback callback) {
    m_queue.push_back({path, "GET", {}, false, std::move(callback)});
    PumpQueue();
}

void WebSession::FetchJsonPost(const std::string& path, const std::string& body, JsonCallback callback) {
    m_queue.push_back({path, "POST", body, false, std::move(callback)});
    PumpQueue();
}

void WebSession::FetchConnectJson(const std::string& path, const std::string& body, JsonCallback callback) {
    m_queue.push_back({path, "POST", body, true, std::move(callback)});
    PumpQueue();
}

void WebSession::OnWebViewLoaded(wxWebViewEvent&) {
    m_navigated = true;
    // Authentication may temporarily navigate through auth.openai.com or an
    // identity provider. Do not run same-origin API fetches from those pages:
    // they either fail CORS or return an unrelated 401 response.
    const wxString currentUrl = m_webView->GetCurrentURL();
    m_atOrigin = currentUrl.StartsWith(wxString::FromUTF8(m_origin.c_str()));
    // Kimi's global site redirects between the apex and www host during
    // login. They share the same authenticated session, so accept both.
    if (!m_atOrigin && m_origin == "https://kimi.ai") {
        m_atOrigin = currentUrl.StartsWith("https://www.kimi.ai");
    }
    PumpQueue();
}

void WebSession::OnWebViewNewWindow(wxWebViewEvent& event) {
    m_webView->LoadURL(event.GetURL());
}

void WebSession::PumpQueue() {
    if (m_phase != Phase::Idle || m_queue.empty() || !m_navigated || !m_atOrigin) return;

    m_phase = Phase::Kickoff;
    const PendingRequest& request = m_queue.front();
    std::string apiOrigin = m_origin;
    // Kimi may redirect the embedded login page to www.kimi.ai. Keep the
    // request same-origin so Chromium does not reject the authenticated RPC
    // as a cross-origin fetch.
    const wxString currentUrl = m_webView->GetCurrentURL();
    if (m_origin == "https://kimi.ai" && currentUrl.StartsWith("https://www.kimi.ai")) {
        apiOrigin = "https://www.kimi.ai";
    }
    const std::string url = apiOrigin + request.path;
    if (request.connectJson) {
        std::string payload = request.body;
        try {
            payload = json::parse(request.body).dump();
        } catch (const json::exception&) {
            payload = "{}";
        }
        wxString js = wxString::Format(ConnectKickoffScriptTemplate(),
                                       wxString::FromUTF8(json(payload).dump().c_str()),
                                       wxString::FromUTF8(json(url).dump().c_str()));
        m_webView->RunScriptAsync(js);
        return;
    }
    std::string options = "{credentials: 'include', headers: {'accept': 'application/json', 'x-msh-platform': 'web', 'x-msh-version': '2.2.0', 'X-Language': navigator.language, 'R-Timezone': Intl.DateTimeFormat().resolvedOptions().timeZone}}";
    if (request.method == "POST") {
        std::string requestBody = request.body;
        try {
            // The caller supplies a JSON document. Keep it as an object in
            // fetch(), rather than JSON-encoding the document as a string.
            requestBody = json::parse(request.body).dump();
        } catch (const json::exception&) {
            requestBody = "{}";
        }
        options = std::string("{method: 'POST', credentials: 'include', headers: {'accept': 'application/json', 'content-type': 'application/json', 'x-msh-platform': 'web', 'x-msh-version': '2.2.0', 'X-Language': navigator.language, 'R-Timezone': Intl.DateTimeFormat().resolvedOptions().timeZone}, body: ") +
                  "JSON.stringify(" + requestBody + ")}";
    }
    wxString js = wxString::Format(KickoffScriptTemplate(),
                                   wxString::FromUTF8(options.c_str()),
                                   wxString::FromUTF8(json(url).dump().c_str()));
    if (!m_bearerToken.empty()) {
        const std::string tokenLiteral = json(m_bearerToken).dump();
        js.Replace("'accept': 'application/json'",
                   "'accept': 'application/json', 'Authorization': 'Bearer ' + " +
                   wxString::FromUTF8(tokenLiteral.c_str()));
    }
    m_webView->RunScriptAsync(js);
}

void WebSession::FailCurrentRequest(const std::string& error) {
    Logger::LogError("WebSession request failed: " + error);

    if (m_queue.empty()) {
        m_phase = Phase::Idle;
        return;
    }
    PendingRequest request = std::move(m_queue.front());
    m_queue.pop_front();
    m_phase = Phase::Idle;
    request.callback(false, "", error);
    PumpQueue();
}

void WebSession::OnPollTimer(wxTimerEvent&) {
    if (m_phase != Phase::Polling) return;
    m_webView->RunScriptAsync(PollScript());
}

void WebSession::OnScriptResult(wxWebViewEvent& event) {
    if (m_phase == Phase::Idle) return; // stray/late event, nothing to resolve

    if (event.IsError()) {
        FailCurrentRequest("Script execution failed: " + event.GetString().ToStdString());
        return;
    }

    if (m_phase == Phase::Kickoff) {
        m_phase = Phase::Polling;
        m_pollTimer.StartOnce(kPollIntervalMs);
        return;
    }

    // m_phase == Polling
    wxString result = event.GetString();
    if (result == "PENDING") {
        m_pollTimer.StartOnce(kPollIntervalMs);
        return;
    }

    try {
        json inner = json::parse(result.ToUTF8().data());
        int status = inner.value("status", 0);
        std::string body = inner.value("body", "");
        std::string href = inner.value("href", "");

        PendingRequest request = std::move(m_queue.front());
        m_queue.pop_front();
        m_phase = Phase::Idle;

        if (status == 200) {
            request.callback(true, body, "");
        } else {
            std::string error;
            if (status == 401 || status == 403) {
                error = "HTTP " + std::to_string(status) +
                        " - not logged in, or Cloudflare blocked the request. Try Settings -> Log in.";
            } else if (status == 0) {
                error = "Request failed: " + body + " (browser is currently at: " + href + ")";
            } else {
                error = "HTTP " + std::to_string(status);
            }
            Logger::LogError("WebSession fetch to " + href + " failed: " + error +
                              " | body: " + body.substr(0, 500));
            request.callback(false, "", error);
        }
        PumpQueue();
    } catch (const json::exception& e) {
        FailCurrentRequest(std::string("Failed to parse script result: ") + e.what() +
                            " | raw: " + result.ToStdString());
    }
}
