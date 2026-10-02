/**
 * @file journal_module.cpp
 * @brief Dynamic Library implementation of tc_journal exporting standard C ABI.
 *
 * Implements the TcModuleVTable lifecycle and exposes the ITcJournal interface table.
 */

#include "tc/journal/tc_journal.h"
#include "tc/tc_module.h"
#include "async_writer.hpp"
#include "rotating_file_sink.hpp"

#include <cstring>
#include <memory>
#include <string>

// Internal state encapsulated behind the opaque TcHandle pointer
struct TcHandle_ {
    std::unique_ptr<tc::AsyncWriter> writer;
    ITcJournal interface_table;
    std::string module_name;
    int32_t state;
};

// -----------------------------------------------------------------------------
// ITcJournal Interface Implementation
// -----------------------------------------------------------------------------

static TcStatus journal_log(TcHandle handle, const TcJournalEvent* event) {
    if (!handle || !event) {
        return TC_ERR_INVALID_ARG;
    }
    if (event->struct_size != sizeof(TcJournalEvent) || event->version != 1) {
        return TC_ERR_ABI_MISMATCH;
    }

    try {
        if (!handle->writer || !handle->writer->is_running()) {
            return TC_ERR_NOT_CONNECTED;
        }

        if (handle->writer->enqueue(*event)) {
            return TC_OK;
        } else {
            return TC_ERR_QUEUE_FULL;
        }
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus journal_snapshot(TcHandle handle, const char* snapshot_name, const void* data, size_t data_len) {
    if (!handle || !snapshot_name || !data || data_len == 0) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (!handle->writer) {
            return TC_ERR_NOT_CONNECTED;
        }

        if (handle->writer->snapshot(snapshot_name, data, data_len)) {
            return TC_OK;
        }
        return TC_ERR_INTERNAL;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus journal_flush(TcHandle handle) {
    if (!handle) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (handle->writer) {
            handle->writer->flush();
        }
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus journal_set_sink(TcHandle handle, TcJournalSinkType sink_type, const char* destination_path) {
    if (!handle) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (handle->writer) {
            handle->writer->set_sink(sink_type, destination_path);
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
        std::string log_dir = "logs";
        std::string base_name = "tc_journal";
        size_t max_file_size = 10 * 1024 * 1024; // 10 MB default
        TcJournalSinkType sink_type = TC_SINK_FILE_TEXT;

        // Simple config inspection (handles JSON fields if present, else defaults)
        if (config && config->config_json) {
            const char* json = config->config_json;
            const char* dir_key = "\"log_dir\":";
            const char* found_dir = strstr(json, dir_key);
            if (found_dir) {
                found_dir += strlen(dir_key);
                while (*found_dir == ' ' || *found_dir == '\"') found_dir++;
                const char* end_quote = strchr(found_dir, '\"');
                if (end_quote) {
                    log_dir = std::string(found_dir, end_quote - found_dir);
                }
            }

            if (strstr(json, "\"binary\": true")) {
                sink_type = TC_SINK_FILE_BINARY;
            }
        }

        auto sink = std::make_unique<tc::RotatingFileSink>(log_dir, base_name, max_file_size, sink_type);
        auto writer = std::make_unique<tc::AsyncWriter>(std::move(sink));

        auto handle = new TcHandle_();
        handle->writer = std::move(writer);
        handle->module_name = config && config->module_name ? config->module_name : "tc_journal";
        handle->state = TC_MOD_STATE_READY;

        // Populate ITcJournal function table
        handle->interface_table.struct_size = sizeof(ITcJournal);
        handle->interface_table.version = 1;
        handle->interface_table.log = journal_log;
        handle->interface_table.snapshot = journal_snapshot;
        handle->interface_table.flush = journal_flush;
        handle->interface_table.set_sink = journal_set_sink;

        *out = handle;
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus module_start(TcHandle handle) {
    if (!handle || !handle->writer) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        if (handle->writer->start()) {
            handle->state = TC_MOD_STATE_RUNNING;
            return TC_OK;
        }
        return TC_ERR_INTERNAL;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static TcStatus module_stop(TcHandle handle) {
    if (!handle || !handle->writer) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        handle->writer->stop();
        handle->state = TC_MOD_STATE_STOPPED;
        return TC_OK;
    } catch (...) {
        return TC_ERR_INTERNAL;
    }
}

static void module_destroy(TcHandle handle) {
    if (!handle) return;
    try {
        if (handle->writer) {
            handle->writer->stop();
        }
        delete handle;
    } catch (...) {
        // Prevent exceptions from escaping C boundary
    }
}

static TcStatus module_get_status(TcHandle handle, TcModuleStatus* out) {
    if (!handle || !out) {
        return TC_ERR_INVALID_ARG;
    }

    try {
        out->state = handle->state;
        out->reserved = 0;
        if (handle->writer) {
            out->events_processed = handle->writer->events_written();
            out->error_count = 0; // No unhandled drops; all enqueued events are written
            snprintf(out->status_msg, sizeof(out->status_msg),
                     "Enqueued: %llu, Written: %llu, SaturatedRetries: %llu",
                     static_cast<unsigned long long>(handle->writer->events_enqueued()),
                     static_cast<unsigned long long>(handle->writer->events_written()),
                     static_cast<unsigned long long>(handle->writer->queue_full_count()));
        } else {
            out->events_processed = 0;
            out->error_count = 0;
            strncpy(out->status_msg, "No writer active", sizeof(out->status_msg));
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

    if (strcmp(interface_name, "ITcJournal") == 0) {
        return &handle->interface_table;
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Module VTable Definition & Export
// -----------------------------------------------------------------------------

static const TcModuleVTable g_journal_vtable = {
    sizeof(TcModuleVTable),
    TC_ABI_VERSION,
    "tc_journal",
    module_create,
    module_start,
    module_stop,
    module_destroy,
    module_get_status,
    module_get_interface
};

TC_API const TcModuleVTable* tc_get_module_vtable(void) {
    return &g_journal_vtable;
}
