/**
 * @file portfolio_module.cpp
 * @brief Dynamic Library implementation of tc_portfolio exporting standard C ABI.
 *
 * Implements the TcModuleVTable lifecycle and exposes the ITcPortfolio interface table.
 */

#include "tc/portfolio/tc_portfolio.h"
#include "tc/tc_module.h"
#include "position_book.hpp"

#include <cstring>
#include <memory>
#include <string>

// Internal state encapsulated behind the opaque TcHandle pointer
struct TcHandle_ {
    std::unique_ptr<tc::PositionBook> book;
    ITcPortfolio interface_table;
    std::string module_name;
    int32_t state;
    uint64_t fills_processed;
};

// -----------------------------------------------------------------------------
// ITcPortfolio Interface Implementation
// -----------------------------------------------------------------------------

static TcStatus portfolio_apply_fill(TcHandle handle, const TcFill* fill) {
    if (!handle || !fill) {
        return TC_ERR_INVALID_ARG;
    }
    if (fill->struct_size != sizeof(TcFill) || fill->version != 1) {
        return TC_ERR_ABI_MISMATCH;
    }

    try {
        if (!handle->book) {
            return TC_ERR_NOT_CONNECTED;
        }

        TcStatus res = handle->book->apply_fill(*fill);
        if (res == TC_OK) {
            handle->fills_processed++;
        }
        return res;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus portfolio_update_mark(TcHandle handle, const char* symbol, TcPrice mark_px) {
    if (!handle || !symbol || mark_px <= 0) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (!handle->book) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->book->update_mark(symbol, mark_px);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus portfolio_get_position(TcHandle handle, const char* symbol, TcPosition* out) {
    if (!handle || !symbol || !out) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (!handle->book) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->book->get_position(symbol, *out);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus portfolio_get_all_positions(TcHandle handle, TcPosition* buffer, size_t capacity, size_t* count_out) {
    if (!handle || !buffer || capacity == 0 || !count_out) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (!handle->book) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->book->get_all_positions(buffer, capacity, *count_out);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus portfolio_get_account(TcHandle handle, TcAccountView* out) {
    if (!handle || !out) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (!handle->book) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->book->get_account(*out);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus portfolio_reconcile(TcHandle handle, const TcBrokerPosition* broker_positions, size_t count, TcReconcileReport* report_out) {
    if (!handle || !report_out) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (!handle->book) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->book->reconcile(broker_positions, count, *report_out);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

// -----------------------------------------------------------------------------
// TcModuleVTable Implementation
// -----------------------------------------------------------------------------

static TcStatus module_create(const TcModuleConfig* config, TcHandle* out) {
    if (!out) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        TcPrice initial_cash = 100000LL * TC_PRICE_SCALE; // Default $100,000

        // Parse starting cash from JSON if provided: {"initial_cash": 250000.0}
        if (config && config->config_json) {
            const char* key = "\"initial_cash\":";
            const char* pos = strstr(config->config_json, key);
            if (pos) {
                pos += strlen(key);
                while (*pos == ' ') pos++;
                double val = atof(pos);
                if (val > 0) {
                    initial_cash = TC_DOUBLE_TO_PRICE(val);
                }
            }
        }

        auto book = std::make_unique<tc::PositionBook>(initial_cash);
        auto handle = new TcHandle_();
        handle->book = std::move(book);
        handle->module_name = config && config->module_name ? config->module_name : "tc_portfolio";
        handle->state = TC_MOD_STATE_READY;
        handle->fills_processed = 0;

        // Populate ITcPortfolio function table
        handle->interface_table.struct_size = sizeof(ITcPortfolio);
        handle->interface_table.version = 1;
        handle->interface_table.apply_fill = portfolio_apply_fill;
        handle->interface_table.update_mark = portfolio_update_mark;
        handle->interface_table.get_position = portfolio_get_position;
        handle->interface_table.get_all_positions = portfolio_get_all_positions;
        handle->interface_table.get_account = portfolio_get_account;
        handle->interface_table.reconcile = portfolio_reconcile;

        *out = handle;
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus module_start(TcHandle handle) {
    if (!handle || !handle->book) {
        return TC_ERR_INVALID_ARG;
    }
    handle->state = TC_MOD_STATE_RUNNING;
    return TC_OK;
}

static TcStatus module_stop(TcHandle handle) {
    if (!handle) {
        return TC_ERR_INVALID_ARG;
    }
    handle->state = TC_MOD_STATE_STOPPED;
    return TC_OK;
}

static void module_destroy(TcHandle handle) {
    if (!handle) return;
    delete handle;
}

static TcStatus module_get_status(TcHandle handle, TcModuleStatus* out) {
    if (!handle || !out) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        out->state = handle->state;
        out->reserved = 0;
        out->events_processed = handle->fills_processed;
        out->error_count = 0;

        TcAccountView acct{};
        if (handle->book && handle->book->get_account(acct) == TC_OK) {
            snprintf(out->status_msg, sizeof(out->status_msg),
                     "Fills: %llu, NetLiquidation: $%.2f, DailyPnL: $%.2f, DD: %.2f%%",
                     static_cast<unsigned long long>(handle->fills_processed),
                     TC_PRICE_TO_DOUBLE(acct.net_liquidation),
                     TC_PRICE_TO_DOUBLE(acct.daily_pnl),
                     acct.drawdown_pct * 100.0);
        } else {
            strncpy(out->status_msg, "Book initialized", sizeof(out->status_msg));
        }

        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static void* module_get_interface(TcHandle handle, const char* interface_name) {
    if (!handle || !interface_name) {
        return nullptr;
    }

    if (strcmp(interface_name, "ITcPortfolio") == 0) {
        return &handle->interface_table;
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Module VTable Definition & Export
// -----------------------------------------------------------------------------

static const TcModuleVTable g_portfolio_vtable = {
    sizeof(TcModuleVTable),
    TC_ABI_VERSION,
    "tc_portfolio",
    module_create,
    module_start,
    module_stop,
    module_destroy,
    module_get_status,
    module_get_interface
};

TC_API const TcModuleVTable* tc_get_module_vtable(void) {
    return &g_portfolio_vtable;
}
