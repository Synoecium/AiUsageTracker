#include "SettingsDialog.h"
#include "Logger.h"

#include <wx/utils.h>

SettingsDialog::SettingsDialog(wxWindow* parent, const AppSettings& initial,
                               WebSession* claudeSession, WebSession* codexSession, WebSession* kimiSession,
                               WebSession* cursorSession)
    : wxDialog(parent, wxID_ANY, "Settings", wxDefaultPosition, wxSize(440, 800)),
      m_webSession(claudeSession), m_codexSession(codexSession), m_kimiSession(kimiSession),
      m_cursorSession(cursorSession), m_settings(initial) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* claudeBox = new wxStaticBoxSizer(wxVERTICAL, this, "Claude settings");
    m_claudeEnabledCtrl = new wxCheckBox(this, wxID_ANY, "Show Claude in main window");
    m_claudeEnabledCtrl->SetValue(m_settings.claudeEnabled);
    claudeBox->Add(m_claudeEnabledCtrl, 0, wxALL, 8);
    auto* instructions = new wxStaticText(this, wxID_ANY,
        "This app tracks your Claude Pro/Max usage via claude.ai's own\n"
        "(undocumented) usage page - there is no public API for this.\n\n"
        "Click \"Log in to claude.ai\" below and sign in normally in the\n"
        "window that opens. Your session is remembered afterwards, just\n"
        "like a regular browser.");
    claudeBox->Add(instructions, 0, wxALL, 8);

    auto* loginBtn = new wxButton(this, wxID_ANY, "Log in to claude.ai...");
    claudeBox->Add(loginBtn, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* fetchBtn = new wxButton(this, wxID_ANY, "Fetch Organizations");
    claudeBox->Add(fetchBtn, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    claudeBox->Add(new wxStaticText(this, wxID_ANY, "Organization:"), 0, wxLEFT | wxRIGHT, 8);
    m_orgChoice = new wxChoice(this, wxID_ANY);
    if (!m_settings.organizationId.empty()) {
        m_orgChoice->Append(m_settings.organizationId);
        m_orgChoice->SetSelection(0);
    }
    claudeBox->Add(m_orgChoice, 0, wxEXPAND | wxALL, 8);

    m_statusText = new wxStaticText(this, wxID_ANY, "");
    claudeBox->Add(m_statusText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* gridSizer = new wxFlexGridSizer(2, 5, 10);
    gridSizer->Add(new wxStaticText(this, wxID_ANY, "Refresh every (seconds):"), 0, wxALIGN_CENTER_VERTICAL);
    m_refreshIntervalCtrl = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                            wxSP_ARROW_KEYS, 15, 3600, m_settings.refreshIntervalSeconds);
    gridSizer->Add(m_refreshIntervalCtrl);

    gridSizer->Add(new wxStaticText(this, wxID_ANY, "Warning threshold (%):"), 0, wxALIGN_CENTER_VERTICAL);
    m_warnThresholdCtrl = new wxSpinCtrlDouble(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                                wxSP_ARROW_KEYS, 0, 100, m_settings.warnThreshold, 1);
    gridSizer->Add(m_warnThresholdCtrl);

    gridSizer->Add(new wxStaticText(this, wxID_ANY, "Danger threshold (%):"), 0, wxALIGN_CENTER_VERTICAL);
    m_dangerThresholdCtrl = new wxSpinCtrlDouble(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                                  wxSP_ARROW_KEYS, 0, 100, m_settings.dangerThreshold, 1);
    gridSizer->Add(m_dangerThresholdCtrl);
    claudeBox->Add(gridSizer, 0, wxALL, 8);
    root->Add(claudeBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto* codexBox = new wxStaticBoxSizer(wxVERTICAL, this, "Codex settings");
    m_codexEnabledCtrl = new wxCheckBox(this, wxID_ANY, "Show Codex in main window");
    m_codexEnabledCtrl->SetValue(m_settings.codexEnabled);
    codexBox->Add(m_codexEnabledCtrl, 0, wxALL, 8);
    codexBox->Add(new wxStaticText(this, wxID_ANY,
        "Sign in to ChatGPT to read Codex usage from the local Codex client session."),
        0, wxALL, 8);
    auto* codexLoginBtn = new wxButton(this, wxID_ANY, "Log in to ChatGPT for Codex...");
    codexBox->Add(codexLoginBtn, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    root->Add(codexBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto* kimiBox = new wxStaticBoxSizer(wxVERTICAL, this, "Kimi settings");
    m_kimiEnabledCtrl = new wxCheckBox(this, wxID_ANY, "Show Kimi in main window");
    m_kimiEnabledCtrl->SetValue(m_settings.kimiEnabled);
    kimiBox->Add(m_kimiEnabledCtrl, 0, wxALL, 8);
    kimiBox->Add(new wxStaticText(this, wxID_ANY,
        "Sign in to Kimi in the window below.\n"
        "The app reads your subscription limits from Kimi."),
        0, wxALL, 8);
    auto* kimiLoginBtn = new wxButton(this, wxID_ANY, "Open Kimi.ai login...");
    kimiBox->Add(kimiLoginBtn, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    root->Add(kimiBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto* cursorBox = new wxStaticBoxSizer(wxVERTICAL, this, "Cursor settings");
    m_cursorEnabledCtrl = new wxCheckBox(this, wxID_ANY, "Show Cursor in main window");
    m_cursorEnabledCtrl->SetValue(m_settings.cursorEnabled);
    cursorBox->Add(m_cursorEnabledCtrl, 0, wxALL, 8);
    cursorBox->Add(new wxStaticText(this, wxID_ANY,
        "Sign in to Cursor in the window below.\n"
        "The app reads your monthly usage balance from cursor.com."),
        0, wxALL, 8);
    auto* cursorLoginBtn = new wxButton(this, wxID_ANY, "Log in to cursor.com...");
    cursorBox->Add(cursorLoginBtn, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    root->Add(cursorBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    root->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);

    SetSizerAndFit(root);

    loginBtn->Bind(wxEVT_BUTTON, &SettingsDialog::OnLogin, this);
    codexLoginBtn->Bind(wxEVT_BUTTON, &SettingsDialog::OnCodexLogin, this);
    kimiLoginBtn->Bind(wxEVT_BUTTON, &SettingsDialog::OnKimiLogin, this);
    cursorLoginBtn->Bind(wxEVT_BUTTON, &SettingsDialog::OnCursorLogin, this);
    fetchBtn->Bind(wxEVT_BUTTON, &SettingsDialog::OnFetchOrganizations, this);
    Bind(wxEVT_BUTTON, &SettingsDialog::OnOk, this, wxID_OK);
}

void SettingsDialog::OnLogin(wxCommandEvent&) {
    m_webSession->ShowLogin();
}

void SettingsDialog::OnCodexLogin(wxCommandEvent&) {
    m_codexSession->ShowLogin();
}

void SettingsDialog::OnKimiLogin(wxCommandEvent&) {
    m_kimiSession->ShowLogin();
}

void SettingsDialog::OnCursorLogin(wxCommandEvent&) {
    m_cursorSession->ShowLogin();
}

void SettingsDialog::OnFetchOrganizations(wxCommandEvent&) {
    m_statusText->SetLabel("Fetching...");
    m_webSession->FetchJson("/api/organizations", [this](bool ok, const std::string& body, const std::string& error) {
        if (!ok) {
            m_statusText->SetLabel("Failed: " + error);
            return;
        }

        std::string parseError;
        m_orgs.clear();
        if (!UsageJson::ParseOrganizations(body, m_orgs, parseError)) {
            Logger::LogError("ParseOrganizations failed: " + parseError + " | body: " + body.substr(0, 500));
            m_statusText->SetLabel("Failed: " + parseError);
            return;
        }

        m_orgChoice->Clear();
        for (const auto& org : m_orgs) {
            m_orgChoice->Append(org.name.empty() ? org.id : org.name);
        }
        m_orgChoice->SetSelection(0);
        m_statusText->SetLabel(wxString::Format("Found %zu organization(s).", m_orgs.size()));
    });
}

void SettingsDialog::OnOk(wxCommandEvent& event) {
    int sel = m_orgChoice->GetSelection();
    if (!m_orgs.empty() && sel != wxNOT_FOUND) {
        m_settings.organizationId = m_orgs[sel].id;
    }
    // If the user never clicked "Fetch Organizations" (e.g. keeping an
    // already-saved org), m_settings.organizationId keeps its initial value.

    m_settings.refreshIntervalSeconds = m_refreshIntervalCtrl->GetValue();
    m_settings.warnThreshold = m_warnThresholdCtrl->GetValue();
    m_settings.dangerThreshold = m_dangerThresholdCtrl->GetValue();

    m_settings.claudeEnabled = m_claudeEnabledCtrl->GetValue();
    m_settings.codexEnabled = m_codexEnabledCtrl->GetValue();
    m_settings.kimiEnabled = m_kimiEnabledCtrl->GetValue();
    m_settings.cursorEnabled = m_cursorEnabledCtrl->GetValue();

    if (m_settings.claudeEnabled && m_settings.organizationId.empty()) {
        m_statusText->SetLabel("Please log in and fetch organizations first.");
        return;
    }

    event.Skip();
}

AppSettings SettingsDialog::GetSettings() const {
    return m_settings;
}
