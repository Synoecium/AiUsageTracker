#include "UsageClient.h"

#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

namespace {

// json::value(key, default) eagerly type-checks against the *default's*
// type even when the key is present with a different type (e.g. claude.ai's
// organizations response has a numeric "id" alongside a string "uuid") -
// that throws type_error.302 instead of just giving us a value back. Read
// fields defensively instead, coercing to string regardless of the actual
// JSON type.
std::string GetStringField(const json& obj, const char* key) {
    if (!obj.is_object() || !obj.contains(key) || obj[key].is_null()) return {};
    const json& v = obj[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float()) return std::to_string(v.get<double>());
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    return v.dump();
}

double GetNumberField(const json& obj, const char* key, double fallback) {
    if (!obj.is_object() || !obj.contains(key) || obj[key].is_null()) return fallback;
    const json& v = obj[key];
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) {
        try { return std::stod(v.get<std::string>()); } catch (...) { return fallback; }
    }
    return fallback;
}

std::string FindToken(const json& value) {
    if (value.is_object()) {
        for (const char* key : {"access_token", "accessToken", "token", "api_key", "apiKey"}) {
            const std::string candidate = GetStringField(value, key);
            if (!candidate.empty()) return candidate;
        }
        for (const auto& item : value.items()) {
            const std::string candidate = FindToken(item.value());
            if (!candidate.empty()) return candidate;
        }
    } else if (value.is_array()) {
        for (const auto& item : value) {
            const std::string candidate = FindToken(item);
            if (!candidate.empty()) return candidate;
        }
    }
    return {};
}

// Parses "YYYY-MM-DDTHH:MM:SS[.fff][Z]" (UTC) into a unix timestamp.
time_t ParseIso8601Utc(const std::string& s) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 6) {
        return 0;
    }
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    return _mkgmtime(&t);
}

UsageWindow ParseWindow(const json& j) {
    UsageWindow w;
    if (j.is_object()) {
        w.available = true;
        // utilization is already a 0-100 percentage (e.g. 1 means "1%"), not
        // a 0-1 fraction - confirmed against the real API response.
        w.percent = GetNumberField(j, "utilization", 0.0);
        w.resetsAt = ParseIso8601Utc(GetStringField(j, "resets_at"));
    }
    return w;
}

UsageWindow ParseCodexWindow(const json& j) {
    UsageWindow w;
    if (!j.is_object() || j.empty()) return w;
    w.available = true;
    w.percent = GetNumberField(j, "used_percent", GetNumberField(j, "utilization", 0.0));
    w.durationSeconds = static_cast<long long>(GetNumberField(j, "limit_window_seconds",
        GetNumberField(j, "window_minutes", 0.0) * 60));
    if (j.contains("reset_at")) {
        if (j["reset_at"].is_number()) w.resetsAt = static_cast<time_t>(j["reset_at"].get<double>());
        else w.resetsAt = ParseIso8601Utc(GetStringField(j, "reset_at"));
    }
    if (w.resetsAt <= 0 && j.contains("resets_at")) {
        if (j["resets_at"].is_number()) w.resetsAt = static_cast<time_t>(j["resets_at"].get<double>());
        else w.resetsAt = ParseIso8601Utc(GetStringField(j, "resets_at"));
    }
    if (w.resetsAt <= 0) {
        const double seconds = GetNumberField(j, "reset_after_seconds", 0.0);
        if (seconds > 0) w.resetsAt = std::time(nullptr) + static_cast<time_t>(seconds);
    }
    return w;
}

UsageWindow ParseKimiLimit(const json& j) {
    UsageWindow w;
    if (!j.is_object()) return w;
    const double limit = GetNumberField(j, "limit", GetNumberField(j, "limit_amount", 0.0));
    double used = GetNumberField(j, "used", GetNumberField(j, "used_amount", -1.0));
    if (used < 0.0) {
        const double remaining = GetNumberField(j, "remaining", -1.0);
        if (remaining >= 0.0) used = limit - remaining;
    }
    if (used < 0.0) used = 0.0;
    if (limit <= 0.0) return w;
    w.available = true;
    w.percent = (used / limit) * 100.0;
    for (const char* key : {"reset_at", "resetTime", "reset_time"}) {
        if (!j.contains(key)) continue;
        if (j[key].is_number()) w.resetsAt = static_cast<time_t>(j[key].get<double>());
        else w.resetsAt = ParseIso8601Utc(GetStringField(j, key));
        if (w.resetsAt > 0) break;
    }
    if (w.resetsAt <= 0) {
        const double resetIn = GetNumberField(j, "reset_in", 0.0);
        if (resetIn > 0.0) w.resetsAt = std::time(nullptr) + static_cast<time_t>(resetIn);
    }
    return w;
}

