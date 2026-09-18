#include <wx/wx.h>
#include <wx/cmdline.h>

#include "MainFrame.h"

class ClaudeUsageApp : public wxApp {
public:
    void OnInitCmdLine(wxCmdLineParser& parser) override {
        wxApp::OnInitCmdLine(parser);
        parser.AddSwitch("m", "minimized", "Start minimized to the system tray");
    }

    bool OnCmdLineParsed(wxCmdLineParser& parser) override {
        if (!wxApp::OnCmdLineParsed(parser)) return false;
        m_startMinimized = parser.Found("minimized");
        return true;
    }

    bool OnInit() override {
        if (!wxApp::OnInit()) return false;
        auto* frame = new MainFrame();
        if (!m_startMinimized) frame->Show();
        return true;
    }

private:
    bool m_startMinimized = false;
};

wxIMPLEMENT_APP(ClaudeUsageApp);
