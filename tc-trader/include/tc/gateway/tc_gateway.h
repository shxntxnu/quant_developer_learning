/**
 * @file tc_gateway.h
 * @brief Standardized C ABI Interface for Exchange and Broker Gateways.
 *
 * Defines the unified ITcGateway interface implemented identically by both the
 * historical simulation replay engine (tc_gateway_sim) and the live broker
 * adapter (tc_gateway_ibkr).
 */

#ifndef TC_GATEWAY_H
#define TC_GATEWAY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(__has_include)
    #if __has_include("tc/tc_abi.h")
        #include "tc/tc_abi.h"
        #include "tc/tc_types.h"
    #elif __has_include("tc_abi.h")
        #include "tc_abi.h"
        #include "tc_types.h"
    #else
        #include "../tc_abi.h"
        #include "../tc_types.h"
    #endif
#else
    #include "tc_abi.h"
    #include "tc_types.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Gateway Order Submission Representation
 * ------------------------------------------------------------------------- */

typedef enum TcGatewayOrderType {
    TC_GW_ORDER_MKT     = 1, /* Market Order */
    TC_GW_ORDER_LMT     = 2, /* Limit Order */
    TC_GW_ORDER_STP     = 3, /* Stop Market Order */
    TC_GW_ORDER_STP_LMT = 4  /* Stop Limit Order */
} TcGatewayOrderType;

typedef struct TcOrder {
    uint32_t struct_size;          /* sizeof(TcOrder) */
    uint16_t version;              /* Version = 1 */
    uint16_t tif;                  /* TcTimeInForce (GTC, IOC, etc.) */
    char     symbol[TC_SYMBOL_MAX];/* Target symbol */
    int64_t  ts_ns;                /* Order placement timestamp */
    
    uint64_t client_order_id;      /* Engine unique client order ID */
    uint64_t parent_order_id;      /* Parent ID for bracket/OCA orders (0 if none) */
    int8_t   side;                 /* BUY (+1), SELL (-1) */
    uint8_t  order_type;           /* TcGatewayOrderType */
    uint16_t flags;                /* Flags (e.g. reduce-only, bracket) */
    uint32_t reserved;             /* Alignment padding */
    
    int64_t  qty;                  /* Order quantity */
    TcPrice  limit_px;             /* Limit price */
    TcPrice  stop_px;              /* Stop trigger price */
} TcOrder;

/* -------------------------------------------------------------------------
 * Gateway Configuration & Telemetry Statistics
 * ------------------------------------------------------------------------- */

typedef struct TcGatewayConfig {
    uint32_t struct_size;          /* sizeof(TcGatewayConfig) */
    uint16_t version;              /* Version = 1 */
    uint16_t reserved;             /* Alignment padding */
    
    char     gateway_name[32];     /* "tc_gateway_sim" or "tc_gateway_ibkr" */
    char     data_file_path[256];  /* Path to historical CSV file for simulation */
    char     default_symbol[TC_SYMBOL_MAX]; /* Default symbol override if any */
    
    double   slippage_pct;         /* Simulated slippage fraction (e.g. 0.0001 = 1 bp) */
    double   commission_per_share; /* Simulated commission per share (e.g. $0.005) */
    double   min_commission;       /* Minimum commission per execution (e.g. $1.00) */
    int64_t  latency_ns;           /* Simulated processing latency in nanoseconds */
    uint32_t flags;                /* Replay control flags */
} TcGatewayConfig;

typedef struct TcGatewayStats {
    uint32_t struct_size;          /* sizeof(TcGatewayStats) */
    uint16_t version;              /* Version = 1 */
    uint16_t reserved;             /* Alignment padding */
    
    uint64_t bars_published;       /* Total bars emitted to market data sink */
    uint64_t ticks_published;      /* Total ticks emitted (if tick level) */
    uint64_t orders_placed;        /* Total orders received by gateway */
    uint64_t orders_filled;        /* Total orders completely filled */
    uint64_t orders_cancelled;     /* Total orders cancelled */
    uint64_t orders_rejected;      /* Total orders rejected */
    double   total_commissions;    /* Cumulative commissions billed */
    int64_t  total_slippage_pnl;   /* Cumulative slippage price delta */
} TcGatewayStats;

/* -------------------------------------------------------------------------
 * Gateway Sink Callback Function Types
 * ------------------------------------------------------------------------- */

typedef void (*TcBarSink)(const TcBar* bar, void* user_data);
typedef void (*TcOrderEventSink)(const TcOrderEvent* event, void* user_data);
typedef void (*TcFillSink)(const TcFill* fill, void* user_data);

/* -------------------------------------------------------------------------
 * Unified ITcGateway Interface Table
 * ------------------------------------------------------------------------- */

