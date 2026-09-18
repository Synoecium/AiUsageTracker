#pragma once

#include <string>

// Minimal append-only error log at %APPDATA%\ClaudeUsageTracker\log.txt, so
// failures (fetch/script/parse errors) survive after the dialog that showed
// them closes and can be inspected or shared for debugging.
namespace Logger {

std::wstring GetLogPath();
void LogError(const std::string& message);

} // namespace Logger
