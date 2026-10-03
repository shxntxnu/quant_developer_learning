/**
 * @file strategy_module.cpp
 * @brief Dynamic Library implementation of tc_strategy exporting standard C ABI.
 *
 * Exposes the ITcStrategy interface table and TcModuleVTable lifecycle.
 */

#include "tc/strategy/tc_strategy.h"
#include "strategy_engine.hpp"
#include "tc/tc_module.h"

#include <cstring>
#include <memory>
#include <string>

// Internal state encapsulated behind opaque TcHandle
struct TcHandle_ {
    std::unique_ptr<tc::StrategyEngine> engine;
    ITcStrategy interface_table;
    std::string module_name;
    int32_t state;
};

// -----------------------------------------------------------------------------
// ITcStrategy Interface Implementation
// -----------------------------------------------------------------------------

static TcStatus strategy_configure(TcHandle handle, const char* config_json) {
    if (!handle) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (!handle->engine) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->engine->configure_json(config_json);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus strategy_get_params(TcHandle handle, TcStrategyParams* out_params) {
    if (!handle || !out_params) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (!handle->engine) {
            return TC_ERR_NOT_CONNECTED;
        }
        *out_params = handle->engine->params();
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus strategy_set_params(TcHandle handle, const TcStrategyParams* params) {
    if (!handle || !params) {
        return TC_ERR_INVALID_ARG;
    }
    if (params->struct_size != sizeof(TcStrategyParams) || params->version != 1) {
        return TC_ERR_ABI_MISMATCH;
    }

    try {
        if (!handle->engine) {
            return TC_ERR_NOT_CONNECTED;
        }
        handle->engine->set_params(*params);
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus strategy_classify_regime(TcHandle handle, const TcIndicatorSnapshot* snapshot, TcMarketRegime* out_regime) {
    if (!handle || !snapshot || !out_regime) {
        return TC_ERR_INVALID_ARG;
    }
    if (snapshot->struct_size != sizeof(TcIndicatorSnapshot) || snapshot->version != 1) {
        return TC_ERR_ABI_MISMATCH;
    }

    try {
        if (!handle->engine) {
            return TC_ERR_NOT_CONNECTED;
        }
        *out_regime = handle->engine->classify_regime(*snapshot);
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus strategy_on_snapshot(TcHandle handle,
                                     const TcIndicatorSnapshot* snapshot,
                                     const TcPositionView* position,
                                     TcSignal* signals_out,
                                     size_t max_signals,
                                     size_t* num_signals) {
    if (!handle || !snapshot || !signals_out || !num_signals || max_signals == 0) {
        return TC_ERR_INVALID_ARG;
    }
    if (snapshot->struct_size != sizeof(TcIndicatorSnapshot) || snapshot->version != 1) {
        return TC_ERR_ABI_MISMATCH;
    }
    if (position && (position->struct_size != sizeof(TcPosition) || position->version != 1)) {
        return TC_ERR_ABI_MISMATCH;
    }

    try {
        if (!handle->engine) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->engine->on_snapshot(*snapshot, position, signals_out, max_signals, num_signals);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus strategy_reset(TcHandle handle) {
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

// -----------------------------------------------------------------------------
// TcModuleVTable Implementation
// -----------------------------------------------------------------------------

static TcStatus module_create(const TcModuleConfig* config, TcHandle* out) {
    if (!out) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        TcStrategyParams params = tc::StrategyEngine::default_params();

        auto engine = std::make_unique<tc::StrategyEngine>(params);
        if (config && config->config_json) {
            engine->configure_json(config->config_json);
        }

        auto handle = new TcHandle_();
        handle->engine = std::move(engine);
        handle->module_name = (config && config->module_name) ? config->module_name : "tc_strategy";
        handle->state = TC_MOD_STATE_READY;

        // Populate ITcStrategy function table
        handle->interface_table.struct_size = sizeof(ITcStrategy);
        handle->interface_table.version = 1;
        handle->interface_table.configure = strategy_configure;
        handle->interface_table.get_params = strategy_get_params;
        handle->interface_table.set_params = strategy_set_params;
        handle->interface_table.classify_regime = strategy_classify_regime;
        handle->interface_table.on_snapshot = strategy_on_snapshot;
        handle->interface_table.reset = strategy_reset;

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
            out->events_processed = handle->engine->total_evaluations();
            out->error_count = 0;
            snprintf(out->status_msg, sizeof(out->status_msg),
                     "Evaluations: %llu, Signals: %llu",
                     static_cast<unsigned long long>(handle->engine->total_evaluations()),
                     static_cast<unsigned long long>(handle->engine->total_signals_emitted()));
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

    if (strcmp(interface_name, "ITcStrategy") == 0) {
        return &handle->interface_table;
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Module VTable Definition & Export
// -----------------------------------------------------------------------------

static const TcModuleVTable g_strategy_vtable = {
    sizeof(TcModuleVTable),
    TC_ABI_VERSION,
    "tc_strategy",
    module_create,
    module_start,
    module_stop,
    module_destroy,
    module_get_status,
    module_get_interface
};

TC_API const TcModuleVTable* tc_get_module_vtable(void) {
    return &g_strategy_vtable;
}