typedef struct ITcGateway {
    uint32_t version; /* Interface version = 1 */

    /**
     * @brief Connect gateway to market and broker endpoint (or load simulation dataset).
     *
     * @param handle Module handle.
     * @param config Gateway connection parameters.
     * @return TC_OK on success.
     */
    TcStatus (*connect)(TcHandle handle, const TcGatewayConfig* config);

    /**
     * @brief Disconnect gateway and terminate active connections/replay.
     *
     * @param handle Module handle.
     * @return TC_OK on success.
     */
    TcStatus (*disconnect)(TcHandle handle);

    /**
     * @brief Check whether gateway is currently connected and active.
     *
     * @param handle        Module handle.
     * @param out_connected Output boolean flag.
     * @return TC_OK on success.
     */
    TcStatus (*is_connected)(TcHandle handle, bool* out_connected);

    /**
     * @brief Subscribe to market data bars for a symbol.
     *
     * @param handle        Module handle.
     * @param symbol        Instrument symbol.
     * @param timeframe_sec Bar interval (e.g. 300 for 5-min).
     * @return TC_OK on success.
     */
    TcStatus (*subscribe_bars)(TcHandle handle, const char* symbol, uint16_t timeframe_sec);

    /**
     * @brief Unsubscribe from market data for a symbol.
     *
     * @param handle Module handle.
     * @param symbol Instrument symbol.
     * @return TC_OK on success.
     */
    TcStatus (*unsubscribe_bars)(TcHandle handle, const char* symbol);

    /**
     * @brief Submit a new order for execution.
     *
     * @param handle              Module handle.
     * @param order               Order descriptor.
     * @param out_client_order_id Assigned or confirmed client order ID.
     * @return TC_OK on success.
     */
    TcStatus (*place_order)(TcHandle handle, const TcOrder* order, uint64_t* out_client_order_id);

    /**
     * @brief Cancel an active working order by client order ID.
     *
     * @param handle          Module handle.
     * @param client_order_id ID of order to cancel.
     * @return TC_OK on success.
     */
    TcStatus (*cancel_order)(TcHandle handle, uint64_t client_order_id);

    /**
     * @brief Cancel all active working orders for a symbol (or all symbols if NULL).
     *
     * @param handle Module handle.
     * @param symbol Target symbol or NULL for global cancel.
     * @return TC_OK on success.
     */
    TcStatus (*cancel_all_orders)(TcHandle handle, const char* symbol);

    /**
     * @brief Register callback sink for incoming market data bars (Thread T1 -> T2).
     *
     * @param handle    Module handle.
     * @param sink      Callback function pointer.
     * @param user_data Opaque user context.
     * @return TC_OK on success.
     */
    TcStatus (*set_bar_sink)(TcHandle handle, TcBarSink sink, void* user_data);

    /**
     * @brief Register callback sink for order status events (ACK, PARTIAL, FILLED, etc.).
     *
     * @param handle    Module handle.
     * @param sink      Callback function pointer.
     * @param user_data Opaque user context.
     * @return TC_OK on success.
     */
    TcStatus (*set_order_event_sink)(TcHandle handle, TcOrderEventSink sink, void* user_data);

    /**
     * @brief Register callback sink for execution fill transactions (Thread T1/T3 -> Portfolio).
     *
     * @param handle    Module handle.
     * @param sink      Callback function pointer.
     * @param user_data Opaque user context.
     * @return TC_OK on success.
     */
    TcStatus (*set_fill_sink)(TcHandle handle, TcFillSink sink, void* user_data);

    /**
     * @brief Step historical simulation by advancing one bar / time unit.
     *
     * @param handle       Module handle.
     * @param out_has_more Set to true if more historical bars remain, false if EOF.
     * @return TC_OK on success.
     */
    TcStatus (*step)(TcHandle handle, bool* out_has_more);

    /**
     * @brief Run replay continuously to completion (or in background simulation thread).
     *
     * @param handle Module handle.
     * @return TC_OK on success.
     */
    TcStatus (*run_replay)(TcHandle handle);

    /**
     * @brief Retrieve operational gateway statistics and execution metrics.
     *
     * @param handle    Module handle.
     * @param out_stats Output statistics struct.
     * @return TC_OK on success.
     */
    TcStatus (*get_stats)(TcHandle handle, TcGatewayStats* out_stats);

    /**
     * @brief Reset all internal state, pending orders, and replay buffers.
     *
     * @param handle Module handle.
     * @return TC_OK on success.
     */
    TcStatus (*reset)(TcHandle handle);

} ITcGateway;

#ifdef __cplusplus
}
#endif

#endif /* TC_GATEWAY_H */
