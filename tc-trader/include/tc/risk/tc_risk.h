#ifndef TC_RISK_H
#define TC_RISK_H

/**
 * @file tc_risk.h
 * @brief Public C ABI Specification for the tc_risk Risk Management and Position Sizing Module.
 *
 * tc_risk.dll (or libtc_risk.dylib / .so) implements capital protection, position sizing,
 * pre-trade risk validation, exposure limits, price collars, and atomic global/symbol kill switches.
 * Operating on the hot path (Thread T2), it transforms approved strategy signals (TcSignal)
 * into sized and validated order intents (TcOrderIntent) with zero dynamic allocation.
 */

#include "tc/tc_abi.h"
#include "tc/tc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum TcRiskDecisionCode
 * @brief Categorization of pre-trade risk decisions.
 */
typedef enum TcRiskDecisionCode {
    TC_RISK_APPROVED               =  0, /**< Order approved and sized */
    TC_RISK_REJECT_KILL_SWITCH     = -1, /**< Global or symbol kill switch active */
    TC_RISK_REJECT_MAX_DRAWDOWN    = -2, /**< Account daily/peak drawdown limit breached */
    TC_RISK_REJECT_MAX_POS_SIZE    = -3, /**< Sized quantity exceeds max single position limit */
    TC_RISK_REJECT_MAX_EXPOSURE    = -4, /**< Order would exceed portfolio gross leverage/exposure */
    TC_RISK_REJECT_BUYING_POWER    = -5, /**< Insufficient available cash / margin buying power */
    TC_RISK_REJECT_PRICE_COLLAR    = -6, /**< Price deviates beyond market reference collar */
    TC_RISK_REJECT_INVALID_STOP    = -7, /**< Stop loss distance is zero, negative, or invalid */
    TC_RISK_REJECT_SHORT_DISALLOWED= -8  /**< Shorting is disabled by policy */
} TcRiskDecisionCode;

/**
 * @struct TcRiskDecision
 * @brief Audit and decision report generated for every evaluated trade signal.
 */
typedef struct TcRiskDecision {
    uint32_t struct_size;          /**< sizeof(TcRiskDecision) */
    uint16_t version;              /**< Version = 1 */
    int16_t  code;                 /**< TcRiskDecisionCode (0 = Approved, <0 = Rejection reason) */
    int64_t  approved_qty;         /**< Risk-approved and sized quantity (0 if rejected) */
    char     reason[128];          /**< Detailed human-readable explanation */
} TcRiskDecision;

/**
 * @struct TcRiskParams
 * @brief Configurable parameters governing capital risk, sizing, and exposure limits.
 */
typedef struct TcRiskParams {
    uint32_t struct_size;             /**< sizeof(TcRiskParams) */
    uint16_t version;                 /**< Version = 1 */
    uint16_t reserved;                /**< Alignment padding */

    /* Position Sizing */
    double   risk_fraction_per_trade; /**< Risk per trade as fraction of equity (e.g. 0.01 = 1.0%) */
    int64_t  max_position_qty;        /**< Max allowable quantity per symbol (e.g. 1000 shares) */
    TcPrice  max_position_notional;   /**< Max allowable notional per symbol (e.g. $100,000) */
    int64_t  min_position_qty;        /**< Min allowable quantity (e.g. 1 share) */

    /* Leverage & Margin Limits */
    double   max_gross_leverage;      /**< Max gross leverage ratio (Total Notional / Equity, e.g. 2.0x) */
    TcPrice  min_buying_power;        /**< Minimum reserved cash buffer required (e.g. $5,000) */
    double   max_drawdown_pct;        /**< Drawdown threshold triggering auto kill-switch (e.g. 0.05 = 5%) */
    TcPrice  max_daily_loss;          /**< Max daily session loss before halt (e.g. $5,000) */

    /* Price Collars & Tolerances */
    double   max_price_collar_pct;    /**< Max price deviation from reference mark (e.g. 0.03 = 3%) */
    double   min_stop_distance_pct;   /**< Minimum required stop distance (e.g. 0.002 = 0.2%) */

    /* Toggles */
    bool     allow_shorting;          /**< Allow short entry orders */
    bool     auto_kill_on_drawdown;   /**< Automatically engage kill switch on drawdown breach */
} TcRiskParams;

