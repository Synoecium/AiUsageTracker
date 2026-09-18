#include "MainFrame.h"

#include "SettingsDialog.h"
#include "Logger.h"

#include <ctime>
#include <vector>

namespace {

wxString FormatCountdown(time_t resetsAt) {
    if (resetsAt <= 0) return "unknown";

    time_t now = std::time(nullptr);
    long long diff = static_cast<long long>(resetsAt - now);
    if (diff <= 0) return "resetting...";

    long long minutes = diff / 60;
    long long hours = minutes / 60;
    long long days = hours / 24;

    if (days > 0) {
        return wxString::Format("%lldd %lldh", days, hours % 24);
    }
    if (hours > 0) {
        return wxString::Format("%lldh %lldm", hours, minutes % 60);
    }
    return wxString::Format("%lldm", minutes);
}

wxColour ColourForPercent(double percent, double warn, double danger) {
    if (percent >= danger) return wxColour(200, 30, 30);
    if (percent >= warn) return wxColour(210, 130, 0);
    return wxColour(20, 120, 40);
}

} // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "AI Usage Tracker", wxDefaultPosition, wxSize(460, 820)),
      m_timer(this) {
    BuildUi();

    m_webSession = new WebSession(this, "https://claude.ai", "Log in to claude.ai");
    m_codexWebSession = new WebSession(this, "https://chatgpt.com", "Log in to ChatGPT for Codex");
    m_kimiWebSession = new WebSession(this, "https://kimi.ai", "Log in to Kimi");
    m_cursorWebSession = new WebSession(this, "https://cursor.com", "Log in to Cursor");
    m_trayIcon = std::make_unique<TrayIcon>(this);

    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    Bind(wxEVT_TIMER, &MainFrame::OnTimer, this, m_timer.GetId());

    SettingsStore::Load(m_settings);
    m_timer.Start(m_settings.refreshIntervalSeconds * 1000);

    ApplyProviderVisibility();

    if (m_settings.claudeEnabled && m_settings.organizationId.empty()) {
        SetStatus("Not logged in yet - opening Settings...", false);
        CallAfter(&MainFrame::OpenSettings);
    } else {
        RefreshNow();
    }
}

MainFrame::~MainFrame() {
    m_timer.Stop();
    if (m_webSession) m_webSession->Destroy();
    if (m_codexWebSession) m_codexWebSession->Destroy();
    if (m_kimiWebSession) m_kimiWebSession->Destroy();
    if (m_cursorWebSession) m_cursorWebSession->Destroy();
}

