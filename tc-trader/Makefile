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
else ifeq ($(UNAME_S),Linux)
    JOURNAL_LIB = $(BIN_DIR)/libtc_journal.so
    PORTFOLIO_LIB = $(BIN_DIR)/libtc_portfolio.so
    INDICATORS_LIB = $(BIN_DIR)/libtc_indicators.so
    STRATEGY_LIB = $(BIN_DIR)/libtc_strategy.so
else
    JOURNAL_LIB = $(BIN_DIR)/tc_journal.dll
    PORTFOLIO_LIB = $(BIN_DIR)/tc_portfolio.dll
    INDICATORS_LIB = $(BIN_DIR)/tc_indicators.dll
    STRATEGY_LIB = $(BIN_DIR)/tc_strategy.dll
endif

JOURNAL_SRCS = $(MODULES_DIR)/journal/journal_module.cpp \
               $(MODULES_DIR)/journal/async_writer.cpp \
               $(MODULES_DIR)/journal/rotating_file_sink.cpp

PORTFOLIO_SRCS = $(MODULES_DIR)/portfolio/portfolio_module.cpp \
                 $(MODULES_DIR)/portfolio/position_book.cpp

INDICATORS_SRCS = $(MODULES_DIR)/indicators/indicators_module.cpp

STRATEGY_SRCS = $(MODULES_DIR)/strategy/strategy_module.cpp

TARGETS = $(BIN_DIR)/test_types \
          $(BIN_DIR)/test_ringbuffer \
          $(JOURNAL_LIB) \
          $(BIN_DIR)/test_journal \
          $(PORTFOLIO_LIB) \
          $(BIN_DIR)/test_portfolio \
          $(INDICATORS_LIB) \
          $(BIN_DIR)/test_indicators \
          $(STRATEGY_LIB) \
          $(BIN_DIR)/test_strategy

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

clean:
	rm -rf $(BIN_DIR) logs
