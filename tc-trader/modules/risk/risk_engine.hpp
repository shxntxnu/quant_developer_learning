#ifndef TC_RISK_ENGINE_HPP
#define TC_RISK_ENGINE_HPP

/**
 * @file risk_engine.hpp
 * @brief High-performance, zero-allocation C++ risk management and sizing engine.
 *
 * Implements capital protection, fixed-fractional position sizing, pre-trade risk checks
 * (max position size, gross leverage, buying power, price collars), and global/symbol kill switches.
 */

#include "tc/tc_abi.h"
#include "tc/tc_types.h"
#include "tc/risk/tc_risk.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <array>
#include <atomic>
#include <algorithm>
#include <string>

namespace tc {

struct SymbolRiskState {
    char    symbol[TC_SYMBOL_MAX]{};
    bool    is_active{false};
    bool    kill_switch{false};
    int64_t net_qty{0};
    TcPrice avg_cost{0};
    TcPrice last_mark_px{0};
};

class RiskEngine {
public:
    static constexpr size_t MAX_SYMBOLS = 128;

    explicit RiskEngine(TcRiskParams params = default_params())
        : params_(params), global_kill_switch_(false) {
        reset();
    }

    static TcRiskParams default_params() noexcept {
        TcRiskParams p{};
        p.struct_size = sizeof(TcRiskParams);
        p.version = 1;
        p.reserved = 0;

        p.risk_fraction_per_trade = 0.01;      /* 1.0% equity risk per trade */
        p.max_position_qty        = 1000;      /* Max 1000 shares/contracts per symbol */
        p.max_position_notional   = TC_DOUBLE_TO_PRICE(100000.0); /* $100,000 max per symbol */
        p.min_position_qty        = 1;         /* Min 1 share */

        p.max_gross_leverage      = 2.0;       /* 2.0x gross leverage */
        p.min_buying_power        = TC_DOUBLE_TO_PRICE(5000.0);   /* $5,000 reserved buffer */
        p.max_drawdown_pct        = 0.05;      /* 5.0% max drawdown */
        p.max_daily_loss          = TC_DOUBLE_TO_PRICE(5000.0);   /* $5,000 max daily loss */

        p.max_price_collar_pct    = 0.03;      /* 3.0% max price deviation */
        p.min_stop_distance_pct   = 0.002;     /* 0.2% min stop distance */

        p.allow_shorting          = true;
        p.auto_kill_on_drawdown   = true;
        return p;
    }

    void reset() noexcept {
        for (auto& s : symbols_) {
            s = SymbolRiskState{};
        }
        account_ = TcAccountView{};
        account_.struct_size = sizeof(TcAccountView);
        account_.version = 1;
        global_kill_switch_.store(false, std::memory_order_relaxed);
        total_evaluations_ = 0;
        total_approvals_ = 0;
        total_rejections_ = 0;
        intent_counter_ = 0;
    }

    [[nodiscard]] const TcRiskParams& params() const noexcept {
        return params_;
    }

