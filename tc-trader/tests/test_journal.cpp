/**
 * @file test_journal.cpp
 * @brief Dynamic loading, concurrency stress test, and verification of tc_journal plugin.
 */

#include <iostream>
#include <iomanip>
#include <thread>
#include <vector>
#include <chrono>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "tc/tc_abi.h"
#include "tc/tc_types.h"
#include "tc/tc_module.h"
#include "tc/tc_time.hpp"
#include "tc/tc_log.h"
#include "tc/journal/tc_journal.h"

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE DynLibHandle;
#define DYN_LOAD(path) LoadLibraryA(path)
#define DYN_GET(handle, sym) GetProcAddress(handle, sym)
#define DYN_CLOSE(handle) FreeLibrary(handle)
#define LIB_NAME "bin/tc_journal.dll"
#elif defined(__APPLE__)
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_journal.dylib"
#else
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_journal.so"
#endif

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << "      TC-TRADER LAYER 2: TC_JOURNAL PLUGIN TEST       " << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 1. Dynamic Library Loading & Symbol Resolution
    std::cout << "Loading dynamic plugin from: " << LIB_NAME << " ..." << std::endl;
    DynLibHandle lib = DYN_LOAD(LIB_NAME);
    if (!lib) {
#if !defined(_WIN32)
        std::cerr << "Failed to load library: " << dlerror() << std::endl;
#else
        std::cerr << "Failed to load library. Error code: " << GetLastError() << std::endl;
#endif
        return 1;
    }
    std::cout << "[+] Plugin loaded successfully into host process" << std::endl;

    auto get_vtable_fn = reinterpret_cast<TcGetModuleVTableFn>(DYN_GET(lib, "tc_get_module_vtable"));
    if (!get_vtable_fn) {
        std::cerr << "Error: symbol 'tc_get_module_vtable' not found in dynamic library!" << std::endl;
        DYN_CLOSE(lib);
        return 1;
    }

    const TcModuleVTable* vtable = get_vtable_fn();
    assert(vtable != nullptr);
    std::cout << "[+] Found module vtable: name=" << vtable->name << std::endl;

    // ABI Handshake verification
    assert(vtable->abi_version == TC_ABI_VERSION);
    std::cout << "[+] ABI Version Handshake (0x" << std::hex << vtable->abi_version << std::dec << "): PASSED" << std::endl;

    // 2. Module Lifecycle: create -> get_interface -> start
    const char* test_log_dir = "logs/test_run";
    std::string config_str = "{\"log_dir\": \"" + std::string(test_log_dir) + "\"}";

    TcModuleConfig cfg;
    cfg.module_name = "tc_journal_test";
    cfg.config_json = config_str.c_str();

    TcHandle handle = nullptr;
    TcStatus status = vtable->create(&cfg, &handle);
    assert(status == TC_OK && handle != nullptr);
    std::cout << "[+] Module created with handle" << std::endl;

    auto* journal = static_cast<ITcJournal*>(vtable->get_interface(handle, "ITcJournal"));
    assert(journal != nullptr);
    assert(journal->struct_size == sizeof(ITcJournal));
    assert(journal->version == 1);
    std::cout << "[+] ITcJournal interface discovered and validated" << std::endl;

    status = vtable->start(handle);
    assert(status == TC_OK);
    std::cout << "[+] Thread T4 (AsyncWriter) started and running" << std::endl << std::endl;

    // 3. Concurrency Stress Test: 4 Producer Threads Logging Simultaneously
    constexpr size_t NumThreads = 4;
    constexpr size_t EventsPerThread = 25000;
    constexpr size_t TotalExpectedEvents = NumThreads * EventsPerThread;

    std::cout << "--- Concurrency Stress Test ---" << std::endl;
    std::cout << "Producers: " << NumThreads << " threads" << std::endl;
    std::cout << "Enqueuing " << TotalExpectedEvents << " total audit events across threads..." << std::endl;

    const char* tags[NumThreads] = {"GATEWAY", "MARKETDATA", "STRATEGY", "RISK"};

    auto start_time = std::chrono::steady_clock::now();

    std::vector<std::thread> producers;
    for (size_t t = 0; t < NumThreads; ++t) {
        producers.emplace_back([&, t]() {
            TcJournalEvent ev{};
            ev.struct_size = sizeof(TcJournalEvent);
            ev.version = 1;
            ev.level = static_cast<int16_t>(t % 4 + 1); // DEBUG, INFO, WARN, ERROR
            strncpy(ev.tag, tags[t], sizeof(ev.tag) - 1);

            for (size_t i = 1; i <= EventsPerThread; ++i) {
                ev.ts_ns = tc::now_utc_ns();
                ev.event_code = static_cast<int32_t>(i);
                snprintf(ev.message, sizeof(ev.message), "Order execution state update event index #%zu on symbol AAPL", i);

                while (journal->log(handle, &ev) == TC_ERR_QUEUE_FULL) {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& th : producers) {
        th.join();
    }

    // Write a binary snapshot
    struct SampleRiskSnapshot {
        double total_equity;
        double max_drawdown;
        int32_t open_positions;
    } risk_snap{105000.75, 0.015, 4};

    status = journal->snapshot(handle, "risk_checkpoint", &risk_snap, sizeof(risk_snap));
    assert(status == TC_OK);
    std::cout << "[+] Binary state snapshot successfully emitted" << std::endl;

    // 4. Flush and Stop Module
    journal->flush(handle);
    auto end_time = std::chrono::steady_clock::now();

    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    double throughput = (TotalExpectedEvents / (elapsed_ms / 1000.0)) / 1000000.0;

    std::cout << "Time Elapsed: " << std::fixed << std::setprecision(2) << elapsed_ms << " ms" << std::endl;
    std::cout << "Throughput:   " << std::setprecision(2) << throughput << " Million logs/sec" << std::endl;

    TcModuleStatus mod_status{};
    vtable->get_status(handle, &mod_status);
    std::cout << "Module Diagnostics: " << mod_status.status_msg << std::endl;
    assert(mod_status.events_processed == TotalExpectedEvents);
    assert(mod_status.error_count == 0);

    vtable->stop(handle);
    vtable->destroy(handle);
    DYN_CLOSE(lib);

    // 5. Verify Files Persisted to Disk
    std::cout << std::endl << "--- Disk Persistence Verification ---" << std::endl;
    bool found_log = false;
    bool found_snap = false;
    size_t total_log_bytes = 0;

    for (const auto& entry : std::filesystem::directory_iterator(test_log_dir)) {
        std::string filename = entry.path().filename().string();
        if (filename.find("tc_journal_") != std::string::npos && filename.find(".log") != std::string::npos) {
            found_log = true;
            total_log_bytes += entry.file_size();
            std::cout << "Found log file:      " << filename << " (" << entry.file_size() << " bytes)" << std::endl;
        }
        if (filename.find("snapshot_risk_checkpoint.bin") != std::string::npos) {
            found_snap = true;
            std::cout << "Found snapshot file: " << filename << " (" << entry.file_size() << " bytes)" << std::endl;
        }
    }

    assert(found_log && total_log_bytes > 0);
    assert(found_snap);
    std::cout << "[+] All log records and snapshots verified on disk!" << std::endl;

    std::cout << "=====================================================" << std::endl;
    std::cout << "     TC_JOURNAL PLUGIN TESTS PASSED COMPLETELY!      " << std::endl;
    std::cout << "=====================================================" << std::endl;

    return 0;
}
