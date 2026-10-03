#ifndef TC_STRATEGY_ENGINE_HPP
#define TC_STRATEGY_ENGINE_HPP

/**
 * @file strategy_engine.hpp
 * @brief High-performance, zero-allocation C++ strategy engine.
 *
 * Implements deterministic market regime classification, exit rule evaluation
 * (ATR trailing stops, take-profit targets, MA/MACD reversals, max holding duration),
 * and entry rule evaluation (trend-following MA/MACD crossover, mean-reversion Bollinger bands).
 */

#include "tc/tc_abi.h"
#include "tc/tc_types.h"
#include "tc/strategy/tc_strategy.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <array>
#include <algorithm>
#include <string>

namespace tc {

/**
 * Per-symbol tracking state maintained on Thread T2 with zero dynamic memory allocation.
 */
struct SymbolState {
    char           symbol[TC_SYMBOL_MAX]{};
    bool           is_active{false};
    bool           in_position{false};
    int8_t         side{0};            /**< +1 long, -1 short, 0 flat */
    int64_t        net_qty{0};
    TcPrice        entry_price{0};
    int64_t        entry_ts_ns{0};
    TcPrice        peak_price{0};      /**< Highest mark seen for trailing stop (long) */
    TcPrice        trough_price{0};    /**< Lowest mark seen for trailing stop (short) */
    TcMarketRegime last_regime{TC_REGIME_UNKNOWN};
    int64_t        last_eval_ts_ns{0};
};

class StrategyEngine {
public:
    static constexpr size_t MAX_SYMBOLS = 128;

    explicit StrategyEngine(TcStrategyParams params = default_params())
        : params_(params) {
        reset();
    }

    static TcStrategyParams default_params() noexcept {
        TcStrategyParams p{};
        p.struct_size = sizeof(TcStrategyParams);
        p.version = 1;
        p.reserved = 0;

        p.adx_trend_threshold    = 25.0;
        p.atr_vol_multiplier     = 2.5;
        p.bb_bandwidth_threshold = 0.15;

        p.rsi_oversold   = 30.0;
        p.rsi_overbought = 70.0;
        p.rsi_bull_min   = 50.0;
        p.rsi_bear_max   = 50.0;

        p.atr_stop_multiplier   = 2.0;
        p.atr_target_multiplier = 3.0;
        p.max_holding_ns        = 14400000000000LL; /* 4 hours = 4 * 3600 * 1e9 ns */

        p.enable_trend          = true;
        p.enable_mean_reversion = true;
        p.enable_trailing_stop  = true;
        p.enable_take_profit    = true;
        p.enable_ma_reversal    = true;
        p.enable_max_holding    = true;
        return p;
    }

    void reset() noexcept {
        for (auto& s : symbols_) {
            s = SymbolState{};
        }
        total_evaluations_ = 0;
        total_signals_emitted_ = 0;
    }

    [[nodiscard]] const TcStrategyParams& params() const noexcept {
        return params_;
    }

    void set_params(const TcStrategyParams& params) noexcept {
        params_ = params;
    }

