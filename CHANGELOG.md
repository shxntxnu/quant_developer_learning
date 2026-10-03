# Project Changelog & Architecture Evolution Tracker

All notable changes, architectural decisions, and milestones for the **Rule-Based Production Trading System (`tc-trader`)** and the **Quant Developer Learning Framework** are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), adhering to Semantic Versioning and the specifications in [`trading_system_workflow.drawio`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/trading_system_workflow.drawio).

---

## [Unreleased] - Phase 2: Core Plugin DLL Modules

### Added - 2026-10-02: `tc_journal` (Logging & Asynchronous Audit Trail Module)
* **Shared ABI Header:** [`include/tc/journal/tc_journal.h`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/include/tc/journal/tc_journal.h)
  - Defined `ITcJournal` C ABI vtable containing `log()`, `snapshot()`, `flush()`, and `set_sink()`.
  - Defined `TcJournalEvent` POD structure (64-bit aligned, zero heap pointers) with level, event code, nanosecond timestamp, component tag, and fixed message buffer.
  - Defined `TcJournalSinkType` (`TC_SINK_FILE_TEXT`, `TC_SINK_FILE_BINARY`).
* **Asynchronous Writer (Thread T4):** [`modules/journal/async_writer.hpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/modules/journal/async_writer.hpp) & `.cpp`
  - Integrated `TcQueueMPSC` lock-free queue allowing any thread (T0, T1, T2, T3, T5) to enqueue audit events without locking or blocking the execution hot path.
  - Dedicated background thread (Thread T4) polling and batch-draining the queue to disk.
* **Rotating File Sink:** [`modules/journal/rotating_file_sink.hpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/modules/journal/rotating_file_sink.hpp) & `.cpp`
  - Size-based automatic file rotation (configurable threshold, default 10MB) and calendar day rotation.
  - High-performance buffered I/O with explicit flush operations.
* **Plugin DLL Implementation:** [`modules/journal/journal_module.cpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/modules/journal/journal_module.cpp)
  - Exports `tc_get_module_vtable()` returning standard `TcModuleVTable`.
  - Implements lifecycle (`create`, `start`, `stop`, `destroy`), status reporting, and interface discovery (`get_interface("ITcJournal")`).
* **Integration & Dynamic Loading Test:** [`tests/test_journal.cpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/tests/test_journal.cpp)
  - Validates dynamic loading via `dlopen`/`dlsym` across shared library boundary.
  - Stress-tests concurrent enqueuing from 4 producer threads logging $100,000$ events total.
  - Verified sustained write throughput of **0.67 - 0.85 Million logs/sec** with zero dropped events.
  - Verified size-based file rotation threshold ($10\text{ MB}$) generating multi-volume log output (`0000.log`, `0001.log`) and binary state snapshot (`snapshot_risk_checkpoint.bin`).

### Added - 2026-10-02: `tc_portfolio` (Real-Time Accounting & Broker Reconciliation Module)
* **Shared ABI Header:** [`include/tc/portfolio/tc_portfolio.h`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/include/tc/portfolio/tc_portfolio.h)
  - Defined `ITcPortfolio` C ABI vtable containing `apply_fill()`, `update_mark()`, `get_position()`, `get_all_positions()`, `get_account()`, and `reconcile()`.
  - Defined `TcBrokerPosition`, `TcDiscrepancy`, and `TcReconcileReport` POD structures.
* **Position & Accounting Engine:** [`modules/portfolio/position_book.hpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/modules/portfolio/position_book.hpp) & `.cpp`
  - **Weighted Average Cost Basis:** Correctly computes blended entry price on scaling into long or short positions.
  - **Realized PnL Calculation:** Accurately accounts for profit/loss on partial/full position closures and position flips.
  - **Mark-to-Market Engine:** Real-time unrealized PnL computation on price ticks.
  - **Cash & Equity Ledger:** Tracks settled cash, gross equity, high-water mark, and drawdown percentage.
  - **Automated Reconciler:** Compares internal positions against broker snapshots, detects quantity and cost discrepancies, and flags orphan positions.
* **Plugin DLL Implementation:** [`modules/portfolio/portfolio_module.cpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/modules/portfolio/portfolio_module.cpp)
  - Exports `tc_get_module_vtable()` and resolves `ITcPortfolio`.
  - Configurable starting capital via JSON (`{"initial_cash": 100000.0}`).
