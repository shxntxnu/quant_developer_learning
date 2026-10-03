/**
 * @file risk_module.cpp
 * @brief Dynamic Library implementation of tc_risk exporting standard C ABI.
 *
 * Exposes the ITcRisk interface table and TcModuleVTable lifecycle.
 */

#include "tc/risk/tc_risk.h"
#include "risk_engine.hpp"
#include "tc/tc_module.h"

#include <cstring>
#include <memory>
#include <string>

// Internal state encapsulated behind opaque TcHandle
struct TcHandle_ {
    std::unique_ptr<tc::RiskEngine> engine;
    ITcRisk interface_table;
    std::string module_name;
    int32_t state;
};

// -----------------------------------------------------------------------------
// ITcRisk Interface Implementation
// -----------------------------------------------------------------------------

static TcStatus risk_configure(TcHandle handle, const char* config_json) {
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

static TcStatus risk_get_params(TcHandle handle, TcRiskParams* out_params) {
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

static TcStatus risk_set_params(TcHandle handle, const TcRiskParams* params) {
    if (!handle || !params) {
        return TC_ERR_INVALID_ARG;
    }
    if (params->struct_size != sizeof(TcRiskParams) || params->version != 1) {
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

static TcStatus risk_update_account(TcHandle handle, const TcAccountView* account) {
    if (!handle || !account) {
        return TC_ERR_INVALID_ARG;
    }
    if (account->struct_size != sizeof(TcAccountView) || account->version != 1) {
        return TC_ERR_ABI_MISMATCH;
    }

    try {
        if (!handle->engine) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->engine->update_account(*account);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus risk_update_position(TcHandle handle, const TcPosition* position) {
    if (!handle || !position) {
        return TC_ERR_INVALID_ARG;
    }
    if (position->struct_size != sizeof(TcPosition) || position->version != 1) {
        return TC_ERR_ABI_MISMATCH;
    }

    try {
        if (!handle->engine) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->engine->update_position(*position);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus risk_evaluate_signal(TcHandle handle,
                                     const TcSignal* signal,
                                     TcOrderIntent* intent,
                                     TcRiskDecision* decision) {
    if (!handle || !signal || !intent || !decision) {
        return TC_ERR_INVALID_ARG;
    }
    if (signal->struct_size != sizeof(TcSignal) || signal->version != 1) {
        return TC_ERR_ABI_MISMATCH;
    }

    try {
        if (!handle->engine) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->engine->evaluate_signal(*signal, intent, decision);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus risk_set_kill_switch(TcHandle handle, const char* symbol, bool active) {
    if (!handle) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (!handle->engine) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->engine->set_kill_switch(symbol, active);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus risk_get_kill_switch(TcHandle handle, const char* symbol, bool* is_active) {
    if (!handle || !is_active) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (!handle->engine) {
            return TC_ERR_NOT_CONNECTED;
        }
        return handle->engine->get_kill_switch(symbol, is_active);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus risk_reset(TcHandle handle) {
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
        TcRiskParams params = tc::RiskEngine::default_params();

        auto engine = std::make_unique<tc::RiskEngine>(params);
        if (config && config->config_json) {
            engine->configure_json(config->config_json);
        }

        auto handle = new TcHandle_();
        handle->engine = std::move(engine);
        handle->module_name = (config && config->module_name) ? config->module_name : "tc_risk";
        handle->state = TC_MOD_STATE_READY;

        // Populate ITcRisk function table
        handle->interface_table.struct_size = sizeof(ITcRisk);
        handle->interface_table.version = 1;
        handle->interface_table.configure = risk_configure;
        handle->interface_table.get_params = risk_get_params;
        handle->interface_table.set_params = risk_set_params;
        handle->interface_table.update_account = risk_update_account;
        handle->interface_table.update_position = risk_update_position;
        handle->interface_table.evaluate_signal = risk_evaluate_signal;
        handle->interface_table.set_kill_switch = risk_set_kill_switch;
        handle->interface_table.get_kill_switch = risk_get_kill_switch;
        handle->interface_table.reset = risk_reset;

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
            out->error_count = handle->engine->total_rejections();
            snprintf(out->status_msg, sizeof(out->status_msg),
                     "Evaluations: %llu, Approved: %llu, Rejected: %llu",
                     static_cast<unsigned long long>(handle->engine->total_evaluations()),
                     static_cast<unsigned long long>(handle->engine->total_approvals()),
                     static_cast<unsigned long long>(handle->engine->total_rejections()));
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

    if (strcmp(interface_name, "ITcRisk") == 0) {
        return &handle->interface_table;
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Module VTable Definition & Export
// -----------------------------------------------------------------------------

static const TcModuleVTable g_risk_vtable = {
    sizeof(TcModuleVTable),
    TC_ABI_VERSION,
    "tc_risk",
    module_create,
    module_start,
    module_stop,
    module_destroy,
    module_get_status,
    module_get_interface
};

TC_API const TcModuleVTable* tc_get_module_vtable(void) {
    return &g_risk_vtable;
}
