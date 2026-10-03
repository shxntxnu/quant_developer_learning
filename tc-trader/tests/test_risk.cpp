/**
 * @file test_risk.cpp
 * @brief Dynamic loading, position sizing math, pre-trade checks, kill switch, and benchmark for tc_risk.
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
#include "tc/risk/tc_risk.h"

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE DynLibHandle;
#define DYN_LOAD(path) LoadLibraryA(path)
#define DYN_GET(handle, sym) GetProcAddress(handle, sym)
#define DYN_CLOSE(handle) FreeLibrary(handle)
#define LIB_NAME "bin/tc_risk.dll"
#elif defined(__APPLE__)
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_risk.dylib"
#else
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_risk.so"
#endif

static TcSignal make_signal(const char* symbol, int8_t side, uint32_t rule_id,
                            double entry_px, double stop_px, double target_px, int64_t ts_ns) {
    TcSignal sig{};
    sig.struct_size = sizeof(TcSignal);
    sig.version = 1;
    sig.side = side;
    sig.order_type = 1; /* Market order */
    strncpy(sig.symbol, symbol, sizeof(sig.symbol) - 1);
    sig.ts_ns = ts_ns;
    sig.rule_id = rule_id;
    sig.flags = 0;
    sig.strength = 0.9;
    sig.entry_ref_px = TC_DOUBLE_TO_PRICE(entry_px);
    sig.stop_px = TC_DOUBLE_TO_PRICE(stop_px);
    sig.target_px = TC_DOUBLE_TO_PRICE(target_px);
    return sig;
}

