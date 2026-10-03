cmake_minimum_required(VERSION 3.20)
project(tc_trader VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shared include directories for Layer 3 headers
include_directories(include)

if(MSVC)
    add_compile_options(/W4 /O2 /permissive-)
else()
    add_compile_options(-Wall -Wextra -pedantic -O3 -pthread)
endif()

# Layer 3 Unit Tests
add_executable(test_types tests/test_types.cpp)
add_executable(test_ringbuffer tests/test_ringbuffer.cpp)

# Layer 2 Plugin DLL: tc_journal
add_library(tc_journal SHARED
    modules/journal/journal_module.cpp
    modules/journal/async_writer.cpp
    modules/journal/rotating_file_sink.cpp
)
target_include_directories(tc_journal PRIVATE modules/journal)
target_compile_definitions(tc_journal PRIVATE TC_EXPORTS)
set_target_properties(tc_journal PROPERTIES PREFIX "" OUTPUT_NAME "tc_journal")

add_executable(test_journal tests/test_journal.cpp)
if(NOT MSVC)
    target_link_libraries(test_journal PRIVATE dl pthread)
endif()
add_dependencies(test_journal tc_journal)

# Layer 2 Plugin DLL: tc_portfolio
add_library(tc_portfolio SHARED
    modules/portfolio/portfolio_module.cpp
    modules/portfolio/position_book.cpp
)
target_include_directories(tc_portfolio PRIVATE modules/portfolio)
target_compile_definitions(tc_portfolio PRIVATE TC_EXPORTS)
set_target_properties(tc_portfolio PROPERTIES PREFIX "" OUTPUT_NAME "tc_portfolio")

add_executable(test_portfolio tests/test_portfolio.cpp)
if(NOT MSVC)
    target_link_libraries(test_portfolio PRIVATE dl pthread)
endif()
add_dependencies(test_portfolio tc_portfolio)

# Layer 2 Plugin DLL: tc_indicators
add_library(tc_indicators SHARED
    modules/indicators/indicators_module.cpp
)
target_include_directories(tc_indicators PRIVATE modules/indicators)
target_compile_definitions(tc_indicators PRIVATE TC_EXPORTS)
set_target_properties(tc_indicators PROPERTIES PREFIX "" OUTPUT_NAME "tc_indicators")

add_executable(test_indicators tests/test_indicators.cpp)
if(NOT MSVC)
    target_link_libraries(test_indicators PRIVATE dl pthread)
endif()
add_dependencies(test_indicators tc_indicators)

# Layer 2 Plugin DLL: tc_strategy
add_library(tc_strategy SHARED
    modules/strategy/strategy_module.cpp
)
target_include_directories(tc_strategy PRIVATE modules/strategy)
target_compile_definitions(tc_strategy PRIVATE TC_EXPORTS)
set_target_properties(tc_strategy PROPERTIES PREFIX "" OUTPUT_NAME "tc_strategy")

add_executable(test_strategy tests/test_strategy.cpp)
if(NOT MSVC)
    target_link_libraries(test_strategy PRIVATE dl pthread)
endif()
add_dependencies(test_strategy tc_strategy)

# Layer 2 Plugin DLL: tc_risk
add_library(tc_risk SHARED
    modules/risk/risk_module.cpp
)
target_include_directories(tc_risk PRIVATE modules/risk)
target_compile_definitions(tc_risk PRIVATE TC_EXPORTS)
set_target_properties(tc_risk PROPERTIES PREFIX "" OUTPUT_NAME "tc_risk")

add_executable(test_risk tests/test_risk.cpp)
if(NOT MSVC)
    target_link_libraries(test_risk PRIVATE dl pthread)
endif()
add_dependencies(test_risk tc_risk)

# Layer 2 Plugin DLL: tc_marketdata
add_library(tc_marketdata SHARED
    modules/marketdata/marketdata_module.cpp
)
target_include_directories(tc_marketdata PRIVATE modules/marketdata)
target_compile_definitions(tc_marketdata PRIVATE TC_EXPORTS)
set_target_properties(tc_marketdata PROPERTIES PREFIX "" OUTPUT_NAME "tc_marketdata")

add_executable(test_marketdata tests/test_marketdata.cpp)
if(NOT MSVC)
    target_link_libraries(test_marketdata PRIVATE dl pthread)
endif()
add_dependencies(test_marketdata tc_marketdata)

enable_testing()
add_test(NAME TestTypes COMMAND test_types)
add_test(NAME TestRingBuffer COMMAND test_ringbuffer)
add_test(NAME TestJournal COMMAND test_journal)
add_test(NAME TestPortfolio COMMAND test_portfolio)
add_test(NAME TestIndicators COMMAND test_indicators)
add_test(NAME TestStrategy COMMAND test_strategy)
add_test(NAME TestRisk COMMAND test_risk)
add_test(NAME TestMarketData COMMAND test_marketdata)

