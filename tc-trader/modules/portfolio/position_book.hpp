#ifndef TC_POSITION_BOOK_HPP
#define TC_POSITION_BOOK_HPP

/**
 * @file position_book.hpp
 * @brief Thread-safe position bookkeeping, PnL calculation, and reconciliation engine.
 *
 * Implements real-time weighted average cost tracking, realized PnL on reductions,
 * mark-to-market unrealized PnL, cash ledger, drawdown, and broker discrepancy detection.
 */

#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <cstdint>

#include "tc/tc_types.h"
#include "tc/portfolio/tc_portfolio.h"

namespace tc {

struct SymbolPositionInternal {
    char     symbol[TC_SYMBOL_MAX];
    int64_t  net_qty{0};         // Long (>0), Short (<0), Flat (0)
    TcPrice  avg_cost{0};        // Volume-weighted cost basis
    TcPrice  realized_pnl{0};    // Cumulative realized profit/loss
    TcPrice  unrealized_pnl{0};  // Marked-to-market unrealized profit/loss
    TcPrice  last_mark_px{0};    // Latest price used for marking
    int64_t  total_shares_traded{0};
    double   total_commissions{0.0};
};

class PositionBook {
public:
    /**
     * @brief Construct a new Position Book with initial cash balance.
     * @param initial_cash Starting cash in fixed-point TcPrice (e.g. $100,000 * 10,000).
     */
    explicit PositionBook(TcPrice initial_cash = 100000LL * TC_PRICE_SCALE);

    ~PositionBook() = default;

    PositionBook(const PositionBook&) = delete;
    PositionBook& operator=(const PositionBook&) = delete;

    /**
     * @brief Process an incremental execution fill.
     * Updates inventory, average cost basis, realized PnL, cash, and commissions.
     */
    TcStatus apply_fill(const TcFill& fill);

    /**
     * @brief Update the latest mark-to-market price for an instrument.
     * Recalculates unrealized PnL and net equity.
     */
    TcStatus update_mark(const char* symbol, TcPrice mark_px);

    /**
     * @brief Query a position snapshot for a specific symbol.
     */
    TcStatus get_position(const char* symbol, TcPosition& out_pos) const;

    /**
     * @brief Copy all tracked positions into a caller-allocated buffer.
     */
    TcStatus get_all_positions(TcPosition* buffer, size_t capacity, size_t& count_out) const;

    /**
     * @brief Query the aggregated account view.
     */
    TcStatus get_account(TcAccountView& out_acct) const;

    /**
     * @brief Reconcile internal position book against a broker snapshot.
     */
    TcStatus reconcile(const TcBrokerPosition* broker_positions, size_t count, TcReconcileReport& report_out) const;

    /**
     * @brief Engage or disengage the global kill switch flag on the account view.
     */
    void set_kill_switch(bool active);

private:
    void update_account_metrics_locked();

    mutable std::mutex mutex_;
    std::unordered_map<std::string, SymbolPositionInternal> positions_;

    // Cash and equity ledger
    TcPrice initial_cash_;
    TcPrice current_cash_;
    TcPrice net_liquidation_;
    TcPrice peak_equity_;
    TcPrice daily_pnl_;
    double  drawdown_pct_{0.0};
    double  total_commissions_paid_{0.0};
    bool    kill_switch_active_{false};
    int64_t last_update_ts_{0};
};

} // namespace tc

#endif /* TC_POSITION_BOOK_HPP */
