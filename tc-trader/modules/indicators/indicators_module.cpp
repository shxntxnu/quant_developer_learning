/**
 * @file indicators_module.cpp
 * @brief Dynamic Library implementation of tc_indicators exporting standard C ABI.
 *
 * Exposes the ITcIndicators interface table and TcModuleVTable lifecycle.
 */

#include "tc/indicators/tc_indicators.h"
#include "tc/indicators/tc_indicators_core.hpp"
#include "tc/tc_module.h"

#include <cstring>
#include <memory>
#include <string>

// Internal state encapsulated behind opaque TcHandle
struct TcHandle_ {
    std::unique_ptr<tc::IndicatorEngine> engine;
    ITcIndicators interface_table;
    std::string module_name;
    int32_t state;
};

// -----------------------------------------------------------------------------
// ITcIndicators Interface Implementation
// -----------------------------------------------------------------------------

static TcStatus indicators_update_bar(TcHandle handle, const TcBar* bar) {
    if (!handle || !bar) {
        return TC_ERR_INVALID_ARG;
    }
    if (bar->struct_size != sizeof(TcBar) || bar->version != 1) {
        return TC_ERR_ABI_MISMATCH;
    }

    try {
        if (!handle->engine) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->engine->update_bar(*bar);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus indicators_get_snapshot(TcHandle handle, const char* symbol, TcIndicatorSnapshot* out) {
    if (!handle || !symbol || !out) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (!handle->engine) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->engine->get_snapshot(symbol, *out);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus indicators_reset(TcHandle handle) {
    if (!handle) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (handle->engine) {
            handle->engine->reset();
        }
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static uint32_t indicators_warmup_bars_required(TcHandle handle) {
    if (!handle || !handle->engine) {
        return 50;
    }
    return handle->engine->warmup_bars_required();
}

// -----------------------------------------------------------------------------
// TcModuleVTable Implementation
// -----------------------------------------------------------------------------

static TcStatus module_create(const TcModuleConfig* config, TcHandle* out) {
    if (!out) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        TcIndicatorSpec spec = tc::IndicatorEngine::default_spec();

        // Parse custom periods from JSON if present
        if (config && config->config_json) {
            const char* json = config->config_json;
            const char* sma_slow_key = "\"sma_slow\":";
            const char* p = strstr(json, sma_slow_key);
            if (p) {
                p += strlen(sma_slow_key);
                int val = atoi(p);
                if (val > 0) spec.sma_slow_period = static_cast<uint16_t>(val);
            }
        }

        auto engine = std::make_unique<tc::IndicatorEngine>(spec);
        auto handle = new TcHandle_();
        handle->engine = std::move(engine);
        handle->module_name = config && config->module_name ? config->module_name : "tc_indicators";
        handle->state = TC_MOD_STATE_READY;

        // Populate ITcIndicators function table
        handle->interface_table.struct_size = sizeof(ITcIndicators);
        handle->interface_table.version = 1;
        handle->interface_table.update_bar = indicators_update_bar;
        handle->interface_table.get_snapshot = indicators_get_snapshot;
        handle->interface_table.reset = indicators_reset;
        handle->interface_table.warmup_bars_required = indicators_warmup_bars_required;

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
            out->events_processed = handle->engine->total_bars_processed();
            out->error_count = 0;
            snprintf(out->status_msg, sizeof(out->status_msg),
                     "BarsProcessed: %llu, WarmupRequired: %u",
                     static_cast<unsigned long long>(handle->engine->total_bars_processed()),
                     handle->engine->warmup_bars_required());
        } else {
            out->events_processed = 0;
            out->error_count = 0;
            strncpy(out->status_msg, "Engine not initialized", sizeof(out->status_msg));
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

    if (strcmp(interface_name, "ITcIndicators") == 0) {
        return &handle->interface_table;
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Module VTable Definition & Export
// -----------------------------------------------------------------------------

static const TcModuleVTable g_indicators_vtable = {
    sizeof(TcModuleVTable),
    TC_ABI_VERSION,
    "tc_indicators",
    module_create,
    module_start,
    module_stop,
    module_destroy,
    module_get_status,
    module_get_interface
};

TC_API const TcModuleVTable* tc_get_module_vtable(void) {
    return &g_indicators_vtable;
}
