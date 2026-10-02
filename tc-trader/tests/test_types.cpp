#include <iostream>
#include <iomanip>
#include <cassert>
#include <cstring>
#include <type_traits>

#include "tc/tc_abi.h"
#include "tc/tc_types.h"
#include "tc/tc_module.h"

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << "      TC-TRADER LAYER 3: STRUCT LAYOUT & ABI TEST     " << std::endl;
    std::cout << "=====================================================" << std::endl;

    // Verify all structs are trivially copyable (PODs suitable for memcpy and lock-free ring buffers)
    static_assert(std::is_trivially_copyable<TcTick>::value, "TcTick must be trivially copyable");
    static_assert(std::is_trivially_copyable<TcBar>::value, "TcBar must be trivially copyable");
    static_assert(std::is_trivially_copyable<TcIndicatorSnapshot>::value, "TcIndicatorSnapshot must be trivially copyable");
    static_assert(std::is_trivially_copyable<TcSignal>::value, "TcSignal must be trivially copyable");
    static_assert(std::is_trivially_copyable<TcOrderIntent>::value, "TcOrderIntent must be trivially copyable");
    static_assert(std::is_trivially_copyable<TcOrderEvent>::value, "TcOrderEvent must be trivially copyable");
    static_assert(std::is_trivially_copyable<TcFill>::value, "TcFill must be trivially copyable");
    static_assert(std::is_trivially_copyable<TcPosition>::value, "TcPosition must be trivially copyable");
    static_assert(std::is_trivially_copyable<TcAccountView>::value, "TcAccountView must be trivially copyable");

    std::cout << "[+] POD Trivial Copyability: ALL PASSED" << std::endl << std::endl;

    std::cout << "--- Memory Sizes & Alignment ---" << std::endl;
    std::cout << "sizeof(TcTick):              " << sizeof(TcTick) << " bytes" << std::endl;
    std::cout << "sizeof(TcBar):               " << sizeof(TcBar) << " bytes" << std::endl;
    std::cout << "sizeof(TcIndicatorSnapshot): " << sizeof(TcIndicatorSnapshot) << " bytes" << std::endl;
    std::cout << "sizeof(TcSignal):            " << sizeof(TcSignal) << " bytes" << std::endl;
    std::cout << "sizeof(TcOrderIntent):       " << sizeof(TcOrderIntent) << " bytes" << std::endl;
    std::cout << "sizeof(TcOrderEvent):        " << sizeof(TcOrderEvent) << " bytes" << std::endl;
    std::cout << "sizeof(TcFill):              " << sizeof(TcFill) << " bytes" << std::endl;
    std::cout << "sizeof(TcPosition):          " << sizeof(TcPosition) << " bytes" << std::endl;
    std::cout << "sizeof(TcAccountView):       " << sizeof(TcAccountView) << " bytes" << std::endl << std::endl;

    // Test Fixed-Point Price Conversions
    std::cout << "--- Fixed-Point Price Conversion Tests ---" << std::endl;
    double original_price = 152.3456;
    TcPrice fixed_price = TC_DOUBLE_TO_PRICE(original_price);
    double converted_back = TC_PRICE_TO_DOUBLE(fixed_price);

    std::cout << "Original Float:  " << std::fixed << std::setprecision(4) << original_price << std::endl;
    std::cout << "Fixed-Point Int: " << fixed_price << " (expected 1523456)" << std::endl;
    std::cout << "Converted Back:  " << converted_back << std::endl;
    assert(fixed_price == 1523456LL);
    assert(std::abs(converted_back - original_price) < 1e-6);

    // Negative price check (e.g. spread pricing / PnL)
    double neg_price = -12.5000;
    TcPrice fixed_neg = TC_DOUBLE_TO_PRICE(neg_price);
    assert(fixed_neg == -125000LL);
    assert(TC_PRICE_TO_DOUBLE(fixed_neg) == -12.5000);
    std::cout << "[+] Fixed-Point Calculations: PASSED" << std::endl << std::endl;

    // Test Symbol String Handling
    std::cout << "--- Symbol String Handling ---" << std::endl;
    TcBar bar{};
    bar.struct_size = sizeof(TcBar);
    bar.version = 1;
    bar.timeframe_sec = 300;
    std::strncpy(bar.symbol, "ES_202612", sizeof(bar.symbol) - 1);
    bar.symbol[sizeof(bar.symbol) - 1] = '\0';
    bar.open = TC_DOUBLE_TO_PRICE(5025.50);
    bar.high = TC_DOUBLE_TO_PRICE(5030.75);
    bar.low = TC_DOUBLE_TO_PRICE(5020.25);
    bar.close = TC_DOUBLE_TO_PRICE(5028.00);
    bar.volume = 15420;

    std::cout << "Bar Symbol: " << bar.symbol << std::endl;
    std::cout << "Bar Close:  $" << TC_PRICE_TO_DOUBLE(bar.close) << std::endl;
    assert(std::strcmp(bar.symbol, "ES_202612") == 0);
    std::cout << "[+] Struct Initialization & String Handling: PASSED" << std::endl << std::endl;

    std::cout << "=====================================================" << std::endl;
    std::cout << "      ALL TYPE AND ABI CHECKS PASSED SUCCESSFULLY!    " << std::endl;
    std::cout << "=====================================================" << std::endl;

    return 0;
}
