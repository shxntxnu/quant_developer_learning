#ifndef TC_MARKETDATA_ENGINE_HPP
#define TC_MARKETDATA_ENGINE_HPP

#include "tc/marketdata/tc_marketdata.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace tc::marketdata {

constexpr size_t MAX_SYMBOLS = 64;

struct SymbolSlot {
    char symbol[TC_SYMBOL_MAX]{};
    bool active{false};

    int64_t last_seen_ts_ns{0};
    TcPrice last_valid_price{0};
    bool has_valid_price{false};

    TcBar current_bar{};
    bool has_open_bar{false};
    int64_t current_bar_start_ns{0};
    int64_t current_bar_close_ns{0};

    __int128 running_turnover{0};
    int64_t running_volume{0};

    void reset() noexcept {
        last_seen_ts_ns = 0;
        last_valid_price = 0;
        has_valid_price = false;
        std::memset(&current_bar, 0, sizeof(current_bar));
        has_open_bar = false;
        current_bar_start_ns = 0;
        current_bar_close_ns = 0;
        running_turnover = 0;
        running_volume = 0;
    }
};

class MarketDataEngine {
public:
    MarketDataEngine() noexcept {
        init_default_config();
        reset_all();
    }

    void init_default_config() noexcept {
        config_.struct_size = sizeof(TcMarketDataConfig);
        config_.version = 1;
        config_.reserved = 0;
        config_.bar_interval_sec = 300;           // 5-minute bars default
        config_.max_price_deviation_pct = 0.10;   // 10% price collar / spike filter
        config_.stale_timeout_ns = 5000000000LL;  // 5 seconds
        config_.filter_stale_ticks = true;
        config_.filter_outliers = true;
        config_.filter_crossed_book = true;
        config_.emit_partial_bars = false;
    }

    void reset_all() noexcept {
        for (auto& slot : slots_) {
            slot.active = false;
            slot.symbol[0] = '\0';
            slot.reset();
        }
        std::memset(&stats_, 0, sizeof(stats_));
        stats_.struct_size = sizeof(TcMarketDataStats);
        stats_.version = 1;
        bar_sink_ = nullptr;
        bar_sink_user_data_ = nullptr;
    }

    void reset_symbol(const char* symbol) noexcept {
        if (!symbol) return;
        int idx = find_symbol(symbol);
        if (idx >= 0) {
            slots_[idx].reset();
        }
    }

    void set_bar_sink(TcBarCallback sink, void* user_data) noexcept {
        bar_sink_ = sink;
        bar_sink_user_data_ = user_data;
    }

    [[nodiscard]] const TcMarketDataConfig& get_config() const noexcept {
        return config_;
    }

    void set_config(const TcMarketDataConfig& cfg) noexcept {
        config_ = cfg;
    }

    [[nodiscard]] const TcMarketDataStats& get_stats() const noexcept {
        return stats_;
    }

    bool configure_from_json(std::string_view json_str) noexcept {
        if (json_str.empty()) return false;

        auto parse_int = [&](std::string_view key, uint32_t& out) {
            auto pos = json_str.find(key);
            if (pos != std::string_view::npos) {
                auto colon = json_str.find(':', pos);
                if (colon != std::string_view::npos) {
                    const char* start = json_str.data() + colon + 1;
                    char* end = nullptr;
                    long val = std::strtol(start, &end, 10);
                    if (end != start && val > 0) out = static_cast<uint32_t>(val);
                }
            }
        };

        auto parse_double = [&](std::string_view key, double& out) {
            auto pos = json_str.find(key);
            if (pos != std::string_view::npos) {
                auto colon = json_str.find(':', pos);
                if (colon != std::string_view::npos) {
                    const char* start = json_str.data() + colon + 1;
                    char* end = nullptr;
                    double val = std::strtod(start, &end);
                    if (end != start && val >= 0.0) out = val;
                }
            }
        };

        auto parse_bool = [&](std::string_view key, bool& out) {
            auto pos = json_str.find(key);
            if (pos != std::string_view::npos) {
                auto colon = json_str.find(':', pos);
                if (colon != std::string_view::npos) {
                    auto true_pos = json_str.find("true", colon);
                    auto false_pos = json_str.find("false", colon);
                    auto comma = json_str.find(',', colon);
                    if (true_pos != std::string_view::npos && (comma == std::string_view::npos || true_pos < comma)) {
                        out = true;
                    } else if (false_pos != std::string_view::npos && (comma == std::string_view::npos || false_pos < comma)) {
                        out = false;
                    }
                }
            }
        };

        parse_int("bar_interval_sec", config_.bar_interval_sec);
        parse_double("max_price_deviation_pct", config_.max_price_deviation_pct);

        double timeout_sec = static_cast<double>(config_.stale_timeout_ns) / 1e9;
        parse_double("stale_timeout_sec", timeout_sec);
        if (timeout_sec > 0.0) {
            config_.stale_timeout_ns = static_cast<int64_t>(timeout_sec * 1e9);
        }

        parse_bool("filter_stale_ticks", config_.filter_stale_ticks);
        parse_bool("filter_outliers", config_.filter_outliers);
        parse_bool("filter_crossed_book", config_.filter_crossed_book);
        parse_bool("emit_partial_bars", config_.emit_partial_bars);

        return true;
    }