    void set_params(const TcRiskParams& params) noexcept {
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

        auto parse_int64 = [json_str](const char* key, int64_t& target) {
            const char* pos = strstr(json_str, key);
            if (pos) {
                pos += strlen(key);
                while (*pos == ' ' || *pos == ':') pos++;
                target = atoll(pos);
            }
        };

        auto parse_price = [json_str](const char* key, TcPrice& target) {
            const char* pos = strstr(json_str, key);
            if (pos) {
                pos += strlen(key);
                while (*pos == ' ' || *pos == ':') pos++;
                double val = atof(pos);
                target = TC_DOUBLE_TO_PRICE(val);
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

        parse_double("\"risk_fraction_per_trade\":", params_.risk_fraction_per_trade);
        parse_int64("\"max_position_qty\":", params_.max_position_qty);
        parse_price("\"max_position_notional\":", params_.max_position_notional);
        parse_int64("\"min_position_qty\":", params_.min_position_qty);
        parse_double("\"max_gross_leverage\":", params_.max_gross_leverage);
        parse_price("\"min_buying_power\":", params_.min_buying_power);
        parse_double("\"max_drawdown_pct\":", params_.max_drawdown_pct);
        parse_price("\"max_daily_loss\":", params_.max_daily_loss);
        parse_double("\"max_price_collar_pct\":", params_.max_price_collar_pct);
        parse_double("\"min_stop_distance_pct\":", params_.min_stop_distance_pct);
        parse_bool("\"allow_shorting\":", params_.allow_shorting);
        parse_bool("\"auto_kill_on_drawdown\":", params_.auto_kill_on_drawdown);

        return TC_OK;
    }

    TcStatus update_account(const TcAccountView& account) noexcept {
        account_ = account;

        if (params_.auto_kill_on_drawdown) {
            if (account.drawdown_pct >= params_.max_drawdown_pct ||
                (params_.max_daily_loss > 0 && account.daily_pnl <= -params_.max_daily_loss) ||
                account.kill_switch_on) {
                global_kill_switch_.store(true, std::memory_order_relaxed);
            }
        }
        return TC_OK;
    }

    TcStatus update_position(const TcPosition& position) noexcept {
        SymbolRiskState* state = find_or_create_symbol(position.symbol);
        if (!state) {
            return TC_ERR_BUFFER_TOO_SMALL;
        }

        state->net_qty = position.net_qty;
        state->avg_cost = position.avg_cost;
        if (position.last_mark_px > 0) {
            state->last_mark_px = position.last_mark_px;
        }
        return TC_OK;
    }

    TcStatus set_kill_switch(const char* symbol, bool active) noexcept {
        if (!symbol || symbol[0] == '\0') {
            global_kill_switch_.store(active, std::memory_order_relaxed);
            return TC_OK;
        }

        SymbolRiskState* state = find_or_create_symbol(symbol);
        if (!state) {
            return TC_ERR_BUFFER_TOO_SMALL;
        }
        state->kill_switch = active;
        return TC_OK;
    }

    TcStatus get_kill_switch(const char* symbol, bool* is_active) const noexcept {
        if (!is_active) {
            return TC_ERR_INVALID_ARG;
        }

        if (!symbol || symbol[0] == '\0') {
            *is_active = global_kill_switch_.load(std::memory_order_relaxed);
            return TC_OK;
        }

        const SymbolRiskState* state = find_symbol(symbol);
        if (!state) {
            *is_active = global_kill_switch_.load(std::memory_order_relaxed);
            return TC_OK;
        }
        *is_active = state->kill_switch || global_kill_switch_.load(std::memory_order_relaxed);
        return TC_OK;
    }

    /**
     * Core risk evaluation and sizing on Thread T2 hot-path.
     * Strictly zero dynamic heap allocation.
     */
    TcStatus evaluate_signal(const TcSignal& signal,
                             TcOrderIntent* intent,
                             TcRiskDecision* decision) noexcept {
        if (!intent || !decision) {
            return TC_ERR_INVALID_ARG;
        }

        total_evaluations_++;

        decision->struct_size = sizeof(TcRiskDecision);
        decision->version = 1;
        decision->code = TC_RISK_APPROVED;
        decision->approved_qty = 0;
        decision->reason[0] = '\0';

        SymbolRiskState* state = find_or_create_symbol(signal.symbol);
        if (!state) {
            set_decision(decision, TC_RISK_REJECT_MAX_EXPOSURE, 0, "Symbol registry capacity exceeded");
            total_rejections_++;
            return TC_OK;
        }

        // 1. Check Global and Symbol Kill Switches
        if (global_kill_switch_.load(std::memory_order_relaxed)) {
            set_decision(decision, TC_RISK_REJECT_KILL_SWITCH, 0, "Global kill switch engaged");
            total_rejections_++;
            return TC_OK;
        }
        if (state->kill_switch) {
            set_decision(decision, TC_RISK_REJECT_KILL_SWITCH, 0, "Symbol kill switch engaged");
            total_rejections_++;
            return TC_OK;
        }

        // 2. Check Account Drawdown Limits
        if (account_.drawdown_pct >= params_.max_drawdown_pct) {
            set_decision(decision, TC_RISK_REJECT_MAX_DRAWDOWN, 0, "Account drawdown threshold breached");
            total_rejections_++;
            return TC_OK;
        }
        if (params_.max_daily_loss > 0 && account_.daily_pnl <= -params_.max_daily_loss) {
            set_decision(decision, TC_RISK_REJECT_MAX_DRAWDOWN, 0, "Max daily session loss breached");
            total_rejections_++;
            return TC_OK;
        }

        // 3. Determine if Signal is an Exit (Reduce-Only) Order
        const bool is_exit_rule = (signal.rule_id >= 200 && signal.rule_id < 300);
        const bool is_reducing = (state->net_qty > 0 && signal.side == TC_SIDE_SELL) ||
                                 (state->net_qty < 0 && signal.side == TC_SIDE_BUY);

        if (is_exit_rule || is_reducing) {
            // Sizing for exit order: close the existing position quantity
            int64_t exit_qty = std::abs(state->net_qty);
            if (exit_qty == 0) {
                // If local book is flat but exit was emitted, use minimum quantity
                exit_qty = params_.min_position_qty;
            }

            populate_intent(intent, signal, exit_qty, TC_INTENT_FLAG_REDUCE_ONLY, 0, 0);
            set_decision(decision, TC_RISK_APPROVED, exit_qty, "Exit order approved (reduce-only)");
            total_approvals_++;
            return TC_OK;
        }

        // ---------------------------------------------------------------------
        // 4. Pre-Trade Checks & Sizing for New Entries
        // ---------------------------------------------------------------------

        // Policy Check: Shorting Allowed?
        if (signal.side == TC_SIDE_SELL && !params_.allow_shorting) {
            set_decision(decision, TC_RISK_REJECT_SHORT_DISALLOWED, 0, "Shorting disallowed by policy");
            total_rejections_++;
            return TC_OK;
        }

        // Reference Price & Price Collar Check
        const TcPrice ref_px = (state->last_mark_px > 0) ? state->last_mark_px : signal.entry_ref_px;
        if (ref_px <= 0 || signal.entry_ref_px <= 0) {
            set_decision(decision, TC_RISK_REJECT_PRICE_COLLAR, 0, "Invalid or missing reference price");
            total_rejections_++;
            return TC_OK;
        }

        const double price_diff = std::abs(TC_PRICE_TO_DOUBLE(signal.entry_ref_px) - TC_PRICE_TO_DOUBLE(ref_px));
        const double price_pct_diff = price_diff / TC_PRICE_TO_DOUBLE(ref_px);
        if (price_pct_diff > params_.max_price_collar_pct) {
            set_decision(decision, TC_RISK_REJECT_PRICE_COLLAR, 0, "Price deviates beyond collar tolerance");
            total_rejections_++;
            return TC_OK;
        }

        // Stop Loss Distance Validation
        TcPrice stop_dist_fixed = 0;
        if (signal.side == TC_SIDE_BUY) {
            stop_dist_fixed = signal.entry_ref_px - signal.stop_px;
        } else {
            stop_dist_fixed = signal.stop_px - signal.entry_ref_px;
        }

        if (stop_dist_fixed <= 0) {
            set_decision(decision, TC_RISK_REJECT_INVALID_STOP, 0, "Stop loss price must be below long or above short entry");
            total_rejections_++;
            return TC_OK;
        }

        const double stop_dist_double = TC_PRICE_TO_DOUBLE(stop_dist_fixed);
        const double min_stop_dist = params_.min_stop_distance_pct * TC_PRICE_TO_DOUBLE(signal.entry_ref_px);
        if (stop_dist_double < min_stop_dist) {
            set_decision(decision, TC_RISK_REJECT_INVALID_STOP, 0, "Stop distance tighter than minimum threshold");
            total_rejections_++;
            return TC_OK;
        }

        // Capital & Equity Validation
        const double equity = (account_.net_liquidation > 0) ?
                              TC_PRICE_TO_DOUBLE(account_.net_liquidation) :
                              TC_PRICE_TO_DOUBLE(account_.total_cash);
        if (equity <= 0.0) {
            set_decision(decision, TC_RISK_REJECT_BUYING_POWER, 0, "Account equity zero or negative");
            total_rejections_++;
            return TC_OK;
        }

        // Fixed-Fractional Sizing Model:
        // Quantity = (Equity * RiskFraction) / StopDistance
        const double dollar_risk = equity * params_.risk_fraction_per_trade;
        int64_t target_qty = static_cast<int64_t>(std::floor(dollar_risk / stop_dist_double));

        if (target_qty < params_.min_position_qty) {
            set_decision(decision, TC_RISK_REJECT_MAX_POS_SIZE, 0, "Sized quantity below minimum order size");
            total_rejections_++;
            return TC_OK;
        }

        // Limit Check: Max Single Position Quantity
        const int64_t projected_qty = std::abs(state->net_qty) + target_qty;
        if (projected_qty > params_.max_position_qty) {
            const int64_t allowed_qty = params_.max_position_qty - std::abs(state->net_qty);
            if (allowed_qty < params_.min_position_qty) {
                set_decision(decision, TC_RISK_REJECT_MAX_POS_SIZE, 0, "Order exceeds max position limit");
                total_rejections_++;
                return TC_OK;
            }
            target_qty = allowed_qty;
        }

        // Limit Check: Max Position Notional
        if (params_.max_position_notional > 0) {
            const TcPrice entry_px = signal.entry_ref_px;
            const TcPrice order_notional = target_qty * entry_px;
            if (order_notional > params_.max_position_notional) {
                const int64_t max_notional_qty = params_.max_position_notional / entry_px;
                if (max_notional_qty < params_.min_position_qty) {
                    set_decision(decision, TC_RISK_REJECT_MAX_POS_SIZE, 0, "Order notional exceeds max symbol limit");
                    total_rejections_++;
                    return TC_OK;
                }
                target_qty = max_notional_qty;
            }
        }

        // Limit Check: Available Buying Power with Buffer
        if (account_.buying_power > 0) {
            const TcPrice available_bp = account_.buying_power - params_.min_buying_power;
            const TcPrice order_cost = target_qty * signal.entry_ref_px;
            if (order_cost > available_bp) {
                if (available_bp <= 0) {
                    set_decision(decision, TC_RISK_REJECT_BUYING_POWER, 0, "Insufficient buying power (below buffer)");
                    total_rejections_++;
                    return TC_OK;
                }
                const int64_t affordable_qty = available_bp / signal.entry_ref_px;
                if (affordable_qty < params_.min_position_qty) {
                    set_decision(decision, TC_RISK_REJECT_BUYING_POWER, 0, "Insufficient buying power for minimum size");
                    total_rejections_++;
                    return TC_OK;
                }
                target_qty = affordable_qty;
            }
        }

        // Limit Check: Gross Portfolio Leverage
        if (params_.max_gross_leverage > 0.0) {
            const double current_gross_exp = calculate_gross_exposure();
            const double order_notional_double = target_qty * TC_PRICE_TO_DOUBLE(signal.entry_ref_px);
            const double max_allowed_exp = equity * params_.max_gross_leverage;

            if ((current_gross_exp + order_notional_double) > max_allowed_exp) {
                const double allowable_notional = max_allowed_exp - current_gross_exp;
                if (allowable_notional <= 0.0) {
                    set_decision(decision, TC_RISK_REJECT_MAX_EXPOSURE, 0, "Gross portfolio leverage limit breached");
                    total_rejections_++;
                    return TC_OK;
                }
                const int64_t max_leverage_qty = static_cast<int64_t>(std::floor(allowable_notional / TC_PRICE_TO_DOUBLE(signal.entry_ref_px)));
                if (max_leverage_qty < params_.min_position_qty) {
                    set_decision(decision, TC_RISK_REJECT_MAX_EXPOSURE, 0, "Gross leverage limit prevents minimum size");
                    total_rejections_++;
                    return TC_OK;
                }
                target_qty = max_leverage_qty;
            }
        }

        // Order Approved and Sized!
        populate_intent(intent, signal, target_qty, TC_INTENT_FLAG_BRACKET, signal.stop_px, signal.target_px);
        set_decision(decision, TC_RISK_APPROVED, target_qty, "Order sized and risk-approved");
        total_approvals_++;
        return TC_OK;
    }

    [[nodiscard]] uint64_t total_evaluations() const noexcept { return total_evaluations_; }
    [[nodiscard]] uint64_t total_approvals() const noexcept { return total_approvals_; }
    [[nodiscard]] uint64_t total_rejections() const noexcept { return total_rejections_; }

private:
    TcRiskParams params_{};
    TcAccountView account_{};
    std::atomic<bool> global_kill_switch_{false};
    std::array<SymbolRiskState, MAX_SYMBOLS> symbols_{};

    uint64_t total_evaluations_{0};
    uint64_t total_approvals_{0};
    uint64_t total_rejections_{0};
    uint64_t intent_counter_{0};

    [[nodiscard]] double calculate_gross_exposure() const noexcept {
        double total_exp = 0.0;
        for (const auto& s : symbols_) {
            if (s.is_active && s.net_qty != 0) {
                const TcPrice px = (s.last_mark_px > 0) ? s.last_mark_px : s.avg_cost;
                total_exp += std::abs(s.net_qty) * TC_PRICE_TO_DOUBLE(px);
            }
        }
        return total_exp;
    }

    SymbolRiskState* find_or_create_symbol(const char* symbol) noexcept {
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

    [[nodiscard]] const SymbolRiskState* find_symbol(const char* symbol) const noexcept {
        for (const auto& s : symbols_) {
            if (s.is_active && strncmp(s.symbol, symbol, TC_SYMBOL_MAX) == 0) {
                return &s;
            }
        }
        return nullptr;
    }

    void populate_intent(TcOrderIntent* intent, const TcSignal& signal, int64_t qty,
                         uint16_t flags, TcPrice stop_px, TcPrice target_px) noexcept {
        intent->struct_size = sizeof(TcOrderIntent);
        intent->version = 1;
        intent->tif = TC_TIF_DAY;
        const size_t len = std::min(strlen(signal.symbol), sizeof(intent->symbol) - 1);
        memcpy(intent->symbol, signal.symbol, len);
        intent->symbol[len] = '\0';
        intent->ts_ns = signal.ts_ns;
        intent->intent_id = ++intent_counter_;
        intent->side = signal.side;
        intent->order_type = signal.order_type;
        intent->flags = flags;
        intent->reserved = 0;
        intent->qty = qty;
        intent->limit_px = 0; /* Market entry */
        intent->stop_loss_px = stop_px;
        intent->take_profit_px = target_px;
    }

    static void set_decision(TcRiskDecision* decision, int16_t code, int64_t approved_qty, const char* reason) noexcept {
        decision->code = code;
        decision->approved_qty = approved_qty;
        const size_t len = std::min(strlen(reason), sizeof(decision->reason) - 1);
        memcpy(decision->reason, reason, len);
        decision->reason[len] = '\0';
    }
};

} // namespace tc

#endif /* TC_RISK_ENGINE_HPP */
