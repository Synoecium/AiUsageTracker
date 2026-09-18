#include "Logger.h"

#include <windows.h>
#include <cstdlib>
#include <fstream>
#include <ctime>

std::wstring Logger::GetLogPath() {
    std::wstring dir;
    wchar_t* appData = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&appData, &len, L"APPDATA") == 0 && appData) {
        dir = appData;
        free(appData);
    }
    dir += L"\\ClaudeUsageTracker";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\log.txt";
}

void Logger::LogError(const std::string& message) {
    std::ofstream file(GetLogPath(), std::ios::binary | std::ios::app);
    if (!file.is_open()) return;

    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);

    file << "[" << timestamp << "] " << message << "\n";
}
