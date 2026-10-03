#ifndef TC_STRATEGY_H
#define TC_STRATEGY_H

/**
 * @file tc_strategy.h
 * @brief Public C ABI Specification for the tc_strategy Rule-Based Strategy Module.
 *
 * tc_strategy.dll (or libtc_strategy.dylib / .so) implements deterministic market
 * regime classification and rule evaluation. Operating as a pure function on the
 * hot path (Thread T2), it evaluates incoming indicator snapshots alongside current
 * position views to emit actionable TcSignal records with zero dynamic allocation.
 */

#include "tc/tc_abi.h"
#include "tc/tc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum TcMarketRegime
 * @brief Market context categorization derived from trend and volatility indicators.
 */
typedef enum TcMarketRegime {
    TC_REGIME_UNKNOWN         = 0,
    TC_REGIME_RANGING         = 1, /**< ADX <= 25, normal volatility: suitable for mean reversion */
    TC_REGIME_TRENDING_BULL   = 2, /**< ADX > 25, Fast SMA > Slow SMA: upward trend following */
    TC_REGIME_TRENDING_BEAR   = 3, /**< ADX > 25, Fast SMA < Slow SMA: downward trend following */
    TC_REGIME_HIGH_VOLATILITY = 4  /**< ATR / Bollinger Band expansion beyond threshold: risk-off */
} TcMarketRegime;

/**
 * @enum TcRuleId
 * @brief Identifiers for entry and exit rules producing strategy signals.
 */
typedef enum TcRuleId {
    TC_RULE_NONE                 = 0,

    /* Entry Rules (101 - 199) */
    TC_RULE_ENTRY_TREND_BULL     = 101, /**< Fast/Slow MA Golden Cross + Bullish MACD + RSI confirmation */
    TC_RULE_ENTRY_TREND_BEAR     = 102, /**< Fast/Slow MA Death Cross + Bearish MACD + RSI confirmation */
    TC_RULE_ENTRY_MEAN_REV_LONG  = 103, /**< Price <= BB Lower + RSI <= Oversold (Oversold bounce) */
    TC_RULE_ENTRY_MEAN_REV_SHORT = 104, /**< Price >= BB Upper + RSI >= Overbought (Overbought fade) */

    /* Exit Rules (201 - 299) */
    TC_RULE_EXIT_TRAILING_STOP   = 201, /**< Trailing ATR stop loss reached */
    TC_RULE_EXIT_TAKE_PROFIT     = 202, /**< Take profit target price achieved */
    TC_RULE_EXIT_MA_REVERSAL     = 203, /**< Fast/Slow MA crossed against active position */
    TC_RULE_EXIT_MAX_HOLDING     = 204  /**< Maximum trade holding duration elapsed */
} TcRuleId;

/**
 * @struct TcStrategyParams
 * @brief Configurable parameters governing strategy rules and regime detection.
 */
typedef struct TcStrategyParams {
    uint32_t struct_size;            /**< sizeof(TcStrategyParams) */
    uint16_t version;                /**< Version = 1 */
    uint16_t reserved;               /**< Alignment padding */

    /* Regime Detection Thresholds */
    double   adx_trend_threshold;    /**< ADX threshold for trending regime (default: 25.0) */
    double   atr_vol_multiplier;     /**< Volatility threshold multiplier over baseline (default: 2.5) */
    double   bb_bandwidth_threshold; /**< Bollinger bandwidth (Upper-Lower)/Mid for volatility (default: 0.15) */

    /* Entry Filters */
    double   rsi_oversold;           /**< RSI oversold boundary for mean-reversion long (default: 30.0) */
    double   rsi_overbought;         /**< RSI overbought boundary for mean-reversion short (default: 70.0) */
    double   rsi_bull_min;           /**< Minimum RSI confirming bullish trend entry (default: 50.0) */
    double   rsi_bear_max;           /**< Maximum RSI confirming bearish trend entry (default: 50.0) */

    /* Exit & Protection Offsets */
    double   atr_stop_multiplier;    /**< Trailing stop distance in ATR units (default: 2.0) */
    double   atr_target_multiplier;  /**< Profit target distance in ATR units (default: 3.0) */
    int64_t  max_holding_ns;         /**< Max position holding time in nanoseconds (default: 4 hrs = 14.4e12 ns) */

    /* Rule Enable Switches */
    bool     enable_trend;           /**< Enable trend-following entries */
    bool     enable_mean_reversion;  /**< Enable mean-reversion entries */
    bool     enable_trailing_stop;   /**< Enable trailing ATR stop exits */
    bool     enable_take_profit;     /**< Enable target take profit exits */
    bool     enable_ma_reversal;     /**< Enable moving average reversal exits */
    bool     enable_max_holding;     /**< Enable max holding time exits */
} TcStrategyParams;