    // -------------------------------------------------------------------------
    // Core Engine Pipeline (Hot-Path): Raw -> Normalized -> Bar
    // -------------------------------------------------------------------------

    TcStatus process_raw_tick(const TcRawTick* raw,
                              TcTick* tick_out,
                              bool* tick_emitted,
                              TcBar* bar_out,
                              bool* bar_emitted) noexcept {
        if (tick_emitted) *tick_emitted = false;
        if (bar_emitted) *bar_emitted = false;
        if (!raw) return TC_ERR_INVALID_ARG;

        stats_.raw_ticks_received++;

        TcTick normalized{};
        if (!normalize_tick(*raw, normalized)) {
            return TC_ERR_INVALID_ARG;
        }

        int slot_idx = find_or_create_symbol(normalized.symbol);
        if (slot_idx < 0) {
            stats_.ticks_dropped_invalid++;
            return TC_ERR_QUEUE_FULL; // Symbol table capacity reached
        }

        SymbolSlot& slot = slots_[slot_idx];

        if (!validate_tick(normalized, slot)) {
            return TC_OK; // Dropped invalid/stale tick
        }

        if (tick_out) {
            *tick_out = normalized;
        }
        if (tick_emitted) {
            *tick_emitted = true;
        }

        // Aggregate validated tick into OHLCV bar
        aggregate_tick(normalized, slot, bar_out, bar_emitted);

        return TC_OK;
    }

    TcStatus process_tick(const TcTick* tick,
                          TcBar* bar_out,
                          bool* bar_emitted) noexcept {
        if (bar_emitted) *bar_emitted = false;
        if (!tick) return TC_ERR_INVALID_ARG;

        int slot_idx = find_or_create_symbol(tick->symbol);
        if (slot_idx < 0) return TC_ERR_QUEUE_FULL;

        SymbolSlot& slot = slots_[slot_idx];

        if (!validate_tick(*tick, slot)) {
            return TC_OK;
        }

        aggregate_tick(*tick, slot, bar_out, bar_emitted);
        return TC_OK;
    }

    TcStatus flush_bar(const char* symbol, TcBar* bar_out, bool* bar_emitted) noexcept {
        if (bar_emitted) *bar_emitted = false;

        if (symbol && symbol[0] != '\0') {
            int idx = find_symbol(symbol);
            if (idx >= 0 && slots_[idx].has_open_bar) {
                emit_open_bar(slots_[idx], bar_out, bar_emitted);
                return TC_OK;
            }
            return TC_ERR_NOT_FOUND;
        }

        // Flush first open bar if no symbol specified
        for (auto& slot : slots_) {
            if (slot.active && slot.has_open_bar) {
                emit_open_bar(slot, bar_out, bar_emitted);
                return TC_OK;
            }
        }

        return TC_OK;
    }