    TcStatus configure_json(const char* json_str) noexcept {
        if (!json_str || strlen(json_str) == 0) {
            return TC_OK;
        }

        auto parse_double = [json_str](const char* key, double& target) {
            const char* pos = strstr(json_str, key);
            if (pos) {
                pos += strlen(key);
                while (*pos == ' ' || *pos == ':') pos++;
                target = atof(pos);
            }
        };

        auto parse_bool = [json_str](const char* key, bool& target) {
            const char* pos = strstr(json_str, key);
            if (pos) {
                pos += strlen(key);
                while (*pos == ' ' || *pos == ':') pos++;
                if (strncmp(pos, "true", 4) == 0 || *pos == '1') {
                    target = true;
                } else if (strncmp(pos, "false", 5) == 0 || *pos == '0') {
                    target = false;
                }
            }
        };

        parse_double("\"adx_trend_threshold\":", params_.adx_trend_threshold);
        parse_double("\"atr_vol_multiplier\":", params_.atr_vol_multiplier);
        parse_double("\"bb_bandwidth_threshold\":", params_.bb_bandwidth_threshold);
        parse_double("\"rsi_oversold\":", params_.rsi_oversold);
        parse_double("\"rsi_overbought\":", params_.rsi_overbought);
        parse_double("\"rsi_bull_min\":", params_.rsi_bull_min);
        parse_double("\"rsi_bear_max\":", params_.rsi_bear_max);
        parse_double("\"atr_stop_multiplier\":", params_.atr_stop_multiplier);
        parse_double("\"atr_target_multiplier\":", params_.atr_target_multiplier);

        const char* p_hold = strstr(json_str, "\"max_holding_ns\":");
        if (p_hold) {
            p_hold += strlen("\"max_holding_ns\":");
            while (*p_hold == ' ' || *p_hold == ':') p_hold++;
            params_.max_holding_ns = static_cast<int64_t>(atoll(p_hold));
        }

        parse_bool("\"enable_trend\":", params_.enable_trend);
        parse_bool("\"enable_mean_reversion\":", params_.enable_mean_reversion);
        parse_bool("\"enable_trailing_stop\":", params_.enable_trailing_stop);
        parse_bool("\"enable_take_profit\":", params_.enable_take_profit);
        parse_bool("\"enable_ma_reversal\":", params_.enable_ma_reversal);
        parse_bool("\"enable_max_holding\":", params_.enable_max_holding);

        return TC_OK;
    }

    [[nodiscard]] TcMarketRegime classify_regime(const TcIndicatorSnapshot& snapshot) const noexcept {
        if (!(snapshot.valid_mask & TC_IND_MASK_ADX) || !(snapshot.valid_mask & TC_IND_MASK_SMA_FAST)) {
            return TC_REGIME_UNKNOWN;
        }

        // Check high volatility first via Bollinger bandwidth
        if ((snapshot.valid_mask & TC_IND_MASK_BB) && snapshot.bb_middle > 0.0) {
            const double bandwidth = (snapshot.bb_upper - snapshot.bb_lower) / snapshot.bb_middle;
            if (bandwidth > params_.bb_bandwidth_threshold) {
                return TC_REGIME_HIGH_VOLATILITY;
            }
        }

        // Check trending regimes via ADX and moving averages
        if (snapshot.adx >= params_.adx_trend_threshold) {
            if (snapshot.sma_fast > snapshot.sma_slow) {
                return TC_REGIME_TRENDING_BULL;
            } else if (snapshot.sma_fast < snapshot.sma_slow) {
                return TC_REGIME_TRENDING_BEAR;
            }
        }

        return TC_REGIME_RANGING;
    }