/**
 * @struct ITcStrategy
 * @brief Versioned C ABI interface table for the rule-based strategy module.
 *
 * Obtained by querying tc_get_module_vtable()->get_interface(handle, "ITcStrategy").
 */
typedef struct ITcStrategy {
    uint32_t struct_size; /**< sizeof(ITcStrategy) */
    uint16_t version;     /**< Version = 1 */

    /**
     * @brief Configure strategy parameters via JSON string or defaults.
     *
     * @param handle      Module handle.
     * @param config_json JSON configuration string, or NULL for defaults.
     * @return TC_OK on success.
     */
    TcStatus (*configure)(TcHandle handle, const char* config_json);

    /**
     * @brief Get active strategy parameters.
     *
     * @param handle     Module handle.
     * @param out_params Receiving TcStrategyParams struct.
     * @return TC_OK on success.
     */
    TcStatus (*get_params)(TcHandle handle, TcStrategyParams* out_params);

    /**
     * @brief Set strategy parameters directly via struct.
     *
     * @param handle Module handle.
     * @param params Pointer to new TcStrategyParams struct.
     * @return TC_OK on success.
     */
    TcStatus (*set_params)(TcHandle handle, const TcStrategyParams* params);

    /**
     * @brief Classify current market regime from an indicator snapshot.
     *
     * @param handle     Module handle.
     * @param snapshot   Pointer to populated TcIndicatorSnapshot.
     * @param out_regime Receiving TcMarketRegime enum.
     * @return TC_OK on success, or error code.
     */
    TcStatus (*classify_regime)(TcHandle handle, const TcIndicatorSnapshot* snapshot, TcMarketRegime* out_regime);

    /**
     * @brief Pure function hot-path evaluation:
     * Evaluates incoming indicator snapshot and current position view, executing exit rules
     * followed by entry rules (if flat), emitting up to max_signals into signals_out.
     *
     * Executed synchronously on Thread T2 with strictly zero dynamic heap allocations.
     *
     * @param handle      Module handle.
     * @param snapshot    Indicator snapshot for the symbol.
     * @param position    Current position view for the symbol (or NULL/flat).
     * @param signals_out Caller-allocated array of TcSignal structs.
     * @param max_signals Maximum capacity of signals_out buffer.
     * @param num_signals Pointer receiving the count of generated signals.
     * @return TC_OK on success, or error code.
     */
    TcStatus (*on_snapshot)(TcHandle handle,
                            const TcIndicatorSnapshot* snapshot,
                            const TcPositionView* position,
                            TcSignal* signals_out,
                            size_t max_signals,
                            size_t* num_signals);

    /**
     * @brief Reset internal symbol tracking states (peak prices, entry times, trade flags).
     *
     * @param handle Module handle.
     * @return TC_OK on success.
     */
    TcStatus (*reset)(TcHandle handle);

} ITcStrategy;

#ifdef __cplusplus
}
#endif

#endif /* TC_STRATEGY_H */