void MainFrame::BuildUi() {
    auto* panel = new wxPanel(this);
    m_panel = panel;
    auto* root = new wxBoxSizer(wxVERTICAL);

    m_rootSizer = root;

    auto* claudeBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Claude");
    m_claudeBox = claudeBox;
    claudeBox->Add(new wxStaticText(panel, wxID_ANY, "5-Hour Session"), 0, wxLEFT | wxTOP, 8);
    m_fiveHourGauge = new wxGauge(panel, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, 22));
    claudeBox->Add(m_fiveHourGauge, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    m_fiveHourLabel = new wxStaticText(panel, wxID_ANY, "--");
    claudeBox->Add(m_fiveHourLabel, 0, wxLEFT | wxTOP, 8);

    claudeBox->AddSpacer(8);

    claudeBox->Add(new wxStaticText(panel, wxID_ANY, "Weekly"), 0, wxLEFT, 8);
    m_weeklyGauge = new wxGauge(panel, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, 22));
    claudeBox->Add(m_weeklyGauge, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    m_weeklyLabel = new wxStaticText(panel, wxID_ANY, "--");
    claudeBox->Add(m_weeklyLabel, 0, wxLEFT | wxTOP, 8);

    m_statusText = new wxStaticText(panel, wxID_ANY, "");
    claudeBox->Add(m_statusText, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 8);
    root->Add(claudeBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

    auto* codexBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Codex");
    m_codexBox = codexBox;
    codexBox->Add(new wxStaticText(panel, wxID_ANY, "Monthly"), 0, wxLEFT | wxTOP, 8);
    m_codexMonthlyGauge = new wxGauge(panel, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, 22));
    codexBox->Add(m_codexMonthlyGauge, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    m_codexMonthlyLabel = new wxStaticText(panel, wxID_ANY, "--");
    codexBox->Add(m_codexMonthlyLabel, 0, wxLEFT | wxTOP, 8);
    m_codexStatusText = new wxStaticText(panel, wxID_ANY, "Codex: not logged in");
    codexBox->Add(m_codexStatusText, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 8);
    root->Add(codexBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

    auto* kimiBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Kimi Code");
    m_kimiBox = kimiBox;
    kimiBox->Add(new wxStaticText(panel, wxID_ANY, "Monthly"), 0, wxLEFT | wxTOP, 8);
    m_kimiMonthlyGauge = new wxGauge(panel, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, 22));
    kimiBox->Add(m_kimiMonthlyGauge, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    m_kimiMonthlyLabel = new wxStaticText(panel, wxID_ANY, "--");
    kimiBox->Add(m_kimiMonthlyLabel, 0, wxLEFT | wxTOP, 8);
    m_kimiStatusText = new wxStaticText(panel, wxID_ANY, "Kimi: not configured");
    kimiBox->Add(m_kimiStatusText, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 8);
    root->Add(kimiBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

    auto* cursorBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Cursor");
    m_cursorBox = cursorBox;
    cursorBox->Add(new wxStaticText(panel, wxID_ANY, "Monthly"), 0, wxLEFT | wxTOP, 8);
    m_cursorMonthlyGauge = new wxGauge(panel, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, 22));
    cursorBox->Add(m_cursorMonthlyGauge, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    m_cursorMonthlyLabel = new wxStaticText(panel, wxID_ANY, "--");
    cursorBox->Add(m_cursorMonthlyLabel, 0, wxLEFT | wxTOP, 8);
    m_cursorStatusText = new wxStaticText(panel, wxID_ANY, "Cursor: not logged in");
    cursorBox->Add(m_cursorStatusText, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 8);
    root->Add(cursorBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

    auto* buttonRow = new wxBoxSizer(wxHORIZONTAL);
    auto* refreshBtn = new wxButton(panel, wxID_ANY, "Refresh Now");
    auto* settingsBtn = new wxButton(panel, wxID_ANY, "Settings");
    buttonRow->Add(refreshBtn, 0, wxRIGHT, 8);
    buttonRow->Add(settingsBtn, 0);
    root->AddStretchSpacer(1);
    root->Add(buttonRow, 0, wxEXPAND | wxALL, 10);
    refreshBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RefreshNow(); });
    settingsBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenSettings(); });

    panel->SetSizerAndFit(root);
    Fit();
}

void MainFrame::OnClose(wxCloseEvent& event) {
    if (m_isExiting) {
        event.Skip();
        return;
    }
    event.Veto();
    Hide();
}

void MainFrame::RestoreFromTray() {
    Show();
    Raise();
    Iconize(false);
}

void MainFrame::ExitApp() {
    m_isExiting = true;
    if (m_trayIcon) m_trayIcon->RemoveIcon();
    Close(true);
}

void MainFrame::OnTimer(wxTimerEvent&) {
    RefreshNow();
}

