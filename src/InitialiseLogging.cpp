#include "stdafx.h"

#include "PluginConfig.h"
#include "Constant.hpp"
#include "Log.h"
#include "VersionInfo.h"


// Helper: map string to spdlog level (case-insensitive, safe defaults)
inline spdlog::level::level_enum to_level(std::string_view s) {
	using spdlog::level::level_enum;
	auto eq = [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); };
	auto ieq = [&](std::string_view a, std::string_view b) {
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), eq);
		};
	if (ieq(s, "trace"))    return spdlog::level::trace;
	if (ieq(s, "debug"))    return spdlog::level::debug;
	if (ieq(s, "info"))     return spdlog::level::info;
	if (ieq(s, "warn") || ieq(s, "warning")) return spdlog::level::warn;
	if (ieq(s, "error"))    return spdlog::level::err;
	if (ieq(s, "critical")) return spdlog::level::critical;
	if (ieq(s, "off"))      return spdlog::level::off;
	return spdlog::level::info;
}

// Returns true if spdlog was successfully initialized, false otherwise
static std::string MakeLogFilename()
{
    // Windows-safe: no ':' characters
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto tt = system_clock::to_time_t(now);

    std::tm local_tm{};
    localtime_s(&local_tm, &tt);

    return std::format("vfpc-{:04}{:02}{:02}{:02}{:02}{:02}.log",
        local_tm.tm_year + 1900,
        local_tm.tm_mon + 1,
        local_tm.tm_mday,
        local_tm.tm_hour,
        local_tm.tm_min,
        local_tm.tm_sec);
}

bool InitializeLogging(PluginConfig& pc)
{
    static std::once_flag log_init_flag;
    static bool log_initialized = false;

    std::call_once(log_init_flag, [&]() {
        try {
            std::filesystem::create_directories(pc.log_path);

            const std::string filename = MakeLogFilename();
            const std::filesystem::path fullpath = std::filesystem::path(pc.log_path) / filename;

            spdlog::drop(VFPC_PLUGIN_NAME);
            auto logger = spdlog::basic_logger_mt(
                VFPC_PLUGIN_NAME,
                fullpath.string(),
                pc.truncate
            );

            // Make it the default logger (useful if you call spdlog::info(...) elsewhere)
            spdlog::set_default_logger(logger);

            // Global settings
            if (!pc.pattern.empty()) spdlog::set_pattern(pc.pattern);
            if (pc.flush_interval_sec > 0)
                spdlog::flush_every(std::chrono::seconds(pc.flush_interval_sec));

            // Per-logger settings
            logger->set_level(to_level(pc.level));
            logger->flush_on(to_level(pc.flush_level));
		

            logger->info("{} plugin version {} logging started.", VFPC_PLUGIN_NAME, VFPC_VERSION_STR);

            log_initialized = true;
        }
        catch (const std::exception& ex) {
            OutputDebugStringA(("Failed to initialize logger: " + std::string(ex.what()) + "\n").c_str());
            if (auto fallback = spdlog::default_logger()) {
                fallback->error("Failed to initialize logger: {}", ex.what());
            }
            log_initialized = false;
        }
        });

    return log_initialized;
}