* **Integration & Dynamic Loading Test:** [`tests/test_portfolio.cpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/tests/test_portfolio.cpp)
  - Validates dynamic loading via `dlopen`/`dlsym`.
  - Verified long entry, scaling in, mark-to-market PnL, partial reduction, position reversal (long to short), and short covering.
  - Verified clean broker reconciliation (0 discrepancies) and intentional discrepancy detection.

### Added - 2026-10-02: `tc_indicators` (O(1) Incremental Technical Indicators Module)
* **Shared ABI Header:** [`include/tc/indicators/tc_indicators.h`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/include/tc/indicators/tc_indicators.h)
  - Defined `ITcIndicators` C ABI vtable containing `update_bar()`, `get_snapshot()`, `reset()`, and `warmup_bars_required()`.
  - Defined `TcIndicatorSpec` configuration struct for custom periods.
* **Header-Only Incremental Math Core:** [`include/tc/indicators/tc_indicators_core.hpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/include/tc/indicators/tc_indicators_core.hpp)
  - Implemented circular ring buffers for strictly zero heap allocations on the hot path.
  - **SMA:** $O(1)$ fast (20) and slow (50) moving averages via running sum buffer.
  - **EMA:** Exponential smoothing ($\alpha = 2 / (N+1)$) seeded with initial period average.
  - **RSI (14):** Wilder's exponential smoothing of upward and downward price deltas with boundary protections.
  - **MACD (12, 26, 9):** Fast EMA, slow EMA, MACD line, 9-EMA signal line, and histogram.
  - **ATR (14):** Average True Range computed using true range expansion and Wilder's smoothing.
  - **Bollinger Bands (20, 2.0):** Incremental running sum of squares for real-time variance and standard deviation.
  - **ADX (14):** Directional movement indices ($+DM$, $-DM$, $+DI$, $-DI$, $DX$) and smoothed ADX for trend strength.
  - **Warmup State Engine:** Incremental bitmask activation (`TC_IND_MASK_*`) tracking readiness as bars stream in.
* **Plugin DLL Implementation:** [`modules/indicators/indicators_module.cpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/modules/indicators/indicators_module.cpp)
  - Exports `tc_get_module_vtable()` and resolves `ITcIndicators`.
* **Integration & Benchmark Test:** [`tests/test_indicators.cpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/tests/test_indicators.cpp)
  - Validates dynamic loading via `dlopen`/`dlsym`.
  - Verified warmup progression: Bar 15 (RSI/ATR), Bar 20 (SMA20/EMA/BB), Bar 50 (SMA50/MACD/ADX - Full Warmup).
  - Verified mathematical bounds ($0 \le \text{RSI}, \text{ADX} \le 100$, $\text{BB}_{\text{lower}} \le \text{BB}_{\text{mid}} \le \text{BB}_{\text{upper}}$).
  - Achieved sustained streaming throughput of **14.41 Million bars/sec** on 1,000,000 synthetic bars.
### Added - 2026-10-03: `tc_strategy` (Rule-Based Strategy Engine & Market Regime Classifier)
* **Shared ABI Header:** [`include/tc/strategy/tc_strategy.h`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/include/tc/strategy/tc_strategy.h)
  - Defined `ITcStrategy` C ABI vtable containing `configure()`, `get_params()`, `set_params()`, `classify_regime()`, `on_snapshot()`, and `reset()`.
  - Defined `TcMarketRegime` enum (`TC_REGIME_UNKNOWN`, `TC_REGIME_RANGING`, `TC_REGIME_TRENDING_BULL`, `TC_REGIME_TRENDING_BEAR`, `TC_REGIME_HIGH_VOLATILITY`).
  - Defined `TcRuleId` enum (`TC_RULE_ENTRY_TREND_BULL`, `TC_RULE_ENTRY_TREND_BEAR`, `TC_RULE_ENTRY_MEAN_REV_LONG`, `TC_RULE_ENTRY_MEAN_REV_SHORT`, `TC_RULE_EXIT_TRAILING_STOP`, `TC_RULE_EXIT_TAKE_PROFIT`, `TC_RULE_EXIT_MA_REVERSAL`, `TC_RULE_EXIT_MAX_HOLDING`).
  - Defined `TcStrategyParams` configuration struct for dynamic rules and thresholds.