UsageWindow ParseKimiWebWindow(const json& j) {
    UsageWindow w;
    if (!j.is_object()) return w;
    double ratio = GetNumberField(j, "ratio", -1.0);
    if (ratio < 0.0) ratio = GetNumberField(j, "used_ratio", -1.0);
    if (ratio < 0.0) ratio = GetNumberField(j, "percentage", -1.0);
    if (ratio < 0.0) ratio = GetNumberField(j, "amountUsedRatio", -1.0);
    if (ratio < 0.0) ratio = GetNumberField(j, "amount_used_ratio", -1.0);
    if (ratio < 0.0) return w;
    w.available = true;
    w.percent = ratio <= 1.0 ? ratio * 100.0 : ratio;
    for (const char* key : {"resetTime", "reset_time", "reset_at", "expireTime", "expire_time"}) {
        if (!j.contains(key)) continue;
        if (j[key].is_number()) {
            w.resetsAt = static_cast<time_t>(j[key].get<double>());
        } else if (j[key].is_object()) {
            w.resetsAt = static_cast<time_t>(GetNumberField(j[key], "seconds", 0.0));
        } else {
            w.resetsAt = ParseIso8601Utc(GetStringField(j, key));
        }
        if (w.resetsAt > 0) break;
    }
    return w;
}

// Cursor's cookie-authenticated "current period usage" endpoint
// (POST /api/dashboard/get-current-period-usage) reports a single rolling
// monthly credit balance in cents, not a 5-hour/weekly split - surfaced as
// the sole "monthly" window, same as Kimi's subscription-balance fallback.
UsageWindow ParseCursorWindow(const json& j) {
    UsageWindow w;
    if (!j.is_object()) return w;
    const json& planUsage = j.value("planUsage", json::object());
    if (!planUsage.is_object()) return w;

    const double limit = GetNumberField(planUsage, "limit", 0.0);
    double percent = GetNumberField(planUsage, "totalPercentUsed", -1.0);
    if (percent < 0.0 && limit > 0.0) {
        double used = GetNumberField(planUsage, "used", -1.0);
        if (used < 0.0) {
            const double remaining = GetNumberField(planUsage, "remaining", -1.0);
            if (remaining >= 0.0) used = limit - remaining;
        }
        if (used >= 0.0) percent = (used / limit) * 100.0;
    }
    if (percent < 0.0) return w;

    w.available = true;
    w.percent = percent;
    const std::string endMs = GetStringField(j, "billingCycleEnd");
    if (!endMs.empty()) {
        try { w.resetsAt = static_cast<time_t>(std::stoll(endMs) / 1000); } catch (...) {}
    }
    return w;
}

// Older Cursor plans (the flat "N premium requests per month" tier) instead
// expose a simple request counter via GET /api/usage.
UsageWindow ParseCursorLegacyWindow(const json& j) {
    UsageWindow w;
    if (!j.is_object()) return w;
    const double used = GetNumberField(j, "numRequests", -1.0);
    const double max = GetNumberField(j, "maxRequestUsage", 0.0);
    if (used < 0.0 || max <= 0.0) return w;
    w.available = true;
    w.percent = (used / max) * 100.0;
    return w;
}

} // namespace

