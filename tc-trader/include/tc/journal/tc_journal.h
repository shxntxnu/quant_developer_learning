#ifndef TC_JOURNAL_H
#define TC_JOURNAL_H

/**
 * @file tc_journal.h
 * @brief Public C ABI Specification for the tc_journal Logging and Audit Module.
 *
 * tc_journal.dll (or libtc_journal.dylib / .so) provides an asynchronous,
 * non-blocking logging and audit trail system. High-frequency trading threads
 * (T1, T2, T3) push structured events into a lock-free multi-producer queue
 * without taking locks or performing file I/O. A dedicated background thread
 * (Thread T4: AsyncWriter) drains the queue and writes records to disk.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(__has_include)
    #if __has_include("tc/tc_abi.h")
        #include "tc/tc_abi.h"
    #elif __has_include("../tc_abi.h")
        #include "../tc_abi.h"
    #else
        #include "tc_abi.h"
    #endif
#else
    #include "tc/tc_abi.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum length for component and message strings in audit events */
#define TC_JOURNAL_TAG_MAX  32
#define TC_JOURNAL_MSG_MAX  256

/**
 * @enum TcJournalSinkType
 * @brief Available output targets for the journal module.
 */
typedef enum TcJournalSinkType {
    TC_SINK_FILE_TEXT   = 0, /**< Human-readable, timestamped rotating text log */
    TC_SINK_FILE_BINARY = 1  /**< Compact binary append-only format for replay */
} TcJournalSinkType;

/**
 * @struct TcJournalEvent
 * @brief Self-contained, trivially copyable POD event record.
 *
 * This struct is pushed directly into the lock-free MPSC queue. It contains no
 * pointers or dynamic allocations, ensuring zero heap allocations on the hot path.
 */
typedef struct TcJournalEvent {
    uint32_t struct_size;              /**< sizeof(TcJournalEvent) for version handshake */
    uint16_t version;                  /**< Version = 1 */
    int16_t  level;                    /**< Severity level (TcLogLevel from tc_log.h) */
    int32_t  event_code;               /**< Application-specific error or event code */
    uint32_t reserved;                 /**< Alignment padding to keep 64-bit alignment */
    int64_t  ts_ns;                    /**< Monotonic or UTC timestamp in nanoseconds */
    char     tag[TC_JOURNAL_TAG_MAX];  /**< Component tag (e.g., "RISK", "ORDER_MGR", "IBKR") */
    char     message[TC_JOURNAL_MSG_MAX]; /**< Null-terminated event description */
} TcJournalEvent;

/**
 * @struct ITcJournal
 * @brief The versioned C ABI interface table for the journal module.
 *
 * Obtained by querying tc_get_module_vtable()->get_interface(handle, "ITcJournal").
 */
typedef struct ITcJournal {
    uint32_t struct_size; /**< sizeof(ITcJournal) */
    uint32_t version;     /**< Version = 1 */

    /**
     * @brief Enqueue an audit event for asynchronous persistence.
     *
     * Non-blocking and lock-free. Safe to call from any engine thread (T0 - T5).
     *
     * @param handle Module handle created via TcModuleVTable::create().
     * @param event  Pointer to the event record to copy into the queue.
     * @return TC_OK on success, or TC_ERR_QUEUE_FULL if the journal queue is full.
     */
    TcStatus (*log)(TcHandle handle, const TcJournalEvent* event);

    /**
     * @brief Write an atomic state snapshot blob (e.g. end-of-day portfolio/risk state).
     *
     * @param handle        Module handle.
     * @param snapshot_name Unique identifier for this snapshot (e.g. "eod_positions_20261002").
     * @param data          Pointer to the contiguous binary payload.
     * @param data_len      Number of bytes to persist.
     * @return TC_OK on success, or error code.
     */
    TcStatus (*snapshot)(TcHandle handle, const char* snapshot_name, const void* data, size_t data_len);

    /**
     * @brief Block until all queued events are completely written and flushed to disk.
     *
     * Typically called during system shutdown or state checkpoints.
     *
     * @param handle Module handle.
     * @return TC_OK on success.
     */
    TcStatus (*flush)(TcHandle handle);

    /**
     * @brief Configure or update the active output sink and directory.
     *
     * @param handle           Module handle.
     * @param sink_type        Output format (text or binary).
     * @param destination_path Directory or filepath prefix for log outputs.
     * @return TC_OK on success.
     */
    TcStatus (*set_sink)(TcHandle handle, TcJournalSinkType sink_type, const char* destination_path);

} ITcJournal;

#ifdef __cplusplus
}
#endif

#endif /* TC_JOURNAL_H */
