/**
 * @file test_strategy.cpp
 * @brief Dynamic loading, regime classification, rule evaluation, and benchmark for tc_strategy.
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
#include "tc/strategy/tc_strategy.h"

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE DynLibHandle;
#define DYN_LOAD(path) LoadLibraryA(path)
#define DYN_GET(handle, sym) GetProcAddress(handle, sym)
#define DYN_CLOSE(handle) FreeLibrary(handle)
#define LIB_NAME "bin/tc_strategy.dll"
#elif defined(__APPLE__)
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_strategy.dylib"
#else
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_strategy.so"
#endif

static TcIndicatorSnapshot make_base_snapshot(const char* symbol, double price, int64_t ts_ns) {
    TcIndicatorSnapshot s{};
    s.struct_size = sizeof(TcIndicatorSnapshot);
    s.version = 1;
    strncpy(s.symbol, symbol, sizeof(s.symbol) - 1);
    s.ts_ns = ts_ns;
    s.last_close = price;
    s.valid_mask = TC_IND_MASK_SMA_FAST | TC_IND_MASK_SMA_SLOW | TC_IND_MASK_EMA |
                   TC_IND_MASK_RSI | TC_IND_MASK_MACD | TC_IND_MASK_ATR |
                   TC_IND_MASK_BB | TC_IND_MASK_ADX;

    s.sma_fast = price;
    s.sma_slow = price;
    s.ema = price;
    s.rsi = 50.0;
    s.macd = 0.0;
    s.macd_signal = 0.0;
    s.macd_hist = 0.0;
    s.atr = 2.0;
    s.bb_middle = price;
    s.bb_upper = price + 4.0;
    s.bb_lower = price - 4.0;
    s.adx = 20.0;
    return s;
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << "      TC-TRADER LAYER 2: TC_STRATEGY PLUGIN TEST     " << std::endl;
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
    cfg.module_name = "tc_strategy";
    cfg.config_json = "{\"adx_trend_threshold\": 25.0, \"atr_vol_multiplier\": 2.5}";

    TcHandle handle = nullptr;
    TcStatus status = vtable->create(&cfg, &handle);
    assert(status == TC_OK && handle != nullptr);

    auto* strat = static_cast<ITcStrategy*>(vtable->get_interface(handle, "ITcStrategy"));
    assert(strat != nullptr && strat->version == 1);
    vtable->start(handle);
    std::cout << "[+] ITcStrategy interface discovered and running" << std::endl << std::endl;

    // 2. Parameter Verification
    std::cout << "--- 1. Parameter Inspection & Configuration ---" << std::endl;
    TcStrategyParams params{};
    status = strat->get_params(handle, &params);
    assert(status == TC_OK);
    assert(params.adx_trend_threshold == 25.0);
    assert(params.enable_trend == true);
    assert(params.enable_mean_reversion == true);
    std::cout << "[+] Default/JSON Parameters verified: ADX Threshold=" << params.adx_trend_threshold
              << ", ATR Stop Mult=" << params.atr_stop_multiplier << std::endl << std::endl;

    // 3. Market Regime Classification Tests
    std::cout << "--- 2. Market Regime Classification Tests ---" << std::endl;
    TcIndicatorSnapshot snap = make_base_snapshot("AAPL", 150.0, 1000000000LL);
    TcMarketRegime regime = TC_REGIME_UNKNOWN;

    // Test 2A: Ranging Market (ADX = 18 <= 25)
    snap.adx = 18.0;
    status = strat->classify_regime(handle, &snap, &regime);
    assert(status == TC_OK && regime == TC_REGIME_RANGING);
    std::cout << "[+] Ranging Regime (ADX 18.0): PASSED" << std::endl;

    // Test 2B: Trending Bull (ADX = 32 > 25, Fast SMA > Slow SMA)
    snap.adx = 32.0;
    snap.sma_fast = 155.0;
    snap.sma_slow = 148.0;
    status = strat->classify_regime(handle, &snap, &regime);
    assert(status == TC_OK && regime == TC_REGIME_TRENDING_BULL);
    std::cout << "[+] Trending Bull Regime (ADX 32.0, SMA20 > SMA50): PASSED" << std::endl;

    // Test 2C: Trending Bear (ADX = 35 > 25, Fast SMA < Slow SMA)
    snap.adx = 35.0;
    snap.sma_fast = 142.0;
    snap.sma_slow = 150.0;
    status = strat->classify_regime(handle, &snap, &regime);
    assert(status == TC_OK && regime == TC_REGIME_TRENDING_BEAR);
    std::cout << "[+] Trending Bear Regime (ADX 35.0, SMA20 < SMA50): PASSED" << std::endl;

    // Test 2D: High Volatility (Wide Bollinger Bandwidth > 0.15)
    snap.adx = 20.0;
    snap.bb_middle = 100.0;
    snap.bb_upper = 125.0;
    snap.bb_lower = 75.0; // Bandwidth = (125-75)/100 = 0.50
    status = strat->classify_regime(handle, &snap, &regime);
    assert(status == TC_OK && regime == TC_REGIME_HIGH_VOLATILITY);
    std::cout << "[+] High Volatility Regime (BB Bandwidth 0.50): PASSED" << std::endl << std::endl;

    // 4. Entry Rules Evaluation
    std::cout << "--- 3. Entry Rules Evaluation (Flat Position) ---" << std::endl;
    TcSignal signals[4]{};
    size_t num_signals = 0;
    TcPosition pos{};
    pos.struct_size = sizeof(TcPosition);
    pos.version = 1;
    strncpy(pos.symbol, "AAPL", sizeof(pos.symbol) - 1);
    pos.net_qty = 0; // Flat

    // Test 3A: Bullish Trend Entry
    snap = make_base_snapshot("AAPL", 150.0, 2000000000LL);
    snap.adx = 30.0;
    snap.sma_fast = 152.0;
    snap.sma_slow = 148.0;
    snap.macd = 1.5;
    snap.macd_signal = 0.8;
    snap.rsi = 60.0; // Healthy bullish momentum
    snap.atr = 2.5;

    status = strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);
    assert(status == TC_OK);
    assert(num_signals == 1);
    assert(signals[0].side == TC_SIDE_BUY);
    assert(signals[0].rule_id == TC_RULE_ENTRY_TREND_BULL);
    assert(signals[0].entry_ref_px == TC_DOUBLE_TO_PRICE(150.0));
    assert(signals[0].stop_px < signals[0].entry_ref_px);
    assert(signals[0].target_px > signals[0].entry_ref_px);
    std::cout << "[+] Bullish Trend Entry Signal: BUY @ $" << TC_PRICE_TO_DOUBLE(signals[0].entry_ref_px)
              << " | Stop: $" << TC_PRICE_TO_DOUBLE(signals[0].stop_px)
              << " | Target: $" << TC_PRICE_TO_DOUBLE(signals[0].target_px) << std::endl;

    // Test 3B: Bearish Trend Entry
    strat->reset(handle);
    snap = make_base_snapshot("AAPL", 140.0, 3000000000LL);
    snap.adx = 32.0;
    snap.sma_fast = 138.0;
    snap.sma_slow = 145.0;
    snap.macd = -1.2;
    snap.macd_signal = -0.5;
    snap.rsi = 40.0; // Healthy bearish momentum
    snap.atr = 2.0;

    status = strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);
    assert(status == TC_OK);
    assert(num_signals == 1);
    assert(signals[0].side == TC_SIDE_SELL);
    assert(signals[0].rule_id == TC_RULE_ENTRY_TREND_BEAR);
    assert(signals[0].stop_px > signals[0].entry_ref_px);
    assert(signals[0].target_px < signals[0].entry_ref_px);
    std::cout << "[+] Bearish Trend Entry Signal: SELL @ $" << TC_PRICE_TO_DOUBLE(signals[0].entry_ref_px)
              << " | Stop: $" << TC_PRICE_TO_DOUBLE(signals[0].stop_px)
              << " | Target: $" << TC_PRICE_TO_DOUBLE(signals[0].target_px) << std::endl;

    // Test 3C: Mean-Reversion Oversold Long Entry (Ranging)
    strat->reset(handle);
    snap = make_base_snapshot("AAPL", 95.0, 4000000000LL);
    snap.adx = 18.0; // Ranging
    snap.bb_middle = 100.0;
    snap.bb_lower = 96.0;
    snap.bb_upper = 104.0;
    snap.last_close = 95.0; // Below lower band
    snap.rsi = 24.0;        // Oversold (< 30)
    snap.atr = 1.5;

    status = strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);
    assert(status == TC_OK);
    assert(num_signals == 1);
    assert(signals[0].side == TC_SIDE_BUY);
    assert(signals[0].rule_id == TC_RULE_ENTRY_MEAN_REV_LONG);
    assert(signals[0].target_px == TC_DOUBLE_TO_PRICE(100.0)); // Mean reversion to BB middle
    std::cout << "[+] Mean-Reversion Oversold Long Signal: BUY @ $" << TC_PRICE_TO_DOUBLE(signals[0].entry_ref_px)
              << " | Target: $" << TC_PRICE_TO_DOUBLE(signals[0].target_px) << std::endl;

    // Test 3D: Mean-Reversion Overbought Short Entry (Ranging)
    strat->reset(handle);
    snap = make_base_snapshot("AAPL", 105.0, 5000000000LL);
    snap.adx = 18.0; // Ranging
    snap.bb_middle = 100.0;
    snap.bb_lower = 96.0;
    snap.bb_upper = 104.0;
    snap.last_close = 105.0; // Above upper band
    snap.rsi = 76.0;         // Overbought (> 70)
    snap.atr = 1.5;

    status = strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);
    assert(status == TC_OK);
    assert(num_signals == 1);
    assert(signals[0].side == TC_SIDE_SELL);
    assert(signals[0].rule_id == TC_RULE_ENTRY_MEAN_REV_SHORT);
    assert(signals[0].target_px == TC_DOUBLE_TO_PRICE(100.0));
    std::cout << "[+] Mean-Reversion Overbought Short Signal: SELL @ $" << TC_PRICE_TO_DOUBLE(signals[0].entry_ref_px)
              << " | Target: $" << TC_PRICE_TO_DOUBLE(signals[0].target_px) << std::endl << std::endl;

    // 5. Exit Rules Evaluation
    std::cout << "--- 4. Exit Rules Evaluation (Active Position) ---" << std::endl;

    // Test 4A: Trailing Stop Loss Exit
    strat->reset(handle);
    pos.net_qty = 100; // Long 100 shares
    pos.avg_cost = TC_DOUBLE_TO_PRICE(100.0);
    pos.last_mark_px = TC_DOUBLE_TO_PRICE(100.0);

    // Initial evaluation establishes baseline entry
    snap = make_base_snapshot("AAPL", 100.0, 1000000000LL);
    snap.atr = 2.0; // Stop distance = 2.0 * 2.0 = $4.00
    strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);
    assert(num_signals == 0); // No exit yet

    // Price rallies to $104.00 (peak price updated to 104.00, trailing stop level = 104 - 4 = 100.00, below $106.00 take profit)
    pos.last_mark_px = TC_DOUBLE_TO_PRICE(104.0);
    snap = make_base_snapshot("AAPL", 104.0, 1060000000LL);
    snap.atr = 2.0;
    strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);
    assert(num_signals == 0); // No exit at $104.00

    // Price pulls back to $99.50 (below 100.00 trailing stop level) -> Exit!
    pos.last_mark_px = TC_DOUBLE_TO_PRICE(99.50);
    snap = make_base_snapshot("AAPL", 99.50, 1120000000LL);
    snap.atr = 2.0;
    status = strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);
    assert(status == TC_OK && num_signals == 1);
    assert(signals[0].side == TC_SIDE_SELL);
    assert(signals[0].rule_id == TC_RULE_EXIT_TRAILING_STOP);
    std::cout << "[+] Trailing Stop Exit: SELL triggered @ $" << TC_PRICE_TO_DOUBLE(signals[0].entry_ref_px)
              << " after peak $104.00 (Trailing Level: $" << TC_PRICE_TO_DOUBLE(signals[0].stop_px) << ")" << std::endl;

    // Test 4B: Take Profit Target Exit
    strat->reset(handle);
    pos.net_qty = 100;
    pos.avg_cost = TC_DOUBLE_TO_PRICE(100.0);
    pos.last_mark_px = TC_DOUBLE_TO_PRICE(100.0);

    snap = make_base_snapshot("AAPL", 100.0, 2000000000LL);
    snap.atr = 2.0; // Target distance = 3.0 * 2.0 = $6.00 -> Target is $106.00
    strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);

    // Price jumps directly to $107.00
    pos.last_mark_px = TC_DOUBLE_TO_PRICE(107.0);
    snap = make_base_snapshot("AAPL", 107.0, 2060000000LL);
    snap.atr = 2.0;
    status = strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);
    assert(status == TC_OK && num_signals == 1);
    assert(signals[0].side == TC_SIDE_SELL);
    assert(signals[0].rule_id == TC_RULE_EXIT_TAKE_PROFIT);
    std::cout << "[+] Take Profit Exit: SELL triggered @ $" << TC_PRICE_TO_DOUBLE(signals[0].entry_ref_px)
              << " (Target: $" << TC_PRICE_TO_DOUBLE(signals[0].target_px) << ")" << std::endl;

    // Test 4C: Moving Average Reversal Exit
    strat->reset(handle);
    pos.net_qty = 100;
    pos.avg_cost = TC_DOUBLE_TO_PRICE(100.0);
    pos.last_mark_px = TC_DOUBLE_TO_PRICE(100.0);

    snap = make_base_snapshot("AAPL", 100.0, 3000000000LL);
    snap.atr = 5.0; // Large ATR so stop/target aren't hit
    strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);

    // Bearish MA Cross occurs: Fast MA 98.0 < Slow MA 102.0 and MACD negative
    snap = make_base_snapshot("AAPL", 100.0, 3060000000LL);
    snap.atr = 5.0;
    snap.sma_fast = 98.0;
    snap.sma_slow = 102.0;
    snap.macd = -0.5;
    snap.macd_signal = 0.1;
    status = strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);
    assert(status == TC_OK && num_signals == 1);
    assert(signals[0].side == TC_SIDE_SELL);
    assert(signals[0].rule_id == TC_RULE_EXIT_MA_REVERSAL);
    std::cout << "[+] MA Reversal Exit: SELL triggered on Bearish MA/MACD cross" << std::endl;

    // Test 4D: Max Holding Duration Exit
    strat->reset(handle);
    pos.net_qty = 100;
    pos.avg_cost = TC_DOUBLE_TO_PRICE(100.0);
    pos.last_mark_px = TC_DOUBLE_TO_PRICE(100.0);

    const int64_t t0 = 4000000000LL;
    snap = make_base_snapshot("AAPL", 100.0, t0);
    snap.atr = 10.0;
    strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);

    // 5 hours later (5 * 3600 * 1e9 ns = 18.0e12 ns > 14.4e12 ns limit)
    const int64_t t_expired = t0 + 18000000000000LL;
    snap = make_base_snapshot("AAPL", 100.0, t_expired);
    snap.atr = 10.0;
    status = strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);
    assert(status == TC_OK && num_signals == 1);
    assert(signals[0].side == TC_SIDE_SELL);
    assert(signals[0].rule_id == TC_RULE_EXIT_MAX_HOLDING);
    std::cout << "[+] Max Holding Timeout Exit: SELL triggered after trade exceeded holding limit" << std::endl << std::endl;

    // 6. High-Throughput Performance Benchmark
    std::cout << "--- 5. Hot-Path Throughput Benchmark ---" << std::endl;
    strat->reset(handle);
    pos.net_qty = 0;
    const int BENCHMARK_COUNT = 1000000;

    snap = make_base_snapshot("AAPL", 150.0, 1000000LL);
    snap.adx = 30.0;
    snap.sma_fast = 152.0;
    snap.sma_slow = 148.0;
    snap.macd = 1.0;
    snap.macd_signal = 0.5;
    snap.rsi = 55.0;

    auto start_time = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < BENCHMARK_COUNT; ++i) {
        snap.ts_ns += 300000000000LL;
        strat->on_snapshot(handle, &snap, &pos, signals, 4, &num_signals);
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    double duration_sec = std::chrono::duration<double>(end_time - start_time).count();
    double throughput_mps = (BENCHMARK_COUNT / duration_sec) / 1000000.0;

    std::cout << "[+] Evaluated " << BENCHMARK_COUNT << " snapshots in " << std::fixed << std::setprecision(4)
              << duration_sec << " seconds" << std::endl;
    std::cout << "[+] Hot-Path Evaluation Throughput: " << std::fixed << std::setprecision(2)
              << throughput_mps << " Million evals/sec" << std::endl << std::endl;

    // 7. Status & Cleanup
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
    std::cout << "      ALL TC_STRATEGY TESTS PASSED SUCCESSFULLY!     " << std::endl;
    std::cout << "=====================================================" << std::endl;

    return 0;
}