void MainFrame::RefreshNow() {
    if (m_settings.claudeEnabled && !m_fetchInProgress) {
        if (m_settings.organizationId.empty()) {
            m_fetchInProgress = true;
            SetStatus("Looking up organization...", false);
            m_webSession->FetchJson("/api/organizations", [this](bool ok, const std::string& body, const std::string& error) {
                m_fetchInProgress = false;
                if (!ok) {
                    SetStatus("Failed to look up organization: " + error, true);
                    m_claudeTooltipPart = "Claude: fetch failed";
                    UpdateTrayTooltip();
                    return;
                }
                std::vector<OrganizationInfo> orgs;
                std::string parseError;
                if (!UsageJson::ParseOrganizations(body, orgs, parseError) || orgs.empty()) {
                    Logger::LogError("ParseOrganizations failed: " + parseError + " | body: " + body.substr(0, 500));
                    SetStatus("Failed to look up organization: " + parseError, true);
                    m_claudeTooltipPart = "Claude: fetch failed";
                    UpdateTrayTooltip();
                    return;
                }
                m_settings.organizationId = orgs.front().id;
                SettingsStore::Save(m_settings);
                RefreshNow();
            });
        } else {
            FetchUsageForCurrentOrg();
        }
    }

    if (m_settings.codexEnabled && !m_codexFetchInProgress) {
        m_codexFetchInProgress = true;
        m_codexStatusText->SetLabel("Codex: refreshing...");
        m_codexWebSession->FetchJson("/backend-api/wham/usage", [this](bool ok, const std::string& body, const std::string& error) {
            m_codexFetchInProgress = false;
            UsageData data;
            std::string parseError;
            bool parsed = ok && UsageJson::ParseCodexUsage(body, data, parseError);
            if (!parsed) {
                std::string localError;
                parsed = UsageJson::LoadCodexLocalUsage(data, localError);
                if (parsed) {
                    m_codexStatusText->SetLabel("Codex: using local client usage");
                } else {
                    parseError = ok ? parseError : error + "; local fallback: " + localError;
                }
            }
            if (!parsed) {
                Logger::LogError("ParseCodexUsage failed: " + parseError + " | body: " + body.substr(0, 500));
                m_codexStatusText->SetLabel("Codex: " + parseError);
                m_codexTooltipPart = "Codex: fetch failed";
                UpdateTrayTooltip();
                return;
            }
            ApplyWindow(m_codexMonthlyGauge, m_codexMonthlyLabel, data.fiveHour);
            m_codexStatusText->SetLabel(wxString::Format("Codex: last updated %s", wxDateTime::Now().FormatTime()));
            m_codexTooltipPart = wxString::Format("Codex %.0f%%", data.fiveHour.percent);
            UpdateTrayTooltip();
        });
    }

    if (m_settings.kimiEnabled && !m_kimiFetchInProgress) {
        m_kimiFetchInProgress = true;
        m_kimiStatusText->SetLabel("Kimi: refreshing...");
        m_kimiWebSession->FetchJsonPost(
            "/apiv2/kimi.gateway.membership.v2.MembershipService/GetSubscriptionStats",
            "{\"mute_notice\":false}",
            [this](bool ok, const std::string& body, const std::string& error) {
            m_kimiFetchInProgress = false;
            UsageData data;
            std::string parseError;
            const bool parsed = ok && UsageJson::ParseKimiWebUsage(body, data, parseError);
            if (!parsed) {
                if (ok) Logger::LogError("ParseKimiWebUsage failed: " + parseError + " | body: " + body.substr(0, 500));
                m_kimiStatusText->SetLabel("Kimi: " + (ok ? parseError : error));
                m_kimiTooltipPart = "Kimi: fetch failed";
                UpdateTrayTooltip();
                return;
            }
            ApplyWindow(m_kimiMonthlyGauge, m_kimiMonthlyLabel, data.fiveHour);
            m_kimiStatusText->SetLabel(wxString::Format("Kimi: last updated %s", wxDateTime::Now().FormatTime()));
            m_kimiTooltipPart = wxString::Format("Kimi %.0f%%", data.fiveHour.percent);
            UpdateTrayTooltip();
        });
    }

    if (m_settings.cursorEnabled && !m_cursorFetchInProgress) {
        m_cursorFetchInProgress = true;
        m_cursorStatusText->SetLabel("Cursor: refreshing...");
        m_cursorWebSession->FetchJsonPost(
            "/api/dashboard/get-current-period-usage", "{}",
            [this](bool ok, const std::string& body, const std::string& error) {
            UsageData data;
            std::string parseError;
            bool parsed = ok && UsageJson::ParseCursorUsage(body, data, parseError);
            if (!parsed) {
                // Older flat-request-count Cursor plans don't expose a
                // credit balance from get-current-period-usage; fall back to
                // the legacy per-account request counter.
                m_cursorWebSession->FetchJson("/api/usage",
                    [this, ok, error, parseError](bool legacyOk, const std::string& legacyBody, const std::string& legacyError) {
                    m_cursorFetchInProgress = false;
                    UsageData legacyData;
                    std::string legacyParseError;
                    if (legacyOk && UsageJson::ParseCursorLegacyUsage(legacyBody, legacyData, legacyParseError)) {
                        ApplyWindow(m_cursorMonthlyGauge, m_cursorMonthlyLabel, legacyData.fiveHour);
                        m_cursorStatusText->SetLabel(wxString::Format("Cursor: last updated %s", wxDateTime::Now().FormatTime()));
                        m_cursorTooltipPart = wxString::Format("Cursor %.0f%%", legacyData.fiveHour.percent);
                        UpdateTrayTooltip();
                        return;
                    }
                    const std::string finalError = ok ? parseError : error;
                    Logger::LogError("ParseCursorUsage failed: " + finalError +
                                      "; legacy fallback failed: " + (legacyOk ? legacyParseError : legacyError));
                    m_cursorStatusText->SetLabel("Cursor: " + finalError);
                    m_cursorTooltipPart = "Cursor: fetch failed";
                    UpdateTrayTooltip();
                });
                return;
            }
            m_cursorFetchInProgress = false;
            ApplyWindow(m_cursorMonthlyGauge, m_cursorMonthlyLabel, data.fiveHour);
            m_cursorStatusText->SetLabel(wxString::Format("Cursor: last updated %s", wxDateTime::Now().FormatTime()));
            m_cursorTooltipPart = wxString::Format("Cursor %.0f%%", data.fiveHour.percent);
            UpdateTrayTooltip();
        });
    }
}

