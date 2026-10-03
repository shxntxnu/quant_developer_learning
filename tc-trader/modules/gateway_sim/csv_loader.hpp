/**
 * @file csv_loader.hpp
 * @brief High-performance CSV parser for historical market data replay.
 *
 * Automatically inspects header columns (supports both datetime,open,low,high,close,volume
 * and Date,Open,High,Low,Close,Adj Close,Volume formats), parses timestamps, and loads
 * data into contiguous TcBar buffers for zero-overhead deterministic backtesting.
 */

#ifndef TC_GATEWAY_CSV_LOADER_HPP
#define TC_GATEWAY_CSV_LOADER_HPP

#include "tc/tc_types.h"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <iostream>

namespace tc::gateway {

// Howard Hinnant's branch-free civil days algorithm (C++20 chrono standard)
inline int64_t days_from_civil(int y, unsigned m, unsigned d) noexcept {
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

// Parses "M/D/YYYY H:MM", "YYYY-MM-DD HH:MM:SS", or "M/D/YYYY" into epoch nanoseconds
inline int64_t parse_datetime_to_ns(const std::string& str) noexcept {
    if (str.empty()) return 0;

    int year = 1970, month = 1, day = 1;
    int hour = 0, min = 0, sec = 0;

    if (str.find('/') != std::string::npos) {
        // Format: M/D/YYYY [H:MM[:SS]]
        char slash1 = 0, slash2 = 0;
        std::istringstream ss(str);
        ss >> month >> slash1 >> day >> slash2 >> year;
        if (ss >> hour) {
            char colon = 0;
            if (ss >> colon >> min) {
                if (ss >> colon) {
                    ss >> sec;
                }
            }
        }
    } else if (str.find('-') != std::string::npos) {
        // Format: YYYY-MM-DD [HH:MM:SS]
        char dash1 = 0, dash2 = 0;
        std::istringstream ss(str);
        ss >> year >> dash1 >> month >> dash2 >> day;
        if (ss >> hour) {
            char colon = 0;
            if (ss >> colon >> min) {
                if (ss >> colon) {
                    ss >> sec;
                }
            }
        }
    } else {
        return 0;
    }

    int64_t days = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    int64_t seconds = days * 86400LL + hour * 3600LL + min * 60LL + sec;
    return seconds * 1000000000LL;
}

class CsvLoader {
public:
    static bool load_file(const std::string& filepath,
                          const std::string& symbol,
                          std::vector<TcBar>& out_bars) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        std::string header_line;
        if (!std::getline(file, header_line)) {
            return false;
        }

        // Parse header line to determine column indices dynamically
        int date_idx = -1;
        int open_idx = -1;
        int high_idx = -1;
        int low_idx = -1;
        int close_idx = -1;
        int volume_idx = -1;

        std::vector<std::string> headers = split_row(header_line);
        for (size_t i = 0; i < headers.size(); ++i) {
            std::string h = to_lower(trim(headers[i]));
            if (h.find("date") != std::string::npos || h.find("time") != std::string::npos) {
                date_idx = static_cast<int>(i);
            } else if (h == "open") {
                open_idx = static_cast<int>(i);
            } else if (h == "high") {
                high_idx = static_cast<int>(i);
            } else if (h == "low") {
                low_idx = static_cast<int>(i);
            } else if (h == "close") {
                close_idx = static_cast<int>(i);
            } else if (h == "volume" || h == "vol") {
                volume_idx = static_cast<int>(i);
            }
        }

        if (date_idx < 0 || open_idx < 0 || high_idx < 0 || low_idx < 0 || close_idx < 0) {
            return false; // Required OHLC columns missing
        }

        out_bars.clear();
        out_bars.reserve(180000); // Pre-reserve capacity for large 5-minute datasets

        std::string row_line;
        while (std::getline(file, row_line)) {
            if (row_line.empty() || row_line[0] == '#') continue;

            std::vector<std::string> cols = split_row(row_line);
            int max_required_idx = std::max({date_idx, open_idx, high_idx, low_idx, close_idx});
            if (static_cast<int>(cols.size()) <= max_required_idx) {
                continue;
            }

            int64_t ts_ns = parse_datetime_to_ns(trim(cols[date_idx]));
            if (ts_ns <= 0) continue;

            double open_f  = std::strtod(cols[open_idx].c_str(), nullptr);
            double high_f  = std::strtod(cols[high_idx].c_str(), nullptr);
            double low_f   = std::strtod(cols[low_idx].c_str(), nullptr);
            double close_f = std::strtod(cols[close_idx].c_str(), nullptr);
            int64_t vol    = (volume_idx >= 0 && static_cast<int>(cols.size()) > volume_idx)
                                 ? std::strtoll(cols[volume_idx].c_str(), nullptr, 10)
                                 : 100;

            if (open_f <= 0.0 || high_f <= 0.0 || low_f <= 0.0 || close_f <= 0.0) {
                continue;
            }

            TcBar bar{};
            bar.struct_size = sizeof(TcBar);
            bar.version = 1;
            bar.ts_ns = ts_ns;

            size_t sym_len = std::min(symbol.size(), sizeof(bar.symbol) - 1);
            std::memcpy(bar.symbol, symbol.c_str(), sym_len);
            bar.symbol[sym_len] = '\0';

            bar.open  = TC_DOUBLE_TO_PRICE(open_f);
            bar.high  = TC_DOUBLE_TO_PRICE(high_f);
            bar.low   = TC_DOUBLE_TO_PRICE(low_f);
            bar.close = TC_DOUBLE_TO_PRICE(close_f);
            bar.volume = std::max<int64_t>(1, vol);
            bar.vwap  = static_cast<TcPrice>((bar.open + bar.high + bar.low + bar.close) / 4);
            bar.num_ticks = 1;

            out_bars.push_back(bar);
        }

        // Sort chronologically in case CSV entries are disordered
        std::sort(out_bars.begin(), out_bars.end(), [](const TcBar& a, const TcBar& b) {
            return a.ts_ns < b.ts_ns;
        });

        return !out_bars.empty();
    }

private:
    static std::vector<std::string> split_row(const std::string& line) {
        std::vector<std::string> tokens;
        tokens.reserve(10);
        std::string token;
        std::istringstream stream(line);
        while (std::getline(stream, token, ',')) {
            tokens.push_back(token);
        }
        return tokens;
    }

    static std::string trim(const std::string& s) {
        auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c){ return std::isspace(c); });
        auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c){ return std::isspace(c); }).base();
        return (wsback <= wsfront ? std::string() : std::string(wsfront, wsback));
    }

    static std::string to_lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        return s;
    }
};

} // namespace tc::gateway

#endif /* TC_GATEWAY_CSV_LOADER_HPP */