    /**
     * Pure function hot-path evaluation:
     * Takes an indicator snapshot and current position view, evaluates exit rules
     * followed by entry rules (if flat), emitting up to max_signals into signals_out.
     */
    TcStatus on_snapshot(const TcIndicatorSnapshot& snapshot,
                         const TcPositionView* position,
                         TcSignal* signals_out,
                         size_t max_signals,
                         size_t* num_signals) noexcept {
        if (!signals_out || !num_signals || max_signals == 0) {
            return TC_ERR_INVALID_ARG;
        }

        *num_signals = 0;
        total_evaluations_++;

        SymbolState* state = find_or_create_symbol(snapshot.symbol);
        if (!state) {
            return TC_ERR_BUFFER_TOO_SMALL;
        }

        // Resolve current price
        TcPrice cur_price_fixed = 0;
        if (position && position->last_mark_px > 0) {
            cur_price_fixed = position->last_mark_px;
        } else if (snapshot.last_close > 0.0) {
            cur_price_fixed = TC_DOUBLE_TO_PRICE(snapshot.last_close);
        } else if (snapshot.bb_middle > 0.0) {
            cur_price_fixed = TC_DOUBLE_TO_PRICE(snapshot.bb_middle);
        }

        const double cur_price_double = TC_PRICE_TO_DOUBLE(cur_price_fixed);
        const TcPrice atr_fixed = TC_DOUBLE_TO_PRICE(snapshot.atr);

        // Sync position state
        if (position && position->net_qty != 0) {
            if (!state->in_position || (state->net_qty == 0)) {
                // Newly opened position
                state->in_position = true;
                state->net_qty = position->net_qty;
                state->side = (position->net_qty > 0) ? 1 : -1;
                state->entry_price = position->avg_cost > 0 ? position->avg_cost : cur_price_fixed;
                state->entry_ts_ns = snapshot.ts_ns;
                state->peak_price = cur_price_fixed;
                state->trough_price = cur_price_fixed;
            } else {
                // Update ongoing position tracking
                state->net_qty = position->net_qty;
                state->side = (position->net_qty > 0) ? 1 : -1;
                if (cur_price_fixed > state->peak_price) {
                    state->peak_price = cur_price_fixed;
                }
                if (state->trough_price == 0 || cur_price_fixed < state->trough_price) {
                    state->trough_price = cur_price_fixed;
                }
            }
        } else {
            // Flat position
            state->in_position = false;
            state->net_qty = 0;
            state->side = 0;
            state->entry_price = 0;
            state->peak_price = 0;
            state->trough_price = 0;
        }

        const TcMarketRegime regime = classify_regime(snapshot);
        state->last_regime = regime;
        state->last_eval_ts_ns = snapshot.ts_ns;

        // ---------------------------------------------------------------------
        // 1. EVALUATE EXIT RULES (if currently in a position)
        // ---------------------------------------------------------------------
        if (state->in_position && state->net_qty != 0) {
            TcSignal exit_sig{};
            bool trigger_exit = false;

            // (A) ATR Trailing Stop
            if (params_.enable_trailing_stop && atr_fixed > 0) {
                const TcPrice stop_distance = static_cast<TcPrice>(params_.atr_stop_multiplier * static_cast<double>(atr_fixed));
                if (state->side > 0) {
                    // Long trailing stop: peak - distance
                    const TcPrice trailing_stop_level = state->peak_price - stop_distance;
                    if (cur_price_fixed <= trailing_stop_level) {
                        exit_sig = create_signal(snapshot.symbol, snapshot.ts_ns, TC_SIDE_SELL,
                                                 TC_RULE_EXIT_TRAILING_STOP, 1.0, cur_price_fixed,
                                                 trailing_stop_level, 0);
                        trigger_exit = true;
                    }
                } else {
                    // Short trailing stop: trough + distance
                    const TcPrice trailing_stop_level = state->trough_price + stop_distance;
                    if (cur_price_fixed >= trailing_stop_level) {
                        exit_sig = create_signal(snapshot.symbol, snapshot.ts_ns, TC_SIDE_BUY,
                                                 TC_RULE_EXIT_TRAILING_STOP, 1.0, cur_price_fixed,
                                                 trailing_stop_level, 0);
                        trigger_exit = true;
                    }
                }
            }

            // (B) Profit Target
            if (!trigger_exit && params_.enable_take_profit && atr_fixed > 0 && state->entry_price > 0) {
                const TcPrice target_distance = static_cast<TcPrice>(params_.atr_target_multiplier * static_cast<double>(atr_fixed));
                if (state->side > 0) {
                    const TcPrice target_level = state->entry_price + target_distance;
                    if (cur_price_fixed >= target_level) {
                        exit_sig = create_signal(snapshot.symbol, snapshot.ts_ns, TC_SIDE_SELL,
                                                 TC_RULE_EXIT_TAKE_PROFIT, 1.0, cur_price_fixed,
                                                 0, target_level);
                        trigger_exit = true;
                    }
                } else {
                    const TcPrice target_level = state->entry_price - target_distance;
                    if (cur_price_fixed <= target_level) {
                        exit_sig = create_signal(snapshot.symbol, snapshot.ts_ns, TC_SIDE_BUY,
                                                 TC_RULE_EXIT_TAKE_PROFIT, 1.0, cur_price_fixed,
                                                 0, target_level);
                        trigger_exit = true;
                    }
                }
            }

            // (C) Moving Average Reversal
            if (!trigger_exit && params_.enable_ma_reversal && (snapshot.valid_mask & TC_IND_MASK_SMA_FAST) && (snapshot.valid_mask & TC_IND_MASK_SMA_SLOW)) {
                if (state->side > 0 && snapshot.sma_fast < snapshot.sma_slow) {
                    exit_sig = create_signal(snapshot.symbol, snapshot.ts_ns, TC_SIDE_SELL,
                                             TC_RULE_EXIT_MA_REVERSAL, 0.85, cur_price_fixed,
                                             0, 0);
                    trigger_exit = true;
                } else if (state->side < 0 && snapshot.sma_fast > snapshot.sma_slow) {
                    exit_sig = create_signal(snapshot.symbol, snapshot.ts_ns, TC_SIDE_BUY,
                                             TC_RULE_EXIT_MA_REVERSAL, 0.85, cur_price_fixed,
                                             0, 0);
                    trigger_exit = true;
                }
            }

            // (D) Max Holding Duration
            if (!trigger_exit && params_.enable_max_holding && params_.max_holding_ns > 0 && state->entry_ts_ns > 0) {
                if ((snapshot.ts_ns - state->entry_ts_ns) >= params_.max_holding_ns) {
                    const int8_t exit_side = (state->side > 0) ? TC_SIDE_SELL : TC_SIDE_BUY;
                    exit_sig = create_signal(snapshot.symbol, snapshot.ts_ns, exit_side,
                                             TC_RULE_EXIT_MAX_HOLDING, 0.70, cur_price_fixed,
                                             0, 0);
                    trigger_exit = true;
                }
            }

            if (trigger_exit) {
                signals_out[0] = exit_sig;
                *num_signals = 1;
                total_signals_emitted_++;
                return TC_OK;
            }

            // While in position, do not evaluate new entries
            return TC_OK;
        }

        // ---------------------------------------------------------------------
        // 2. EVALUATE ENTRY RULES (only when flat)
        // ---------------------------------------------------------------------
        // Verify indicators are ready for entry evaluation
        constexpr uint32_t REQUIRED_ENTRY_MASK = TC_IND_MASK_SMA_FAST | TC_IND_MASK_SMA_SLOW |
                                                 TC_IND_MASK_RSI | TC_IND_MASK_ATR | TC_IND_MASK_BB;
        if ((snapshot.valid_mask & REQUIRED_ENTRY_MASK) != REQUIRED_ENTRY_MASK) {
            return TC_OK;
        }

        if (cur_price_fixed <= 0) {
            return TC_OK;
        }

        // (A) Trend Following Entries
        if (params_.enable_trend) {
            if (regime == TC_REGIME_TRENDING_BULL) {
                // Bullish Trend Entry
                const bool ma_bull = snapshot.sma_fast > snapshot.sma_slow;
                const bool macd_bull = !(snapshot.valid_mask & TC_IND_MASK_MACD) || (snapshot.macd > snapshot.macd_signal);
                const bool rsi_bull = snapshot.rsi >= params_.rsi_bull_min && snapshot.rsi <= params_.rsi_overbought;

                if (ma_bull && macd_bull && rsi_bull) {
                    const TcPrice stop_px = cur_price_fixed - static_cast<TcPrice>(params_.atr_stop_multiplier * static_cast<double>(atr_fixed));
                    const TcPrice target_px = cur_price_fixed + static_cast<TcPrice>(params_.atr_target_multiplier * static_cast<double>(atr_fixed));

                    signals_out[0] = create_signal(snapshot.symbol, snapshot.ts_ns, TC_SIDE_BUY,
                                                   TC_RULE_ENTRY_TREND_BULL, 0.90, cur_price_fixed,
                                                   stop_px, target_px);
                    *num_signals = 1;
                    total_signals_emitted_++;
                    return TC_OK;
                }
            } else if (regime == TC_REGIME_TRENDING_BEAR) {
                // Bearish Trend Entry
                const bool ma_bear = snapshot.sma_fast < snapshot.sma_slow;
                const bool macd_bear = !(snapshot.valid_mask & TC_IND_MASK_MACD) || (snapshot.macd < snapshot.macd_signal);
                const bool rsi_bear = snapshot.rsi <= params_.rsi_bear_max && snapshot.rsi >= params_.rsi_oversold;

                if (ma_bear && macd_bear && rsi_bear) {
                    const TcPrice stop_px = cur_price_fixed + static_cast<TcPrice>(params_.atr_stop_multiplier * static_cast<double>(atr_fixed));
                    const TcPrice target_px = cur_price_fixed - static_cast<TcPrice>(params_.atr_target_multiplier * static_cast<double>(atr_fixed));

                    signals_out[0] = create_signal(snapshot.symbol, snapshot.ts_ns, TC_SIDE_SELL,
                                                   TC_RULE_ENTRY_TREND_BEAR, 0.90, cur_price_fixed,
                                                   stop_px, target_px);
                    *num_signals = 1;
                    total_signals_emitted_++;
                    return TC_OK;
                }
            }
        }

        // (B) Mean Reversion Entries (Ranging Regime)
        if (params_.enable_mean_reversion && regime == TC_REGIME_RANGING) {
            // Oversold Bounce (Long)
            if (cur_price_double <= snapshot.bb_lower && snapshot.rsi <= params_.rsi_oversold) {
                const TcPrice stop_px = cur_price_fixed - static_cast<TcPrice>(1.5 * static_cast<double>(atr_fixed));
                const TcPrice target_px = TC_DOUBLE_TO_PRICE(snapshot.bb_middle);

                signals_out[0] = create_signal(snapshot.symbol, snapshot.ts_ns, TC_SIDE_BUY,
                                               TC_RULE_ENTRY_MEAN_REV_LONG, 0.85, cur_price_fixed,
                                               stop_px, target_px);
                *num_signals = 1;
                total_signals_emitted_++;
                return TC_OK;
            }
            // Overbought Fade (Short)
            else if (cur_price_double >= snapshot.bb_upper && snapshot.rsi >= params_.rsi_overbought) {
                const TcPrice stop_px = cur_price_fixed + static_cast<TcPrice>(1.5 * static_cast<double>(atr_fixed));
                const TcPrice target_px = TC_DOUBLE_TO_PRICE(snapshot.bb_middle);

                signals_out[0] = create_signal(snapshot.symbol, snapshot.ts_ns, TC_SIDE_SELL,
                                               TC_RULE_ENTRY_MEAN_REV_SHORT, 0.85, cur_price_fixed,
                                               stop_px, target_px);
                *num_signals = 1;
                total_signals_emitted_++;
                return TC_OK;
            }
        }

        // In HIGH_VOLATILITY or neutral ranging, no entry signal is emitted (capital preservation)
        return TC_OK;
    }

