#ifndef TC_PORTFOLIO_H
#define TC_PORTFOLIO_H

/**
 * @file tc_portfolio.h
 * @brief Public C ABI Specification for the tc_portfolio Accounting and Bookkeeping Module.
 *
 * tc_portfolio.dll (or libtc_portfolio.dylib / .so) maintains the real-time internal
 * position inventory, cash ledger, realized and unrealized PnL calculations, equity
 * high-water mark, drawdown tracking, and automated reconciliation against broker statements.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(__has_include)
    #if __has_include("tc/tc_abi.h")
        #include "tc/tc_abi.h"
        #include "tc/tc_types.h"
    #elif __has_include("../tc_abi.h")
        #include "../tc_abi.h"
        #include "../tc_types.h"
    #else
        #include "tc_abi.h"
        #include "tc_types.h"
    #endif
#else
    #include "tc/tc_abi.h"
    #include "tc/tc_types.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TC_MAX_DISCREPANCIES 32

/**
 * @struct TcBrokerPosition
 * @brief Snapshot of a position as reported by the broker (e.g. via IBKR reqPositions).
 */
typedef struct TcBrokerPosition {
    uint32_t struct_size;          /**< sizeof(TcBrokerPosition) */
    uint16_t version;              /**< Version = 1 */
    uint16_t reserved;             /**< Alignment padding */
    char     symbol[TC_SYMBOL_MAX];/**< Symbol identifier */
    int64_t  broker_qty;           /**< Position quantity reported by broker */
    TcPrice  broker_avg_cost;      /**< Average price reported by broker */
    TcPrice  broker_market_price;  /**< Current market price reported by broker */
} TcBrokerPosition;

/**
 * @struct TcDiscrepancy
 * @brief Details of any mismatch identified between internal book and broker.
 */
typedef struct TcDiscrepancy {
    char     symbol[TC_SYMBOL_MAX];/**< Discrepant symbol */
    int64_t  internal_qty;         /**< Quantity in internal book */
    int64_t  broker_qty;           /**< Quantity at broker */
    int64_t  qty_diff;             /**< internal_qty - broker_qty */
    TcPrice  internal_avg_cost;    /**< Internal average cost basis */
    TcPrice  broker_avg_cost;      /**< Broker reported average cost */
} TcDiscrepancy;

/**
 * @struct TcReconcileReport
 * @brief Audit report produced by comparing internal books vs broker statements.
 */
typedef struct TcReconcileReport {
    uint32_t      struct_size;        /**< sizeof(TcReconcileReport) */
    uint16_t      version;            /**< Version = 1 */
    bool          is_reconciled;      /**< True if internal book matches broker with 0 discrepancies */
    uint16_t      num_discrepancies;  /**< Count of discrepancies found */
    int64_t       ts_ns;              /**< Nanosecond timestamp of reconciliation */
    TcDiscrepancy discrepancies[TC_MAX_DISCREPANCIES]; /**< Details of discrepancies */
} TcReconcileReport;

/**
 * @struct ITcPortfolio
 * @brief The versioned C ABI interface table for the portfolio module.
 *
 * Obtained by querying tc_get_module_vtable()->get_interface(handle, "ITcPortfolio").
 */
typedef struct ITcPortfolio {
    uint32_t struct_size; /**< sizeof(ITcPortfolio) */
    uint16_t version;     /**< Version = 1 */

    /**
     * @brief Apply an execution fill report to the position book and cash ledger.
     *
     * Updates inventory, computes realized PnL if reducing position, updates
     * average cost basis if increasing position, and deducts commissions.
     *
     * @param handle Module handle.
     * @param fill   Pointer to the execution fill record.
     * @return TC_OK on success, or error code.
     */
    TcStatus (*apply_fill)(TcHandle handle, const TcFill* fill);

    /**
     * @brief Mark an instrument to a new market price to update unrealized PnL.
     *
     * @param handle  Module handle.
     * @param symbol  Symbol identifier.
     * @param mark_px Latest market price (fixed-point).
     * @return TC_OK on success.
     */
    TcStatus (*update_mark)(TcHandle handle, const char* symbol, TcPrice mark_px);

    /**
     * @brief Query the current position details for a specific symbol.
     *
     * @param handle Module handle.
     * @param symbol Symbol string.
     * @param out    Pointer to receiving TcPosition struct.
     * @return TC_OK on success, or TC_ERR_NOT_FOUND if symbol has never been traded.
     */
    TcStatus (*get_position)(TcHandle handle, const char* symbol, TcPosition* out);

    /**
     * @brief Query all currently tracked positions.
     *
     * @param handle          Module handle.
     * @param buffer          Caller-allocated array of TcPosition structs.
     * @param buffer_capacity Maximum number of items buffer can hold.
     * @param count_out       Number of positions copied into buffer.
     * @return TC_OK on success.
     */
    TcStatus (*get_all_positions)(TcHandle handle, TcPosition* buffer, size_t buffer_capacity, size_t* count_out);

    /**
     * @brief Query the aggregated account view (cash, net equity, PnL, drawdown).
     *
     * @param handle Module handle.
     * @param out    Pointer to receiving TcAccountView struct.
     * @return TC_OK on success.
     */
    TcStatus (*get_account)(TcHandle handle, TcAccountView* out);

    /**
     * @brief Reconcile internal position book against broker snapshot.
     *
     * @param handle           Module handle.
     * @param broker_positions Array of positions returned from broker.
     * @param count            Count of broker positions.
     * @param report_out       Receiving audit report.
     * @return TC_OK on success.
     */
    TcStatus (*reconcile)(TcHandle handle, const TcBrokerPosition* broker_positions, size_t count, TcReconcileReport* report_out);

} ITcPortfolio;

#ifdef __cplusplus
}
#endif

#endif /* TC_PORTFOLIO_H */