bool UsageJson::ParseOrganizations(const std::string& body, std::vector<OrganizationInfo>& out, std::string& error) {
    try {
        json j = json::parse(body);
        for (const auto& org : j) {
            OrganizationInfo info;
            info.id = GetStringField(org, "uuid");
            if (info.id.empty()) info.id = GetStringField(org, "id");
            info.name = GetStringField(org, "name");
            if (info.name.empty()) info.name = info.id;
            if (!info.id.empty()) out.push_back(info);
        }
    } catch (const json::exception& e) {
        error = std::string("Failed to parse organizations response: ") + e.what();
        return false;
    }

    if (out.empty()) {
        error = "No organizations found";
        return false;
    }
    return true;
}

bool UsageJson::ParseUsage(const std::string& body, UsageData& out, std::string& error) {
    try {
        json j = json::parse(body);
        out.fiveHour = ParseWindow(j.value("five_hour", json::object()));
        out.sevenDay = ParseWindow(j.value("seven_day", json::object()));
    } catch (const json::exception& e) {
        error = std::string("Failed to parse usage response: ") + e.what();
        return false;
    }
    return true;
}

bool UsageJson::ParseCodexUsage(const std::string& body, UsageData& out, std::string& error) {
    try {
        const json j = json::parse(body);
        if (!j.is_object()) {
            error = "Codex usage response was not an object";
            return false;
        }
        const json* rateLimit = &j;
        if (j.contains("rate_limit")) rateLimit = &j["rate_limit"];
        else if (j.contains("rate_limits")) rateLimit = &j["rate_limits"];
        if (!rateLimit->is_object()) {
            error = "Codex usage response did not contain a rate limit object";
            return false;
        }
        const bool localShape = rateLimit->contains("primary") || rateLimit->contains("secondary");
        out.fiveHour = ParseCodexWindow(rateLimit->value(localShape ? "primary" : "primary_window", json::object()));
        out.sevenDay = ParseCodexWindow(rateLimit->value(localShape ? "secondary" : "secondary_window", json::object()));
        if ((!localShape && !rateLimit->contains("primary_window") && !rateLimit->contains("secondary_window")) ||
            (localShape && !rateLimit->contains("primary") && !rateLimit->contains("secondary"))) {
            error = "Codex usage response did not contain usage windows";
            return false;
        }
    } catch (const json::exception& e) {
        error = std::string("Failed to parse Codex usage response: ") + e.what();
        return false;
    }
    return true;
}

bool UsageJson::LoadCodexLocalUsage(UsageData& out, std::string& error) {
    const char* userProfile = std::getenv("USERPROFILE");
    if (!userProfile || !*userProfile) {
        error = "USERPROFILE is not available";
        return false;
    }

    const std::filesystem::path sessionRoot = std::filesystem::path(userProfile) / ".codex" / "sessions";
    if (!std::filesystem::exists(sessionRoot)) {
        error = "Codex local session data was not found";
        return false;
    }

    std::filesystem::path newestFile;
    std::filesystem::file_time_type newestTime{};
    for (const auto& entry : std::filesystem::recursive_directory_iterator(sessionRoot)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".jsonl") continue;
        const auto time = entry.last_write_time();
        if (newestFile.empty() || time > newestTime) {
            newestFile = entry.path();
            newestTime = time;
        }
    }
    if (newestFile.empty()) {
        error = "No Codex session files were found";
        return false;
    }

    std::ifstream file(newestFile, std::ios::binary);
    std::string line, latest;
    while (std::getline(file, line)) {
        if (line.find("\"rate_limits\"") != std::string::npos &&
            line.find("\"limit_id\":\"codex\"") != std::string::npos) {
            latest = line;
        }
    }
    if (latest.empty()) {
        error = "The latest Codex session has no usage snapshot yet";
        return false;
    }
    try {
        const json record = json::parse(latest);
        if (record.contains("payload") && record["payload"].is_object() &&
            record["payload"].contains("rate_limits")) {
            return ParseCodexUsage(record["payload"].dump(), out, error);
        }
        return ParseCodexUsage(latest, out, error);
    } catch (const json::exception& e) {
        error = std::string("Failed to parse local Codex session record: ") + e.what();
        return false;
    }
}

