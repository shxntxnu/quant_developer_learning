#ifndef CALCULATEADF_H
#define CALCULATEADF_H

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef CALCULATEADF_EXPORTS
        #define ADF_API __declspec(dllexport)
    #else
        #define ADF_API __declspec(dllimport)
    #endif
#else
    #define ADF_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Structure containing the results of the Augmented Dickey-Fuller (ADF) test.
 */
typedef struct {
    double test_statistic;   // The calculated t-statistic for gamma (H0: gamma = 0)
    double crit_1pct;        // 1% critical value (e.g., -3.43 for constant model)
    double crit_5pct;        // 5% critical value (e.g., -2.86 for constant model)
    double crit_10pct;       // 10% critical value (e.g., -2.57 for constant model)
    int lags_used;           // Number of lagged difference terms included
    int is_stationary;       // 1 if test_statistic < crit_5pct (rejects unit root at 5%), 0 otherwise
} ADFResult;

/**
 * @brief Performs the Augmented Dickey-Fuller (ADF) test on a time series.
 * 
 * Regression model (with constant):
 *   \Delta y_t = \alpha + \gamma y_{t-1} + \sum_{i=1}^{p} \delta_i \Delta y_{t-i} + \epsilon_t
 * 
 * Null Hypothesis (H0): \gamma = 0 (The series possesses a unit root and is non-stationary).
 * Alternative Hypothesis (H1): \gamma < 0 (The series is stationary / mean-reverting).
 * 
 * @param series Pointer to an array of double precision floating point numbers
 * @param n Total number of data points in the series (must be > lags + 5)
 * @param lags Number of lagged differences to include (p >= 0). If negative (e.g., -1), default 1 lag is used.
 * @return ADFResult struct populated with test results and critical values
 */
ADF_API ADFResult calculate_adf_test(const double* series, int n, int lags);

#ifdef __cplusplus
}
#endif

#endif // CALCULATEADF_H
