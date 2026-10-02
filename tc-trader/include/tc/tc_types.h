#ifndef TC_TYPES_H
#define TC_TYPES_H

#include "tc_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Market Data Types
 * ------------------------------------------------------------------------- */

typedef enum TcTickFlags {
    TC_TICK_FLAG_HAS_BID  = 0x1,
    TC_TICK_FLAG_HAS_ASK  = 0x2,
    TC_TICK_FLAG_HAS_LAST = 0x4
} TcTickFlags;

/* Raw or normalized tick event (Thread T1 -> Thread T2 via SPSC Ring) */
typedef struct TcTick {
    uint32_t struct_size;          /* sizeof(TcTick) */
    uint16_t version;              /* Version = 1 */
    uint16_t flags;                /* TcTickFlags bitmask */
    char     symbol[TC_SYMBOL_MAX];/* Fixed string symbol (e.g. "AAPL", "ES_202612") */
    int64_t  ts_ns;                /* UTC timestamp in nanoseconds */
    
    TcPrice  bid;                  /* Best bid price (fixed-point * 10,000) */
    TcPrice  ask;                  /* Best ask price (fixed-point * 10,000) */
    TcPrice  last;                 /* Last traded price (fixed-point * 10,000) */
    
    int64_t  bid_sz;               /* Bid size (shares/contracts) */
    int64_t  ask_sz;               /* Ask size */
    int64_t  last_sz;              /* Last trade volume */
} TcTick;

/* Aggregated OHLCV Bar */
typedef struct TcBar {
    uint32_t struct_size;          /* sizeof(TcBar) */
    uint16_t version;              /* Version = 1 */
    uint16_t timeframe_sec;        /* Duration in seconds (e.g., 300 for 5-min bars) */
    char     symbol[TC_SYMBOL_MAX];/* Symbol string */
    int64_t  ts_ns;                /* Bar close UTC timestamp in nanoseconds */
    
    TcPrice  open;                 /* Open price */
    TcPrice  high;                 /* High price */
    TcPrice  low;                  /* Low price */
    TcPrice  close;                /* Close price */
    
    int64_t  volume;               /* Aggregate volume */
    int64_t  num_ticks;            /* Tick count */
    TcPrice  vwap;                 /* Volume-weighted average price */
} TcBar;

/* -------------------------------------------------------------------------
 * Indicators & Strategy Types
 * ------------------------------------------------------------------------- */

typedef enum TcIndicatorMask {
    TC_IND_MASK_SMA_FAST = 1 << 0,
    TC_IND_MASK_SMA_SLOW = 1 << 1,
    TC_IND_MASK_EMA      = 1 << 2,
    TC_IND_MASK_RSI      = 1 << 3,
    TC_IND_MASK_MACD     = 1 << 4,
    TC_IND_MASK_ATR      = 1 << 5,
    TC_IND_MASK_BB       = 1 << 6,
    TC_IND_MASK_ADX      = 1 << 7
} TcIndicatorMask;

/* Snapshot of computed indicators passed into Strategy */
typedef struct TcIndicatorSnapshot {
    uint32_t struct_size;          /* sizeof(TcIndicatorSnapshot) */
    uint16_t version;              /* Version = 1 */
    uint16_t reserved;             /* Alignment padding */
    char     symbol[TC_SYMBOL_MAX];/* Symbol string */
    int64_t  ts_ns;                /* Snapshot timestamp */
    
    uint32_t valid_mask;           /* Bitmask of initialized indicators */
    uint32_t flags;                /* Custom feature flags */
    
    double   sma_fast;             /* Fast SMA (e.g., 20) */
    double   sma_slow;             /* Slow SMA (e.g., 50) */
    double   ema;                  /* Exponential Moving Average */
    double   rsi;                  /* 14-period RSI (0.0 to 100.0) */
    double   macd;                 /* MACD line (12-26) */
    double   macd_signal;          /* Signal line (9 EMA of MACD) */
    double   macd_hist;            /* MACD histogram */
    double   atr;                  /* 14-period Average True Range */
    double   bb_upper;             /* Upper Bollinger Band (+2 sigma) */
    double   bb_middle;            /* Middle Bollinger Band (20 SMA) */
    double   bb_lower;             /* Lower Bollinger Band (-2 sigma) */
    double   adx;                  /* 14-period Average Directional Index */
} TcIndicatorSnapshot;

typedef enum TcSignalSide {
    TC_SIDE_FLAT =  0,
    TC_SIDE_BUY  =  1,
    TC_SIDE_SELL = -1
} TcSignalSide;

/* Strategy Trade Signal */
typedef struct TcSignal {
    uint32_t struct_size;          /* sizeof(TcSignal) */
    uint16_t version;              /* Version = 1 */
    int8_t   side;                 /* TcSignalSide: BUY (+1), SELL (-1), FLAT (0) */
    uint8_t  order_type;           /* 1=Market, 2=Limit */
    char     symbol[TC_SYMBOL_MAX];/* Symbol string */
    int64_t  ts_ns;                /* Signal generation timestamp */
    
    uint32_t rule_id;              /* Rule identifier (e.g., 101) */
    uint32_t flags;                /* Extra signal flags */
    double   strength;             /* Signal conviction (0.0 to 1.0) */
    
    TcPrice  entry_ref_px;         /* Price when signal was triggered */
    TcPrice  stop_px;              /* Suggested protective stop price */
    TcPrice  target_px;            /* Suggested take-profit price */
} TcSignal;