bool UsageJson::ParseKimiUsage(const std::string& body, UsageData& out, std::string& error) {
    try {
        const json root = json::parse(body);
        out = UsageData{};
        const json* payload = &root;
        if (root.is_object() && root.contains("data") && root["data"].is_object()) payload = &root["data"];
        if (!payload->is_object()) {
            error = "Kimi usage response did not contain an object";
            return false;
        }
        if (payload->contains("kind") && payload->value("kind", "ok") != "ok") {
            error = payload->value("message", "Kimi usage is unavailable");
            return false;
        }
        const json* rows = nullptr;
        if (payload->contains("limits") && (*payload)["limits"].is_array()) rows = &(*payload)["limits"];
        else if (payload->contains("data") && (*payload)["data"].is_array()) rows = &(*payload)["data"];
        if (!rows) {
            error = "Kimi usage response did not contain quota limits";
            return false;
        }
        for (const auto& row : *rows) {
            const json window = row.value("window", json::object());
            const json detail = row.contains("detail") && row["detail"].is_object() ? row["detail"] : row;
            std::string unit = GetStringField(window, "unit").empty()
                ? GetStringField(window, "timeUnit") : GetStringField(window, "unit");
            std::transform(unit.begin(), unit.end(), unit.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const double duration = GetNumberField(window, "duration", GetNumberField(row, "duration", 0.0));
            const std::string name = GetStringField(detail, "name").empty()
                ? GetStringField(detail, "title") : GetStringField(detail, "name");
            const UsageWindow parsed = ParseKimiLimit(detail);
            if (!parsed.available) continue;
            if (!out.fiveHour.available &&
                ((unit == "hour" && duration <= 5.0) || name.find("5") != std::string::npos)) {
                out.fiveHour = parsed;
            } else if (!out.sevenDay.available &&
                       ((unit == "week") || (unit == "day" && duration >= 6.0) ||
                        name.find("week") != std::string::npos || name.find("7") != std::string::npos)) {
                out.sevenDay = parsed;
            }
        }
        if (!out.fiveHour.available && !out.sevenDay.available) {
            error = "Kimi usage response contained no usable quota limits";
            return false;
        }
    } catch (const json::exception& e) {
        error = std::string("Failed to parse Kimi usage response: ") + e.what();
        return false;
    }
    return true;
}

bool UsageJson::ParseKimiWebUsage(const std::string& body, UsageData& out, std::string& error) {
    try {
        const json root = json::parse(body);
        out = UsageData{};
        const json* payload = &root;
        if (root.is_object() && root.contains("data") && root["data"].is_object()) payload = &root["data"];
        if (!payload->is_object()) {
            error = "Kimi web usage response was not an object";
            return false;
        }
        const auto findObject = [&](const char* a, const char* b) -> const json* {
            if (payload->contains(a) && (*payload)[a].is_object()) return &(*payload)[a];
            if (payload->contains(b) && (*payload)[b].is_object()) return &(*payload)[b];
            return nullptr;
        };
        const json* fiveHour = findObject("ratelimit5h", "ratelimit_5h");
        const json* weekly = findObject("ratelimit7d", "ratelimit_7d");
        if (fiveHour) out.fiveHour = ParseKimiWebWindow(*fiveHour);
        if (weekly) out.sevenDay = ParseKimiWebWindow(*weekly);
        if (!out.fiveHour.available && !out.sevenDay.available) {
            // Kimi's current GetSubscriptionStats response has no separate
            // 5h/7d rate-limit windows - it reports a single rolling credit
            // balance for the billing period instead. Surface that as the
            // only available window rather than failing outright.
            const json* balance = findObject("subscriptionBalance", "subscription_balance");
            if (balance) out.fiveHour = ParseKimiWebWindow(*balance);
        }
        if (!out.fiveHour.available && !out.sevenDay.available) {
            error = "Kimi web response did not contain subscription rate limits";
            return false;
        }
    } catch (const json::exception& e) {
        error = std::string("Failed to parse Kimi web usage response: ") + e.what();
        return false;
    }
    return true;
}

bool UsageJson::ParseCursorUsage(const std::string& body, UsageData& out, std::string& error) {
    try {
        const json j = json::parse(body);
        out = UsageData{};
        if (!j.is_object()) {
            error = "Cursor usage response was not an object";
            return false;
        }
        out.fiveHour = ParseCursorWindow(j);
        const std::string displayMessage = GetStringField(j, "displayMessage");
        if (!out.fiveHour.available) {
            // Cursor omits planUsage entirely once the plan's limit is hit and
            // just sends a displayMessage instead - treat that as 100% used
            // rather than a hard failure, so the gauge still shows something.
            if (!displayMessage.empty()) {
                out.fiveHour.available = true;
                out.fiveHour.percent = 100.0;
                const std::string endMs = GetStringField(j, "billingCycleEnd");
                if (!endMs.empty()) {
                    try { out.fiveHour.resetsAt = static_cast<time_t>(std::stoll(endMs) / 1000); } catch (...) {}
                }
            } else {
                error = "Cursor usage response did not contain a plan usage balance";
                return false;
            }
        }
    } catch (const json::exception& e) {
        error = std::string("Failed to parse Cursor usage response: ") + e.what();
        return false;
    }
    return true;
}

bool UsageJson::ParseCursorLegacyUsage(const std::string& body, UsageData& out, std::string& error) {
    try {
        const json root = json::parse(body);
        out = UsageData{};
        if (!root.is_object() || !root.contains("gpt-4")) {
            error = "Cursor legacy usage response did not contain a request counter";
            return false;
        }
        out.fiveHour = ParseCursorLegacyWindow(root["gpt-4"]);
        if (!out.fiveHour.available) {
            error = "Cursor legacy usage response had no usable request limit";
            return false;
        }
        const std::string startIso = GetStringField(root, "startOfMonth");
        const time_t start = ParseIso8601Utc(startIso);
        if (start > 0) {
            std::tm t{};
            gmtime_s(&t, &start);
            t.tm_mon += 1;
            out.fiveHour.resetsAt = _mkgmtime(&t);
        }
    } catch (const json::exception& e) {
        error = std::string("Failed to parse Cursor legacy usage response: ") + e.what();
        return false;
    }
    return true;
}

bool UsageJson::LoadKimiCredentials(std::string& token, std::string& error) {
    for (const char* key : {"KIMI_API_KEY", "KIMI_CODING_API_KEY"}) {
        const char* value = std::getenv(key);
        if (value && *value) {
            token = value;
            return true;
        }
    }
    const char* userProfile = std::getenv("USERPROFILE");
    if (!userProfile || !*userProfile) {
        error = "USERPROFILE is not available";
        return false;
    }
    const std::filesystem::path root = std::filesystem::path(userProfile) / ".kimi-code";
    const char* customHome = std::getenv("KIMI_CODE_HOME");
    const std::filesystem::path credentialRoot = (customHome && *customHome)
        ? std::filesystem::path(customHome) : root;
    const std::filesystem::path credentials = credentialRoot / "credentials";
    const std::filesystem::path serverRoot = credentialRoot / "server";
    if (!std::filesystem::exists(credentials) && !std::filesystem::exists(serverRoot)) {
        error = "Kimi Code credentials were not found; set KIMI_API_KEY or log in to Kimi Code";
        return false;
    }
    try {
        const std::filesystem::path preferred = credentials / "kimi-code.json";
        if (std::filesystem::is_regular_file(preferred)) {
            std::ifstream file(preferred, std::ios::binary);
            const json j = json::parse(file);
            token = FindToken(j);
            if (!token.empty()) return true;
        }
        for (const auto& rootToScan : {credentials, serverRoot}) {
            if (!std::filesystem::exists(rootToScan)) continue;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(rootToScan)) {
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() == ".json") {
                    std::ifstream file(entry.path(), std::ios::binary);
                    const json j = json::parse(file);
                    token = FindToken(j);
                    if (!token.empty()) return true;
                } else if (entry.path().filename().wstring().find(L"token") != std::wstring::npos) {
                    std::ifstream file(entry.path(), std::ios::binary);
                    std::getline(file, token);
                    if (!token.empty()) return true;
                }
            }
        }
    } catch (const std::exception& e) {
        error = std::string("Failed to read Kimi credentials: ") + e.what();
        return false;
    }
    error = "No Kimi Code API key was found; set KIMI_API_KEY or log in to Kimi Code";
    return false;
}
