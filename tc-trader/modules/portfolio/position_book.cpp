#include "position_book.hpp"
#include "tc/tc_time.hpp"

#include <cstring>
#include <algorithm>
#include <cmath>

namespace tc {

PositionBook::PositionBook(TcPrice initial_cash)
    : initial_cash_(initial_cash),
      current_cash_(initial_cash),
      net_liquidation_(initial_cash),
      peak_equity_(initial_cash),
      daily_pnl_(0),
      last_update_ts_(now_utc_ns()) {
}

TcStatus PositionBook::apply_fill(const TcFill& fill) {
    if (fill.fill_qty <= 0 || fill.fill_px <= 0) {
        return TC_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    last_update_ts_ = fill.ts_ns > 0 ? fill.ts_ns : now_utc_ns();

    std::string sym(fill.symbol);
    auto& pos = positions_[sym];
    if (pos.symbol[0] == '\0') {
        std::strncpy(pos.symbol, fill.symbol, sizeof(pos.symbol) - 1);
        pos.last_mark_px = fill.fill_px;
    }

    const TcPrice fill_cost_notional = fill.fill_qty * fill.fill_px;
    const TcPrice commission_price = TC_DOUBLE_TO_PRICE(fill.commission);
    total_commissions_paid_ += fill.commission;
    pos.total_commissions += fill.commission;
    pos.total_shares_traded += fill.fill_qty;
    pos.last_mark_px = fill.fill_px;

    if (fill.side == 1) { // BUY Fill
        // Deduct cash for purchase + commission
        current_cash_ -= (fill_cost_notional + commission_price);

        if (pos.net_qty >= 0) {
            // Case 1: Opening or adding to LONG position (weighted average cost)
            const int64_t new_qty = pos.net_qty + fill.fill_qty;
            const TcPrice total_cost = (pos.net_qty * pos.avg_cost) + fill_cost_notional;
            pos.avg_cost = total_cost / new_qty;
            pos.net_qty = new_qty;
        } else {
            // Case 2: Covering an existing SHORT position
            const int64_t short_qty = -pos.net_qty;
            if (fill.fill_qty <= short_qty) {
                // Partial or full short cover (PnL = covered_shares * (avg_cost - fill_px))
                const TcPrice realized = fill.fill_qty * (pos.avg_cost - fill.fill_px) - commission_price;
                pos.realized_pnl += realized;
                pos.net_qty += fill.fill_qty;
                if (pos.net_qty == 0) {
                    pos.avg_cost = 0;
                }
            } else {
                // Short cover + reversal into new LONG
                const int64_t covered_qty = short_qty;
                const int64_t remaining_long = fill.fill_qty - covered_qty;
                const TcPrice realized = covered_qty * (pos.avg_cost - fill.fill_px) - commission_price;
                pos.realized_pnl += realized;
                pos.net_qty = remaining_long;
                pos.avg_cost = fill.fill_px;
            }
        }
    } else if (fill.side == -1) { // SELL Fill
        // Add cash from sale - commission
        current_cash_ += (fill_cost_notional - commission_price);

        if (pos.net_qty <= 0) {
            // Case 1: Opening or adding to SHORT position (weighted average cost)
            const int64_t current_short = -pos.net_qty;
            const int64_t new_short = current_short + fill.fill_qty;
            const TcPrice total_cost = (current_short * pos.avg_cost) + fill_cost_notional;
            pos.avg_cost = total_cost / new_short;
            pos.net_qty = -new_short;
        } else {
            // Case 2: Reducing or closing an existing LONG position
            if (fill.fill_qty <= pos.net_qty) {
                // Partial or full long reduction (PnL = sold_shares * (fill_px - avg_cost))
                const TcPrice realized = fill.fill_qty * (fill.fill_px - pos.avg_cost) - commission_price;
                pos.realized_pnl += realized;
                pos.net_qty -= fill.fill_qty;
                if (pos.net_qty == 0) {
                    pos.avg_cost = 0;
                }
            } else {
                // Long close + reversal into new SHORT
                const int64_t closed_long = pos.net_qty;
                const int64_t remaining_short = fill.fill_qty - closed_long;
                const TcPrice realized = closed_long * (fill.fill_px - pos.avg_cost) - commission_price;
                pos.realized_pnl += realized;
                pos.net_qty = -remaining_short;
                pos.avg_cost = fill.fill_px;
            }
        }
    } else {
        return TC_ERR_INVALID_ARG;
    }

    update_account_metrics_locked();
    return TC_OK;
}

TcStatus PositionBook::update_mark(const char* symbol, TcPrice mark_px) {
    if (!symbol || mark_px <= 0) {
        return TC_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    last_update_ts_ = now_utc_ns();

    std::string sym(symbol);
    auto it = positions_.find(sym);
    if (it != positions_.end()) {
        it->second.last_mark_px = mark_px;
        if (it->second.net_qty > 0) {
            it->second.unrealized_pnl = it->second.net_qty * (mark_px - it->second.avg_cost);
        } else if (it->second.net_qty < 0) {
            it->second.unrealized_pnl = (-it->second.net_qty) * (it->second.avg_cost - mark_px);
        } else {
            it->second.unrealized_pnl = 0;
        }
    } else {
        // Track symbol for future mark-to-market
        auto& pos = positions_[sym];
        std::strncpy(pos.symbol, symbol, sizeof(pos.symbol) - 1);
        pos.last_mark_px = mark_px;
        pos.avg_cost = 0;
        pos.net_qty = 0;
        pos.unrealized_pnl = 0;
    }

    update_account_metrics_locked();
    return TC_OK;
}

void PositionBook::update_account_metrics_locked() {
    TcPrice total_market_value = 0;

    for (auto& [_, pos] : positions_) {
        if (pos.net_qty != 0 && pos.last_mark_px > 0) {
            if (pos.net_qty > 0) {
                pos.unrealized_pnl = pos.net_qty * (pos.last_mark_px - pos.avg_cost);
            } else {
                pos.unrealized_pnl = (-pos.net_qty) * (pos.avg_cost - pos.last_mark_px);
            }
            total_market_value += pos.net_qty * pos.last_mark_px;
        } else {
            pos.unrealized_pnl = 0;
        }
    }

    // Net Liquidation Value = Settled Cash + Market Value of all positions
    net_liquidation_ = current_cash_ + total_market_value;
    daily_pnl_ = net_liquidation_ - initial_cash_;

    if (net_liquidation_ > peak_equity_) {
        peak_equity_ = net_liquidation_;
    }

    if (peak_equity_ > 0 && net_liquidation_ < peak_equity_) {
        drawdown_pct_ = static_cast<double>(peak_equity_ - net_liquidation_) / static_cast<double>(peak_equity_);
    } else {
        drawdown_pct_ = 0.0;
    }
}

TcStatus PositionBook::get_position(const char* symbol, TcPosition& out_pos) const {
    if (!symbol) {
        return TC_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = positions_.find(symbol);
    if (it == positions_.end()) {
        return TC_ERR_NOT_FOUND;
    }

    const auto& pos = it->second;
    out_pos.struct_size = sizeof(TcPosition);
    out_pos.version = 1;
    out_pos.reserved = 0;
    std::strncpy(out_pos.symbol, pos.symbol, sizeof(out_pos.symbol) - 1);
    out_pos.symbol[sizeof(out_pos.symbol) - 1] = '\0';
    out_pos.net_qty = pos.net_qty;
    out_pos.avg_cost = pos.avg_cost;
    out_pos.realized_pnl = pos.realized_pnl;
    out_pos.unrealized_pnl = pos.unrealized_pnl;
    out_pos.last_mark_px = pos.last_mark_px;

    return TC_OK;
}

TcStatus PositionBook::get_all_positions(TcPosition* buffer, size_t capacity, size_t& count_out) const {
    if (!buffer || capacity == 0) {
        return TC_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    count_out = 0;

    for (const auto& [_, pos] : positions_) {
        if (count_out >= capacity) break;

        auto& p = buffer[count_out];
        p.struct_size = sizeof(TcPosition);
        p.version = 1;
        p.reserved = 0;
        std::strncpy(p.symbol, pos.symbol, sizeof(p.symbol) - 1);
        p.symbol[sizeof(p.symbol) - 1] = '\0';
        p.net_qty = pos.net_qty;
        p.avg_cost = pos.avg_cost;
        p.realized_pnl = pos.realized_pnl;
        p.unrealized_pnl = pos.unrealized_pnl;
        p.last_mark_px = pos.last_mark_px;

        count_out++;
    }

    return TC_OK;
}

TcStatus PositionBook::get_account(TcAccountView& out_acct) const {
    std::lock_guard<std::mutex> lock(mutex_);

    out_acct.struct_size = sizeof(TcAccountView);
    out_acct.version = 1;
    out_acct.reserved = 0;
    out_acct.ts_ns = last_update_ts_;
    out_acct.total_cash = current_cash_;
    out_acct.net_liquidation = net_liquidation_;
    out_acct.buying_power = current_cash_; // For cash accounts, equal to settled cash
    out_acct.daily_pnl = daily_pnl_;
    out_acct.peak_equity = peak_equity_;
    out_acct.drawdown_pct = drawdown_pct_;
    out_acct.kill_switch_on = kill_switch_active_;

    return TC_OK;
}

TcStatus PositionBook::reconcile(const TcBrokerPosition* broker_positions, size_t count, TcReconcileReport& report_out) const {
    std::lock_guard<std::mutex> lock(mutex_);

    report_out.struct_size = sizeof(TcReconcileReport);
    report_out.version = 1;
    report_out.is_reconciled = true;
    report_out.num_discrepancies = 0;
    report_out.ts_ns = now_utc_ns();

    std::unordered_map<std::string, const TcBrokerPosition*> broker_map;
    for (size_t i = 0; i < count; ++i) {
        if (broker_positions && broker_positions[i].symbol[0] != '\0') {
            broker_map[broker_positions[i].symbol] = &broker_positions[i];
        }
    }

    // 1. Check all internal positions against broker report
    for (const auto& [sym, int_pos] : positions_) {
        if (int_pos.net_qty == 0) continue; // Flat positions don't need reconciliation if absent

        auto b_it = broker_map.find(sym);
        if (b_it == broker_map.end()) {
            // Position exists internally but is completely missing from broker report!
            if (report_out.num_discrepancies < TC_MAX_DISCREPANCIES) {
                auto& disc = report_out.discrepancies[report_out.num_discrepancies++];
                std::strncpy(disc.symbol, sym.c_str(), sizeof(disc.symbol) - 1);
                disc.internal_qty = int_pos.net_qty;
                disc.broker_qty = 0;
                disc.qty_diff = int_pos.net_qty;
                disc.internal_avg_cost = int_pos.avg_cost;
                disc.broker_avg_cost = 0;
            }
            report_out.is_reconciled = false;
        } else {
            const auto* b_pos = b_it->second;
            if (int_pos.net_qty != b_pos->broker_qty) {
                if (report_out.num_discrepancies < TC_MAX_DISCREPANCIES) {
                    auto& disc = report_out.discrepancies[report_out.num_discrepancies++];
                    std::strncpy(disc.symbol, sym.c_str(), sizeof(disc.symbol) - 1);
                    disc.internal_qty = int_pos.net_qty;
                    disc.broker_qty = b_pos->broker_qty;
                    disc.qty_diff = int_pos.net_qty - b_pos->broker_qty;
                    disc.internal_avg_cost = int_pos.avg_cost;
                    disc.broker_avg_cost = b_pos->broker_avg_cost;
                }
                report_out.is_reconciled = false;
            }
        }
    }

    // 2. Check for positions present at broker but unknown to internal book
    for (const auto& [sym, b_pos] : broker_map) {
        if (b_pos->broker_qty == 0) continue;

        auto i_it = positions_.find(sym);
        if (i_it == positions_.end() || i_it->second.net_qty == 0) {
            // Position at broker not in internal book
            if (report_out.num_discrepancies < TC_MAX_DISCREPANCIES) {
                auto& disc = report_out.discrepancies[report_out.num_discrepancies++];
                std::strncpy(disc.symbol, sym.c_str(), sizeof(disc.symbol) - 1);
                disc.internal_qty = (i_it != positions_.end()) ? i_it->second.net_qty : 0;
                disc.broker_qty = b_pos->broker_qty;
                disc.qty_diff = disc.internal_qty - b_pos->broker_qty;
                disc.internal_avg_cost = (i_it != positions_.end()) ? i_it->second.avg_cost : 0;
                disc.broker_avg_cost = b_pos->broker_avg_cost;
            }
            report_out.is_reconciled = false;
        }
    }

    return TC_OK;
}

void PositionBook::set_kill_switch(bool active) {
    std::lock_guard<std::mutex> lock(mutex_);
    kill_switch_active_ = active;
}

} // namespace tc
