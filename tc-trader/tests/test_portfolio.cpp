/**
 * @file test_portfolio.cpp
 * @brief Dynamic loading, bookkeeping mathematics, and reconciliation verification for tc_portfolio.
 */

#include <iostream>
#include <iomanip>
#include <cassert>
#include <cstring>
#include <cmath>
#include <vector>

#include "tc/tc_abi.h"
#include "tc/tc_types.h"
#include "tc/tc_module.h"
#include "tc/portfolio/tc_portfolio.h"

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE DynLibHandle;
#define DYN_LOAD(path) LoadLibraryA(path)
#define DYN_GET(handle, sym) GetProcAddress(handle, sym)
#define DYN_CLOSE(handle) FreeLibrary(handle)
#define LIB_NAME "bin/tc_portfolio.dll"
#elif defined(__APPLE__)
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_portfolio.dylib"
#else
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_portfolio.so"
#endif

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << "     TC-TRADER LAYER 2: TC_PORTFOLIO PLUGIN TEST      " << std::endl;
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

    auto get_vtable_fn = reinterpret_cast<TcGetModuleVTableFn>(DYN_GET(lib, "tc_get_module_vtable"));
    assert(get_vtable_fn != nullptr);

    const TcModuleVTable* vtable = get_vtable_fn();
    assert(vtable != nullptr && vtable->abi_version == TC_ABI_VERSION);
    std::cout << "[+] Found module: " << vtable->name << " (ABI 0x" << std::hex << vtable->abi_version << std::dec << ")" << std::endl;

    // 2. Initialize Portfolio with $100,000 initial cash
    TcModuleConfig cfg;
    cfg.module_name = "portfolio_test";
    cfg.config_json = "{\"initial_cash\": 100000.0}";

    TcHandle handle = nullptr;
    TcStatus status = vtable->create(&cfg, &handle);
    assert(status == TC_OK && handle != nullptr);
    std::cout << "[+] Module created with $100,000.00 initial cash" << std::endl;

    auto* pf = static_cast<ITcPortfolio*>(vtable->get_interface(handle, "ITcPortfolio"));
    assert(pf != nullptr && pf->version == 1);
    vtable->start(handle);
    std::cout << "[+] ITcPortfolio interface discovered and activated" << std::endl << std::endl;

    // Verify initial account state
    TcAccountView acct{};
    pf->get_account(handle, &acct);
    assert(acct.total_cash == 100000LL * TC_PRICE_SCALE);
    assert(acct.net_liquidation == 100000LL * TC_PRICE_SCALE);
    assert(acct.daily_pnl == 0);
    std::cout << "[+] Initial Account Verification: Cash=$" << TC_PRICE_TO_DOUBLE(acct.total_cash)
              << ", Equity=$" << TC_PRICE_TO_DOUBLE(acct.net_liquidation) << std::endl;

    // -------------------------------------------------------------
    // Test 1: Opening Long & Scaling In (Weighted Average Price)
    // -------------------------------------------------------------
    std::cout << "\n--- Test 1: Opening Long & Scaling In ---" << std::endl;
    // Fill 1: BUY 100 AAPL @ $150.00, Comm = $1.00
    TcFill fill1{};
    fill1.struct_size = sizeof(TcFill);
    fill1.version = 1;
    fill1.side = 1; // BUY
    strncpy(fill1.symbol, "AAPL", sizeof(fill1.symbol) - 1);
    fill1.fill_qty = 100;
    fill1.fill_px = TC_DOUBLE_TO_PRICE(150.00);
    fill1.commission = 1.00;
    status = pf->apply_fill(handle, &fill1);
    assert(status == TC_OK);

    TcPosition pos{};
    pf->get_position(handle, "AAPL", &pos);
    assert(pos.net_qty == 100);
    assert(pos.avg_cost == TC_DOUBLE_TO_PRICE(150.00));
    std::cout << "Fill 1: BUY 100 @ $150.00 -> Qty: " << pos.net_qty << ", AvgCost: $" << TC_PRICE_TO_DOUBLE(pos.avg_cost) << std::endl;

    // Fill 2: BUY 100 AAPL @ $160.00, Comm = $1.00
    TcFill fill2 = fill1;
    fill2.fill_qty = 100;
    fill2.fill_px = TC_DOUBLE_TO_PRICE(160.00);
    status = pf->apply_fill(handle, &fill2);
    assert(status == TC_OK);

    pf->get_position(handle, "AAPL", &pos);
    assert(pos.net_qty == 200);
    // Expected avg cost = (100 * 150 + 100 * 160) / 200 = 155.00
    assert(pos.avg_cost == TC_DOUBLE_TO_PRICE(155.00));
    std::cout << "Fill 2: BUY 100 @ $160.00 -> Qty: " << pos.net_qty << ", AvgCost: $" << TC_PRICE_TO_DOUBLE(pos.avg_cost) << std::endl;
    std::cout << "[+] Weighted Average Calculation: PASSED" << std::endl;

    // -------------------------------------------------------------
    // Test 2: Mark-to-Market Unrealized PnL
    // -------------------------------------------------------------
    std::cout << "\n--- Test 2: Mark-to-Market Unrealized PnL ---" << std::endl;
    // Mark AAPL @ $165.00
    pf->update_mark(handle, "AAPL", TC_DOUBLE_TO_PRICE(165.00));
    pf->get_position(handle, "AAPL", &pos);
    pf->get_account(handle, &acct);

    // Unrealized = 200 * ($165 - $155) = +$2,000.00
    assert(pos.unrealized_pnl == TC_DOUBLE_TO_PRICE(2000.00));
    // Cash = 100,000 - 15,000 - 16,000 - 2 = $68,998.00
    assert(acct.total_cash == TC_DOUBLE_TO_PRICE(68998.00));
    // Net Liquidation = 68,998 + (200 * 165) = $101,998.00
    assert(acct.net_liquidation == TC_DOUBLE_TO_PRICE(101998.00));
    assert(acct.daily_pnl == TC_DOUBLE_TO_PRICE(1998.00));
    std::cout << "Mark: $165.00 -> Unrealized PnL: $" << TC_PRICE_TO_DOUBLE(pos.unrealized_pnl)
              << ", Equity: $" << TC_PRICE_TO_DOUBLE(acct.net_liquidation) << std::endl;
    std::cout << "[+] Mark-to-Market PnL: PASSED" << std::endl;

    // -------------------------------------------------------------
    // Test 3: Partial Long Reduction (Realized PnL)
    // -------------------------------------------------------------
    std::cout << "\n--- Test 3: Partial Long Reduction ---" << std::endl;
    // Sell 50 AAPL @ $170.00, Comm = $1.00
    TcFill fill3{};
    fill3.struct_size = sizeof(TcFill);
    fill3.version = 1;
    fill3.side = -1; // SELL
    strncpy(fill3.symbol, "AAPL", sizeof(fill3.symbol) - 1);
    fill3.fill_qty = 50;
    fill3.fill_px = TC_DOUBLE_TO_PRICE(170.00);
    fill3.commission = 1.00;
    status = pf->apply_fill(handle, &fill3);
    assert(status == TC_OK);

    pf->get_position(handle, "AAPL", &pos);
    assert(pos.net_qty == 150);
    assert(pos.avg_cost == TC_DOUBLE_TO_PRICE(155.00)); // Avg cost unchanged on reduction
    // Realized on 50 shares = 50 * (170 - 155) - 1.00 = 750 - 1 = $749.00
    assert(pos.realized_pnl == TC_DOUBLE_TO_PRICE(749.00));
    std::cout << "Fill 3: SELL 50 @ $170.00 -> Remaining: " << pos.net_qty
              << ", Realized PnL: $" << TC_PRICE_TO_DOUBLE(pos.realized_pnl) << std::endl;
    std::cout << "[+] Long Reduction & Realized PnL: PASSED" << std::endl;

    // -------------------------------------------------------------
    // Test 4: Position Reversal (Flipping from Long 150 to Short 50)
    // -------------------------------------------------------------
    std::cout << "\n--- Test 4: Position Reversal (Long -> Short) ---" << std::endl;
    // Sell 200 AAPL @ $175.00, Comm = $1.00
    TcFill fill4 = fill3;
    fill4.fill_qty = 200;
    fill4.fill_px = TC_DOUBLE_TO_PRICE(175.00);
    status = pf->apply_fill(handle, &fill4);
    assert(status == TC_OK);

    pf->get_position(handle, "AAPL", &pos);
    assert(pos.net_qty == -50); // Now short 50 shares
    assert(pos.avg_cost == TC_DOUBLE_TO_PRICE(175.00)); // Cost basis of short position is $175
    // Previous realized (749) + Closed 150 long * (175 - 155) - 1.00 = 749 + 3000 - 1 = $3,748.00
    assert(pos.realized_pnl == TC_DOUBLE_TO_PRICE(3748.00));
    std::cout << "Fill 4: SELL 200 @ $175.00 -> Flipped to SHORT " << pos.net_qty
              << ", Short AvgCost: $" << TC_PRICE_TO_DOUBLE(pos.avg_cost)
              << ", Total Realized PnL: $" << TC_PRICE_TO_DOUBLE(pos.realized_pnl) << std::endl;
    std::cout << "[+] Position Reversal: PASSED" << std::endl;

    // -------------------------------------------------------------
    // Test 5: Closing Short Position
    // -------------------------------------------------------------
    std::cout << "\n--- Test 5: Closing Short Position ---" << std::endl;
    // Buy 50 AAPL @ $165.00 to cover short, Comm = $1.00
    TcFill fill5{};
    fill5.struct_size = sizeof(TcFill);
    fill5.version = 1;
    fill5.side = 1; // BUY
    strncpy(fill5.symbol, "AAPL", sizeof(fill5.symbol) - 1);
    fill5.fill_qty = 50;
    fill5.fill_px = TC_DOUBLE_TO_PRICE(165.00);
    fill5.commission = 1.00;
    status = pf->apply_fill(handle, &fill5);
    assert(status == TC_OK);

    pf->get_position(handle, "AAPL", &pos);
    assert(pos.net_qty == 0); // Flat
    assert(pos.avg_cost == 0);
    // Previous realized (3748) + Closed 50 short * (175 - 165) - 1.00 = 3748 + 500 - 1 = $4,247.00
    assert(pos.realized_pnl == TC_DOUBLE_TO_PRICE(4247.00));
    assert(pos.unrealized_pnl == 0);

    pf->get_account(handle, &acct);
    // Ending Cash = Initial 100,000 + Gross PnL (4,250) - 5 Commissions ($5) = $104,245.00
    assert(acct.total_cash == TC_DOUBLE_TO_PRICE(104245.00));
    assert(acct.net_liquidation == TC_DOUBLE_TO_PRICE(104245.00));
    std::cout << "Covered Short -> NetQty: " << pos.net_qty << " (FLAT), Total Realized: $"
              << TC_PRICE_TO_DOUBLE(pos.realized_pnl)
              << ", Cash: $" << TC_PRICE_TO_DOUBLE(acct.total_cash) << std::endl;
    std::cout << "[+] Short Cover & Realized PnL: PASSED" << std::endl;

    // -------------------------------------------------------------
    // Test 6: Broker Reconciliation Engine
    // -------------------------------------------------------------
    std::cout << "\n--- Test 6: Broker Reconciliation Engine ---" << std::endl;
    // Open a fresh position: BUY 50 MSFT @ $400.00
    TcFill fill_msft{};
    fill_msft.struct_size = sizeof(TcFill);
    fill_msft.version = 1;
    fill_msft.side = 1;
    strncpy(fill_msft.symbol, "MSFT", sizeof(fill_msft.symbol) - 1);
    fill_msft.fill_qty = 50;
    fill_msft.fill_px = TC_DOUBLE_TO_PRICE(400.00);
    fill_msft.commission = 1.00;
    pf->apply_fill(handle, &fill_msft);

    // Case A: Clean Reconciliation (Broker matches MSFT 50 shares)
    TcBrokerPosition broker_clean[1];
    broker_clean[0].struct_size = sizeof(TcBrokerPosition);
    broker_clean[0].version = 1;
    strncpy(broker_clean[0].symbol, "MSFT", sizeof(broker_clean[0].symbol) - 1);
    broker_clean[0].broker_qty = 50;
    broker_clean[0].broker_avg_cost = TC_DOUBLE_TO_PRICE(400.00);

    TcReconcileReport report{};
    status = pf->reconcile(handle, broker_clean, 1, &report);
    assert(status == TC_OK);
    assert(report.is_reconciled == true);
    assert(report.num_discrepancies == 0);
    std::cout << "[+] Clean Broker Reconciliation: PASSED (0 discrepancies)" << std::endl;

    // Case B: Injected Discrepancy (Broker reports 40 shares instead of 50)
    TcBrokerPosition broker_mismatch[1];
    broker_mismatch[0] = broker_clean[0];
    broker_mismatch[0].broker_qty = 40; // 10 shares missing at broker!

    status = pf->reconcile(handle, broker_mismatch, 1, &report);
    assert(status == TC_OK);
    assert(report.is_reconciled == false);
    assert(report.num_discrepancies == 1);
    assert(report.discrepancies[0].qty_diff == 10);
    std::cout << "[+] Discrepancy Detection: PASSED (Detected "
              << report.discrepancies[0].qty_diff << " shares difference on "
              << report.discrepancies[0].symbol << ")" << std::endl;

    // Cleanup
    vtable->stop(handle);
    vtable->destroy(handle);
#if !defined(_WIN32)
    DYN_CLOSE(lib);
#endif

    std::cout << "\n=====================================================" << std::endl;
    std::cout << "    TC_PORTFOLIO PLUGIN TESTS PASSED COMPLETELY!     " << std::endl;
    std::cout << "=====================================================" << std::endl;

    return 0;
}