/**
 * @struct ITcRisk
 * @brief Versioned C ABI interface table for the risk management and sizing module.
 *
 * Obtained by querying tc_get_module_vtable()->get_interface(handle, "ITcRisk").
 */
typedef struct ITcRisk {
    uint32_t struct_size; /**< sizeof(ITcRisk) */
    uint16_t version;     /**< Version = 1 */

    /**
     * @brief Configure risk parameters via JSON string or defaults.
     *
     * @param handle      Module handle.
     * @param config_json JSON configuration string, or NULL for defaults.
     * @return TC_OK on success.
     */
    TcStatus (*configure)(TcHandle handle, const char* config_json);

    /**
     * @brief Get active risk parameters.
     *
     * @param handle     Module handle.
     * @param out_params Receiving TcRiskParams struct.
     * @return TC_OK on success.
     */
    TcStatus (*get_params)(TcHandle handle, TcRiskParams* out_params);

    /**
     * @brief Set risk parameters directly via struct.
     *
     * @param handle Module handle.
     * @param params Pointer to new TcRiskParams struct.
     * @return TC_OK on success.
     */
    TcStatus (*set_params)(TcHandle handle, const TcRiskParams* params);

    /**
     * @brief Synchronize latest account view (equity, cash, buying power, daily PnL, drawdown).
     *
     * @param handle  Module handle.
     * @param account Pointer to TcAccountView struct.
     * @return TC_OK on success.
     */
    TcStatus (*update_account)(TcHandle handle, const TcAccountView* account);

    /**
     * @brief Synchronize current position for a symbol.
     *
     * @param handle   Module handle.
     * @param position Pointer to TcPosition struct.
     * @return TC_OK on success.
     */
    TcStatus (*update_position)(TcHandle handle, const TcPosition* position);

    /**
     * @brief Core risk evaluation on Thread T2 hot path:
     * Evaluates trade signal, sizes order using fixed-fractional formula, performs pre-trade
     * checks, and populates TcOrderIntent if approved.
     *
     * Executed synchronously on Thread T2 with strictly zero dynamic heap allocations.
     *
     * @param handle   Module handle.
     * @param signal   Incoming trade signal from strategy.
     * @param intent   Receiving TcOrderIntent struct (populated on approval).
     * @param decision Receiving TcRiskDecision struct.
     * @return TC_OK on evaluation completion (decision->code indicates outcome).
     */
    TcStatus (*evaluate_signal)(TcHandle handle,
                                const TcSignal* signal,
                                TcOrderIntent* intent,
                                TcRiskDecision* decision);

    /**
     * @brief Engage or disengage global or symbol-level kill switch.
     * If symbol is NULL or empty string, engages/disengages the global kill switch.
     *
     * @param handle Module handle.
     * @param symbol Symbol string, or NULL for global.
     * @param active True to lock/halt, false to disarm.
     * @return TC_OK on success.
     */
    TcStatus (*set_kill_switch)(TcHandle handle, const char* symbol, bool active);

    /**
     * @brief Query global or symbol-level kill switch state.
     * If symbol is NULL or empty string, queries global kill switch.
     *
     * @param handle    Module handle.
     * @param symbol    Symbol string, or NULL for global.
     * @param is_active Receiving boolean pointer.
     * @return TC_OK on success.
     */
    TcStatus (*get_kill_switch)(TcHandle handle, const char* symbol, bool* is_active);

    /**
     * @brief Reset internal risk state, exposure ledger, and counters.
     *
     * @param handle Module handle.
     * @return TC_OK on success.
     */
    TcStatus (*reset)(TcHandle handle);

} ITcRisk;

#ifdef __cplusplus
}
#endif

#endif /* TC_RISK_H */
