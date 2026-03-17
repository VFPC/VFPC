#ifndef LOG_H
#define LOG_H


#include <spdlog/spdlog.h>

#if defined(SPDLOG_USE_STD_FORMAT)
// Using C++20 std::format
#include <format>
template <typename... Args>
using vfpc_format_string = std::format_string<Args...>;
#else
// Using bundled/external {fmt}
#include <spdlog/fmt/fmt.h>
template <typename... Args>
using vfpc_format_string = fmt::format_string<Args...>;
#endif


#ifndef VFPC_LOGGER_NAME
#define VFPC_LOGGER_NAME "VFPC"
#endif

namespace vfpc::logging {

    enum class LogLevel { TRACE, DBG, INFO, WARN, ERR, CRITICAL };

    constexpr inline spdlog::level::level_enum to_spd(LogLevel lvl) noexcept {
        using L = spdlog::level::level_enum;
        switch (lvl) {
        case LogLevel::TRACE:    return L::trace;
        case LogLevel::DBG:    return L::debug;
        case LogLevel::INFO:     return L::info;
        case LogLevel::WARN:     return L::warn;
        case LogLevel::ERR:    return L::err;
        case LogLevel::CRITICAL: return L::critical;
        }
        return L::info;
    }

    // Dynamic-level wrapper (can’t be compiled out by SPDLOG_ACTIVE_LEVEL)
    template <typename... Args>
    inline void log(LogLevel lvl, vfpc_format_string<Args...> fmtStr, Args&&... args) {
        if (auto lg = spdlog::get(VFPC_LOGGER_NAME)) {
            lg->log(to_spd(lvl), fmtStr, std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    inline void log(std::string_view loggerName,
        LogLevel lvl,
        vfpc_format_string<Args...> fmtStr,
        Args&&... args) {
        if (auto lg = spdlog::get(std::string{ loggerName })) {
            lg->log(to_spd(lvl), fmtStr, std::forward<Args>(args)...);
        }
    }

    // Per-level helpers that honor SPDLOG_ACTIVE_LEVEL
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    template <typename... Args>
    inline void log_trace(vfpc_format_string<Args...> f, Args&&... a) {
        if (auto lg = spdlog::get(VFPC_LOGGER_NAME)) lg->log(spdlog::level::trace, f, std::forward<Args>(a)...);
    }
#else
    template <typename... Args>
    inline void log_trace(vfpc_format_string<Args...>, Args&&...) {}
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
    template <typename... Args>
    inline void log_debug(vfpc_format_string<Args...> f, Args&&... a) {
        if (auto lg = spdlog::get(VFPC_LOGGER_NAME)) lg->log(spdlog::level::debug, f, std::forward<Args>(a)...);
    }
#else
    template <typename... Args>
    inline void log_debug(vfpc_format_string<Args...>, Args&&...) {}
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_INFO
    template <typename... Args>
    inline void log_info(vfpc_format_string<Args...> f, Args&&... a) {
        if (auto lg = spdlog::get(VFPC_LOGGER_NAME)) lg->log(spdlog::level::info, f, std::forward<Args>(a)...);
    }
#else
    template <typename... Args>
    inline void log_info(vfpc_format_string<Args...>, Args&&...) {}
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_WARN
    template <typename... Args>
    inline void log_warn(vfpc_format_string<Args...> f, Args&&... a) {
        if (auto lg = spdlog::get(VFPC_LOGGER_NAME)) lg->log(spdlog::level::warn, f, std::forward<Args>(a)...);
    }
#else
    template <typename... Args>
    inline void log_warn(vfpc_format_string<Args...>, Args&&...) {}
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_ERROR
    template <typename... Args>
    inline void log_error(vfpc_format_string<Args...> f, Args&&... a) {
        if (auto lg = spdlog::get(VFPC_LOGGER_NAME)) lg->log(spdlog::level::err, f, std::forward<Args>(a)...);
    }
#else
    template <typename... Args>
    inline void log_error(vfpc_format_string<Args...>, Args&&...) {}
#endif

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_CRITICAL
    template <typename... Args>
    inline void log_critical(vfpc_format_string<Args...> f, Args&&... a) {
        if (auto lg = spdlog::get(VFPC_LOGGER_NAME)) lg->log(spdlog::level::critical, f, std::forward<Args>(a)...);
    }
#else
    template <typename... Args>
    inline void log_critical(vfpc_format_string<Args...>, Args&&...) {}
#endif

} // namespace vfpc::logging

#ifndef VFPC_NO_LEVEL_MACROS
#define LOG_TRACE(fmtStr, ...)   do { if (auto _lg = spdlog::get(VFPC_LOGGER_NAME)) SPDLOG_LOGGER_TRACE(_lg, fmtStr, ##__VA_ARGS__); } while (0)
#define LOG_DEBUG(fmtStr, ...)   do { if (auto _lg = spdlog::get(VFPC_LOGGER_NAME)) SPDLOG_LOGGER_DEBUG(_lg, fmtStr, ##__VA_ARGS__); } while (0)
#define LOG_INFO(fmtStr, ...)    do { if (auto _lg = spdlog::get(VFPC_LOGGER_NAME)) SPDLOG_LOGGER_INFO (_lg, fmtStr, ##__VA_ARGS__); } while (0)
#define LOG_WARN(fmtStr, ...)    do { if (auto _lg = spdlog::get(VFPC_LOGGER_NAME)) SPDLOG_LOGGER_WARN (_lg, fmtStr, ##__VA_ARGS__); } while (0)
#define LOG_ERROR(fmtStr, ...)   do { if (auto _lg = spdlog::get(VFPC_LOGGER_NAME)) SPDLOG_LOGGER_ERROR(_lg, fmtStr, ##__VA_ARGS__); } while (0)
#define LOG_CRITICAL(fmtStr, ...)do { if (auto _lg = spdlog::get(VFPC_LOGGER_NAME)) SPDLOG_LOGGER_CRITICAL(_lg, fmtStr, ##__VA_ARGS__); } while (0)
#endif


#endif // LOG_H