void MainFrame::FetchUsageForCurrentOrg() {
    m_fetchInProgress = true;
    SetStatus("Refreshing...", false);

    std::string path = "/api/organizations/" + m_settings.organizationId + "/usage";
    m_webSession->FetchJson(path, [this](bool ok, const std::string& body, const std::string& error) {
        m_fetchInProgress = false;

        if (!ok) {
            SetStatus("Failed to fetch: " + error, true);
            m_claudeTooltipPart = "Claude: fetch failed";
            UpdateTrayTooltip();
            return;
        }

        UsageData data;
        std::string parseError;
        if (!UsageJson::ParseUsage(body, data, parseError)) {
            Logger::LogError("ParseUsage failed: " + parseError + " | body: " + body.substr(0, 500));
            SetStatus("Failed to fetch: " + parseError, true);
            m_claudeTooltipPart = "Claude: fetch failed";
            UpdateTrayTooltip();
            return;
        }

        ApplyWindow(m_fiveHourGauge, m_fiveHourLabel, data.fiveHour);
        ApplyWindow(m_weeklyGauge, m_weeklyLabel, data.sevenDay);
        SetStatus(wxString::Format("Last updated %s", wxDateTime::Now().FormatTime()), false);

        m_claudeTooltipPart = wxString::Format("Claude 5h %.0f%% / Week %.0f%%",
                                                data.fiveHour.percent, data.sevenDay.percent);
        UpdateTrayTooltip();
    });
}

void MainFrame::ApplyWindow(wxGauge* gauge, wxStaticText* label, const UsageWindow& window) {
    if (!window.available) {
        gauge->SetValue(0);
        label->SetLabel("not available");
        label->SetForegroundColour(wxColour(90, 90, 90));
        label->Refresh();
        return;
    }
    int pct = static_cast<int>(window.percent + 0.5);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    gauge->SetValue(pct);
    label->SetLabel(wxString::Format("%d%% used - resets in %s", pct, FormatCountdown(window.resetsAt)));
    label->SetForegroundColour(ColourForPercent(window.percent, m_settings.warnThreshold, m_settings.dangerThreshold));
    label->Refresh();
}

void MainFrame::SetStatus(const wxString& text, bool isError) {
    m_statusText->SetLabel(text);
    m_statusText->SetForegroundColour(isError ? wxColour(200, 30, 30) : wxColour(90, 90, 90));
    m_statusText->Refresh();
}

void MainFrame::OpenSettings() {
    SettingsDialog dlg(this, m_settings, m_webSession, m_codexWebSession, m_kimiWebSession, m_cursorWebSession);
    if (dlg.ShowModal() == wxID_OK) {
        m_settings = dlg.GetSettings();
        SettingsStore::Save(m_settings);
        ApplyProviderVisibility();
        m_timer.Stop();
        m_timer.Start(m_settings.refreshIntervalSeconds * 1000);
        RefreshNow();
    }
}

void MainFrame::ApplyProviderVisibility() {
    if (!m_rootSizer) return;

    m_rootSizer->Show(m_claudeBox, m_settings.claudeEnabled, true);
    m_rootSizer->Show(m_codexBox, m_settings.codexEnabled, true);
    m_rootSizer->Show(m_kimiBox, m_settings.kimiEnabled, true);
    m_rootSizer->Show(m_cursorBox, m_settings.cursorEnabled, true);

    m_panel->Layout();
    m_panel->Fit();
    Fit();

    UpdateTrayTooltip();
}

void MainFrame::UpdateTrayTooltip() {
    if (!m_trayIcon) return;

    std::vector<wxString> parts;
    if (m_settings.claudeEnabled && !m_claudeTooltipPart.empty()) parts.push_back(m_claudeTooltipPart);
    if (m_settings.codexEnabled && !m_codexTooltipPart.empty()) parts.push_back(m_codexTooltipPart);
    if (m_settings.kimiEnabled && !m_kimiTooltipPart.empty()) parts.push_back(m_kimiTooltipPart);
    if (m_settings.cursorEnabled && !m_cursorTooltipPart.empty()) parts.push_back(m_cursorTooltipPart);

    if (parts.empty()) {
        m_trayIcon->UpdateTooltip("AI Usage Tracker");
        return;
    }

    // Windows tray tooltips are capped at 128 chars; keep this well under
    // that even with all four providers enabled.
    wxString tooltip = parts[0];
    for (size_t i = 1; i < parts.size(); ++i) tooltip += " | " + parts[i];
    m_trayIcon->UpdateTooltip(tooltip);
}
