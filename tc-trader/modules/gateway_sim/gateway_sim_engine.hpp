/**
 * @file gateway_sim_engine.hpp
 * @brief Complete historical simulation gateway engine.
 *
 * Coordinates CSV market data replay, bar distribution, and the in-memory matching engine.
 */

#ifndef TC_GATEWAY_SIM_ENGINE_HPP
#define TC_GATEWAY_SIM_ENGINE_HPP

#include "tc/tc_types.h"
#include "tc/tc_abi.h"
#include "tc/gateway/tc_gateway.h"
#include "csv_loader.hpp"
#include "matching_engine.hpp"

#include <vector>
#include <string>
#include <mutex>
#include <filesystem>
#include <iostream>

namespace tc::gateway {

class GatewaySimEngine {
public:
    GatewaySimEngine() noexcept {
        reset();
    }

    TcStatus connect(const TcGatewayConfig* config) noexcept {
        if (!config) return TC_ERR_INVALID_ARG;

        std::lock_guard<std::mutex> lock(mutex_);
        config_ = *config;

        matching_engine_.set_config(config_);

        std::string filepath = config_.data_file_path;
        std::string symbol = (config_.default_symbol[0] != '\0') ? config_.default_symbol : "SIM";

        bars_.clear();
        cursor_ = 0;

        if (!filepath.empty()) {
            // Attempt to load from given path, with robust fallbacks
            bool loaded = CsvLoader::load_file(filepath, symbol, bars_);
            if (!loaded) {
                // Try candidate fallback paths relative to typical repo locations
                std::vector<std::string> candidates = {
                    "../Algorithmic Trading Machine Learning Strategies/simulated_5min_data.csv",
                    "../../Algorithmic Trading Machine Learning Strategies/simulated_5min_data.csv",
                    "Algorithmic Trading Machine Learning Strategies/simulated_5min_data.csv",
                    "../Algorithmic Trading Machine Learning Strategies/simulated_daily_data.csv",
                    "Algorithmic Trading Machine Learning Strategies/simulated_daily_data.csv"
                };

                for (const auto& cand : candidates) {
                    if (CsvLoader::load_file(cand, symbol, bars_)) {
                        loaded = true;
                        break;
                    }
                }
            }

            if (!loaded) {
                return TC_ERR_NOT_FOUND;
            }
        }

        connected_ = true;
        return TC_OK;
    }

    TcStatus disconnect() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        connected_ = false;
        return TC_OK;
    }

    TcStatus is_connected(bool* out_connected) const noexcept {
        if (!out_connected) return TC_ERR_INVALID_ARG;
        std::lock_guard<std::mutex> lock(mutex_);
        *out_connected = connected_;
        return TC_OK;
    }

    TcStatus subscribe_bars(const char* symbol, uint16_t timeframe_sec) noexcept {
        (void)symbol;
        (void)timeframe_sec;
        return TC_OK;
    }

    TcStatus unsubscribe_bars(const char* symbol) noexcept {
        (void)symbol;
        return TC_OK;
    }

    TcStatus place_order(const TcOrder* order, uint64_t* out_client_order_id) noexcept {
        if (!connected_) return TC_ERR_NOT_CONNECTED;
        
        int64_t current_ts_ns = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cursor_ > 0 && cursor_ <= bars_.size()) {
                current_ts_ns = bars_[cursor_ - 1].ts_ns;
            } else if (!bars_.empty()) {
                current_ts_ns = bars_[0].ts_ns;
            }
        }

        return matching_engine_.place_order(order, out_client_order_id, current_ts_ns);
    }

    TcStatus cancel_order(uint64_t client_order_id) noexcept {
        if (!connected_) return TC_ERR_NOT_CONNECTED;

        int64_t current_ts_ns = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cursor_ > 0 && cursor_ <= bars_.size()) {
                current_ts_ns = bars_[cursor_ - 1].ts_ns;
            }
        }

        return matching_engine_.cancel_order(client_order_id, current_ts_ns);
    }

    TcStatus cancel_all_orders(const char* symbol) noexcept {
        if (!connected_) return TC_ERR_NOT_CONNECTED;

        int64_t current_ts_ns = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cursor_ > 0 && cursor_ <= bars_.size()) {
                current_ts_ns = bars_[cursor_ - 1].ts_ns;
            }
        }

        return matching_engine_.cancel_all_orders(symbol, current_ts_ns);
    }

    TcStatus set_bar_sink(TcBarSink sink, void* user_data) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        bar_sink_ = sink;
        bar_user_data_ = user_data;
        return TC_OK;
    }

    TcStatus set_order_event_sink(TcOrderEventSink sink, void* user_data) noexcept {
        matching_engine_.set_order_event_sink(sink, user_data);
        return TC_OK;
    }

    TcStatus set_fill_sink(TcFillSink sink, void* user_data) noexcept {
        matching_engine_.set_fill_sink(sink, user_data);
        return TC_OK;
    }

    TcStatus step(bool* out_has_more) noexcept {
        if (!out_has_more) return TC_ERR_INVALID_ARG;

        TcBar current_bar{};
        TcBarSink current_sink = nullptr;
        void* current_data = nullptr;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!connected_ || cursor_ >= bars_.size()) {
                *out_has_more = false;
                return TC_OK;
            }

            current_bar = bars_[cursor_++];
            bars_published_++;
            *out_has_more = (cursor_ < bars_.size());

            current_sink = bar_sink_;
            current_data = bar_user_data_;
        }

        // Evaluate and match open orders against the current bar
        matching_engine_.process_bar(current_bar);

        // Emit bar to sink (Thread T1 -> T2 ring)
        if (current_sink) {
            current_sink(&current_bar, current_data);
        }

        return TC_OK;
    }

    TcStatus run_replay() noexcept {
        bool has_more = true;
        while (has_more) {
            TcStatus status = step(&has_more);
            if (status != TC_OK) return status;
        }
        return TC_OK;
    }

    TcStatus get_stats(TcGatewayStats* out_stats) const noexcept {
        if (!out_stats) return TC_ERR_INVALID_ARG;

        TcGatewayStats me_stats{};
        matching_engine_.get_stats(&me_stats);

        std::lock_guard<std::mutex> lock(mutex_);
        *out_stats = me_stats;
        out_stats->bars_published = bars_published_;
        return TC_OK;
    }

    TcStatus reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        cursor_ = 0;
        bars_published_ = 0;
        connected_ = false;
        matching_engine_.reset();
        return TC_OK;
    }

    // Helper for direct programmatic data injection (useful for unit tests)
    void inject_bars(const std::vector<TcBar>& bars) {
        std::lock_guard<std::mutex> lock(mutex_);
        bars_ = bars;
        cursor_ = 0;
    }

    size_t get_bar_count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return bars_.size();
    }

private:
    mutable std::mutex mutex_;
    TcGatewayConfig    config_{};
    bool               connected_{false};
    std::vector<TcBar> bars_;
    size_t             cursor_{0};
    uint64_t           bars_published_{0};

    TcBarSink          bar_sink_{nullptr};
    void*              bar_user_data_{nullptr};

    MatchingEngine     matching_engine_;
};

} // namespace tc::gateway

#endif /* TC_GATEWAY_SIM_ENGINE_HPP */
