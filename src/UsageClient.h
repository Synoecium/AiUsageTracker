#pragma once

#include <string>
#include <vector>
#include <ctime>

// One usage window (either the 5-hour session or the 7-day/weekly window).
struct UsageWindow {
    bool available = false;
    double percent = 0.0;   // 0-100
    time_t resetsAt = 0;    // UTC unix timestamp; 0 if unknown
    long long durationSeconds = 0; // window length; 0 if unknown
};

struct UsageData {
    UsageWindow fiveHour;
    UsageWindow sevenDay;
};

struct OrganizationInfo {
    std::string id;
    std::string name;
};

// Parses claude.ai's internal (undocumented) usage/organizations JSON
// responses. The actual HTTP fetching happens through an embedded real
// browser (see WebSession) so requests carry a genuine browser TLS
// fingerprint and cookie jar - Cloudflare's bot protection blocks plain
// HTTP clients even with correct cookies/headers.
namespace UsageJson {

bool ParseOrganizations(const std::string& json, std::vector<OrganizationInfo>& out, std::string& error);
bool ParseUsage(const std::string& json, UsageData& out, std::string& error);
bool ParseCodexUsage(const std::string& json, UsageData& out, std::string& error);
bool LoadCodexLocalUsage(UsageData& out, std::string& error);
bool ParseKimiUsage(const std::string& json, UsageData& out, std::string& error);
bool ParseKimiWebUsage(const std::string& json, UsageData& out, std::string& error);
bool LoadKimiCredentials(std::string& token, std::string& error);
bool ParseCursorUsage(const std::string& json, UsageData& out, std::string& error);
bool ParseCursorLegacyUsage(const std::string& json, UsageData& out, std::string& error);

} // namespace UsageJson
