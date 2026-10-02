#include "rotating_file_sink.hpp"
#include "tc/tc_time.hpp"
#include "tc/tc_log.h"

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <cstring>

namespace tc {

static const char* log_level_to_string(int16_t level) noexcept {
    switch (level) {
        case TC_LOG_TRACE: return "TRACE";
        case TC_LOG_DEBUG: return "DEBUG";
        case TC_LOG_INFO:  return "INFO ";
        case TC_LOG_WARN:  return "WARN ";
        case TC_LOG_ERROR: return "ERROR";
        case TC_LOG_FATAL: return "FATAL";
        default:           return "UNKN ";
    }
}

RotatingFileSink::RotatingFileSink(std::string directory,
                                   std::string base_filename,
                                   size_t max_file_size_bytes,
                                   TcJournalSinkType sink_type)
    : directory_(std::move(directory)),
      base_filename_(std::move(base_filename)),
      max_file_size_bytes_(max_file_size_bytes),
      sink_type_(sink_type) {
    if (directory_.empty()) {
        directory_ = "logs";
    }
}

RotatingFileSink::~RotatingFileSink() {
    close();
}

std::string RotatingFileSink::generate_filename() const {
    const int64_t now = now_utc_ns();
    const time_t sec = static_cast<time_t>(now / 1000000000LL);
    struct tm tm_buf;
#if defined(_WIN32)
    gmtime_s(&tm_buf, &sec);
#else
    gmtime_r(&sec, &tm_buf);
#endif

    char buf[128];
    const char* ext = (sink_type_ == TC_SINK_FILE_BINARY) ? ".bin" : ".log";
    snprintf(buf, sizeof(buf), "%s_%04d%02d%02d_%02d%02d%02d_%04u%s",
             base_filename_.c_str(),
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
             file_index_, ext);

    std::filesystem::path dir(directory_);
    return (dir / buf).string();
}

bool RotatingFileSink::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::filesystem::create_directories(directory_);
    } catch (...) {
        return false;
    }

    current_filepath_ = generate_filename();
    auto mode = std::ios::out | std::ios::app;
    if (sink_type_ == TC_SINK_FILE_BINARY) {
        mode |= std::ios::binary;
    }

    stream_.open(current_filepath_, mode);
    if (!stream_.is_open()) {
        return false;
    }

    stream_.seekp(0, std::ios::end);
    current_file_size_ = static_cast<size_t>(stream_.tellp());
    return true;
}

void RotatingFileSink::rotate_if_needed() {
    if (current_file_size_ >= max_file_size_bytes_) {
        if (stream_.is_open()) {
            stream_.flush();
            stream_.close();
        }

        file_index_++;
        current_filepath_ = generate_filename();
        auto mode = std::ios::out | std::ios::trunc;
        if (sink_type_ == TC_SINK_FILE_BINARY) {
            mode |= std::ios::binary;
        }

        stream_.open(current_filepath_, mode);
        current_file_size_ = 0;
    }
}

void RotatingFileSink::write(const TcJournalEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stream_.is_open()) {
        if (!open()) return;
    }

    rotate_if_needed();

    if (sink_type_ == TC_SINK_FILE_TEXT) {
        // Human-readable formatted string:
        // [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [TAG] (code=X) message
        std::string ts_str = format_utc_timestamp(event.ts_ns);
        char line_buf[512];
        int written = snprintf(line_buf, sizeof(line_buf),
                               "[%s] [%s] [%-10s] (code=%-4d) %s\n",
                               ts_str.c_str(),
                               log_level_to_string(event.level),
                               event.tag,
                               event.event_code,
                               event.message);

        if (written > 0) {
            stream_.write(line_buf, written);
            current_file_size_ += static_cast<size_t>(written);
        }
    } else {
        // Raw POD binary serialization
        stream_.write(reinterpret_cast<const char*>(&event), sizeof(TcJournalEvent));
        current_file_size_ += sizeof(TcJournalEvent);
    }
}

void RotatingFileSink::write_batch(const TcJournalEvent* events, size_t count) {
    if (!events || count == 0) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!stream_.is_open()) {
        if (!open()) return;
    }

    for (size_t i = 0; i < count; ++i) {
        rotate_if_needed();
        const auto& event = events[i];

        if (sink_type_ == TC_SINK_FILE_TEXT) {
            std::string ts_str = format_utc_timestamp(event.ts_ns);
            char line_buf[512];
            int written = snprintf(line_buf, sizeof(line_buf),
                                   "[%s] [%s] [%-10s] (code=%-4d) %s\n",
                                   ts_str.c_str(),
                                   log_level_to_string(event.level),
                                   event.tag,
                                   event.event_code,
                                   event.message);
            if (written > 0) {
                stream_.write(line_buf, written);
                current_file_size_ += static_cast<size_t>(written);
            }
        } else {
            stream_.write(reinterpret_cast<const char*>(&event), sizeof(TcJournalEvent));
            current_file_size_ += sizeof(TcJournalEvent);
        }
    }
}

bool RotatingFileSink::write_snapshot(const char* snapshot_name, const void* data, size_t len) {
    if (!snapshot_name || !data || len == 0) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    try {
        std::filesystem::create_directories(directory_);
    } catch (...) {
        return false;
    }

    const int64_t now = now_utc_ns();
    std::string ts_str = format_utc_timestamp(now);

    std::filesystem::path dir(directory_);
    std::string filename = std::string("snapshot_") + snapshot_name + ".bin";
    std::string full_path = (dir / filename).string();

    std::ofstream snap_file(full_path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!snap_file.is_open()) {
        return false;
    }

    // Write a standard 32-byte header: magic + timestamp + data length
    uint32_t magic = 0x5443534E; // "TCSN"
    snap_file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    snap_file.write(reinterpret_cast<const char*>(&now), sizeof(now));
    uint64_t ulen = static_cast<uint64_t>(len);
    snap_file.write(reinterpret_cast<const char*>(&ulen), sizeof(ulen));
    snap_file.write(reinterpret_cast<const char*>(data), len);
    snap_file.flush();
    return true;
}

void RotatingFileSink::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_.is_open()) {
        stream_.flush();
    }
}

void RotatingFileSink::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_.is_open()) {
        stream_.flush();
        stream_.close();
    }
}

void RotatingFileSink::set_sink(TcJournalSinkType sink_type, const std::string& directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    close();
    sink_type_ = sink_type;
    if (!directory.empty()) {
        directory_ = directory;
    }
    file_index_ = 0;
    open();
}

} // namespace tc
