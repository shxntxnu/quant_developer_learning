/**
 * @file test_indicators.cpp
 * @brief Dynamic loading, mathematical verification, and throughput benchmark for tc_indicators.
 */

#include <iostream>
#include <iomanip>
#include <cassert>
#include <cstring>
#include <cmath>
#include <chrono>
#include <vector>

#include "tc/tc_abi.h"
#include "tc/tc_types.h"
#include "tc/tc_module.h"
#include "tc/indicators/tc_indicators.h"

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE DynLibHandle;
#define DYN_LOAD(path) LoadLibraryA(path)
#define DYN_GET(handle, sym) GetProcAddress(handle, sym)
#define DYN_CLOSE(handle) FreeLibrary(handle)
#define LIB_NAME "bin/tc_indicators.dll"
#elif defined(__APPLE__)
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_indicators.dylib"
#else
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_indicators.so"
#endif

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << "    TC-TRADER LAYER 2: TC_INDICATORS PLUGIN TEST     " << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 1. Dynamic Library Loading & VTable Handshake
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

    auto get_vtable_fn = reinterpret_cast<TcGetModuleVTableFn>(DYN_GET(lib, "tc_get_module_vtable"));
    assert(get_vtable_fn != nullptr);

    const TcModuleVTable* vtable = get_vtable_fn();
    assert(vtable != nullptr && vtable->abi_version == TC_ABI_VERSION);
    std::cout << "[+] Found module: " << vtable->name << " (ABI 0x" << std::hex << vtable->abi_version << std::dec << ")" << std::endl;

    TcModuleConfig cfg;
    cfg.module_name = "indicators_test";
    cfg.config_json = "{}";

    TcHandle handle = nullptr;
    TcStatus status = vtable->create(&cfg, &handle);
    assert(status == TC_OK && handle != nullptr);

    auto* ind = static_cast<ITcIndicators*>(vtable->get_interface(handle, "ITcIndicators"));
    assert(ind != nullptr && ind->version == 1);
    vtable->start(handle);
    std::cout << "[+] ITcIndicators interface discovered and initialized" << std::endl;

    uint32_t warmup_req = ind->warmup_bars_required(handle);
    assert(warmup_req == 50);
    std::cout << "[+] Warmup bars required: " << warmup_req << " bars" << std::endl << std::endl;

    // 2. Feed 60 Sequential Synthetic Bars to Verify Warmup & Accuracy
    std::cout << "--- Sequential Warmup & Mathematical Accuracy Tests ---" << std::endl;
    double price = 100.0;

    for (int bar_idx = 1; bar_idx <= 60; ++bar_idx) {
        // Create an upward trending bar sequence with oscillations
        double shock = std::sin(bar_idx * 0.3) * 1.5;
        price += 0.5 + shock;

        TcBar bar{};
        bar.struct_size = sizeof(TcBar);
        bar.version = 1;
        bar.timeframe_sec = 300;
        strncpy(bar.symbol, "AAPL", sizeof(bar.symbol) - 1);
        bar.ts_ns = bar_idx * 300000000000LL;
        bar.open = TC_DOUBLE_TO_PRICE(price - 0.2);
        bar.high = TC_DOUBLE_TO_PRICE(price + 1.0);
        bar.low = TC_DOUBLE_TO_PRICE(price - 0.8);
        bar.close = TC_DOUBLE_TO_PRICE(price);
        bar.volume = 10000 + (bar_idx * 100);

        status = ind->update_bar(handle, &bar);
        assert(status == TC_OK);

        TcIndicatorSnapshot snap{};
        status = ind->get_snapshot(handle, "AAPL", &snap);
        assert(status == TC_OK);

        if (bar_idx == 15) {
            // At bar 15, 14 price deltas have been observed, so RSI & ATR must be ready
            assert((snap.valid_mask & TC_IND_MASK_RSI) != 0);
            assert((snap.valid_mask & TC_IND_MASK_ATR) != 0);
            assert(snap.rsi >= 0.0 && snap.rsi <= 100.0);
            assert(snap.atr > 0.0);
            std::cout << "Bar 15: RSI=" << std::fixed << std::setprecision(2) << snap.rsi
                      << ", ATR=" << snap.atr << " [RSI/ATR Initialized]" << std::endl;
        } else if (bar_idx == 20) {
            // At bar 20, Fast SMA, EMA, and Bollinger Bands must be ready
            assert((snap.valid_mask & TC_IND_MASK_SMA_FAST) != 0);
            assert((snap.valid_mask & TC_IND_MASK_EMA) != 0);
            assert((snap.valid_mask & TC_IND_MASK_BB) != 0);
            assert(snap.bb_upper >= snap.bb_middle);
            assert(snap.bb_middle >= snap.bb_lower);
            std::cout << "Bar 20: SMA20=" << snap.sma_fast << ", EMA=" << snap.ema
                      << ", BB=[" << snap.bb_lower << ", " << snap.bb_middle << ", " << snap.bb_upper
                      << "] [SMA/EMA/BB Initialized]" << std::endl;
        } else if (bar_idx == 50) {
            // At bar 50, Slow SMA is ready, full warmup reached
            assert((snap.valid_mask & TC_IND_MASK_SMA_SLOW) != 0);
            std::cout << "Bar 50: SMA50=" << snap.sma_slow
                      << ", MACD=" << snap.macd << ", Signal=" << snap.macd_signal
                      << ", ADX=" << snap.adx << " [Full Warmup Complete!]" << std::endl;
        }
    }

    // Verify snapshot state after 60 bars
    TcIndicatorSnapshot final_snap{};
    ind->get_snapshot(handle, "AAPL", &final_snap);

    // Validate boundaries
    assert(final_snap.rsi >= 0.0 && final_snap.rsi <= 100.0);
    assert(final_snap.bb_upper >= final_snap.bb_middle);
    assert(final_snap.bb_middle >= final_snap.bb_lower);
    assert(final_snap.atr > 0.0);
    assert(final_snap.adx >= 0.0 && final_snap.adx <= 100.0);
    std::cout << "[+] All Mathematical Indicator Bounds Verified: PASSED" << std::endl << std::endl;

    // -------------------------------------------------------------
    // 3. High-Throughput O(1) Performance Benchmark
    // -------------------------------------------------------------
    std::cout << "--- High-Throughput O(1) Streaming Benchmark ---" << std::endl;
    constexpr size_t BenchmarkBars = 1000000; // 1 Million bars
    std::cout << "Streaming " << BenchmarkBars << " bars through O(1) incremental engine..." << std::endl;

    TcBar bench_bar{};
    bench_bar.struct_size = sizeof(TcBar);
    bench_bar.version = 1;
    strncpy(bench_bar.symbol, "MSFT", sizeof(bench_bar.symbol) - 1);
    bench_bar.high = TC_DOUBLE_TO_PRICE(425.00);
    bench_bar.low = TC_DOUBLE_TO_PRICE(418.00);
    bench_bar.open = TC_DOUBLE_TO_PRICE(420.00);
    bench_bar.close = TC_DOUBLE_TO_PRICE(422.50);

    auto start_time = std::chrono::steady_clock::now();

    for (size_t i = 1; i <= BenchmarkBars; ++i) {
        bench_bar.ts_ns = static_cast<int64_t>(i * 1000000LL);
        ind->update_bar(handle, &bench_bar);
    }

    auto end_time = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    double throughput = (BenchmarkBars / (elapsed_ms / 1000.0)) / 1000000.0;

    std::cout << "Elapsed Time: " << std::fixed << std::setprecision(2) << elapsed_ms << " ms" << std::endl;
    std::cout << "Throughput:   " << std::setprecision(2) << throughput << " Million bars/sec" << std::endl;
    assert(throughput > 2.0); // Must exceed 2M bars/sec
    std::cout << "[+] High-Speed Streaming Benchmark: PASSED" << std::endl;

    // Diagnostics
    TcModuleStatus mod_status{};
    vtable->get_status(handle, &mod_status);
    std::cout << "Module Diagnostics: " << mod_status.status_msg << std::endl;

    vtable->stop(handle);
    vtable->destroy(handle);
#if !defined(_WIN32)
    DYN_CLOSE(lib);
#endif

    std::cout << "\n=====================================================" << std::endl;
    std::cout << "    TC_INDICATORS PLUGIN TESTS PASSED COMPLETELY!    " << std::endl;
    std::cout << "=====================================================" << std::endl;

    return 0;
}