static TcAccountView make_account(double cash, double equity, double bp, double daily_pnl, double drawdown_pct) {
    TcAccountView acct{};
    acct.struct_size = sizeof(TcAccountView);
    acct.version = 1;
    acct.ts_ns = 1000000000LL;
    acct.total_cash = TC_DOUBLE_TO_PRICE(cash);
    acct.net_liquidation = TC_DOUBLE_TO_PRICE(equity);
    acct.buying_power = TC_DOUBLE_TO_PRICE(bp);
    acct.daily_pnl = TC_DOUBLE_TO_PRICE(daily_pnl);
    acct.peak_equity = TC_DOUBLE_TO_PRICE(equity);
    acct.drawdown_pct = drawdown_pct;
    acct.kill_switch_on = false;
    return acct;
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << "       TC-TRADER LAYER 2: TC_RISK PLUGIN TEST        " << std::endl;
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
    cfg.module_name = "tc_risk";
    cfg.config_json = "{\"risk_fraction_per_trade\": 0.01, \"max_position_qty\": 1000}";

    TcHandle handle = nullptr;
    TcStatus status = vtable->create(&cfg, &handle);
    assert(status == TC_OK && handle != nullptr);

    auto* risk = static_cast<ITcRisk*>(vtable->get_interface(handle, "ITcRisk"));
    assert(risk != nullptr && risk->version == 1);
    vtable->start(handle);
    std::cout << "[+] ITcRisk interface discovered and running" << std::endl << std::endl;

    // 2. Setup Baseline Account State: $100,000 Equity, $200,000 Buying Power
    TcAccountView acct = make_account(100000.0, 100000.0, 200000.0, 0.0, 0.0);
    risk->update_account(handle, &acct);

    // 3. Test Position Sizing Math (Fixed-Fractional)
    std::cout << "--- 1. Position Sizing Mathematical Verification ---" << std::endl;
    // Equity = $100,000, RiskFraction = 1% -> Risk Capital = $1,000
    // Long Entry = $150.00, Stop = $145.00 -> Per-share risk = $5.00
    // Expected Sized Quantity = floor(1000 / 5) = 200 shares!
    TcSignal sig = make_signal("AAPL", TC_SIDE_BUY, 101, 150.0, 145.0, 160.0, 1000000LL);
    TcOrderIntent intent{};
    TcRiskDecision decision{};

    status = risk->evaluate_signal(handle, &sig, &intent, &decision);
    assert(status == TC_OK);
    assert(decision.code == TC_RISK_APPROVED);
    assert(decision.approved_qty == 200);
    assert(intent.qty == 200);
    assert(intent.side == TC_SIDE_BUY);
    assert(intent.stop_loss_px == TC_DOUBLE_TO_PRICE(145.0));
    assert(intent.take_profit_px == TC_DOUBLE_TO_PRICE(160.0));
    assert(intent.flags == TC_INTENT_FLAG_BRACKET);
    std::cout << "[+] Sizing: Equity $100k, 1.0% Risk ($1,000), Stop $5.00 -> Sized Qty: "
              << decision.approved_qty << " shares (Expected: 200): PASSED" << std::endl;

    // Short Entry: $100.00, Stop = $102.50 -> Per-share risk = $2.50
    // Expected Sized Quantity = floor(1000 / 2.50) = 400 shares!
    sig = make_signal("AAPL", TC_SIDE_SELL, 102, 100.0, 102.5, 95.0, 2000000LL);
    status = risk->evaluate_signal(handle, &sig, &intent, &decision);
    assert(status == TC_OK && decision.code == TC_RISK_APPROVED);
    assert(decision.approved_qty == 400);
    std::cout << "[+] Short Sizing: Stop $2.50 -> Sized Qty: " << decision.approved_qty
              << " shares (Expected: 400): PASSED" << std::endl << std::endl;

    // 4. Pre-Trade Risk Checks & Boundary Tests
    std::cout << "--- 2. Pre-Trade Risk Validation & Boundary Tests ---" << std::endl;

    // Test 2A: Invalid Stop Price (Stop >= Entry on Long)
    sig = make_signal("AAPL", TC_SIDE_BUY, 101, 150.0, 150.0, 160.0, 3000000LL);
    status = risk->evaluate_signal(handle, &sig, &intent, &decision);
    assert(status == TC_OK && decision.code == TC_RISK_REJECT_INVALID_STOP);
    std::cout << "[+] Invalid Stop Check (Stop >= Entry): REJECTED (" << decision.reason << ")" << std::endl;

    // Test 2B: Stop Distance Too Tight (< 0.2% min threshold)
    sig = make_signal("AAPL", TC_SIDE_BUY, 101, 100.0, 99.90, 105.0, 4000000LL); // 0.10% distance < 0.20% min
    status = risk->evaluate_signal(handle, &sig, &intent, &decision);
    assert(status == TC_OK && decision.code == TC_RISK_REJECT_INVALID_STOP);
    std::cout << "[+] Tight Stop Check (< 0.2%): REJECTED (" << decision.reason << ")" << std::endl;

    // Test 2C: Price Collar Deviation (> 3% from last mark price)
    TcPosition pos{};
    pos.struct_size = sizeof(TcPosition);
    pos.version = 1;
    strncpy(pos.symbol, "AAPL", sizeof(pos.symbol) - 1);
    pos.last_mark_px = TC_DOUBLE_TO_PRICE(100.0);
    risk->update_position(handle, &pos);

    // Signal arrives with price $105.00 (5% deviation > 3% collar)
    sig = make_signal("AAPL", TC_SIDE_BUY, 101, 105.0, 102.0, 110.0, 5000000LL);
    status = risk->evaluate_signal(handle, &sig, &intent, &decision);
    assert(status == TC_OK && decision.code == TC_RISK_REJECT_PRICE_COLLAR);
    std::cout << "[+] Price Collar Check (5% dev > 3% collar): REJECTED (" << decision.reason << ")" << std::endl;

    // Test 2D: Max Single Position Clamping (Limit = 1000 shares)
    // Very tight stop: Entry $100.00, Stop $99.50 -> Per-share risk $0.50 -> Sizing = 2,000 shares
    pos.last_mark_px = TC_DOUBLE_TO_PRICE(100.0);
    risk->update_position(handle, &pos);
    sig = make_signal("AAPL", TC_SIDE_BUY, 101, 100.0, 99.50, 102.0, 6000000LL);
    status = risk->evaluate_signal(handle, &sig, &intent, &decision);
    assert(status == TC_OK && decision.code == TC_RISK_APPROVED);
    assert(decision.approved_qty == 1000); // Clamped from 2,000 down to max 1,000!
    std::cout << "[+] Max Position Clamping: 2,000 shares clamped to limit " << decision.approved_qty << ": PASSED" << std::endl;

    // Test 2E: Insufficient Buying Power
    // Set low buying power = $8,000 with $5,000 buffer -> available $3,000
    // Sized order: 100 shares @ $100 = $10,000 -> exceeds $3,000 available -> clamped to 30 shares
    acct = make_account(8000.0, 100000.0, 8000.0, 0.0, 0.0);
    risk->update_account(handle, &acct);
    sig = make_signal("AAPL", TC_SIDE_BUY, 101, 100.0, 90.0, 110.0, 7000000LL); // 100 shares sized
    status = risk->evaluate_signal(handle, &sig, &intent, &decision);
    assert(status == TC_OK && decision.code == TC_RISK_APPROVED);
    assert(decision.approved_qty == 30); // $3,000 / $100 = 30 shares
    std::cout << "[+] Buying Power Clamping: Clamped to available capital (" << decision.approved_qty << " shares): PASSED" << std::endl;

    // Test 2F: Disallowed Shorting Policy
    TcRiskParams custom_params{};
    risk->get_params(handle, &custom_params);
    custom_params.allow_shorting = false;
    risk->set_params(handle, &custom_params);

    sig = make_signal("AAPL", TC_SIDE_SELL, 102, 100.0, 105.0, 90.0, 8000000LL);
    status = risk->evaluate_signal(handle, &sig, &intent, &decision);
    assert(status == TC_OK && decision.code == TC_RISK_REJECT_SHORT_DISALLOWED);
    std::cout << "[+] Policy Short Check (allow_shorting = false): REJECTED (" << decision.reason << ")" << std::endl << std::endl;

    // 5. Exit Order (Reduce-Only) Pass-Through
    std::cout << "--- 3. Reduce-Only Exit Order Pass-Through ---" << std::endl;
    // Restore normal params and account
    custom_params.allow_shorting = true;
    risk->set_params(handle, &custom_params);
    acct = make_account(100000.0, 100000.0, 200000.0, 0.0, 0.0);
    risk->update_account(handle, &acct);

    // Register active position of 150 shares
    pos.net_qty = 150;
    pos.avg_cost = TC_DOUBLE_TO_PRICE(100.0);
    pos.last_mark_px = TC_DOUBLE_TO_PRICE(105.0);
    risk->update_position(handle, &pos);

    // Exit signal arrives (e.g. Trailing Stop Exit)
    sig = make_signal("AAPL", TC_SIDE_SELL, 201 /* TC_RULE_EXIT_TRAILING_STOP */, 105.0, 0, 0, 9000000LL);
    status = risk->evaluate_signal(handle, &sig, &intent, &decision);
    assert(status == TC_OK && decision.code == TC_RISK_APPROVED);
    assert(decision.approved_qty == 150); // Closes entire active position
    assert(intent.qty == 150);
    assert(intent.flags == TC_INTENT_FLAG_REDUCE_ONLY);
    std::cout << "[+] Reduce-Only Exit Order Approved: " << decision.approved_qty
              << " shares with TC_INTENT_FLAG_REDUCE_ONLY: PASSED" << std::endl << std::endl;

    // 6. Kill Switch Tests (Symbol & Global & Drawdown Auto-Kill)
    std::cout << "--- 4. Kill Switch Verification ---" << std::endl;
    pos.net_qty = 0; // Flat
    risk->update_position(handle, &pos);

    // Test 4A: Symbol Kill Switch
    risk->set_kill_switch(handle, "AAPL", true);
    bool is_killed = false;
    risk->get_kill_switch(handle, "AAPL", &is_killed);
    assert(is_killed == true);

    sig = make_signal("AAPL", TC_SIDE_BUY, 101, 100.0, 95.0, 110.0, 10000000LL);
    status = risk->evaluate_signal(handle, &sig, &intent, &decision);
    assert(status == TC_OK && decision.code == TC_RISK_REJECT_KILL_SWITCH);
    std::cout << "[+] Symbol Kill Switch ('AAPL'): REJECTED (" << decision.reason << ")" << std::endl;

    // Other symbols remain unaffected
    sig = make_signal("MSFT", TC_SIDE_BUY, 101, 400.0, 390.0, 420.0, 11000000LL);
    status = risk->evaluate_signal(handle, &sig, &intent, &decision);
    assert(status == TC_OK && decision.code == TC_RISK_APPROVED);
    std::cout << "[+] Other Symbol ('MSFT') during AAPL Kill: APPROVED" << std::endl;

    // Disarm AAPL
    risk->set_kill_switch(handle, "AAPL", false);

    // Test 4B: Global Kill Switch
    risk->set_kill_switch(handle, nullptr, true);
    sig = make_signal("MSFT", TC_SIDE_BUY, 101, 400.0, 390.0, 420.0, 12000000LL);
    status = risk->evaluate_signal(handle, &sig, &intent, &decision);
    assert(status == TC_OK && decision.code == TC_RISK_REJECT_KILL_SWITCH);
    std::cout << "[+] Global Kill Switch: REJECTED (" << decision.reason << ")" << std::endl;
    risk->set_kill_switch(handle, nullptr, false); // Disarm

    // Test 4C: Auto-Kill on Drawdown Breach (> 5%)
    acct = make_account(100000.0, 94000.0, 180000.0, -6000.0, 0.06 /* 6% drawdown */);
    risk->update_account(handle, &acct);

    risk->get_kill_switch(handle, nullptr, &is_killed);
    assert(is_killed == true); // Automatically engaged!

    sig = make_signal("AAPL", TC_SIDE_BUY, 101, 100.0, 95.0, 110.0, 13000000LL);
    status = risk->evaluate_signal(handle, &sig, &intent, &decision);
    assert(status == TC_OK && (decision.code == TC_RISK_REJECT_KILL_SWITCH || decision.code == TC_RISK_REJECT_MAX_DRAWDOWN));
    std::cout << "[+] Auto-Kill on 6% Drawdown Breach: ENGAGED & REJECTED" << std::endl << std::endl;

    // 7. High-Throughput Performance Benchmark
    std::cout << "--- 5. Hot-Path Throughput Benchmark ---" << std::endl;
    risk->reset(handle);
    acct = make_account(100000.0, 100000.0, 200000.0, 0.0, 0.0);
    risk->update_account(handle, &acct);

    sig = make_signal("AAPL", TC_SIDE_BUY, 101, 100.0, 95.0, 110.0, 1000000LL);
    const int BENCHMARK_COUNT = 1000000;

    auto start_time = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < BENCHMARK_COUNT; ++i) {
        sig.ts_ns += 1000000LL;
        risk->evaluate_signal(handle, &sig, &intent, &decision);
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    double duration_sec = std::chrono::duration<double>(end_time - start_time).count();
    double throughput_mps = (BENCHMARK_COUNT / duration_sec) / 1000000.0;

    std::cout << "[+] Evaluated " << BENCHMARK_COUNT << " trade signals in " << std::fixed << std::setprecision(4)
              << duration_sec << " seconds" << std::endl;
    std::cout << "[+] Hot-Path Evaluation Throughput: " << std::fixed << std::setprecision(2)
              << throughput_mps << " Million evals/sec" << std::endl << std::endl;

    // 8. Diagnostics & Shutdown
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
    std::cout << "       ALL TC_RISK TESTS PASSED SUCCESSFULLY!        " << std::endl;
    std::cout << "=====================================================" << std::endl;

    return 0;
}
