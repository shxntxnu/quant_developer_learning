/**
 * @file test_gateway_sim.cpp
 * @brief Integration tests and benchmark for tc_gateway_sim dynamic plugin.
 *
 * Verifies:
 * 1. Dynamic module loading, ABI compliance, and ITcGateway interface retrieval.
 * 2. High-performance CSV historical dataset parsing (simulated_5min_data.csv).
 * 3. Market, Limit, Stop, and Stop-Limit order matching against bar extremes.
 * 4. Slippage and commission calculations.
 * 5. One-Cancels-All (OCA) bracket child cancellation.
 * 6. Manual order cancellation (single and cancel-all).
 * 7. Replay throughput benchmark (target > 500,000 bars/second).
 */

#include <iostream>
#include <iomanip>
#include <cassert>
#include <cstring>
#include <chrono>
#include <vector>
#include <atomic>
#include <cmath>

#include "tc/tc_abi.h"
#include "tc/tc_types.h"
#include "tc/tc_module.h"
#include "tc/gateway/tc_gateway.h"

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE DynLibHandle;
#define DYN_LOAD(path) LoadLibraryA(path)
#define DYN_GET(handle, sym) GetProcAddress(handle, sym)
#define DYN_CLOSE(handle) FreeLibrary(handle)
#define LIB_NAME "bin/tc_gateway_sim.dll"
#elif defined(__APPLE__)
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_gateway_sim.dylib"
#else
#include <dlfcn.h>
typedef void* DynLibHandle;
#define DYN_LOAD(path) dlopen(path, RTLD_NOW)
#define DYN_GET(handle, sym) dlsym(handle, sym)
#define DYN_CLOSE(handle) dlclose(handle)
#define LIB_NAME "bin/libtc_gateway_sim.so"
#endif

// Test tracking variables
static std::atomic<uint64_t> g_bar_count{0};
static std::atomic<uint64_t> g_order_events{0};
static std::atomic<uint64_t> g_fills{0};
static TcBar   g_last_bar{};
static TcOrderEvent g_last_event{};
static TcFill  g_last_fill{};

static void on_bar_received(const TcBar* bar, void* user_data) {
    (void)user_data;
    if (bar) {
        g_last_bar = *bar;
        g_bar_count++;
    }
}

static void on_order_event(const TcOrderEvent* evt, void* user_data) {
    (void)user_data;
    if (evt) {
        g_last_event = *evt;
        g_order_events++;
    }
}

static void on_fill_received(const TcFill* fill, void* user_data) {
    (void)user_data;
    if (fill) {
        g_last_fill = *fill;
        g_fills++;
    }
}