    TcStatus get_feed_status(const char* symbol, int64_t current_ts_ns, TcFeedStatus* out_status) const noexcept {
        if (!out_status) return TC_ERR_INVALID_ARG;
        if (!symbol || symbol[0] == '\0') {
            *out_status = TC_FEED_UNKNOWN;
            return TC_ERR_INVALID_ARG;
        }

        int idx = find_symbol(symbol);
        if (idx < 0 || !slots_[idx].active || slots_[idx].last_seen_ts_ns <= 0) {
            *out_status = TC_FEED_UNKNOWN;
            return TC_OK;
        }

        const auto& slot = slots_[idx];
        int64_t elapsed_ns = current_ts_ns - slot.last_seen_ts_ns;

        if (elapsed_ns < 0) {
            // Clock skew / timestamp anomaly
            *out_status = TC_FEED_OK;
            return TC_OK;
        }

        if (elapsed_ns > config_.stale_timeout_ns) {
            *out_status = TC_FEED_DEGRADED;
        } else {
            *out_status = TC_FEED_OK;
        }

        return TC_OK;
    }

private:
    int find_symbol(const char* symbol) const noexcept {
        if (!symbol) return -1;
        for (size_t i = 0; i < MAX_SYMBOLS; ++i) {
            if (slots_[i].active && std::strncmp(slots_[i].symbol, symbol, TC_SYMBOL_MAX) == 0) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int find_or_create_symbol(const char* symbol) noexcept {
        if (!symbol || symbol[0] == '\0') return -1;

        int existing = find_symbol(symbol);
        if (existing >= 0) return existing;

        for (size_t i = 0; i < MAX_SYMBOLS; ++i) {
            if (!slots_[i].active) {
                slots_[i].active = true;
                size_t len = std::min(std::strlen(symbol), sizeof(slots_[i].symbol) - 1);
                std::memcpy(slots_[i].symbol, symbol, len);
                slots_[i].symbol[len] = '\0';
                slots_[i].reset();
                return static_cast<int>(i);
            }
        }
        return -1; // Capacity reached
    }

    bool normalize_tick(const TcRawTick& raw, TcTick& out) const noexcept {
        if (raw.symbol[0] == '\0') return false;

        out.struct_size = sizeof(TcTick);
        out.version = 1;
        out.flags = 0;

        size_t len = std::min(std::strlen(raw.symbol), sizeof(out.symbol) - 1);
        std::memcpy(out.symbol, raw.symbol, len);
        out.symbol[len] = '\0';

        out.ts_ns = raw.ts_ns;

        out.bid = (raw.bid > 0.0) ? TC_DOUBLE_TO_PRICE(raw.bid) : 0;
        out.ask = (raw.ask > 0.0) ? TC_DOUBLE_TO_PRICE(raw.ask) : 0;
        out.last = (raw.last > 0.0) ? TC_DOUBLE_TO_PRICE(raw.last) : 0;

        out.bid_sz = std::max<int64_t>(0, raw.bid_sz);
        out.ask_sz = std::max<int64_t>(0, raw.ask_sz);
        out.last_sz = std::max<int64_t>(0, raw.last_sz);

        if (out.bid > 0) out.flags |= TC_TICK_FLAG_HAS_BID;
        if (out.ask > 0) out.flags |= TC_TICK_FLAG_HAS_ASK;
        if (out.last > 0) out.flags |= TC_TICK_FLAG_HAS_LAST;

        return true;
    }

    bool validate_tick(const TcTick& tick, SymbolSlot& slot) noexcept {
        // 1. Stale / Inverted Timestamp Check
        if (config_.filter_stale_ticks && slot.last_seen_ts_ns > 0) {
            if (tick.ts_ns <= slot.last_seen_ts_ns) {
                stats_.ticks_dropped_stale++;
                return false;
            }
        }

        // 2. Price Sanity Check (at least one positive price required)
        if (tick.last <= 0 && tick.bid <= 0 && tick.ask <= 0) {
            stats_.ticks_dropped_invalid++;
            return false;
        }

        // 3. Crossed Book Check (bid > ask)
        if (config_.filter_crossed_book && tick.bid > 0 && tick.ask > 0) {
            if (tick.bid > tick.ask) {
                stats_.ticks_dropped_invalid++;
                return false;
            }
        }

        // 4. Reference Price Determination
        TcPrice ref_price = 0;
        if (tick.last > 0) {
            ref_price = tick.last;
        } else if (tick.bid > 0 && tick.ask > 0) {
            ref_price = (tick.bid + tick.ask) / 2;
        } else if (tick.bid > 0) {
            ref_price = tick.bid;
        } else {
            ref_price = tick.ask;
        }

        // 5. Price Outlier / Spike Filter
        if (config_.filter_outliers && slot.has_valid_price && ref_price > 0) {
            double last_px = TC_PRICE_TO_DOUBLE(slot.last_valid_price);
            double curr_px = TC_PRICE_TO_DOUBLE(ref_price);
            if (last_px > 0.0) {
                double deviation = std::abs(curr_px - last_px) / last_px;
                if (deviation > config_.max_price_deviation_pct) {
                    stats_.ticks_dropped_outlier++;
                    return false;
                }
            }
        }

        // Validation passed: update tracking state
        slot.last_seen_ts_ns = tick.ts_ns;
        if (ref_price > 0) {
            slot.last_valid_price = ref_price;
            slot.has_valid_price = true;
        }

        stats_.ticks_normalized++;
        return true;
    }

    void aggregate_tick(const TcTick& tick,
                        SymbolSlot& slot,
                        TcBar* bar_out,
                        bool* bar_emitted) noexcept {
        // Only trade ticks form trade OHLCV bars
        // Quote ticks update mark and feed health, but don't advance trade volume
        TcPrice trade_px = tick.last;
        int64_t trade_sz = tick.last_sz;

        if (trade_px <= 0) {
            // Fallback if quote-only and slot already has valid price
            if (slot.has_valid_price) {
                trade_px = slot.last_valid_price;
                trade_sz = 0;
            } else {
                return;
            }
        }

        const int64_t interval_ns = static_cast<int64_t>(config_.bar_interval_sec) * 1000000000LL;
        const int64_t bar_start = (tick.ts_ns / interval_ns) * interval_ns;
        const int64_t bar_close = bar_start + interval_ns;

        if (slot.has_open_bar) {
            if (tick.ts_ns >= slot.current_bar_close_ns) {
                // Completed previous bar! Emit it.
                emit_open_bar(slot, bar_out, bar_emitted);

                // Initialize new bar starting at bar_start
                init_new_bar(slot, bar_start, bar_close, trade_px, trade_sz);
            } else {
                // Accumulate into existing active bar
                if (trade_px > slot.current_bar.high) slot.current_bar.high = trade_px;
                if (trade_px < slot.current_bar.low)  slot.current_bar.low  = trade_px;
                slot.current_bar.close = trade_px;
                slot.current_bar.volume += trade_sz;
                slot.current_bar.num_ticks++;

                if (trade_sz > 0) {
                    slot.running_turnover += static_cast<__int128>(trade_px) * trade_sz;
                    slot.running_volume += trade_sz;
                }

                if (slot.running_volume > 0) {
                    slot.current_bar.vwap = static_cast<TcPrice>(slot.running_turnover / slot.running_volume);
                }
            }
        } else {
            // First tick for this symbol: initialize active bar
            init_new_bar(slot, bar_start, bar_close, trade_px, trade_sz);
        }
    }

    void init_new_bar(SymbolSlot& slot,
                      int64_t bar_start,
                      int64_t bar_close,
                      TcPrice px,
                      int64_t sz) noexcept {
        slot.current_bar.struct_size = sizeof(TcBar);
        slot.current_bar.version = 1;
        slot.current_bar.timeframe_sec = static_cast<uint16_t>(config_.bar_interval_sec);

        size_t len = std::min(std::strlen(slot.symbol), sizeof(slot.current_bar.symbol) - 1);
        std::memcpy(slot.current_bar.symbol, slot.symbol, len);
        slot.current_bar.symbol[len] = '\0';

        slot.current_bar.ts_ns = bar_close;
        slot.current_bar.open = px;
        slot.current_bar.high = px;
        slot.current_bar.low = px;
        slot.current_bar.close = px;
        slot.current_bar.volume = sz;
        slot.current_bar.num_ticks = 1;
        slot.current_bar.vwap = px;

        slot.running_turnover = static_cast<__int128>(px) * sz;
        slot.running_volume = sz;

        slot.current_bar_start_ns = bar_start;
        slot.current_bar_close_ns = bar_close;
        slot.has_open_bar = true;
    }

    void emit_open_bar(SymbolSlot& slot, TcBar* bar_out, bool* bar_emitted) noexcept {
        if (!slot.has_open_bar) return;

        // Finalize VWAP
        if (slot.running_volume > 0) {
            slot.current_bar.vwap = static_cast<TcPrice>(slot.running_turnover / slot.running_volume);
        } else {
            slot.current_bar.vwap = slot.current_bar.close;
        }

        if (bar_out) {
            *bar_out = slot.current_bar;
        }
        if (bar_emitted) {
            *bar_emitted = true;
        }

        stats_.bars_emitted++;

        if (bar_sink_) {
            bar_sink_(&slot.current_bar, bar_sink_user_data_);
        }

        slot.has_open_bar = false;
    }

private:
    TcMarketDataConfig config_{};
    TcMarketDataStats stats_{};
    SymbolSlot slots_[MAX_SYMBOLS]{};

    TcBarCallback bar_sink_{nullptr};
    void* bar_sink_user_data_{nullptr};
};

} // namespace tc::marketdata

#endif // TC_MARKETDATA_ENGINE_HPP
