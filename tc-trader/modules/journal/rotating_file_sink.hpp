#ifndef TC_ROTATING_FILE_SINK_HPP
#define TC_ROTATING_FILE_SINK_HPP

/**
 * @file rotating_file_sink.hpp
 * @brief High-performance rotating file sink for text logs and binary snapshots.
 *
 * Automatically manages file creation, buffered writing, and file rotation
 * when a log file exceeds the configured maximum size or when the trading day rolls over.
 */

#include <string>
#include <fstream>
#include <cstdint>
#include <mutex>
#include <vector>

#include "tc/journal/tc_journal.h"

namespace tc {

class RotatingFileSink {
public:
    /**
     * @brief Construct a new Rotating File Sink.
     * @param directory Base directory where log and snapshot files will be written.
     * @param base_filename Base name for log files (e.g., "tc_journal").
     * @param max_file_size_bytes Threshold size in bytes before rotating to a new file (default 10 MB).
     * @param sink_type Text or binary format.
     */
    RotatingFileSink(std::string directory,
                     std::string base_filename = "tc_journal",
                     size_t max_file_size_bytes = 10 * 1024 * 1024,
                     TcJournalSinkType sink_type = TC_SINK_FILE_TEXT);

    ~RotatingFileSink();

    // Non-copyable, non-movable
    RotatingFileSink(const RotatingFileSink&) = delete;
    RotatingFileSink& operator=(const RotatingFileSink&) = delete;

    /**
     * @brief Open the initial file or prepare directory.
     * @return true on success, false if file could not be opened.
     */
    bool open();

    /**
     * @brief Write a single journal event to disk, rotating the file if needed.
     * @param event The event record to write.
     */
    void write(const TcJournalEvent& event);

    /**
     * @brief Write a batch of events to amortize rotation checks and I/O.
     * @param events Pointer to array of events.
     * @param count Number of events in the batch.
     */
    void write_batch(const TcJournalEvent* events, size_t count);

    /**
     * @brief Write a standalone binary snapshot blob to disk.
     * @param snapshot_name Name prefix (e.g. "eod_positions").
     * @param data Pointer to binary data.
     * @param len Length in bytes.
     * @return true on success.
     */
    bool write_snapshot(const char* snapshot_name, const void* data, size_t len);

    /**
     * @brief Flush file stream buffers to operating system.
     */
    void flush();

    /**
     * @brief Close current file handle.
     */
    void close();

    /**
     * @brief Change active destination directory or sink type.
     */
    void set_sink(TcJournalSinkType sink_type, const std::string& directory);

    [[nodiscard]] size_t current_file_size() const noexcept { return current_file_size_; }
    [[nodiscard]] uint32_t file_index() const noexcept { return file_index_; }

private:
    void rotate_if_needed();
    std::string generate_filename() const;

    std::string directory_;
    std::string base_filename_;
    size_t max_file_size_bytes_;
    TcJournalSinkType sink_type_;

    std::ofstream stream_;
    size_t current_file_size_{0};
    uint32_t file_index_{0};
    std::string current_filepath_;

    // Protects sink operations (called only by Thread T4, but allows re-entrant snapshot calls)
    std::mutex mutex_;
};

} // namespace tc

#endif /* TC_ROTATING_FILE_SINK_HPP */