    [[nodiscard]] uint64_t total_evaluations() const noexcept { return total_evaluations_; }
    [[nodiscard]] uint64_t total_signals_emitted() const noexcept { return total_signals_emitted_; }

private:
    TcStrategyParams params_{};
    std::array<SymbolState, MAX_SYMBOLS> symbols_{};
    uint64_t total_evaluations_{0};
    uint64_t total_signals_emitted_{0};

    SymbolState* find_or_create_symbol(const char* symbol) noexcept {
        for (auto& s : symbols_) {
            if (s.is_active && strncmp(s.symbol, symbol, TC_SYMBOL_MAX) == 0) {
                return &s;
            }
        }
        for (auto& s : symbols_) {
            if (!s.is_active) {
                s.is_active = true;
                const size_t len = std::min(strlen(symbol), static_cast<size_t>(TC_SYMBOL_MAX - 1));
                memcpy(s.symbol, symbol, len);
                s.symbol[len] = '\0';
                return &s;
            }
        }
        return nullptr;
    }

    static TcSignal create_signal(const char* symbol, int64_t ts_ns, int8_t side,
                                  uint32_t rule_id, double strength,
                                  TcPrice entry_ref_px, TcPrice stop_px, TcPrice target_px) noexcept {
        TcSignal sig{};
        sig.struct_size = sizeof(TcSignal);
        sig.version = 1;
        sig.side = side;
        sig.order_type = 1; /* Market order */
        const size_t len = std::min(strlen(symbol), sizeof(sig.symbol) - 1);
        memcpy(sig.symbol, symbol, len);
        sig.symbol[len] = '\0';
        sig.ts_ns = ts_ns;
        sig.rule_id = rule_id;
        sig.flags = 0;
        sig.strength = strength;
        sig.entry_ref_px = entry_ref_px;
        sig.stop_px = stop_px;
        sig.target_px = target_px;
        return sig;
    }
};

} // namespace tc

#endif /* TC_STRATEGY_ENGINE_HPP */
