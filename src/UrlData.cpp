#include "stdafx.h"


enum class VersionCompareResult { EARLIER, SAME, LATER };

static std::vector<int> splitVersion(const std::string& v) {
    std::vector<int> parts;
    std::stringstream ss(v);
    std::string item;
    while (std::getline(ss, item, '.')) parts.push_back(std::stoi(item));
    return parts;
}

// v1 >= v2 ?
static bool isSameOrLaterVersion(const std::string& v1, const std::string& v2) {
    auto a = splitVersion(v1), b = splitVersion(v2);
    while (a.size() < b.size()) a.push_back(0);
    while (b.size() < a.size()) b.push_back(0);
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] > b[i]) return true;
        if (a[i] < b[i]) return false;
    }
    return true; // equal
}

static std::optional<std::tm> parseDate(const std::string& d) {
    std::tm tm{}; std::istringstream ss(d); ss >> std::get_time(&tm, "%d/%m/%Y");
    if (ss.fail()) return std::nullopt;
    return tm;
}
static std::optional<std::tm> parseTime(const std::string& t) {
    std::tm tm{}; std::istringstream ss(t); ss >> std::get_time(&tm, "%H:%M:%S");
    if (ss.fail()) return std::nullopt;
    return tm;
}

// returns time_t for local time; nullopt if either part fails
static std::optional<std::time_t> toTimestamp(const std::string& d, const std::string& t) {
    auto dOpt = parseDate(d);
    auto tOpt = parseTime(t);
    if (!dOpt || !tOpt) return std::nullopt;
    std::tm tm = *dOpt;
    tm.tm_hour = tOpt->tm_hour;
    tm.tm_min = tOpt->tm_min;
    tm.tm_sec = tOpt->tm_sec;
    // mktime normalizes; treat as local time
    return std::mktime(&tm);
}

// d1 >= d2 ?
static bool isSameOrLaterDate(const std::string& d1, const std::string& d2) {
    auto a = parseDate(d1), b = parseDate(d2);
    if (!a || !b) return false;
    // Compare Y/M/D lexicographically
    if (a->tm_year != b->tm_year) return a->tm_year > b->tm_year;
    if (a->tm_mon != b->tm_mon)  return a->tm_mon > b->tm_mon;
    return a->tm_mday >= b->tm_mday;
}

// t1 >= t2 ?
static bool isSameOrLaterTimeOnly(const std::string& t1, const std::string& t2) {
    auto a = parseTime(t1), b = parseTime(t2);
    if (!a || !b) return false;
    if (a->tm_hour != b->tm_hour) return a->tm_hour > b->tm_hour;
    if (a->tm_min != b->tm_min)  return a->tm_min > b->tm_min;
    return a->tm_sec >= b->tm_sec;
}

static bool IsValidVersion(const VersionData& data) {

    CompareResult r;
    r.version_ok = isSameOrLaterVersion(data.vfpc_version, data.min_version);
    r.date_ok = isSameOrLaterDate(data.date, data.last_updated_date);
    r.time_ok = isSameOrLaterTimeOnly(data.time, data.last_updated_time);

    // Combined (strictly newer) check using timestamps
    auto now_ts = toTimestamp(data.date, data.time);
    auto prev_ts = toTimestamp(data.last_updated_date, data.last_updated_time);
    if (now_ts && prev_ts) r.older_dt = (*prev_ts > *now_ts);

    // ---- logging ----
    // Version
    if (r.version_ok)
        LOG_INFO("Version OK: Plugin Version: {} >= Minimum Version: {}", data.vfpc_version, data.min_version);
    else
        LOG_WARN("Version too old: Plugin Version: {} < Minimum Version: {}", data.vfpc_version, data.min_version);

    // Date
    if (parseDate(data.date) && parseDate(data.last_updated_date)) {
        if (r.date_ok)
            LOG_INFO("Date OK: Current Date: {} >= Last Update Date: {}", data.date, data.last_updated_date);
        else
            LOG_WARN("Date older: Current Date: {} < Last Updated Date: {}", data.date, data.last_updated_date);
    }
    else {
        LOG_ERROR("Date parse failed: '{}' or '{}'", data.date, data.last_updated_date);
    }

    // Time
    if (parseTime(data.time) && parseTime(data.last_updated_time)) {
        if (r.time_ok)
            LOG_INFO("Time OK: Current Time: {} >= Last Updated Time: {}", data.time, data.last_updated_time);
        else
            LOG_WARN("Time older: Current Time: {} < Last Updated Time:{}", data.time, data.last_updated_time);
    }
    else {
        LOG_ERROR("Time parse failed: '{}' or '{}'", data.time, data.last_updated_time);
    }

    // Summary (combined datetime)
    if (now_ts && prev_ts) {
        if (r.older_dt)
            LOG_INFO("Update detected: Update Date/Time: {} {} is newer than Current Date/Time: {} {}",
                data.last_updated_date, data.last_updated_time, data.date, data.time);
        else if (*now_ts == *prev_ts)
            LOG_INFO("No change: Current Date/Time: {} {} equals Update Date/Time: {} {}",
                data.date, data.time, data.last_updated_date, data.last_updated_time);
        else
            LOG_INFO("No update: Update Date/Time: {} {} is not newer than Current Date/Time: {} {}",
                data.last_updated_date, data.last_updated_time, data.date, data.time);
    }
    else {
        LOG_ERROR("Combined datetime parse failed; cannot determine update status.");
    }

    return (r.version_ok && r.date_ok && r.time_ok);
}


bool ValidateFields(const json& doc) {
	for (const auto& key : { "VFPC_Version", "api_version", "date", "day", "last_updated_date", "last_updated_time",
						 "min_version", "time", "vfpc_version"}) {
		if (!doc.contains(key)) {
			return false;
		}
	}
	return true;
}


// Consider moving this to CVFPCPlugin.cpp?
bool ValidVersion(PluginConfig& pc, VersionData& data) {
	
	auto jsonOut = HttpClient::GetJsonFromUrl(pc, "version");
    if (!jsonOut) {
        LOG_ERROR("Failed to fetch version data from server.");
        return false;
    }
    data.vfpc_version = (*jsonOut)["VFPC_Version"].get<std::string>();
    data.api_version = (*jsonOut)["api_version"].get<std::string>();
    data.date = (*jsonOut)["date"].get<std::string>();
    data.day = (*jsonOut)["day"].get<int>();
    data.last_updated_date = (*jsonOut)["last_updated_date"].get<std::string>();
    data.last_updated_time = (*jsonOut)["last_updated_time"].get<std::string>();
    data.min_version = (*jsonOut)["min_version"].get<std::string>();
    data.time = (*jsonOut)["time"].get<std::string>();
    data.version_string = (*jsonOut)["vfpc_version"].get<std::string>();
	
	if (ValidateFields(jsonOut)) {
		data.vfpc_version = (*jsonOut)["VFPC_Version"].get<std::string>();
		data.api_version = (*jsonOut)["api_version"].get<std::string>();
		data.date = (*jsonOut)["date"].get<std::string>();
		data.day = (*jsonOut)["day"].get<int>();
		data.last_updated_date = (*jsonOut)["last_updated_date"].get<std::string>();
		data.last_updated_time = (*jsonOut)["last_updated_time"].get<std::string>();
		data.min_version = (*jsonOut)["min_version"].get<std::string>();
		data.time = (*jsonOut)["time"].get<std::string>();
		data.version_string = (*jsonOut)["vfpc_version"].get<std::string>();

		if (IsValidVersion(data)) return true;
	}
	LOG_ERROR("Version data validation failed. Missing fields in JSON response.");
	return false;
}