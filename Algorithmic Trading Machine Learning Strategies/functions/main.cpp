#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

// Include headers from each independent DLL in include/
#include "include/MathLib.h"
#include "include/calculateYTMYield.h"
#include "include/calculateZeroCouponYield.h"
#include "include/calculateADF.h"
#include "include/calculateCointegration.h"

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

    double calculated_ytm = calculate_bond_ytm(market_price, face_val, coupon_rate, total_periods, frequency);
    double check_price = calculate_bond_price(face_val, coupon_rate, calculated_ytm, total_periods, frequency);

    std::cout << "Bond Parameters: Face Value=$" << std::setprecision(2) << face_val
              << ", Coupon=" << (coupon_rate * 100.0) << "% (Semi-Annual)"
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

    double zero_yield_annual = calculate_zero_coupon_yield(zero_price, zero_face, maturity_years);
    double zero_yield_continuous = calculate_zero_coupon_yield_continuous(zero_price, zero_face, maturity_years);
    double zero_check_price = calculate_zero_coupon_price(zero_face, zero_yield_annual, maturity_years);

    std::cout << "Zero-Coupon Parameters: Face Value=$" << std::setprecision(2) << zero_face
              << ", Price=$" << zero_price
              << ", Maturity=" << maturity_years << " years" << std::endl;
    std::cout << "Annual Compounding Spot Yield:       " << std::setprecision(4) << (zero_yield_annual * 100.0) << "%" << std::endl;
    std::cout << "Continuous Compounding Spot Yield:   " << std::setprecision(4) << (zero_yield_continuous * 100.0) << "%" << std::endl;
    std::cout << "Verification Price using Spot Yield: $" << std::setprecision(2) << zero_check_price << std::endl << std::endl;

    // ---------------------------------------------------------
    // 4. calculateADF.dll: Augmented Dickey-Fuller (ADF) Test
    // ---------------------------------------------------------
    std::cout << "--- [4] calculateADF.dll (Stationarity Test) ---" << std::endl;
    const int N = 150;
    std::vector<double> random_walk(N);
    std::vector<double> mean_reverting(N);

    random_walk[0] = 100.0;
    mean_reverting[0] = 0.0;

    // Deterministic pseudo-random sequence for reproducible demonstration
    for (int t = 1; t < N; ++t) {
        double shock = std::sin(static_cast<double>(t) * 0.45) * 1.5;
        random_walk[t] = random_walk[t - 1] + shock;
        mean_reverting[t] = 0.35 * mean_reverting[t - 1] + shock;
    }

    // Test 4A: Random Walk (Expect Non-Stationary / Fail to Reject H0)
    ADFResult adf_rw = calculate_adf_test(random_walk.data(), N, 1);
    std::cout << "Series A (Random Walk Prices):" << std::endl;
    std::cout << "  ADF Statistic: " << std::setprecision(4) << adf_rw.test_statistic
              << " | 5% Critical Value: " << adf_rw.crit_5pct
              << " | Stationary: " << (adf_rw.is_stationary ? "YES" : "NO (Unit Root Present)") << std::endl;

    // Test 4B: Mean-Reverting Spread (Expect Stationary / Reject H0)
    ADFResult adf_mr = calculate_adf_test(mean_reverting.data(), N, 1);
    std::cout << "Series B (Mean-Reverting Spread):" << std::endl;
    std::cout << "  ADF Statistic: " << std::setprecision(4) << adf_mr.test_statistic
              << " | 5% Critical Value: " << adf_mr.crit_5pct
              << " | Stationary: " << (adf_mr.is_stationary ? "YES (Stationary)" : "NO") << std::endl << std::endl;

    // ---------------------------------------------------------
    // 5. calculateCointegration.dll: Engle-Granger Cointegration
    // ---------------------------------------------------------
    std::cout << "--- [5] calculateCointegration.dll (Statistical Arbitrage) ---" << std::endl;
    // Construct two synthetic cointegrated assets: Y_t = 15.0 + 1.75 * X_t + spread_t
    std::vector<double> asset_X(N);
    std::vector<double> asset_Y(N);

    for (int t = 0; t < N; ++t) {
        asset_X[t] = random_walk[t];
        asset_Y[t] = 15.0 + (1.75 * asset_X[t]) + mean_reverting[t];
    }

    CointegrationResult coint = calculate_engle_granger_test(asset_Y.data(), asset_X.data(), N, 1);

    std::cout << "Cointegrating Pair (Asset Y vs Asset X):" << std::endl;
    std::cout << "  Estimated Hedge Ratio (Beta):  " << std::setprecision(4) << coint.hedge_ratio_beta << " (Target: ~1.75)" << std::endl;
    std::cout << "  Estimated Intercept (Alpha):   " << std::setprecision(4) << coint.intercept_alpha << " (Target: ~15.00)" << std::endl;
    std::cout << "  Cointegrating R-Squared:       " << std::setprecision(4) << coint.r_squared << std::endl;
    std::cout << "  Residual Spread ADF t-stat:    " << std::setprecision(4) << coint.adf_test_stat
              << " | 5% Critical Value: " << coint.crit_5pct << std::endl;
    std::cout << "  Cointegrated for Pairs Trade:  " << (coint.is_cointegrated ? "YES (Cointegrated)" : "NO") << std::endl << std::endl;

    std::cout << "=====================================================" << std::endl;
    std::cout << "All 5 DLLs successfully linked and executed!" << std::endl;
    std::cout << "=====================================================" << std::endl;

    return 0;
}
