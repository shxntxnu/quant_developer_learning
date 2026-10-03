/**
 * @file gateway_sim_module.cpp
 * @brief Dynamic Library implementation of tc_gateway_sim exporting standard C ABI.
 *
 * Implements the ITcGateway interface table and TcModuleVTable lifecycle for
 * high-performance deterministic historical backtesting.
 */

#include "tc/gateway/tc_gateway.h"
#include "gateway_sim_engine.hpp"
#include "tc/tc_module.h"

#include <cstring>
#include <memory>
#include <string>
#include <cstdio>

// Internal state encapsulated behind opaque TcHandle
struct TcHandle_ {
    std::unique_ptr<tc::gateway::GatewaySimEngine> engine;
    ITcGateway interface_table;
    std::string module_name;
    int32_t state;
};

// -----------------------------------------------------------------------------
// ITcGateway Interface Implementation
// -----------------------------------------------------------------------------

static TcStatus gateway_connect(TcHandle handle, const TcGatewayConfig* config) {
    if (!handle || !handle->engine || !config) {
        return TC_ERR_INVALID_ARG;
    }
    if (config->struct_size != sizeof(TcGatewayConfig) || config->version != 1) {
        return TC_ERR_ABI_MISMATCH;
    }

    try {
        return handle->engine->connect(config);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_disconnect(TcHandle handle) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->disconnect();
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_is_connected(TcHandle handle, bool* out_connected) {
    if (!handle || !handle->engine || !out_connected) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->is_connected(out_connected);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_subscribe_bars(TcHandle handle, const char* symbol, uint16_t timeframe_sec) {
    if (!handle || !handle->engine || !symbol) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->subscribe_bars(symbol, timeframe_sec);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_unsubscribe_bars(TcHandle handle, const char* symbol) {
    if (!handle || !handle->engine || !symbol) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->unsubscribe_bars(symbol);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_place_order(TcHandle handle, const TcOrder* order, uint64_t* out_client_order_id) {
    if (!handle || !handle->engine || !order) {
        return TC_ERR_INVALID_ARG;
    }
    if (order->struct_size != sizeof(TcOrder) || order->version != 1) {
        return TC_ERR_ABI_MISMATCH;
    }

    try {
        return handle->engine->place_order(order, out_client_order_id);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_cancel_order(TcHandle handle, uint64_t client_order_id) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->cancel_order(client_order_id);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_cancel_all_orders(TcHandle handle, const char* symbol) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->cancel_all_orders(symbol);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_set_bar_sink(TcHandle handle, TcBarSink sink, void* user_data) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->set_bar_sink(sink, user_data);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_set_order_event_sink(TcHandle handle, TcOrderEventSink sink, void* user_data) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->set_order_event_sink(sink, user_data);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_set_fill_sink(TcHandle handle, TcFillSink sink, void* user_data) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->set_fill_sink(sink, user_data);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_step(TcHandle handle, bool* out_has_more) {
    if (!handle || !handle->engine || !out_has_more) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->step(out_has_more);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_run_replay(TcHandle handle) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->run_replay();
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_get_stats(TcHandle handle, TcGatewayStats* out_stats) {
    if (!handle || !handle->engine || !out_stats) {
        return TC_ERR_INVALID_ARG;
    }
    if (out_stats->struct_size != sizeof(TcGatewayStats) || out_stats->version != 1) {
        return TC_ERR_ABI_MISMATCH;
    }

    try {
        return handle->engine->get_stats(out_stats);
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus gateway_reset(TcHandle handle) {
    if (!handle || !handle->engine) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        return handle->engine->reset();
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

// -----------------------------------------------------------------------------
// Module Lifecycle & VTable Implementation
// -----------------------------------------------------------------------------

static TcStatus module_create(const TcModuleConfig* config, TcHandle* out) {
    if (!out) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        auto* handle = new TcHandle_();
        handle->engine = std::make_unique<tc::gateway::GatewaySimEngine>();
        handle->module_name = (config && config->module_name) ? config->module_name : "tc_gateway_sim";
        handle->state = TC_MOD_STATE_READY;

        handle->interface_table.version = 1;
        handle->interface_table.connect = gateway_connect;
        handle->interface_table.disconnect = gateway_disconnect;
        handle->interface_table.is_connected = gateway_is_connected;
        handle->interface_table.subscribe_bars = gateway_subscribe_bars;
        handle->interface_table.unsubscribe_bars = gateway_unsubscribe_bars;
        handle->interface_table.place_order = gateway_place_order;
        handle->interface_table.cancel_order = gateway_cancel_order;
        handle->interface_table.cancel_all_orders = gateway_cancel_all_orders;
        handle->interface_table.set_bar_sink = gateway_set_bar_sink;
        handle->interface_table.set_order_event_sink = gateway_set_order_event_sink;
        handle->interface_table.set_fill_sink = gateway_set_fill_sink;
        handle->interface_table.step = gateway_step;
        handle->interface_table.run_replay = gateway_run_replay;
        handle->interface_table.get_stats = gateway_get_stats;
        handle->interface_table.reset = gateway_reset;

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
            TcGatewayStats stats{};
            stats.struct_size = sizeof(TcGatewayStats);
            stats.version = 1;
            handle->engine->get_stats(&stats);

            out->events_processed = stats.bars_published;
            out->error_count = stats.orders_rejected;
            std::snprintf(out->status_msg, sizeof(out->status_msg),
                          "Bars: %llu, Orders: %llu, Filled: %llu, Cancelled: %llu, Comm: $%.2f",
                          static_cast<unsigned long long>(stats.bars_published),
                          static_cast<unsigned long long>(stats.orders_placed),
                          static_cast<unsigned long long>(stats.orders_filled),
                          static_cast<unsigned long long>(stats.orders_cancelled),
                          stats.total_commissions);
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

    if (std::strcmp(interface_name, "ITcGateway") == 0) {
        return &handle->interface_table;
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Module VTable Definition & Export
// -----------------------------------------------------------------------------

static const TcModuleVTable g_gateway_sim_vtable = {
    sizeof(TcModuleVTable),
    TC_ABI_VERSION,
    "tc_gateway_sim",
    module_create,
    module_start,
    module_stop,
    module_destroy,
    module_get_status,
    module_get_interface
};

TC_API const TcModuleVTable* tc_get_module_vtable(void) {
    return &g_gateway_sim_vtable;
}