* **High-Performance Pure Function Engine:** [`modules/strategy/strategy_engine.hpp`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/modules/strategy/strategy_engine.hpp)
  - Evaluates `(TcIndicatorSnapshot, TcPositionView) -> TcSignal[]` synchronously on Thread T2 with strictly zero dynamic heap allocations.
  - Zero I/O, zero network hops, and zero blocking on the hot path.
  - Pre-allocated zero-allocation symbol state table tracking active positions, entry timestamps, entry prices, and high/low extremes.
  - **Market Regime Classifier:** Categorizes market dynamics via ADX ($> 25$) and Bollinger bandwidth expansions.
  - **Exit Rules Evaluator:** Evaluates open positions for ATR trailing stops, target profit takes, moving average reversals, and trade timeout stops.
  - **Entry Rules Evaluator:** Evaluates trend-following golden/death crosses with RSI confirmation and mean-reversion boundary rejections with oversold/overbought RSI.
* **Configuration:** [`config/rules.json`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/config/rules.json)
  - Configurable regime and rule parameters with JSON parser and hot-reload support.
* **Plugin DLL Implementation:** [`modules/strategy/strategy_module.cpp`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/modules/strategy/strategy_module.cpp)
  - Exports `tc_get_module_vtable()` returning standard `TcModuleVTable`.
  - Resolves `ITcStrategy` interface table.
* **Integration & Benchmark Test:** [`tests/test_strategy.cpp`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/tests/test_strategy.cpp)
  - Validates dynamic loading via `LoadLibrary`/`GetProcAddress`.
  - Verified all regime states, bull/bear trend entries, oversold/overbought mean reversion entries, trailing stops, profit targets, MA reversal exits, and timeout exits.
### Added - 2026-10-03: `tc_risk` (Position Sizing & Capital Protection Engine)
* **Shared ABI Header:** [`include/tc/risk/tc_risk.h`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/include/tc/risk/tc_risk.h)
  - Defined `ITcRisk` C ABI vtable containing `configure()`, `get_params()`, `set_params()`, `evaluate_signal()`, `set_symbol_kill_switch()`, `get_symbol_kill_switch()`, `set_global_kill_switch()`, `get_global_kill_switch()`, and `reset()`.
  - Defined `TcRiskDecision` POD struct returning `decision_code`, `approved_qty`, `reason`, `suggested_limit_price`, `suggested_stop_loss`, and `suggested_take_profit`.
  - Defined `TcRiskDecisionCode` enum (`TC_RISK_APPROVED`, `TC_RISK_CLAMPED`, `TC_RISK_REJECTED_MAX_POSITION`, `TC_RISK_REJECTED_MAX_ORDER_VALUE`, `TC_RISK_REJECTED_BUYING_POWER`, `TC_RISK_REJECTED_LEVERAGE`, `TC_RISK_REJECTED_PRICE_COLLAR`, `TC_RISK_REJECTED_MAX_DRAWDOWN`, `TC_RISK_REJECTED_SYMBOL_HALTED`, `TC_RISK_REJECTED_GLOBAL_KILL_SWITCH`, `TC_RISK_REJECTED_INVALID_SIGNAL`).
  - Defined `TcRiskParams` configuration struct for fixed-fractional sizing, leverage limits, drawdown limits, price collar percentages, and max order/position thresholds.
* **High-Performance Pure Function Engine:** [`modules/risk/risk_engine.hpp`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/modules/risk/risk_engine.hpp)
  - Evaluates `(TcSignal, TcPortfolioSnapshot) -> TcRiskDecision` synchronously on Thread T2 with strictly zero dynamic heap allocations.
  - **Fixed-Fractional Position Sizing:** Dynamically sizes order quantities based on risk per trade ($\text{Qty} = \lfloor \frac{\text{Equity} \times \text{RiskPct}}{\text{StopDistance}} \rfloor$).
  - **Pre-Trade Risk Checks:**
    1. Global kill switch validation.
    2. Per-symbol halt / kill switch validation.
    3. Maximum portfolio drawdown breach detection with auto-kill activation.
    4. Price collar check ($\le 3\%$ deviation from current market price).
    5. Reduce-only exit pass-through (bypasses new-risk limits to guarantee exit liquidity).
    6. Max single order value and max symbol position clamping.
    7. Buying power and gross portfolio leverage limit ($2.0\times$) enforcement.
  - Thread-safe atomic kill switches for individual symbols and global trading halt.
* **Configuration:** [`config/risk.json`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/config/risk.json)
  - Configurable risk thresholds (1% risk fraction, 2.0x max leverage, 5% max drawdown, 3% price collar).
