#ifndef CALCULATEYTMYIELD_H
#define CALCULATEYTMYIELD_H

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef CALCULATEYTMYIELD_EXPORTS
        #define YTMYIELD_API __declspec(dllexport)
    #else
        #define YTMYIELD_API __declspec(dllimport)
    #endif
#else
    #define YTMYIELD_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calculates the theoretical price of a coupon-paying bond given its YTM.
 * 
 * Formula:
 *   Price = sum_{t=1}^{n} [ C / (1 + y/f)^t ] + [ F / (1 + y/f)^n ]
 * where:
 *   C = (annual_coupon_rate * face_value) / frequency
 *   F = face_value
 *   y = annual yield to maturity (ytm)
 *   f = frequency (e.g. 2 for semi-annual)
 *   n = total_periods
 * 
 * @param face_value Par value of the bond (e.g. 1000.0)
 * @param annual_coupon_rate Annual coupon rate as a decimal (e.g. 0.05 for 5%)
 * @param ytm Annual yield to maturity as a decimal (e.g. 0.06 for 6%)
 * @param total_periods Total number of coupon payment periods remaining
 * @param frequency Number of coupon payments per year (e.g. 1 = Annual, 2 = Semi-annual, 4 = Quarterly)
 * @return Theoretical bond price
 */
YTMYIELD_API double calculate_bond_price(double face_value, double annual_coupon_rate, double ytm, int total_periods, int frequency);

/**
 * @brief Calculates the Yield to Maturity (YTM) of a coupon-paying bond using the Newton-Raphson root-finding method.
 * 
 * @param market_price Current clean market price of the bond
 * @param face_value Par value of the bond (e.g. 1000.0)
 * @param annual_coupon_rate Annual coupon rate as a decimal (e.g. 0.05 for 5%)
 * @param total_periods Total number of coupon payment periods remaining
 * @param frequency Number of coupon payments per year (e.g. 1 = Annual, 2 = Semi-annual, 4 = Quarterly)
 * @return Annualized Yield to Maturity (YTM) as a decimal (e.g. 0.0543 for 5.43%)
 */
YTMYIELD_API double calculate_bond_ytm(double market_price, double face_value, double annual_coupon_rate, int total_periods, int frequency);

#ifdef __cplusplus
}
#endif

#endif // CALCULATEYTMYIELD_H
