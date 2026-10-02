#ifndef TC_INDICATORS_H
#define TC_INDICATORS_H

/**
 * @file tc_indicators.h
 * @brief Public C ABI Specification for the tc_indicators Technical Indicators Module.
 *
 * tc_indicators.dll (or libtc_indicators.dylib / .so) computes technical indicator
 * feature vectors in O(1) incremental time per bar. The module produces TcIndicatorSnapshot
 * structures passed directly to tc_strategy for regime classification and rule evaluation.
 */

#include "tc/tc_abi.h"
#include "tc/tc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct TcIndicatorSpec
 * @brief Period configuration for technical indicator calculations.
 */
typedef struct TcIndicatorSpec {
    uint32_t struct_size;          /**< sizeof(TcIndicatorSpec) */
    uint16_t version;              /**< Version = 1 */
    uint16_t reserved;             /**< Alignment padding */

    uint16_t sma_fast_period;      /**< Fast Simple Moving Average period (default 20) */
    uint16_t sma_slow_period;      /**< Slow Simple Moving Average period (default 50) */
    uint16_t ema_period;           /**< Exponential Moving Average period (default 20) */
    uint16_t rsi_period;           /**< Relative Strength Index period (default 14) */
    uint16_t macd_fast_period;     /**< MACD Fast EMA period (default 12) */
    uint16_t macd_slow_period;     /**< MACD Slow EMA period (default 26) */
    uint16_t macd_signal_period;   /**< MACD Signal EMA period (default 9) */
    uint16_t atr_period;           /**< Average True Range period (default 14) */
    uint16_t bb_period;            /**< Bollinger Bands period (default 20) */
    double   bb_std_dev;           /**< Bollinger Bands standard deviation multiplier (default 2.0) */
    uint16_t adx_period;           /**< Average Directional Index period (default 14) */
} TcIndicatorSpec;

/**
 * @struct ITcIndicators
 * @brief Versioned C ABI interface table for technical indicators.
 *
 * Obtained by querying tc_get_module_vtable()->get_interface(handle, "ITcIndicators").
 */
typedef struct ITcIndicators {
    uint32_t struct_size; /**< sizeof(ITcIndicators) */
    uint16_t version;     /**< Version = 1 */

    /**
     * @brief Update indicator states with a newly closed bar.
     *
     * Executed on Thread T2 on the trading hot path. O(1) incremental update
     * with zero dynamic memory allocation.
     *
     * @param handle Module handle.
     * @param bar    Pointer to the newly closed bar.
     * @return TC_OK on success.
     */
    TcStatus (*update_bar)(TcHandle handle, const TcBar* bar);

    /**
     * @brief Query the latest indicator feature snapshot for a symbol.
     *
     * @param handle Module handle.
     * @param symbol Symbol string.
     * @param out    Receiving TcIndicatorSnapshot struct.
     * @return TC_OK on success, or TC_ERR_NOT_FOUND if symbol has no history.
     */
    TcStatus (*get_snapshot)(TcHandle handle, const char* symbol, TcIndicatorSnapshot* out);

    /**
     * @brief Reset indicator states for all symbols or clear history.
     *
     * @param handle Module handle.
     * @return TC_OK on success.
     */
    TcStatus (*reset)(TcHandle handle);

    /**
     * @brief Query the minimum number of historical bars required before indicators
     * are fully warmed up and valid_mask is completely set.
     *
     * @param handle Module handle.
     * @return Minimum bar count (e.g. 50 bars).
     */
    uint32_t (*warmup_bars_required)(TcHandle handle);

} ITcIndicators;

#ifdef __cplusplus
}
#endif

#endif /* TC_INDICATORS_H */
