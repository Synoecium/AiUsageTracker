#include "TrayIcon.h"
#include "MainFrame.h"

#include <wx/artprov.h>

namespace {
enum { ID_SHOW = wxID_HIGHEST + 1, ID_REFRESH, ID_SETTINGS, ID_EXIT };
}

TrayIcon::TrayIcon(MainFrame* frame) : m_frame(frame) {
    m_icon = wxArtProvider::GetIcon(wxART_INFORMATION, wxART_OTHER, wxSize(16, 16));
    SetIcon(m_icon, "Claude Usage Tracker");
    Bind(wxEVT_TASKBAR_LEFT_DCLICK, &TrayIcon::OnLeftDClick, this);
    Bind(wxEVT_MENU, &TrayIcon::OnShow, this, ID_SHOW);
    Bind(wxEVT_MENU, &TrayIcon::OnRefresh, this, ID_REFRESH);
    Bind(wxEVT_MENU, &TrayIcon::OnSettings, this, ID_SETTINGS);
    Bind(wxEVT_MENU, &TrayIcon::OnExit, this, ID_EXIT);
}

void TrayIcon::UpdateTooltip(const wxString& text) {
    SetIcon(m_icon, text);
}

wxMenu* TrayIcon::CreatePopupMenu() {
    auto* menu = new wxMenu();
    menu->Append(ID_SHOW, "Show");
    menu->Append(ID_REFRESH, "Refresh Now");
    menu->Append(ID_SETTINGS, "Settings...");
    menu->AppendSeparator();
    menu->Append(ID_EXIT, "Exit");
    return menu;
}

void TrayIcon::OnLeftDClick(wxTaskBarIconEvent&) {
    m_frame->RestoreFromTray();
}

void TrayIcon::OnShow(wxCommandEvent&) {
    m_frame->RestoreFromTray();
}

void TrayIcon::OnRefresh(wxCommandEvent&) {
    m_frame->RefreshNow();
}

void TrayIcon::OnSettings(wxCommandEvent&) {
    m_frame->RestoreFromTray();
    m_frame->OpenSettings();
}

void TrayIcon::OnExit(wxCommandEvent&) {
    m_frame->ExitApp();
}
