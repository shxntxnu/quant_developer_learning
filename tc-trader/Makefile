CXX ?= clang++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -pthread -Iinclude
SO_FLAGS ?= -shared -fPIC

BIN_DIR = bin
TESTS_DIR = tests
MODULES_DIR = modules

# Detect OS for dynamic library naming
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    JOURNAL_LIB = $(BIN_DIR)/libtc_journal.dylib
    PORTFOLIO_LIB = $(BIN_DIR)/libtc_portfolio.dylib
    INDICATORS_LIB = $(BIN_DIR)/libtc_indicators.dylib
    STRATEGY_LIB = $(BIN_DIR)/libtc_strategy.dylib
    RISK_LIB = $(BIN_DIR)/libtc_risk.dylib
    MARKETDATA_LIB = $(BIN_DIR)/libtc_marketdata.dylib
    GATEWAY_SIM_LIB = $(BIN_DIR)/libtc_gateway_sim.dylib
else ifeq ($(UNAME_S),Linux)
    JOURNAL_LIB = $(BIN_DIR)/libtc_journal.so
    PORTFOLIO_LIB = $(BIN_DIR)/libtc_portfolio.so
    INDICATORS_LIB = $(BIN_DIR)/libtc_indicators.so
    STRATEGY_LIB = $(BIN_DIR)/libtc_strategy.so
    RISK_LIB = $(BIN_DIR)/libtc_risk.so
    MARKETDATA_LIB = $(BIN_DIR)/libtc_marketdata.so
    GATEWAY_SIM_LIB = $(BIN_DIR)/libtc_gateway_sim.so
else
    JOURNAL_LIB = $(BIN_DIR)/tc_journal.dll
    PORTFOLIO_LIB = $(BIN_DIR)/tc_portfolio.dll
    INDICATORS_LIB = $(BIN_DIR)/tc_indicators.dll
    STRATEGY_LIB = $(BIN_DIR)/tc_strategy.dll
    RISK_LIB = $(BIN_DIR)/tc_risk.dll
    MARKETDATA_LIB = $(BIN_DIR)/tc_marketdata.dll
    GATEWAY_SIM_LIB = $(BIN_DIR)/tc_gateway_sim.dll
endif

JOURNAL_SRCS = $(MODULES_DIR)/journal/journal_module.cpp \
               $(MODULES_DIR)/journal/async_writer.cpp \
               $(MODULES_DIR)/journal/rotating_file_sink.cpp

PORTFOLIO_SRCS = $(MODULES_DIR)/portfolio/portfolio_module.cpp \
                 $(MODULES_DIR)/portfolio/position_book.cpp

INDICATORS_SRCS = $(MODULES_DIR)/indicators/indicators_module.cpp

STRATEGY_SRCS = $(MODULES_DIR)/strategy/strategy_module.cpp

RISK_SRCS = $(MODULES_DIR)/risk/risk_module.cpp

MARKETDATA_SRCS = $(MODULES_DIR)/marketdata/marketdata_module.cpp

GATEWAY_SIM_SRCS = $(MODULES_DIR)/gateway_sim/gateway_sim_module.cpp

TARGETS = $(BIN_DIR)/test_types \
          $(BIN_DIR)/test_ringbuffer \
          $(JOURNAL_LIB) \
          $(BIN_DIR)/test_journal \
          $(PORTFOLIO_LIB) \
          $(BIN_DIR)/test_portfolio \
          $(INDICATORS_LIB) \
          $(BIN_DIR)/test_indicators \
          $(STRATEGY_LIB) \
          $(BIN_DIR)/test_strategy \
          $(RISK_LIB) \
          $(BIN_DIR)/test_risk \
          $(MARKETDATA_LIB) \
          $(BIN_DIR)/test_marketdata \
          $(GATEWAY_SIM_LIB) \
          $(BIN_DIR)/test_gateway_sim

.PHONY: all clean run_tests

