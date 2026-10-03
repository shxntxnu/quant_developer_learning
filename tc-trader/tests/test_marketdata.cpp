/**
 * @file test_marketdata.cpp
 * @brief Dynamic loading, tick normalization, outlier filtering, bar aggregation, feed health, and benchmark for tc_marketdata.
 */

#include <iostream>
#include <iomanip>
#include <cassert>
#include <cstring>
#include <cmath>
#include <chrono>
#include <vector>
#include <atomic>

#include "tc/tc_abi.h"
#include "tc/tc_types.h"
#include "tc/tc_module.h"
#include "tc/marketdata/tc_marketdata.h"

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE DynLibHandle;
#define DYN_LOAD(path) LoadLibraryA(path)
#define DYN_GET(handle, sym) GetProcAddress(handle, sym)
#define DYN_CLOSE(handle) FreeLibrary(handle)
#define LIB_NAME "bin/tc_marketdata.dll"
#elif defined(__APPLE__)
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_marketdata.dylib"
#else
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_marketdata.so"
#endif

// Global test callback counters
static std::atomic<uint64_t> g_bar_count{0};
static TcBar g_last_bar{};

static void on_bar_completed_cb(const TcBar* bar, void* user_data) {
    (void)user_data;
    if (bar) {
        g_last_bar = *bar;
        g_bar_count++;
    }
}

