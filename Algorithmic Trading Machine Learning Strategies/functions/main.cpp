#include <iostream>
#include <iomanip>

// Include headers from each independent DLL
#include "MathLib.h"
#include "calculateYTMYield.h"
#include "calculateZeroCouponYield.h"

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << "       QUANTITATIVE FINANCE DLL INTEGRATION DEMO     " << std::endl;
    std::cout << "=====================================================" << std::endl << std::endl;

    std::cout << std::fixed << std::setprecision(4);

    // ---------------------------------------------------------
    // 1. MathLib.dll: General Financial & Math Functions
    // ---------------------------------------------------------
    std::cout << "--- [1] MathLib.dll ---" << std::endl;
    int sum = add(15, 27);
    std::cout << "Basic Addition (15 + 27): " << sum << std::endl;

    double payment = 1000.0;     // $1,000 initial payment
    double growth_rate = 0.03;   // 3% growth per period
    double discount_rate = 0.07; // 7% discount rate
    int periods = 10;            // 10 periods

    double pv_annuity = calculate_geometric_annuity(payment, growth_rate, discount_rate, periods);
    std::cout << "Present Value of Geometric Annuity: $" << std::setprecision(2) << pv_annuity << std::endl << std::endl;

    // ---------------------------------------------------------
    // 2. calculateYTMYield.dll: Coupon Bond Pricing & YTM Solver
    // ---------------------------------------------------------
    std::cout << "--- [2] calculateYTMYield.dll (Coupon Bond YTM) ---" << std::endl;
    double face_val = 1000.0;          // $1,000 par value
    double coupon_rate = 0.05;         // 5.00% annual coupon
    int frequency = 2;                 // Semi-annual coupon payments (2 per year)
    int total_periods = 20;            // 10 years * 2 = 20 periods
    double market_price = 960.00;      // Bond trading at discount ($960.00)

    // Calculate YTM using Newton-Raphson method
    double calculated_ytm = calculate_bond_ytm(market_price, face_val, coupon_rate, total_periods, frequency);

    // Verify by recalculating price with the derived YTM
    double check_price = calculate_bond_price(face_val, coupon_rate, calculated_ytm, total_periods, frequency);

    std::cout << "Bond Parameters: Face Value=$" << std::setprecision(2) << face_val
              << ", Coupon=" << std::setprecision(2) << (coupon_rate * 100.0) << "% (Semi-Annual)"
              << ", Periods=" << total_periods
              << ", Market Price=$" << market_price << std::endl;
    std::cout << "Calculated YTM (Yield to Maturity): " << std::setprecision(4) << (calculated_ytm * 100.0) << "%" << std::endl;
    std::cout << "Verification Price using YTM:      $" << std::setprecision(2) << check_price << std::endl << std::endl;

    // ---------------------------------------------------------
    // 3. calculateZeroCouponYield.dll: Zero-Coupon Spot Rates
    // ---------------------------------------------------------
    std::cout << "--- [3] calculateZeroCouponYield.dll (Zero-Coupon Spot Yield) ---" << std::endl;
    double zero_face = 1000.0;         // $1,000 par value
    double zero_price = 783.53;        // Market price
    double maturity_years = 5.0;       // 5 years to maturity

    // Calculate annual compounding spot yield and continuous compounding yield
    double zero_yield_annual = calculate_zero_coupon_yield(zero_price, zero_face, maturity_years);
    double zero_yield_continuous = calculate_zero_coupon_yield_continuous(zero_price, zero_face, maturity_years);
    double zero_check_price = calculate_zero_coupon_price(zero_face, zero_yield_annual, maturity_years);

    std::cout << "Zero-Coupon Parameters: Face Value=$" << std::setprecision(2) << zero_face
              << ", Price=$" << zero_price
              << ", Maturity=" << maturity_years << " years" << std::endl;
    std::cout << "Annual Compounding Spot Yield:       " << std::setprecision(4) << (zero_yield_annual * 100.0) << "%" << std::endl;
    std::cout << "Continuous Compounding Spot Yield:   " << std::setprecision(4) << (zero_yield_continuous * 100.0) << "%" << std::endl;
    std::cout << "Verification Price using Spot Yield: $" << std::setprecision(2) << zero_check_price << std::endl << std::endl;

    std::cout << "=====================================================" << std::endl;
    std::cout << "All DLLs successfully linked and executed!" << std::endl;
    std::cout << "=====================================================" << std::endl;

    return 0;
}