* **Plugin DLL Implementation:** [`modules/risk/risk_module.cpp`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/modules/risk/risk_module.cpp)
  - Exports `tc_get_module_vtable()` returning standard `TcModuleVTable`.
  - Resolves `ITcRisk` interface table.
* **Integration & Benchmark Test:** [`tests/test_risk.cpp`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/tests/test_risk.cpp)
  - Validates dynamic loading via `LoadLibrary`/`GetProcAddress`.
  - Verified exact mathematical sizing (e.g. 200 shares long, 400 shares short on $100k capital).
  - Verified pre-trade check rejections: max position clamping, gross leverage rejection, price collar rejection, symbol kill switch rejection, global kill switch rejection, and drawdown breach auto-kill.
  - Verified reduce-only exits always approved.
  - Achieved sustained evaluation throughput of **7.50 Million evals/sec** on 1,000,000 signals.

### Added - 2026-10-03: `tc_marketdata` (Tick Normalization, Outlier Filtering & Deterministic Bar Aggregation Module)
* **Shared ABI Header:** [`include/tc/marketdata/tc_marketdata.h`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/include/tc/marketdata/tc_marketdata.h)
  - Defined `ITcMarketData` C ABI vtable containing `configure()`, `get_config()`, `set_config()`, `process_raw_tick()`, `process_tick()`, `flush_bar()`, `set_bar_sink()`, `get_feed_status()`, `get_stats()`, `reset_symbol()`, and `reset()`.
  - Defined `TcRawTick` POD struct for external exchange/broker feed ingestion.
  - Defined `TcMarketDataConfig` and `TcMarketDataStats` structs for runtime configuration and operational drop telemetry.
  - Defined `TcFeedStatus` enum (`TC_FEED_UNKNOWN`, `TC_FEED_OK`, `TC_FEED_DEGRADED`, `TC_FEED_DISCONNECTED`).
* **High-Performance Aggregation & Normalization Engine:** [`modules/marketdata/marketdata_engine.hpp`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/modules/marketdata/marketdata_engine.hpp)
  - Strictly zero heap allocations on the hot path via pre-allocated fixed-capacity `SymbolSlot` table.
  - **Tick Normalization:** Converts raw floating-point prices and sizes into deterministic 64-bit integer fixed-point `TcTick` structs.
  - **Integrity & Anomaly Filtering:** Real-time filtering of stale/inverted timestamps, non-positive prices, crossed-book conditions (`bid > ask`), and price outlier spikes ($> 10\%$ deviation from recent valid price).
  - **Deterministic Time-Windowed Bar Aggregation:** Aggregates trade ticks into epoch-aligned OHLCV bars (e.g. 1-min, 5-min intervals).
  - **128-Bit Overflow-Safe VWAP Accumulation:** Computes running bar VWAP using `__int128` integer turnover accumulation.
  - **Feed Health Monitoring:** Evaluates inter-tick arrival latency against configurable heartbeat timeouts to flag degraded or stale market feeds.
  - **Synchronous & Asynchronous Bar Emission:** Dual-path delivery via direct `bar_out` pointers and registered `TcBarCallback` sinks.
* **Configuration:** [`config/marketdata.json`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/config/marketdata.json)
  - Configurable bar interval (default 300s), price deviation threshold (10%), stale timeout (5.0s), and filter flags.
* **Plugin DLL Implementation:** [`modules/marketdata/marketdata_module.cpp`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/modules/marketdata/marketdata_module.cpp)
  - Exports `tc_get_module_vtable()` returning standard `TcModuleVTable`.
  - Resolves `ITcMarketData` interface table.
