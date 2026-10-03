/**
 * @file marketdata_module.cpp
 * @brief Dynamic Library implementation of tc_marketdata exporting standard C ABI.
 *
 * Exposes the ITcMarketData interface table and TcModuleVTable lifecycle.
 */

#include "tc/marketdata/tc_marketdata.h"
#include "marketdata_engine.hpp"
#include "tc/tc_module.h"

#include <cstring>
#include <memory>
#include <string>

// Internal state encapsulated behind opaque TcHandle
struct TcHandle_ {
    std::unique_ptr<tc::marketdata::MarketDataEngine> engine;
    ITcMarketData interface_table;
    std::string module_name;
    int32_t state;
};

// -----------------------------------------------------------------------------
// ITcMarketData Interface Implementation
// -----------------------------------------------------------------------------

static TcStatus marketdata_configure(TcHandle handle, const char* config_json) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (config_json && config_json[0] != '\0') {
            handle->engine->configure_from_json(config_json);
        } else {
            handle->engine->init_default_config();
        }
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus marketdata_get_config(TcHandle handle, TcMarketDataConfig* out_config) {
    if (!handle || !handle->engine || !out_config) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        *out_config = handle->engine->get_config();
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus marketdata_set_config(TcHandle handle, const TcMarketDataConfig* config) {
    if (!handle || !handle->engine || !config) {
        return TC_ERR_INVALID_ARG;
    }
    if (config->struct_size != sizeof(TcMarketDataConfig) || config->version != 1) {
        return TC_ERR_ABI_MISMATCH;
    }

    try {
        handle->engine->set_config(*config);
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus marketdata_process_raw_tick(TcHandle handle,
                                           const TcRawTick* raw,
                                           TcTick* tick_out,
                                           bool* tick_emitted,
                                           TcBar* bar_out,
                                           bool* bar_emitted) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->process_raw_tick(raw, tick_out, tick_emitted, bar_out, bar_emitted);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus marketdata_process_tick(TcHandle handle,
                                       const TcTick* tick,
                                       TcBar* bar_out,
                                       bool* bar_emitted) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->process_tick(tick, bar_out, bar_emitted);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus marketdata_flush_bar(TcHandle handle,
                                    const char* symbol,
                                    TcBar* bar_out,
                                    bool* bar_emitted) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->flush_bar(symbol, bar_out, bar_emitted);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus marketdata_set_bar_sink(TcHandle handle, TcBarCallback sink, void* user_data) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        handle->engine->set_bar_sink(sink, user_data);
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus marketdata_get_feed_status(TcHandle handle,
                                          const char* symbol,
                                          int64_t current_ts_ns,
                                          TcFeedStatus* out_status) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->get_feed_status(symbol, current_ts_ns, out_status);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus marketdata_get_stats(TcHandle handle, TcMarketDataStats* out_stats) {
    if (!handle || !handle->engine || !out_stats) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        *out_stats = handle->engine->get_stats();
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus marketdata_reset_symbol(TcHandle handle, const char* symbol) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        handle->engine->reset_symbol(symbol);
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus marketdata_reset(TcHandle handle) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        handle->engine->reset_all();
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

// -----------------------------------------------------------------------------
// Module Lifecycle Implementation (TcModuleVTable)
// -----------------------------------------------------------------------------

static TcStatus module_create(const TcModuleConfig* config, TcHandle* out) {
    if (!out) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        auto* handle = new TcHandle_();
        handle->engine = std::make_unique<tc::marketdata::MarketDataEngine>();
        handle->module_name = (config && config->module_name) ? config->module_name : "tc_marketdata";
        handle->state = TC_MOD_STATE_READY;

        if (config && config->config_json && config->config_json[0] != '\0') {
            handle->engine->configure_from_json(config->config_json);
        }

        // Initialize ITcMarketData interface table
        handle->interface_table.struct_size = sizeof(ITcMarketData);
        handle->interface_table.version = 1;
        handle->interface_table.configure = marketdata_configure;
        handle->interface_table.get_config = marketdata_get_config;
        handle->interface_table.set_config = marketdata_set_config;
        handle->interface_table.process_raw_tick = marketdata_process_raw_tick;
        handle->interface_table.process_tick = marketdata_process_tick;
        handle->interface_table.flush_bar = marketdata_flush_bar;
        handle->interface_table.set_bar_sink = marketdata_set_bar_sink;
        handle->interface_table.get_feed_status = marketdata_get_feed_status;
        handle->interface_table.get_stats = marketdata_get_stats;
        handle->interface_table.reset_symbol = marketdata_reset_symbol;
        handle->interface_table.reset = marketdata_reset;

        *out = handle;
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus module_start(TcHandle handle) {
    if (!handle || !handle->engine) {
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
        if (handle->engine) {
            const auto& stats = handle->engine->get_stats();
            out->events_processed = stats.raw_ticks_received;
            out->error_count = stats.ticks_dropped_invalid + stats.ticks_dropped_outlier + stats.ticks_dropped_stale;
            snprintf(out->status_msg, sizeof(out->status_msg),
                     "Ticks: %llu, Normalized: %llu, Bars: %llu, Dropped: %llu",
                     static_cast<unsigned long long>(stats.raw_ticks_received),
                     static_cast<unsigned long long>(stats.ticks_normalized),
                     static_cast<unsigned long long>(stats.bars_emitted),
                     static_cast<unsigned long long>(out->error_count));
        } else {
            out->events_processed = 0;
            out->error_count = 0;
            size_t msg_len = std::min(std::strlen("Engine not initialized"), sizeof(out->status_msg) - 1);
            std::memcpy(out->status_msg, "Engine not initialized", msg_len);
            out->status_msg[msg_len] = '\0';
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

    if (strcmp(interface_name, "ITcMarketData") == 0) {
        return &handle->interface_table;
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Module VTable Definition & Export
// -----------------------------------------------------------------------------

static const TcModuleVTable g_marketdata_vtable = {
    sizeof(TcModuleVTable),
    TC_ABI_VERSION,
    "tc_marketdata",
    module_create,
    module_start,
    module_stop,
    module_destroy,
    module_get_status,
    module_get_interface
};

TC_API const TcModuleVTable* tc_get_module_vtable(void) {
    return &g_marketdata_vtable;
}
