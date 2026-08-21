#ifndef CALCULATECOINTEGRATION_H
#define CALCULATECOINTEGRATION_H

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef CALCULATECOINTEGRATION_EXPORTS
        #define COINT_API __declspec(dllexport)
    #else
        #define COINT_API __declspec(dllimport)
    #endif
#else
    #define COINT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Structure containing the results of the Engle-Granger Two-Step Cointegration Test.
 */
typedef struct {
    double hedge_ratio_beta; // Optimal hedge ratio (\beta) from OLS: Y = \alpha + \beta * X + \epsilon
    double intercept_alpha;  // Intercept (\alpha)
    double r_squared;        // R-squared of the cointegrating regression
    double adf_test_stat;    // ADF t-statistic of the residual spread series
    double crit_1pct;        // Engle-Yoo / MacKinnon 1% critical value for cointegration (e.g., -3.90)
    double crit_5pct;        // Engle-Yoo / MacKinnon 5% critical value for cointegration (e.g., -3.34)
    double crit_10pct;       // Engle-Yoo / MacKinnon 10% critical value for cointegration (e.g., -3.04)
    int is_cointegrated;     // 1 if adf_test_stat < crit_5pct (spread is stationary), 0 otherwise
} CointegrationResult;

/**
 * @brief Performs the Engle-Granger Two-Step Cointegration test between two asset price series Y and X.
 * 
 * Step 1: Fits OLS linear regression: Y_t = \alpha + \beta * X_t + e_t to find optimal hedge ratio \beta.
 * Step 2: Tests residuals e_t = Y_t - (\alpha + \beta * X_t) for stationarity using Engle-Yoo critical values.
 * 
 * @param y Array of prices for Asset Y (dependent variable)
 * @param x Array of prices for Asset X (independent variable)
 * @param n Length of the price series (both must be equal)
 * @param lags Number of autoregressive lags for residual ADF test (default: 1)
 * @return CointegrationResult struct
 */
COINT_API CointegrationResult calculate_engle_granger_test(const double* y, const double* x, int n, int lags);

/**
 * @brief Computes the synthetic residual spread series: Spread_t = Y_t - (\alpha + \beta * X_t)
 * 
 * @param y Array of prices for Asset Y
 * @param x Array of prices for Asset X
 * @param n Length of arrays
 * @param alpha Intercept
 * @param beta Hedge ratio
 * @param spread_out Output array of size n to store the computed spread
 */
COINT_API void calculate_cointegration_spread(const double* y, const double* x, int n, double alpha, double beta, double* spread_out);

#ifdef __cplusplus
}
#endif

#endif // CALCULATECOINTEGRATION_H
