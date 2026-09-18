#pragma once

#include <wx/wx.h>
#include <wx/spinctrl.h>
#include <vector>

#include "SettingsStore.h"
#include "UsageClient.h"
#include "WebSession.h"

// Lets the user log into claude.ai (via the embedded browser), pick which
// organization to track (if they have more than one), and tune the refresh
// interval and warning/danger color thresholds.
class SettingsDialog : public wxDialog {
public:
    SettingsDialog(wxWindow* parent, const AppSettings& initial,
                   WebSession* claudeSession, WebSession* codexSession, WebSession* kimiSession,
                   WebSession* cursorSession);

    AppSettings GetSettings() const;

private:
    void OnLogin(wxCommandEvent& event);
    void OnCodexLogin(wxCommandEvent& event);
    void OnKimiLogin(wxCommandEvent& event);
    void OnCursorLogin(wxCommandEvent& event);
    void OnFetchOrganizations(wxCommandEvent& event);
    void OnOk(wxCommandEvent& event);

    WebSession* m_webSession;
    WebSession* m_codexSession;
    WebSession* m_kimiSession;
    WebSession* m_cursorSession;
    wxChoice* m_orgChoice = nullptr;
    wxSpinCtrl* m_refreshIntervalCtrl = nullptr;
    wxSpinCtrlDouble* m_warnThresholdCtrl = nullptr;
    wxSpinCtrlDouble* m_dangerThresholdCtrl = nullptr;
    wxStaticText* m_statusText = nullptr;

    wxCheckBox* m_claudeEnabledCtrl = nullptr;
    wxCheckBox* m_codexEnabledCtrl = nullptr;
    wxCheckBox* m_kimiEnabledCtrl = nullptr;
    wxCheckBox* m_cursorEnabledCtrl = nullptr;

    std::vector<OrganizationInfo> m_orgs;
    AppSettings m_settings;
};
