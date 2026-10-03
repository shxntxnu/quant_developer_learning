#ifndef TC_MARKETDATA_H
#define TC_MARKETDATA_H

/**
 * @file tc_marketdata.h
 * @brief Public C ABI Specification for the tc_marketdata Market Data Module.
 *
 * tc_marketdata.dll (or libtc_marketdata.dylib / .so) ingests raw gateway callbacks,
 * normalizes floating-point prices into fixed-point TcPrice, validates data integrity
 * (filtering stale timestamps, duplicate ticks, and statistical outlier spikes), aggregates
 * validated ticks into deterministic OHLCV bars with running VWAP, and monitors feed health.
 *
 * Operating on Thread T1/T2, all processing is strictly zero-allocation on the hot path.
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

/**
 * @enum TcRawTickType
 * @brief Categorization of incoming raw gateway tick events.
 */
typedef enum TcRawTickType {
    TC_RAW_TICK_UNKNOWN = 0,
    TC_RAW_TICK_TRADE   = 1, /**< Last trade execution */
    TC_RAW_TICK_BID     = 2, /**< Top-of-book bid update */
    TC_RAW_TICK_ASK     = 3, /**< Top-of-book ask update */
    TC_RAW_TICK_FULL    = 4  /**< Combined BBO + trade update */
} TcRawTickType;

/**
 * @struct TcRawTick
 * @brief Unprocessed raw tick directly from gateway / broker callback.
 */
typedef struct TcRawTick {
    uint32_t struct_size;          /**< sizeof(TcRawTick) */
    uint16_t version;              /**< Version = 1 */
    uint16_t tick_type;            /**< TcRawTickType */
    char     symbol[TC_SYMBOL_MAX];/**< Symbol string */
    int64_t  ts_ns;                /**< Gateway timestamp in nanoseconds */

    double   bid;                  /**< Best bid price (floating point) */
    double   ask;                  /**< Best ask price (floating point) */
    double   last;                 /**< Last trade price (floating point) */

    int64_t  bid_sz;               /**< Best bid size */
    int64_t  ask_sz;               /**< Best ask size */
    int64_t  last_sz;              /**< Last trade volume */
} TcRawTick;

/**
 * @enum TcFeedStatus
 * @brief Real-time data feed operational state.
 */
typedef enum TcFeedStatus {
    TC_FEED_OK           = 0, /**< Active ticks arriving within expected interval */
    TC_FEED_DEGRADED     = 1, /**< No tick received within stale timeout threshold */
    TC_FEED_DISCONNECTED = 2, /**< Gateway feed disconnected */
    TC_FEED_UNKNOWN      = 3  /**< Uninitialized or unmonitored symbol */
} TcFeedStatus;

/**
 * @struct TcMarketDataConfig
 * @brief Configuration parameters governing tick filtering, aggregation, and monitoring.
 */
typedef struct TcMarketDataConfig {
    uint32_t struct_size;             /**< sizeof(TcMarketDataConfig) */
    uint16_t version;                 /**< Version = 1 */
    uint16_t reserved;                /**< Alignment padding */

    uint32_t bar_interval_sec;        /**< Bar timeframe in seconds (default: 300 = 5 min) */
    double   max_price_deviation_pct; /**< Maximum allowed price spike deviation fraction (default: 0.10 = 10%) */
    int64_t  stale_timeout_ns;        /**< Feed timeout threshold in nanoseconds (default: 5s = 5e9 ns) */

    bool     filter_stale_ticks;      /**< Filter ticks with timestamp <= last seen */
    bool     filter_outliers;         /**< Filter price spikes outside allowed deviation */
    bool     filter_crossed_book;     /**< Filter quotes where bid > ask */
    bool     emit_partial_bars;       /**< Allow manual flush of incomplete bars */
} TcMarketDataConfig;

/**
 * @struct TcMarketDataStats
 * @brief Operational statistics and diagnostic counters.
 */
typedef struct TcMarketDataStats {
    uint32_t struct_size;             /**< sizeof(TcMarketDataStats) */
    uint16_t version;                 /**< Version = 1 */
    uint16_t reserved;                /**< Alignment padding */

    uint64_t raw_ticks_received;      /**< Total raw ticks ingested */
    uint64_t ticks_normalized;        /**< Ticks successfully normalized */
    uint64_t ticks_dropped_stale;     /**< Ticks dropped due to timestamp <= last seen */
    uint64_t ticks_dropped_outlier;   /**< Ticks dropped due to abnormal price spike */
    uint64_t ticks_dropped_invalid;   /**< Ticks dropped due to non-positive price or invalid symbol */
    uint64_t bars_emitted;            /**< Total completed OHLCV bars emitted */
} TcMarketDataStats;

/**
 * @typedef TcBarCallback
 * @brief Callback invoked when an aggregated bar completes and is emitted.
 */
typedef void (*TcBarCallback)(const TcBar* bar, void* user_data);

