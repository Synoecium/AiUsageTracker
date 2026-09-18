#pragma once

#include <wx/taskbar.h>

class MainFrame;

class TrayIcon : public wxTaskBarIcon {
public:
    explicit TrayIcon(MainFrame* frame);

    // Updates the hover tooltip, e.g. "5h: 42% | Week: 18%".
    void UpdateTooltip(const wxString& text);

private:
    wxMenu* CreatePopupMenu() override;
    void OnLeftDClick(wxTaskBarIconEvent& event);
    void OnShow(wxCommandEvent& event);
    void OnRefresh(wxCommandEvent& event);
    void OnSettings(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);

    MainFrame* m_frame;
    wxIcon m_icon;
};
