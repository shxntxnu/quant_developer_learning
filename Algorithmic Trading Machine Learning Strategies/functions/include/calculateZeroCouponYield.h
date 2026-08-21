#ifndef CALCULATEZEROCOUPONYIELD_H
#define CALCULATEZEROCOUPONYIELD_H

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef CALCULATEZEROCOUPONYIELD_EXPORTS
        #define ZEROYIELD_API __declspec(dllexport)
    #else
        #define ZEROYIELD_API __declspec(dllimport)
    #endif
#else
    #define ZEROYIELD_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calculates the annual compounding spot yield for a zero-coupon bond.
 * 
 * Formula:
 *   y = (Face Value / Market Price) ^ (1 / years_to_maturity) - 1
 * 
 * @param market_price Current market price of the zero-coupon bond (e.g. 750.0)
 * @param face_value Face value of the bond received at maturity (e.g. 1000.0)
 * @param years_to_maturity Time remaining until maturity in years (e.g. 5.0)
 * @return Annualized spot yield as a decimal (e.g. 0.0592 for 5.92%)
 */
ZEROYIELD_API double calculate_zero_coupon_yield(double market_price, double face_value, double years_to_maturity);

/**
 * @brief Calculates the continuously compounded spot yield for a zero-coupon bond.
 * 
 * Formula:
 *   y_cont = ln(Face Value / Market Price) / years_to_maturity
 * 
 * @param market_price Current market price of the zero-coupon bond
 * @param face_value Face value of the bond received at maturity
 * @param years_to_maturity Time remaining until maturity in years
 * @return Continuously compounded annualized spot rate as a decimal
 */
ZEROYIELD_API double calculate_zero_coupon_yield_continuous(double market_price, double face_value, double years_to_maturity);

/**
 * @brief Calculates the theoretical price of a zero-coupon bond given annual compounding yield.
 * 
 * Formula:
 *   Price = Face Value / (1 + yield) ^ years_to_maturity
 * 
 * @param face_value Face value of the bond received at maturity
 * @param annual_yield Annual spot yield rate as a decimal (e.g. 0.05 for 5%)
 * @param years_to_maturity Time remaining until maturity in years
 * @return Theoretical price of the zero-coupon bond
 */
ZEROYIELD_API double calculate_zero_coupon_price(double face_value, double annual_yield, double years_to_maturity);

#ifdef __cplusplus
}
#endif

#endif // CALCULATEZEROCOUPONYIELD_H