static TcRawTick make_raw_tick(const char* symbol, uint16_t tick_type, int64_t ts_ns,
                               double bid, double ask, double last,
                               int64_t bid_sz, int64_t ask_sz, int64_t last_sz) {
    TcRawTick t{};
    t.struct_size = sizeof(TcRawTick);
    t.version = 1;
    t.tick_type = tick_type;
    strncpy(t.symbol, symbol, sizeof(t.symbol) - 1);
    t.ts_ns = ts_ns;
    t.bid = bid;
    t.ask = ask;
    t.last = last;
    t.bid_sz = bid_sz;
    t.ask_sz = ask_sz;
    t.last_sz = last_sz;
    return t;
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << "    TC-TRADER LAYER 2: TC_MARKETDATA PLUGIN TEST     " << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 1. Dynamic Library Loading & VTable Resolution
    std::cout << "Loading dynamic plugin from: " << LIB_NAME << " ..." << std::endl;
    DynLibHandle lib = DYN_LOAD(LIB_NAME);
    if (!lib) {
#if !defined(_WIN32)
        std::cerr << "Failed to load library: " << dlerror() << std::endl;
#else
        std::cerr << "Failed to load library. Error: " << GetLastError() << std::endl;
#endif
        return 1;
    }
    std::cout << "[+] Plugin loaded successfully into host process" << std::endl;

    auto get_vtable_fn = reinterpret_cast<TcGetModuleVTableFn>(reinterpret_cast<void*>(DYN_GET(lib, "tc_get_module_vtable")));
    assert(get_vtable_fn != nullptr);

    const TcModuleVTable* vtable = get_vtable_fn();
    assert(vtable != nullptr && vtable->abi_version == TC_ABI_VERSION);
    std::cout << "[+] Found module: " << vtable->name << " (ABI 0x" << std::hex << vtable->abi_version << std::dec << ")" << std::endl;

    TcModuleConfig cfg{};
    cfg.module_name = "tc_marketdata";
    cfg.config_json = "{\"bar_interval_sec\": 60, \"max_price_deviation_pct\": 0.10, \"stale_timeout_sec\": 5.0}";

    TcHandle handle = nullptr;
    TcStatus status = vtable->create(&cfg, &handle);
    assert(status == TC_OK && handle != nullptr);

    auto* md = static_cast<ITcMarketData*>(vtable->get_interface(handle, "ITcMarketData"));
    assert(md != nullptr && md->version == 1);
    vtable->start(handle);
    std::cout << "[+] ITcMarketData interface discovered and running" << std::endl << std::endl;

    // 2. Configuration Inspection & Setting
    std::cout << "--- 1. Configuration Verification ---" << std::endl;
    TcMarketDataConfig md_cfg{};
    status = md->get_config(handle, &md_cfg);
    assert(status == TC_OK);
    assert(md_cfg.bar_interval_sec == 60);
    assert(std::abs(md_cfg.max_price_deviation_pct - 0.10) < 0.001);
    std::cout << "[+] Active Config: bar_interval = " << md_cfg.bar_interval_sec
              << "s, max_price_deviation = " << (md_cfg.max_price_deviation_pct * 100.0) << "%"
              << ", stale_timeout = " << (md_cfg.stale_timeout_ns / 1'000'000'000.0) << "s" << std::endl;

    // Register bar completion callback
    status = md->set_bar_sink(handle, on_bar_completed_cb, nullptr);
    assert(status == TC_OK);
    std::cout << "[+] Registered bar sink callback" << std::endl << std::endl;

    // 3. Raw Tick Normalization & Outlier Filtering
    std::cout << "--- 2. Tick Normalization & Integrity Filtering ---" << std::endl;
    const int64_t interval_ns = 60'000'000'000LL;
    int64_t base_ts = (1700000000000000000LL / interval_ns) * interval_ns; // Aligned to 60s boundary
    TcTick norm_tick{};
    bool tick_emitted = false;
    TcBar out_bar{};
    bool bar_emitted = false;

    // Test 2A: Valid raw full tick (BBO + Trade)
    TcRawTick r1 = make_raw_tick("AAPL", TC_RAW_TICK_FULL, base_ts, 150.00, 150.05, 150.02, 500, 700, 100);
    status = md->process_raw_tick(handle, &r1, &norm_tick, &tick_emitted, &out_bar, &bar_emitted);
    assert(status == TC_OK);
    assert(tick_emitted == true);
    assert(bar_emitted == false); // First tick within minute bar
    assert(strcmp(norm_tick.symbol, "AAPL") == 0);
    assert(TC_PRICE_TO_DOUBLE(norm_tick.bid) == 150.00);
    assert(TC_PRICE_TO_DOUBLE(norm_tick.ask) == 150.05);
    assert(TC_PRICE_TO_DOUBLE(norm_tick.last) == 150.02);
    assert(norm_tick.bid_sz == 500);
    assert(norm_tick.ask_sz == 700);
    assert(norm_tick.last_sz == 100);
    assert((norm_tick.flags & TC_TICK_FLAG_HAS_BID) != 0);
    assert((norm_tick.flags & TC_TICK_FLAG_HAS_ASK) != 0);
    assert((norm_tick.flags & TC_TICK_FLAG_HAS_LAST) != 0);
    std::cout << "[+] Valid Raw Tick Normalization: PASSED (AAPL last $"
              << TC_PRICE_TO_DOUBLE(norm_tick.last) << " size " << norm_tick.last_sz << ")" << std::endl;

    // Test 2B: Stale Timestamp Filtering
    // Send a tick with an earlier timestamp
    TcRawTick r_stale = make_raw_tick("AAPL", TC_RAW_TICK_TRADE, base_ts - 1000LL, 0, 0, 150.03, 0, 0, 50);
    status = md->process_raw_tick(handle, &r_stale, &norm_tick, &tick_emitted, &out_bar, &bar_emitted);
    assert(status == TC_OK);
    assert(tick_emitted == false); // Should be dropped as stale!
    std::cout << "[+] Stale Timestamp Filter: DROPPED correctly" << std::endl;

    // Test 2C: Outlier Price Spike Filtering (> 10% deviation from 150.02)
    TcRawTick r_outlier = make_raw_tick("AAPL", TC_RAW_TICK_TRADE, base_ts + 1'000'000'000LL, 0, 0, 200.00, 0, 0, 100);
    status = md->process_raw_tick(handle, &r_outlier, &norm_tick, &tick_emitted, &out_bar, &bar_emitted);
    assert(status == TC_OK);
    assert(tick_emitted == false); // Should be dropped as outlier spike!
    std::cout << "[+] Price Spike Outlier Filter (+33% deviation): DROPPED correctly" << std::endl;

    // Test 2D: Negative or Zero Price Validation
    TcRawTick r_invalid = make_raw_tick("AAPL", TC_RAW_TICK_TRADE, base_ts + 2'000'000'000LL, 0, 0, -5.00, 0, 0, 10);
    status = md->process_raw_tick(handle, &r_invalid, &norm_tick, &tick_emitted, &out_bar, &bar_emitted);
    assert(status == TC_OK);
    assert(tick_emitted == false); // Should be dropped as invalid!
    std::cout << "[+] Non-Positive Price Filter: DROPPED correctly" << std::endl << std::endl;

    // 4. Bar Aggregation & VWAP Verification
    std::cout << "--- 3. Deterministic OHLCV Bar Aggregation & VWAP ---" << std::endl;
    // Current bar has tick 1 at base_ts: last = 150.02, qty = 100
    // Trade 2: High = 151.50, qty = 200 at +15s
    TcRawTick r2 = make_raw_tick("AAPL", TC_RAW_TICK_TRADE, base_ts + 15'000'000'000LL, 0, 0, 151.50, 0, 0, 200);
    status = md->process_raw_tick(handle, &r2, nullptr, &tick_emitted, &out_bar, &bar_emitted);
    assert(status == TC_OK && tick_emitted && !bar_emitted);

    // Trade 3: Low = 149.20, qty = 150 at +30s
    TcRawTick r3 = make_raw_tick("AAPL", TC_RAW_TICK_TRADE, base_ts + 30'000'000'000LL, 0, 0, 149.20, 0, 0, 150);
    status = md->process_raw_tick(handle, &r3, nullptr, &tick_emitted, &out_bar, &bar_emitted);
    assert(status == TC_OK && tick_emitted && !bar_emitted);

    // Trade 4: Close = 150.80, qty = 300 at +45s
    TcRawTick r4 = make_raw_tick("AAPL", TC_RAW_TICK_TRADE, base_ts + 45'000'000'000LL, 0, 0, 150.80, 0, 0, 300);
    status = md->process_raw_tick(handle, &r4, nullptr, &tick_emitted, &out_bar, &bar_emitted);
    assert(status == TC_OK && tick_emitted && !bar_emitted);

    // Now send Trade 5 at +61s (new 60-second bar period).
    // This should close the previous 1-minute bar and emit it both via `bar_out` and callback!
    TcRawTick r5 = make_raw_tick("AAPL", TC_RAW_TICK_TRADE, base_ts + 61'000'000'000LL, 0, 0, 150.90, 0, 0, 50);
    status = md->process_raw_tick(handle, &r5, nullptr, &tick_emitted, &out_bar, &bar_emitted);
    assert(status == TC_OK);
    assert(tick_emitted == true);
    assert(bar_emitted == true);
    assert(g_bar_count.load() == 1);

    // Validate Aggregated Bar Math:
    // Open: 150.02
    // High: 151.50
    // Low: 149.20
    // Close: 150.80
    // Volume: 100 + 200 + 150 + 300 = 750
    // Num ticks: 4
    // Sum dollar volume: (150.02 * 100) + (151.50 * 200) + (149.20 * 150) + (150.80 * 300)
    // = 15002 + 30300 + 22380 + 45240 = 112922
    // VWAP = 112922 / 750 = 150.562666...
    assert(TC_PRICE_TO_DOUBLE(out_bar.open) == 150.02);
    assert(TC_PRICE_TO_DOUBLE(out_bar.high) == 151.50);
    assert(TC_PRICE_TO_DOUBLE(out_bar.low) == 149.20);
    assert(TC_PRICE_TO_DOUBLE(out_bar.close) == 150.80);
    assert(out_bar.volume == 750);
    assert(out_bar.num_ticks == 4);
    double expected_vwap = 112922.0 / 750.0;
    assert(std::abs(TC_PRICE_TO_DOUBLE(out_bar.vwap) - expected_vwap) < 0.01);

    std::cout << "[+] Aggregated Bar: O=$" << TC_PRICE_TO_DOUBLE(out_bar.open)
              << " H=$" << TC_PRICE_TO_DOUBLE(out_bar.high)
              << " L=$" << TC_PRICE_TO_DOUBLE(out_bar.low)
              << " C=$" << TC_PRICE_TO_DOUBLE(out_bar.close)
              << " Vol=" << out_bar.volume
              << " Ticks=" << out_bar.num_ticks
              << " VWAP=$" << std::fixed << std::setprecision(4) << TC_PRICE_TO_DOUBLE(out_bar.vwap) << std::endl;
    std::cout << "[+] Bar aggregation math verified successfully" << std::endl << std::endl;

    // Test 3B: Manual flush_bar
    std::cout << "--- 4. Manual Bar Flush ---" << std::endl;
    TcBar flushed_bar{};
    bool flushed = false;
    status = md->flush_bar(handle, "AAPL", &flushed_bar, &flushed);
    assert(status == TC_OK);
    assert(flushed == true);
    assert(TC_PRICE_TO_DOUBLE(flushed_bar.open) == 150.90);
    assert(flushed_bar.volume == 50);
    assert(flushed_bar.num_ticks == 1);
    std::cout << "[+] Manual flush_bar succeeded: C=$" << TC_PRICE_TO_DOUBLE(flushed_bar.close)
              << " Vol=" << flushed_bar.volume << std::endl << std::endl;

    // 5. Feed Health Monitoring
    std::cout << "--- 5. Real-Time Feed Health Monitoring ---" << std::endl;
    TcFeedStatus feed_st = TC_FEED_UNKNOWN;
    // Current time right after last tick: +62s (well within 5.0s threshold)
    status = md->get_feed_status(handle, "AAPL", base_ts + 62'000'000'000LL, &feed_st);
    assert(status == TC_OK);
    assert(feed_st == TC_FEED_OK);
    std::cout << "[+] Feed Status (delta = 1.0s): TC_FEED_OK (Normal)" << std::endl;

    // Current time 10s after last tick (exceeds 5.0s threshold)
    status = md->get_feed_status(handle, "AAPL", base_ts + 72'000'000'000LL, &feed_st);
    assert(status == TC_OK);
    assert(feed_st == TC_FEED_DEGRADED);
    std::cout << "[+] Feed Status (delta = 11.0s): TC_FEED_DEGRADED (Stale feed detected)" << std::endl;

    // Unseen symbol
    status = md->get_feed_status(handle, "UNKNOWN", base_ts + 72'000'000'000LL, &feed_st);
    assert(status == TC_OK);
    assert(feed_st == TC_FEED_UNKNOWN);
    std::cout << "[+] Feed Status (unseen symbol): TC_FEED_UNKNOWN" << std::endl << std::endl;

    // 6. Diagnostic Statistics Verification
    std::cout << "--- 6. Diagnostic Counters Verification ---" << std::endl;
    TcMarketDataStats stats{};
    status = md->get_stats(handle, &stats);
    assert(status == TC_OK);
    std::cout << "[+] Raw Ticks Ingested:   " << stats.raw_ticks_received << std::endl;
    std::cout << "[+] Ticks Normalized:     " << stats.ticks_normalized << std::endl;
    std::cout << "[+] Ticks Dropped Stale:   " << stats.ticks_dropped_stale << std::endl;
    std::cout << "[+] Ticks Dropped Outlier: " << stats.ticks_dropped_outlier << std::endl;
    std::cout << "[+] Ticks Dropped Invalid: " << stats.ticks_dropped_invalid << std::endl;
    std::cout << "[+] Bars Emitted:          " << stats.bars_emitted << std::endl;
    assert(stats.ticks_dropped_stale >= 1);
    assert(stats.ticks_dropped_outlier >= 1);
    assert(stats.ticks_dropped_invalid >= 1);
    assert(stats.bars_emitted >= 2); // 1 natural + 1 flushed
    std::cout << "[+] Diagnostic stats match expected drop counters" << std::endl << std::endl;

    // 7. High-Throughput Performance Benchmark
    std::cout << "--- 7. Hot-Path Tick Processing Benchmark ---" << std::endl;
    const int BENCHMARK_COUNT = 2'000'000;
    TcRawTick bench_raw = make_raw_tick("NVDA", TC_RAW_TICK_FULL, base_ts, 120.00, 120.05, 120.02, 1000, 1000, 100);

    auto start_time = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < BENCHMARK_COUNT; ++i) {
        bench_raw.ts_ns += 1'000'000LL; // Advance 1ms
        bench_raw.last += ((i & 1) ? 0.01 : -0.01);
        md->process_raw_tick(handle, &bench_raw, nullptr, &tick_emitted, nullptr, &bar_emitted);
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    double duration_sec = std::chrono::duration<double>(end_time - start_time).count();
    double throughput_mps = (BENCHMARK_COUNT / duration_sec) / 1'000'000.0;

    std::cout << "[+] Processed " << BENCHMARK_COUNT << " raw ticks in " << std::fixed << std::setprecision(4)
              << duration_sec << " seconds" << std::endl;
    std::cout << "[+] Raw Tick Processing Throughput: " << std::fixed << std::setprecision(2)
              << throughput_mps << " Million ticks/sec" << std::endl << std::endl;

    // 8. Lifecycle Shutdown
    TcModuleStatus mod_status{};
    vtable->get_status(handle, &mod_status);
    std::cout << "[+] Module Status: " << mod_status.status_msg << std::endl;

    vtable->stop(handle);
    vtable->destroy(handle);
#if !defined(_WIN32)
    DYN_CLOSE(lib);
#endif
    std::cout << "[+] Module successfully stopped, destroyed, and unloaded" << std::endl;

    std::cout << "=====================================================" << std::endl;
    std::cout << "    ALL TC_MARKETDATA TESTS PASSED SUCCESSFULLY!     " << std::endl;
    std::cout << "=====================================================" << std::endl;

    return 0;
}