all: $(TARGETS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Layer 3 Unit Tests
$(BIN_DIR)/test_types: $(TESTS_DIR)/test_types.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BIN_DIR)/test_ringbuffer: $(TESTS_DIR)/test_ringbuffer.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Layer 2 tc_journal Plugin Shared Library & Test
$(JOURNAL_LIB): $(JOURNAL_SRCS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SO_FLAGS) -DTC_EXPORTS -I$(MODULES_DIR)/journal -o $@ $(JOURNAL_SRCS)

$(BIN_DIR)/test_journal: $(TESTS_DIR)/test_journal.cpp $(JOURNAL_LIB) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Layer 2 tc_portfolio Plugin Shared Library & Test
$(PORTFOLIO_LIB): $(PORTFOLIO_SRCS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SO_FLAGS) -DTC_EXPORTS -I$(MODULES_DIR)/portfolio -o $@ $(PORTFOLIO_SRCS)

$(BIN_DIR)/test_portfolio: $(TESTS_DIR)/test_portfolio.cpp $(PORTFOLIO_LIB) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Layer 2 tc_indicators Plugin Shared Library & Test
$(INDICATORS_LIB): $(INDICATORS_SRCS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SO_FLAGS) -DTC_EXPORTS -I$(MODULES_DIR)/indicators -o $@ $(INDICATORS_SRCS)

$(BIN_DIR)/test_indicators: $(TESTS_DIR)/test_indicators.cpp $(INDICATORS_LIB) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Layer 2 tc_strategy Plugin Shared Library & Test
$(STRATEGY_LIB): $(STRATEGY_SRCS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SO_FLAGS) -DTC_EXPORTS -I$(MODULES_DIR)/strategy -o $@ $(STRATEGY_SRCS)

$(BIN_DIR)/test_strategy: $(TESTS_DIR)/test_strategy.cpp $(STRATEGY_LIB) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Layer 2 tc_risk Plugin Shared Library & Test
$(RISK_LIB): $(RISK_SRCS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SO_FLAGS) -DTC_EXPORTS -I$(MODULES_DIR)/risk -o $@ $(RISK_SRCS)

$(BIN_DIR)/test_risk: $(TESTS_DIR)/test_risk.cpp $(RISK_LIB) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Layer 2 tc_marketdata Plugin Shared Library & Test
$(MARKETDATA_LIB): $(MARKETDATA_SRCS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SO_FLAGS) -DTC_EXPORTS -I$(MODULES_DIR)/marketdata -o $@ $(MARKETDATA_SRCS)

$(BIN_DIR)/test_marketdata: $(TESTS_DIR)/test_marketdata.cpp $(MARKETDATA_LIB) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Layer 2 tc_gateway_sim Plugin Shared Library & Test
$(GATEWAY_SIM_LIB): $(GATEWAY_SIM_SRCS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(SO_FLAGS) -DTC_EXPORTS -I$(MODULES_DIR)/gateway_sim -o $@ $(GATEWAY_SIM_SRCS)

$(BIN_DIR)/test_gateway_sim: $(TESTS_DIR)/test_gateway_sim.cpp $(GATEWAY_SIM_LIB) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

run_tests: all
	@echo "\n=== Running test_types ==="
	@./$(BIN_DIR)/test_types
	@echo "\n=== Running test_ringbuffer ==="
	@./$(BIN_DIR)/test_ringbuffer
	@echo "\n=== Running test_journal ==="
	@./$(BIN_DIR)/test_journal
	@echo "\n=== Running test_portfolio ==="
	@./$(BIN_DIR)/test_portfolio
	@echo "\n=== Running test_indicators ==="
	@./$(BIN_DIR)/test_indicators
	@echo "\n=== Running test_strategy ==="
	@./$(BIN_DIR)/test_strategy
	@echo "\n=== Running test_risk ==="
	@./$(BIN_DIR)/test_risk
	@echo "\n=== Running test_marketdata ==="
	@./$(BIN_DIR)/test_marketdata
	@echo "\n=== Running test_gateway_sim ==="
	@./$(BIN_DIR)/test_gateway_sim

clean:
	rm -rf $(BIN_DIR) logs

