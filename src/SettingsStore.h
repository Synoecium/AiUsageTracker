#pragma once

#include <string>

// Persisted app settings. There is no credential to protect here anymore -
// login/session cookies are owned entirely by the embedded WebView2 browser
// profile (see WebSession), which manages its own secure storage.
struct AppSettings {
    std::string organizationId;
    std::string kimiApiKey;
    int refreshIntervalSeconds = 60;
    double warnThreshold = 75.0;
    double dangerThreshold = 90.0;

    // Per-provider visibility: whether the provider's section is shown in
    // the main window and whether it is polled for usage at all.
    bool claudeEnabled = true;
    bool codexEnabled = true;
    bool kimiEnabled = true;
    bool cursorEnabled = true;
};

class SettingsStore {
public:
    // Returns %APPDATA%\ClaudeUsageTracker\settings.json
    static std::wstring GetSettingsPath();

    // Loads settings from disk. Returns false if no file exists yet (out is
    // left default-constructed).
    static bool Load(AppSettings& out);

    // Saves settings to disk. Returns false on I/O failure.
    static bool Save(const AppSettings& settings);
};