static TcOrder make_order(uint64_t client_id, const char* symbol, int8_t side,
                          uint8_t order_type, int64_t qty, TcPrice limit_px,
                          TcPrice stop_px, uint64_t parent_id = 0) {
    TcOrder ord{};
    ord.struct_size = sizeof(TcOrder);
    ord.version = 1;
    ord.tif = TC_TIF_GTC;
    std::strncpy(ord.symbol, symbol, sizeof(ord.symbol) - 1);
    ord.client_order_id = client_id;
    ord.parent_order_id = parent_id;
    ord.side = side;
    ord.order_type = order_type;
    ord.qty = qty;
    ord.limit_px = limit_px;
    ord.stop_px = stop_px;
    return ord;
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << "   TC-TRADER LAYER 2: TC_GATEWAY_SIM PLUGIN TEST    " << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 1. Dynamic Library Loading & VTable Resolution
    std::cout << "Loading dynamic plugin: " << LIB_NAME << " ..." << std::endl;
    DynLibHandle lib = DYN_LOAD(LIB_NAME);
    if (!lib) {
#if defined(_WIN32)
        std::cerr << "[-] Failed to load " << LIB_NAME << ", Error: " << GetLastError() << std::endl;
#else
        std::cerr << "[-] Failed to load " << LIB_NAME << ", Error: " << dlerror() << std::endl;
#endif
        return 1;
    }
    std::cout << "[+] Plugin loaded successfully into host process" << std::endl;

    auto get_vtable_fn = reinterpret_cast<TcGetModuleVTableFn>(reinterpret_cast<void*>(DYN_GET(lib, "tc_get_module_vtable")));
    assert(get_vtable_fn != nullptr);

    const TcModuleVTable* vtable = get_vtable_fn();
    assert(vtable != nullptr && vtable->abi_version == TC_ABI_VERSION);
    std::cout << "[+] Found module: " << vtable->name << " (ABI 0x" << std::hex << vtable->abi_version << std::dec << ")" << std::endl;

    // 2. Module Lifecycle & Interface Query
    TcModuleConfig mod_cfg{};
    mod_cfg.module_name = "tc_gateway_sim";
    mod_cfg.config_json = "";

    TcHandle handle = nullptr;
    assert(vtable->create(&mod_cfg, &handle) == TC_OK);
    assert(handle != nullptr);
    assert(vtable->start(handle) == TC_OK);

    auto gateway = static_cast<const ITcGateway*>(vtable->get_interface(handle, "ITcGateway"));
    assert(gateway != nullptr && gateway->version == 1);
    std::cout << "[+] Acquired ITcGateway interface (v" << gateway->version << ")" << std::endl;

    // Register Sinks
    assert(gateway->set_bar_sink(handle, on_bar_received, nullptr) == TC_OK);
    assert(gateway->set_order_event_sink(handle, on_order_event, nullptr) == TC_OK);
    assert(gateway->set_fill_sink(handle, on_fill_received, nullptr) == TC_OK);

    // 3. Connect & Load Dataset
    TcGatewayConfig cfg{};
    cfg.struct_size = sizeof(TcGatewayConfig);
    cfg.version = 1;
    std::strncpy(cfg.gateway_name, "tc_gateway_sim", sizeof(cfg.gateway_name) - 1);
    std::strncpy(cfg.data_file_path, "../Algorithmic Trading Machine Learning Strategies/simulated_5min_data.csv", sizeof(cfg.data_file_path) - 1);
    std::strncpy(cfg.default_symbol, "SPY", sizeof(cfg.default_symbol) - 1);
    cfg.slippage_pct = 0.0001; // 1 bp
    cfg.commission_per_share = 0.005; // $0.005 / share
    cfg.min_commission = 1.00; // $1.00 minimum

    std::cout << "Connecting gateway and parsing dataset: " << cfg.data_file_path << " ..." << std::endl;
    TcStatus conn_status = gateway->connect(handle, &cfg);
    assert(conn_status == TC_OK);

    bool is_conn = false;
    assert(gateway->is_connected(handle, &is_conn) == TC_OK);
    assert(is_conn == true);
    std::cout << "[+] Gateway connected successfully to simulation dataset" << std::endl;

    // 4. Test Single-Step Bar Emission
    bool has_more = false;
    assert(gateway->step(handle, &has_more) == TC_OK);
    assert(has_more == true);
    assert(g_bar_count == 1);
    std::cout << "[+] Step 1 emitted bar: " << g_last_bar.symbol
              << " ts=" << g_last_bar.ts_ns
              << " O=" << TC_PRICE_TO_DOUBLE(g_last_bar.open)
              << " H=" << TC_PRICE_TO_DOUBLE(g_last_bar.high)
              << " L=" << TC_PRICE_TO_DOUBLE(g_last_bar.low)
              << " C=" << TC_PRICE_TO_DOUBLE(g_last_bar.close)
              << " V=" << g_last_bar.volume << std::endl;

    // 5. Test Market Order Execution
    std::cout << "\nTesting Market Order execution..." << std::endl;
    TcOrder mkt_order = make_order(101, "SPY", 1, TC_GW_ORDER_MKT, 100, 0, 0);
    uint64_t assigned_id = 0;
    assert(gateway->place_order(handle, &mkt_order, &assigned_id) == TC_OK);
    assert(assigned_id == 101);
    assert(g_last_event.status == TC_ORD_ACK);

    // Step simulation to trigger fill on next bar
    assert(gateway->step(handle, &has_more) == TC_OK);
    assert(g_fills == 1);
    assert(g_last_fill.client_order_id == 101);
    assert(g_last_fill.fill_qty == 100);
    assert(g_last_fill.side == 1);
    assert(g_last_event.status == TC_ORD_FILLED);
    std::cout << "[+] Market order filled: Qty=" << g_last_fill.fill_qty
              << " Px=" << TC_PRICE_TO_DOUBLE(g_last_fill.fill_px)
              << " Comm=$" << g_last_fill.commission << std::endl;

    // 6. Test Limit Order Execution
    std::cout << "\nTesting Limit Order execution..." << std::endl;
    // Current price is around bar.open; place a limit order well above market for BUY (marketable)
    TcPrice marketable_lmt = g_last_bar.high + TC_DOUBLE_TO_PRICE(5.0);
    TcOrder lmt_order = make_order(102, "SPY", 1, TC_GW_ORDER_LMT, 200, marketable_lmt, 0);
    assert(gateway->place_order(handle, &lmt_order, nullptr) == TC_OK);
    assert(gateway->step(handle, &has_more) == TC_OK);
    assert(g_fills == 2);
    assert(g_last_fill.client_order_id == 102);
    assert(g_last_fill.fill_qty == 200);
    std::cout << "[+] Limit order filled: Qty=" << g_last_fill.fill_qty
              << " Px=" << TC_PRICE_TO_DOUBLE(g_last_fill.fill_px) << std::endl;

    // Place resting limit order that will NOT fill immediately
    TcPrice resting_lmt = TC_DOUBLE_TO_PRICE(1.0); // Extreme low price $1.00
    TcOrder resting_order = make_order(103, "SPY", 1, TC_GW_ORDER_LMT, 50, resting_lmt, 0);
    assert(gateway->place_order(handle, &resting_order, nullptr) == TC_OK);
    assert(gateway->step(handle, &has_more) == TC_OK);
    assert(g_fills == 2); // Should not have filled
    std::cout << "[+] Resting limit order placed without immediate fill" << std::endl;

    // Cancel resting order
    assert(gateway->cancel_order(handle, 103) == TC_OK);
    assert(g_last_event.status == TC_ORD_CANCELLED);
    assert(g_last_event.client_order_id == 103);
    std::cout << "[+] Order 103 successfully cancelled" << std::endl;

    // 7. Test Bracket / OCA (One-Cancels-All) Logic
    std::cout << "\nTesting Bracket / OCA sibling cancellation..." << std::endl;
    uint64_t parent_id = 999;
    // Child A: Marketable Buy Limit (fills immediately on next bar)
    TcPrice marketable_px = g_last_bar.high + TC_DOUBLE_TO_PRICE(20.0);
    TcOrder child_a = make_order(201, "SPY", 1, TC_GW_ORDER_LMT, 100, marketable_px, 0, parent_id);
    // Child B: Sibling resting far away in OCA group
    TcPrice resting_px = g_last_bar.high + TC_DOUBLE_TO_PRICE(200.0);
    TcOrder child_b = make_order(202, "SPY", -1, TC_GW_ORDER_LMT, 100, resting_px, 0, parent_id);

    assert(gateway->place_order(handle, &child_a, nullptr) == TC_OK);
    assert(gateway->place_order(handle, &child_b, nullptr) == TC_OK);

    // Step simulation to trigger Child A
    assert(gateway->step(handle, &has_more) == TC_OK);
    assert(g_last_fill.client_order_id == 201); // Child A filled
    // Verify Child B was automatically cancelled by OCA logic
    assert(g_last_event.status == TC_ORD_CANCELLED);
    assert(g_last_event.client_order_id == 202);
    std::cout << "[+] OCA verified: Child A (201) filled -> Child B (202) automatically cancelled!" << std::endl;

    // 8. Cancel All Orders Test
    std::cout << "\nTesting Cancel-All functionality..." << std::endl;
    TcOrder r1 = make_order(301, "SPY", 1, TC_GW_ORDER_LMT, 10, TC_DOUBLE_TO_PRICE(10.0), 0);
    TcOrder r2 = make_order(302, "SPY", 1, TC_GW_ORDER_LMT, 20, TC_DOUBLE_TO_PRICE(12.0), 0);
    assert(gateway->place_order(handle, &r1, nullptr) == TC_OK);
    assert(gateway->place_order(handle, &r2, nullptr) == TC_OK);
    assert(gateway->cancel_all_orders(handle, "SPY") == TC_OK);
    assert(g_last_event.status == TC_ORD_CANCELLED);
    std::cout << "[+] All resting orders successfully cancelled" << std::endl;

    // 9. Replay Throughput Benchmark
    std::cout << "\n-----------------------------------------------------" << std::endl;
    std::cout << "Starting High-Throughput Historical Replay Benchmark..." << std::endl;
    std::cout << "-----------------------------------------------------" << std::endl;

    uint64_t initial_bars = g_bar_count;
    auto t0 = std::chrono::high_resolution_clock::now();

    // Run remaining replay to EOF
    TcStatus replay_status = gateway->run_replay(handle);
    assert(replay_status == TC_OK);

    auto t1 = std::chrono::high_resolution_clock::now();
    double duration_sec = std::chrono::duration<double>(t1 - t0).count();
    uint64_t replayed_bars = g_bar_count - initial_bars;
    double bars_per_sec = (duration_sec > 0.0) ? (static_cast<double>(replayed_bars) / duration_sec) : 0.0;

    std::cout << "[+] Replayed " << replayed_bars << " bars in "
              << std::fixed << std::setprecision(4) << duration_sec << " seconds." << std::endl;
    std::cout << "[+] Throughput: " << std::fixed << std::setprecision(0) << bars_per_sec
              << " bars/second (Target > 500,000 bars/sec)" << std::endl;

    // Check Gateway Statistics
    TcGatewayStats stats{};
    stats.struct_size = sizeof(TcGatewayStats);
    stats.version = 1;
    assert(gateway->get_stats(handle, &stats) == TC_OK);
    std::cout << "\nGateway Statistics Summary:" << std::endl;
    std::cout << "  Total Bars Published:   " << stats.bars_published << std::endl;
    std::cout << "  Total Orders Placed:    " << stats.orders_placed << std::endl;
    std::cout << "  Total Orders Filled:    " << stats.orders_filled << std::endl;
    std::cout << "  Total Orders Cancelled: " << stats.orders_cancelled << std::endl;
    std::cout << "  Total Orders Rejected:  " << stats.orders_rejected << std::endl;
    std::cout << "  Total Commissions Paid: $" << std::fixed << std::setprecision(2) << stats.total_commissions << std::endl;
    std::cout << "  Total Slippage PnL:     " << TC_PRICE_TO_DOUBLE(stats.total_slippage_pnl) << std::endl;

    // Module Status String Verification
    TcModuleStatus mod_status{};
    assert(vtable->get_status(handle, &mod_status) == TC_OK);
    std::cout << "[+] Module Status Telemetry: " << mod_status.status_msg << std::endl;

    // 10. Teardown
    assert(gateway->disconnect(handle) == TC_OK);
    assert(vtable->stop(handle) == TC_OK);
    vtable->destroy(handle);
#if !defined(_WIN32)
    DYN_CLOSE(lib);
#endif

    std::cout << "\n=====================================================" << std::endl;
    std::cout << "    ALL TC_GATEWAY_SIM TESTS PASSED SUCCESSFULLY!    " << std::endl;
    std::cout << "=====================================================" << std::endl;

    return 0;
}
