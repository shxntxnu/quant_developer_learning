#ifndef TC_TIME_HPP
#define TC_TIME_HPP

#include <chrono>
#include <cstdint>
#include <string>
#include <ctime>

namespace tc {

/**
 * Returns current monotonic timestamp in nanoseconds.
 * Ideal for measuring latency and delta timestamps between pipeline stages.
 */
inline int64_t now_monotonic_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

/**
 * Returns current real-world UTC timestamp in nanoseconds since Unix epoch.
 * Used for trade event timestamps, bar timestamps, and audit logging.
 */
inline int64_t now_utc_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

/**
 * Formats a UTC nanosecond timestamp into an ISO-8601 string: YYYY-MM-DD HH:MM:SS.mmm
 */
inline std::string format_utc_timestamp(int64_t ts_ns) {
    const time_t sec = static_cast<time_t>(ts_ns / 1000000000LL);
    const int32_t millis = static_cast<int32_t>((ts_ns % 1000000000LL) / 1000000LL);
    
    struct tm tm_buf;
#if defined(_WIN32)
    gmtime_s(&tm_buf, &sec);
#else
    gmtime_r(&sec, &tm_buf);
#endif

    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, millis);
    return std::string(buf);
}

} // namespace tc

#endif /* TC_TIME_HPP */
