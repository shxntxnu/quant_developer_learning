#include "calculateZeroCouponYield.h"
#include <cmath>

/**
 * @brief Calculates the annual compounding spot yield for a zero-coupon bond.
 */
ZEROYIELD_API double calculate_zero_coupon_yield(double market_price, double face_value, double years_to_maturity) {
    if (market_price <= 0.0 || face_value <= 0.0 || years_to_maturity <= 0.0) {
        return 0.0;
    }
    // Analytical solution: y = (Face Value / Price) ^ (1 / T) - 1
    return std::pow(face_value / market_price, 1.0 / years_to_maturity) - 1.0;
}

/**
 * @brief Calculates the continuously compounded spot yield for a zero-coupon bond.
 */
ZEROYIELD_API double calculate_zero_coupon_yield_continuous(double market_price, double face_value, double years_to_maturity) {
    if (market_price <= 0.0 || face_value <= 0.0 || years_to_maturity <= 0.0) {
        return 0.0;
    }
    // Analytical continuous compounding: y_cont = ln(Face Value / Price) / T
    return std::log(face_value / market_price) / years_to_maturity;
}

/**
 * @brief Calculates the theoretical price of a zero-coupon bond given annual compounding yield.
 */
ZEROYIELD_API double calculate_zero_coupon_price(double face_value, double annual_yield, double years_to_maturity) {
    if (face_value <= 0.0 || years_to_maturity <= 0.0) {
        return 0.0;
    }
    // Analytical price: Price = Face Value / (1 + y)^T
    return face_value / std::pow(1.0 + annual_yield, years_to_maturity);
}
