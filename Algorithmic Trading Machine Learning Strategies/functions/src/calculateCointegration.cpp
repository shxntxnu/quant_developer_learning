#include "calculateCointegration.h"
#include <cmath>
#include <vector>
#include <iostream>

/**
 * @brief Performs OLS regression on residual series without drift:
 * Delta e_t = gamma * e_{t-1} + delta_1 * Delta e_{t-1} + ... + delta_p * Delta e_{t-p} + v_t
 */
static double compute_residual_adf_t_stat(const std::vector<double>& residuals, int n, int p) {
    if (n < (p + 10)) {
        return 0.0;
    }

    std::vector<double> de(n - 1);
    for (int i = 0; i < n - 1; ++i) {
        de[i] = residuals[i + 1] - residuals[i];
    }

    int M = (n - 1) - p;
    int K = 1 + p; // gamma * e_{t-1} + p lagged difference terms

    if (M <= K) {
        return 0.0;
    }

    std::vector<double> Y(M);
    std::vector<std::vector<double>> X(M, std::vector<double>(K, 0.0));

    for (int i = 0; i < M; ++i) {
        int t = i + p + 1;
        Y[i] = de[i + p];
        X[i][0] = residuals[t - 1]; // Lagged residual level e_{t-1}

        for (int lag = 0; lag < p; ++lag) {
            X[i][1 + lag] = de[i + p - 1 - lag];
        }
    }

    // Compute (X^T * X) and (X^T * Y)
    std::vector<std::vector<double>> XtX(K, std::vector<double>(K, 0.0));
    std::vector<double> XtY(K, 0.0);

    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < K; ++j) {
            XtY[j] += X[i][j] * Y[i];
            for (int k = 0; k < K; ++k) {
                XtX[j][k] += X[i][j] * X[i][k];
            }
        }
    }

    // Invert XtX using Gauss-Jordan
    std::vector<std::vector<double>> aug(K, std::vector<double>(2 * K, 0.0));
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < K; ++j) aug[i][j] = XtX[i][j];
        aug[i][i + K] = 1.0;
    }

    for (int i = 0; i < K; ++i) {
        int pivot = i;
        double max_val = std::abs(aug[i][i]);
        for (int r = i + 1; r < K; ++r) {
            if (std::abs(aug[r][i]) > max_val) {
                max_val = std::abs(aug[r][i]);
                pivot = r;
            }
        }
        if (max_val < 1e-12) return 0.0;
        if (pivot != i) std::swap(aug[i], aug[pivot]);

        double diag = aug[i][i];
        for (int c = 0; c < 2 * K; ++c) aug[i][c] /= diag;

        for (int r = 0; r < K; ++r) {
            if (r != i) {
                double factor = aug[r][i];
                for (int c = 0; c < 2 * K; ++c) aug[r][c] -= factor * aug[i][c];
            }
        }
    }

    // Solve beta: beta[0] is gamma
    std::vector<double> beta(K, 0.0);
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < K; ++j) {
            beta[i] += aug[i][j + K] * XtY[j];
        }
    }

    // Compute RSS
    double RSS = 0.0;
    for (int i = 0; i < M; ++i) {
        double y_hat = 0.0;
        for (int j = 0; j < K; ++j) {
            y_hat += X[i][j] * beta[j];
        }
        double res = Y[i] - y_hat;
        RSS += res * res;
    }

    int df = M - K;
    double variance = RSS / static_cast<double>(df);
    double se_gamma = std::sqrt(variance * aug[0][0 + K]);

    if (se_gamma < 1e-12) {
        return 0.0;
    }

    return beta[0] / se_gamma;
}

/**
 * @brief Performs the Engle-Granger Two-Step Cointegration test between two asset price series Y and X.
 */
COINT_API CointegrationResult calculate_engle_granger_test(const double* y, const double* x, int n, int lags) {
    CointegrationResult result;
    result.hedge_ratio_beta = 0.0;
    result.intercept_alpha = 0.0;
    result.r_squared = 0.0;
    result.adf_test_stat = 0.0;
    // Engle-Yoo / MacKinnon Critical Values for 2 variables:
    result.crit_1pct = -3.90;
    result.crit_5pct = -3.34;
    result.crit_10pct = -3.04;
    result.is_cointegrated = 0;

    int p = (lags < 0) ? 1 : lags;
    if (y == nullptr || x == nullptr || n < (p + 15)) {
        return result;
    }

    // -------------------------------------------------------------
    // Step 1: OLS Cointegrating Regression: Y_t = alpha + beta * X_t + e_t
    // -------------------------------------------------------------
    double sum_x = 0.0, sum_y = 0.0;
    for (int i = 0; i < n; ++i) {
        sum_x += x[i];
        sum_y += y[i];
    }
    double mean_x = sum_x / static_cast<double>(n);
    double mean_y = sum_y / static_cast<double>(n);

    double cov_xy = 0.0, var_x = 0.0, var_y = 0.0;
    for (int i = 0; i < n; ++i) {
        double dx = x[i] - mean_x;
        double dy = y[i] - mean_y;
        cov_xy += dx * dy;
        var_x += dx * dx;
        var_y += dy * dy;
    }

    if (var_x < 1e-12) {
        return result; // No variance in independent series
    }

    double beta = cov_xy / var_x;
    double alpha = mean_y - (beta * mean_x);

    // Compute R-squared and residuals
    std::vector<double> residuals(n);
    double rss = 0.0;
    for (int i = 0; i < n; ++i) {
        residuals[i] = y[i] - (alpha + beta * x[i]);
        rss += residuals[i] * residuals[i];
    }

    double r2 = (var_y > 1e-12) ? (1.0 - (rss / var_y)) : 0.0;
    if (r2 < 0.0) r2 = 0.0;

    result.hedge_ratio_beta = beta;
    result.intercept_alpha = alpha;
    result.r_squared = r2;

    // -------------------------------------------------------------
    // Step 2: Unit Root Test on Residuals (ADF on e_t)
    // -------------------------------------------------------------
    double t_stat = compute_residual_adf_t_stat(residuals, n, p);
    result.adf_test_stat = t_stat;

    // Null Hypothesis of No Cointegration is rejected if t_stat < crit_5pct (-3.34)
    result.is_cointegrated = (t_stat < result.crit_5pct) ? 1 : 0;

    return result;
}

/**
 * @brief Computes the synthetic residual spread series: Spread_t = Y_t - (\alpha + \beta * X_t)
 */
COINT_API void calculate_cointegration_spread(const double* y, const double* x, int n, double alpha, double beta, double* spread_out) {
    if (y == nullptr || x == nullptr || spread_out == nullptr || n <= 0) {
        return;
    }
    for (int i = 0; i < n; ++i) {
        spread_out[i] = y[i] - (alpha + beta * x[i]);
    }
}
