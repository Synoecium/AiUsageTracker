#include "SettingsStore.h"

#include <windows.h>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::wstring SettingsStore::GetSettingsPath() {
    std::wstring dir;
    wchar_t* appData = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&appData, &len, L"APPDATA") == 0 && appData) {
        dir = appData;
        free(appData);
    }
    dir += L"\\ClaudeUsageTracker";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.json";
}

bool SettingsStore::Load(AppSettings& out) {
    std::ifstream file(GetSettingsPath(), std::ios::binary);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();

    json j;
    try {
        j = json::parse(buffer.str());
    } catch (const json::parse_error&) {
        return false;
    }

    out.organizationId = j.value("organization_id", "");
    out.kimiApiKey = j.value("kimi_api_key", "");
    out.refreshIntervalSeconds = j.value("refresh_interval_seconds", 60);
    out.warnThreshold = j.value("warn_threshold", 75.0);
    out.dangerThreshold = j.value("danger_threshold", 90.0);
    out.claudeEnabled = j.value("claude_enabled", true);
    out.codexEnabled = j.value("codex_enabled", true);
    out.kimiEnabled = j.value("kimi_enabled", true);
    out.cursorEnabled = j.value("cursor_enabled", true);
    return true;
}

bool SettingsStore::Save(const AppSettings& settings) {
    json j;
    j["organization_id"] = settings.organizationId;
    j["kimi_api_key"] = settings.kimiApiKey;
    j["refresh_interval_seconds"] = settings.refreshIntervalSeconds;
    j["warn_threshold"] = settings.warnThreshold;
    j["danger_threshold"] = settings.dangerThreshold;
    j["claude_enabled"] = settings.claudeEnabled;
    j["codex_enabled"] = settings.codexEnabled;
    j["kimi_enabled"] = settings.kimiEnabled;
    j["cursor_enabled"] = settings.cursorEnabled;

    std::ofstream file(GetSettingsPath(), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    file << j.dump(2);
    return true;
}