/* -------------------------------------------------------------------------
 * Execution & Risk Types
 * ------------------------------------------------------------------------- */

typedef enum TcTimeInForce {
    TC_TIF_DAY = 0,
    TC_TIF_GTC = 1,
    TC_TIF_IOC = 2
} TcTimeInForce;

typedef enum TcOrderIntentFlags {
    TC_INTENT_FLAG_BRACKET     = 0x1,
    TC_INTENT_FLAG_REDUCE_ONLY = 0x2
} TcOrderIntentFlags;

/* Risk-Approved Order Intent (Thread T2 -> Thread T3 via SPSC Ring) */
typedef struct TcOrderIntent {
    uint32_t struct_size;          /* sizeof(TcOrderIntent) */
    uint16_t version;              /* Version = 1 */
    uint16_t tif;                  /* TcTimeInForce */
    char     symbol[TC_SYMBOL_MAX];/* Symbol string */
    int64_t  ts_ns;                /* Nanosecond timestamp */
    
    uint64_t intent_id;            /* Engine-wide unique intent identifier */
    int8_t   side;                 /* BUY (+1), SELL (-1) */
    uint8_t  order_type;           /* 1=MKT, 2=LMT, 3=STP, 4=STP_LMT */
    uint16_t flags;                /* TcOrderIntentFlags bitmask */
    uint32_t reserved;             /* Alignment padding */
    
    int64_t  qty;                  /* Risk-approved quantity */
    TcPrice  limit_px;             /* Limit price (0 if Market) */
    TcPrice  stop_loss_px;         /* Protective stop loss price */
    TcPrice  take_profit_px;       /* Profit taker limit price */
} TcOrderIntent;

typedef enum TcOrderStatus {
    TC_ORD_NEW       = 0,
    TC_ORD_SENT      = 1,
    TC_ORD_ACK       = 2,
    TC_ORD_PARTIAL   = 3,
    TC_ORD_FILLED    = 4,
    TC_ORD_CANCELLED = 5,
    TC_ORD_REJECTED  = 6
} TcOrderStatus;

/* Broker Order Lifecycle Callback */
typedef struct TcOrderEvent {
    uint32_t struct_size;          /* sizeof(TcOrderEvent) */
    uint16_t version;              /* Version = 1 */
    uint8_t  status;               /* TcOrderStatus */
    uint8_t  reserved1;            /* Alignment padding */
    char     symbol[TC_SYMBOL_MAX];/* Symbol string */
    int64_t  ts_ns;                /* Callback timestamp */
    
    uint64_t client_order_id;      /* Engine internal order ID */
    int64_t  broker_order_id;      /* Broker (IBKR) order ID */
    int64_t  filled_qty;           /* Cumulative filled quantity */
    int64_t  remaining_qty;        /* Remaining open quantity */
    TcPrice  avg_fill_px;          /* Average filled price */
    double   commission;           /* Broker commission paid */
    int32_t  error_code;           /* Broker error code (0 if none) */
    uint32_t reserved2;            /* Alignment padding */
} TcOrderEvent;

/* Execution Fill Record */
typedef struct TcFill {
    uint32_t struct_size;          /* sizeof(TcFill) */
    uint16_t version;              /* Version = 1 */
    int8_t   side;                 /* BUY (+1), SELL (-1) */
    uint8_t  reserved;             /* Alignment padding */
    char     symbol[TC_SYMBOL_MAX];/* Symbol string */
    int64_t  ts_ns;                /* Execution timestamp */
    
    uint64_t client_order_id;      /* Engine order ID */
    int64_t  fill_qty;             /* Incremental filled quantity */
    TcPrice  fill_px;              /* Execution price */
    double   commission;           /* Commission for this slice */
} TcFill;

/* -------------------------------------------------------------------------
 * Portfolio & Account Types
 * ------------------------------------------------------------------------- */

typedef struct TcPosition {
    uint32_t struct_size;          /* sizeof(TcPosition) */
    uint16_t version;              /* Version = 1 */
    uint16_t reserved;             /* Alignment padding */
    char     symbol[TC_SYMBOL_MAX];/* Symbol string */
    
    int64_t  net_qty;              /* Long (>0), Short (<0), Flat (0) */
    TcPrice  avg_cost;             /* Cost basis per share */
    TcPrice  realized_pnl;         /* Cumulative realized PnL */
    TcPrice  unrealized_pnl;       /* Mark-to-market unrealized PnL */
    TcPrice  last_mark_px;         /* Price used for marking */
} TcPosition;

typedef struct TcAccountView {
    uint32_t struct_size;          /* sizeof(TcAccountView) */
    uint16_t version;              /* Version = 1 */
    uint16_t reserved;             /* Alignment padding */
    int64_t  ts_ns;                /* State snapshot timestamp */
    
    TcPrice  total_cash;           /* Settled cash balance */
    TcPrice  net_liquidation;      /* Total account equity */
    TcPrice  buying_power;         /* Available margin / buying power */
    TcPrice  daily_pnl;            /* Current trading session PnL */
    TcPrice  peak_equity;          /* High-water mark for drawdown calculation */
    double   drawdown_pct;         /* Current drawdown fraction (e.g. 0.02 = 2%) */
    bool     kill_switch_on;       /* True if kill switch is active */
} TcAccountView;

#ifdef __cplusplus
}
#endif

#endif /* TC_TYPES_H */