* **Integration & Benchmark Test:** [`tests/test_marketdata.cpp`](file:///d:/Documents/_MyStuff/Projects/quant_developer_learning/tc-trader/tests/test_marketdata.cpp)
  - Validates dynamic loading via `LoadLibrary`/`GetProcAddress`.
  - Verified tick normalization, timestamp filtering, outlier spike rejection, crossed-book drops, bar OHLCV and VWAP math, manual bar flushing, and real-time feed health status transitions.
  - Achieved sustained hot-path throughput of **31.58 Million ticks/sec** on 2,000,000 raw ticks.

---


## [1.0.0] - Phase 1: Shared C ABI & Concurrency Foundation - 2026-10-02

### Added
* **C ABI Header Core:** [`include/tc/tc_abi.h`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/include/tc/tc_abi.h)
  - Defined `TC_ABI_VERSION (0x00010000)` major/minor handshake constant.
  - Cross-platform dynamic library export/import macros (`TC_API`).
  - Fixed-point price type `typedef int64_t TcPrice` scaled by $10^4$ ($0.0001$ tick precision) to eliminate floating-point non-determinism.
  - Opaque handle `typedef struct TcHandle_* TcHandle` for memory encapsulation.
  - Standard error codes (`TcStatus`).
* **Versioned POD Struct Suite:** [`include/tc/tc_types.h`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/include/tc/tc_types.h)
  - Implemented 16-byte fixed-string symbol field (`char symbol[TC_SYMBOL_MAX]`).
  - Implemented `TcTick`, `TcBar`, `TcIndicatorSnapshot`, `TcSignal`, `TcOrderIntent`, `TcOrderEvent`, `TcFill`, `TcPosition`, and `TcAccountView`.
  - Enforced `struct_size` and `version` header discipline on all structures for forward and backward binary compatibility.
  - Enforced trivial copyability (`std::is_trivially_copyable`) for direct zero-copy ring buffer passing.
* **Module VTable Contract:** [`include/tc/tc_module.h`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/include/tc/tc_module.h)
  - Standardized `TcModuleVTable` struct exported as the sole symbol from all plugin DLLs.
  - Implemented interface discovery pattern (`get_interface`) replacing individual exported function lookups.
* **Lock-Free Ring Buffers:** [`include/tc/tc_ringbuffer.hpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/include/tc/tc_ringbuffer.hpp)
  - `TcRingBufferSPSC<T, Capacity>`: Single-Producer Single-Consumer lock-free ring buffer with `alignas(64)` cache-line alignment on head and tail pointers. Zero heap allocation on hot path.
  - `TcQueueMPSC<T, Capacity>`: Bounded Multi-Producer Single-Consumer queue for non-blocking logging.
* **Clocks & Diagnostics:**
  - [`include/tc/tc_time.hpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/include/tc/tc_time.hpp): High-resolution monotonic and UTC nanosecond clocks with ISO-8601 formatting.
  - [`include/tc/tc_log.h`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/include/tc/tc_log.h): Severity levels and callback sink signatures.
* **Build System & Test Suite:**
  - Cross-platform [`Makefile`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/Makefile) and [`CMakeLists.txt`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/CMakeLists.txt).
  - [`tests/test_types.cpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/tests/test_types.cpp): Verified memory layout, struct padding, string handling, and fixed-point conversions.
  - [`tests/test_ringbuffer.cpp`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/tc-trader/tests/test_ringbuffer.cpp): Benchmark achieved **24.3 - 34.1 Million msgs/sec** SPSC throughput and **5.7 - 6.0 Million msgs/sec** MPSC throughput with zero dropped items.

---

## [0.1.0] - Phase 0: Analytical Baseline & Machine Learning Research - Initial

### Added
* **C++ Standalone Mathematical DLLs:** [`Algorithmic Trading Machine Learning Strategies/functions/`](file:///Users/yash/Documents/Projects/others/quant_developer_learning/Algorithmic%20Trading%20Machine%20Learning%20Strategies/functions)
  - `MathLib.dll`: Arithmetic and geometric annuity present value calculations.
  - `calculateYTMYield.dll`: Coupon bond pricing and Newton-Raphson Yield to Maturity solver.
  - `calculateZeroCouponYield.dll`: Zero-coupon spot yield calculator (annual and continuous).
  - `calculateADF.dll`: Augmented Dickey-Fuller stationarity test with asymptotic MacKinnon critical values.
  - `calculateCointegration.dll`: Engle-Granger two-step cointegration test for statistical arbitrage.
  - Verification driver: `main.cpp`, `build.bat`, and `Makefile`.
* **Python Quantitative Machine Learning Engine:**
  - `Algorithmic_Trading_Machine_Learning_Quant_Strategies.ipynb`: Technical feature engineering, rolling Fama-French 5-factor models, K-Means clustering, and Markowitz portfolio optimization.
* **Roadmap & System Specifications:**
  - `trading_system_workflow.drawio`: 4-page system blueprint (Architecture, Runtime Workflow, Lifecycle & Faults, ABI Reference).
  - `TRADING_SYSTEM_DEVELOPMENT_GUIDE.md`: Comprehensive engineering roadmap and implementation guide.
