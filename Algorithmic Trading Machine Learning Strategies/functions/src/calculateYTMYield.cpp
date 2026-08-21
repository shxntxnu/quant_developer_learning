#include "calculateYTMYield.h"
#include <cmath>
#include <iostream>

/**
 * @brief Calculates the theoretical clean price of a coupon-paying bond.
 */
YTMYIELD_API double calculate_bond_price(double face_value, double annual_coupon_rate, double ytm, int total_periods, int frequency) {
    if (frequency <= 0 || total_periods <= 0 || face_value <= 0.0) {
        return 0.0;
    }

    // Periodic coupon payment (C)
    double coupon_payment = (annual_coupon_rate * face_value) / static_cast<double>(frequency);
    
    // Periodic yield rate (r = y / f)
    double periodic_rate = ytm / static_cast<double>(frequency);

    // If periodic rate is 0%, simple sum of all cash flows
    if (std::abs(periodic_rate) < 1e-12) {
        return (coupon_payment * total_periods) + face_value;
    }

    // Calculate present value of all coupon payments using the standard annuity formula:
    // PV_coupons = C * [1 - (1 + r)^(-n)] / r
    double discount_factor = std::pow(1.0 + periodic_rate, total_periods);
    double pv_coupons = coupon_payment * (1.0 - (1.0 / discount_factor)) / periodic_rate;

    // Calculate present value of the face value paid at maturity:
    // PV_face = F / (1 + r)^n
    double pv_face_value = face_value / discount_factor;

    return pv_coupons + pv_face_value;
}

/**
 * @brief Calculates the derivative of the bond price with respect to annual YTM (dP/dy).
 * Used internally for the Newton-Raphson update step.
 */
static double calculate_bond_price_derivative(double face_value, double annual_coupon_rate, double ytm, int total_periods, int frequency) {
    double coupon_payment = (annual_coupon_rate * face_value) / static_cast<double>(frequency);
    double periodic_rate = ytm / static_cast<double>(frequency);
    double inv_freq = 1.0 / static_cast<double>(frequency);

    double dP_dy = 0.0;

    // Derivative of each coupon cash flow: d/dy [ C / (1 + y/f)^t ] = -t * C * (1/f) / (1 + y/f)^(t+1)
    for (int t = 1; t <= total_periods; ++t) {
        double denom = std::pow(1.0 + periodic_rate, t + 1);
        dP_dy -= (t * coupon_payment * inv_freq) / denom;
    }

    // Derivative of face value cash flow: d/dy [ F / (1 + y/f)^n ] = -n * F * (1/f) / (1 + y/f)^(n+1)
    double denom_face = std::pow(1.0 + periodic_rate, total_periods + 1);
    dP_dy -= (total_periods * face_value * inv_freq) / denom_face;

    return dP_dy;
}

/**
 * @brief Calculates the Yield to Maturity (YTM) using the Newton-Raphson method.
 */
YTMYIELD_API double calculate_bond_ytm(double market_price, double face_value, double annual_coupon_rate, int total_periods, int frequency) {
    if (market_price <= 0.0 || face_value <= 0.0 || frequency <= 0 || total_periods <= 0) {
        return 0.0;
    }

    // 1. Initial Guess for YTM:
    // Standard financial approximation formula for bond yield:
    // YTM_approx = [ Annual Coupon + (Face Value - Price) / Years ] / [ (Face Value + Price) / 2 ]
    double annual_coupon = annual_coupon_rate * face_value;
    double years_to_maturity = static_cast<double>(total_periods) / static_cast<double>(frequency);
    double initial_guess = (annual_coupon + (face_value - market_price) / years_to_maturity) 
                          / ((face_value + market_price) / 2.0);

    // Ensure positive initial guess
    if (initial_guess <= 0.001) {
        initial_guess = 0.05; // 5% default fallback guess
    }

    double ytm = initial_guess;
    const double tolerance = 1e-7; // Precision target for price difference ($0.0000001)
    const int max_iterations = 100;

    // 2. Newton-Raphson Iteration:
    // Formula: y_{k+1} = y_k - f(y_k) / f'(y_k)
    // where f(y) = calculated_price(y) - market_price
    for (int i = 0; i < max_iterations; ++i) {
        double current_price = calculate_bond_price(face_value, annual_coupon_rate, ytm, total_periods, frequency);
        double price_diff = current_price - market_price;

        // Check if we reached desired precision
        if (std::abs(price_diff) < tolerance) {
            return ytm;
        }

        double derivative = calculate_bond_price_derivative(face_value, annual_coupon_rate, ytm, total_periods, frequency);
        
        // Prevent division by zero
        if (std::abs(derivative) < 1e-12) {
            break;
        }

        double next_ytm = ytm - (price_diff / derivative);

        // Keep yield within realistic boundaries to avoid divergence
        if (next_ytm <= -0.99) {
            next_ytm = 0.001;
        }

        ytm = next_ytm;
    }

    return ytm;
}
