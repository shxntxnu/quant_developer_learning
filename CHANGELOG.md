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