/**
 * @struct ITcMarketData
 * @brief Versioned C ABI interface table for the market data module.
 *
 * Obtained via tc_get_module_vtable()->get_interface(handle, "ITcMarketData").
 */
typedef struct ITcMarketData {
    uint32_t struct_size; /**< sizeof(ITcMarketData) */
    uint16_t version;     /**< Version = 1 */

    /**
     * @brief Configure market data parameters via JSON string or defaults.
     *
     * @param handle      Module handle.
     * @param config_json JSON configuration string, or NULL for defaults.
     * @return TC_OK on success.
     */
    TcStatus (*configure)(TcHandle handle, const char* config_json);

    /**
     * @brief Get active market data configuration.
     *
     * @param handle     Module handle.
     * @param out_config Receiving TcMarketDataConfig struct.
     * @return TC_OK on success.
     */
    TcStatus (*get_config)(TcHandle handle, TcMarketDataConfig* out_config);

    /**
     * @brief Set market data configuration directly via struct.
     *
     * @param handle Module handle.
     * @param config Pointer to new TcMarketDataConfig struct.
     * @return TC_OK on success.
     */
    TcStatus (*set_config)(TcHandle handle, const TcMarketDataConfig* config);

    /**
     * @brief Process a raw gateway tick event: normalizes floating-point prices,
     * validates data integrity, updates current bar, and emits completed bar if boundary crossed.
     *
     * Synchronous zero-heap hot-path operation.
     *
     * @param handle            Module handle.
     * @param raw               Incoming raw tick from gateway.
     * @param tick_out          Receiving normalized TcTick struct (optional, can be NULL).
     * @param tick_emitted      Set to true if tick was valid and normalized.
     * @param bar_out           Receiving completed TcBar struct if boundary was crossed.
     * @param bar_emitted       Set to true if a completed bar was emitted.
     * @return TC_OK on success, or error code.
     */
    TcStatus (*process_raw_tick)(TcHandle handle,
                                 const TcRawTick* raw,
                                 TcTick* tick_out,
                                 bool* tick_emitted,
                                 TcBar* bar_out,
                                 bool* bar_emitted);

    /**
     * @brief Ingest an already normalized TcTick into the bar aggregator.
     *
     * @param handle       Module handle.
     * @param tick         Incoming normalized tick.
     * @param bar_out      Receiving completed TcBar struct if boundary was crossed.
     * @param bar_emitted  Set to true if a completed bar was emitted.
     * @return TC_OK on success.
     */
    TcStatus (*process_tick)(TcHandle handle,
                             const TcTick* tick,
                             TcBar* bar_out,
                             bool* bar_emitted);

    /**
     * @brief Manually flush the currently active (incomplete) bar for a symbol.
     *
     * Useful at market session close or forced shutdown.
     *
     * @param handle      Module handle.
     * @param symbol      Symbol to flush (or NULL to flush all).
     * @param bar_out     Receiving flushed TcBar struct.
     * @param bar_emitted Set to true if a bar was flushed.
     * @return TC_OK on success.
     */
    TcStatus (*flush_bar)(TcHandle handle,
                          const char* symbol,
                          TcBar* bar_out,
                          bool* bar_emitted);

    /**
     * @brief Register a callback sink for completed bars.
     *
     * @param handle    Module handle.
     * @param sink      Callback function pointer.
     * @param user_data User context pointer passed to callback.
     * @return TC_OK on success.
     */
    TcStatus (*set_bar_sink)(TcHandle handle, TcBarCallback sink, void* user_data);

    /**
     * @brief Evaluate feed health status for a given symbol relative to current timestamp.
     *
     * @param handle        Module handle.
     * @param symbol        Symbol identifier.
     * @param current_ts_ns Current engine nanosecond timestamp.
     * @param out_status    Receiving TcFeedStatus enum.
     * @return TC_OK on success.
     */
    TcStatus (*get_feed_status)(TcHandle handle,
                                const char* symbol,
                                int64_t current_ts_ns,
                                TcFeedStatus* out_status);

    /**
     * @brief Retrieve operational diagnostic statistics.
     *
     * @param handle    Module handle.
     * @param out_stats Receiving TcMarketDataStats struct.
     * @return TC_OK on success.
     */
    TcStatus (*get_stats)(TcHandle handle, TcMarketDataStats* out_stats);

    /**
     * @brief Reset aggregator state and history for a specific symbol.
     *
     * @param handle Module handle.
     * @param symbol Symbol string to reset.
     * @return TC_OK on success.
     */
    TcStatus (*reset_symbol)(TcHandle handle, const char* symbol);

    /**
     * @brief Reset all symbols, aggregators, and diagnostic counters.
     *
     * @param handle Module handle.
     * @return TC_OK on success.
     */
    TcStatus (*reset)(TcHandle handle);

} ITcMarketData;

#ifdef __cplusplus
}
#endif

#endif /* TC_MARKETDATA_H */
