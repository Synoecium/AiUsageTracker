#pragma once

#include <wx/wx.h>
#include <memory>

#include "SettingsStore.h"
#include "UsageClient.h"
#include "TrayIcon.h"
#include "WebSession.h"

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

    void RestoreFromTray();
    void RefreshNow();
    void OpenSettings();
    void ExitApp();

private:
    void BuildUi();
    void OnClose(wxCloseEvent& event);
    void OnTimer(wxTimerEvent& event);

    void FetchUsageForCurrentOrg();
    void ApplyWindow(wxGauge* gauge, wxStaticText* label, const UsageWindow& window);
    void SetStatus(const wxString& text, bool isError);

    AppSettings m_settings;
    bool m_isExiting = false;
    bool m_fetchInProgress = false;
    bool m_codexFetchInProgress = false;
    bool m_kimiFetchInProgress = false;
    bool m_cursorFetchInProgress = false;

    wxGauge* m_fiveHourGauge = nullptr;
    wxStaticText* m_fiveHourLabel = nullptr;
    wxGauge* m_weeklyGauge = nullptr;
    wxStaticText* m_weeklyLabel = nullptr;
    wxStaticText* m_statusText = nullptr;
    wxGauge* m_codexMonthlyGauge = nullptr;
    wxStaticText* m_codexMonthlyLabel = nullptr;
    wxStaticText* m_codexStatusText = nullptr;
    wxGauge* m_kimiMonthlyGauge = nullptr;
    wxStaticText* m_kimiMonthlyLabel = nullptr;
    wxStaticText* m_kimiStatusText = nullptr;
    wxGauge* m_cursorMonthlyGauge = nullptr;
    wxStaticText* m_cursorMonthlyLabel = nullptr;
    wxStaticText* m_cursorStatusText = nullptr;

    // Each provider's static box sizer, so a disabled provider's whole
    // block (box, gauges, labels) can be hidden from the main window.
    wxPanel* m_panel = nullptr;
    wxSizer* m_rootSizer = nullptr;
    wxStaticBoxSizer* m_claudeBox = nullptr;
    wxStaticBoxSizer* m_codexBox = nullptr;
    wxStaticBoxSizer* m_kimiBox = nullptr;
    wxStaticBoxSizer* m_cursorBox = nullptr;

    void ApplyProviderVisibility();

    // Latest one-line summary per provider (e.g. "Claude 5h 42% - Week 18%"),
    // used to compose the tray icon's combined tooltip. Empty until that
    // provider's first fetch completes.
    wxString m_claudeTooltipPart;
    wxString m_codexTooltipPart;
    wxString m_kimiTooltipPart;
    wxString m_cursorTooltipPart;
    void UpdateTrayTooltip();

    wxTimer m_timer;
    std::unique_ptr<TrayIcon> m_trayIcon;

    // Raw + explicit Destroy(): WebSession is a wxFrame, and wx windows must
    // be torn down via Destroy() (which defers to let pending events drain)
    // rather than a smart pointer's plain delete.
    WebSession* m_webSession = nullptr;
    WebSession* m_codexWebSession = nullptr;
    WebSession* m_kimiWebSession = nullptr;
    WebSession* m_cursorWebSession = nullptr;
};
