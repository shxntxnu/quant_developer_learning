#include "calculateADF.h"
#include <cmath>
#include <vector>
#include <iostream>

/**
 * @brief Helper function to invert a small square matrix (K x K) using Gauss-Jordan elimination.
 * K is typically small (2 to 5) for ADF regressions.
 */
static bool invert_matrix(const std::vector<std::vector<double>>& A, std::vector<std::vector<double>>& inv, int K) {
    std::vector<std::vector<double>> aug(K, std::vector<double>(2 * K, 0.0));

    // Create augmented matrix [A | I]
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < K; ++j) {
            aug[i][j] = A[i][j];
        }
        aug[i][i + K] = 1.0;
    }

    // Gauss-Jordan elimination with partial pivoting
    for (int i = 0; i < K; ++i) {
        int pivot = i;
        double max_val = std::abs(aug[i][i]);
        for (int r = i + 1; r < K; ++r) {
            if (std::abs(aug[r][i]) > max_val) {
                max_val = std::abs(aug[r][i]);
                pivot = r;
            }
        }

        if (max_val < 1e-12) {
            return false; // Singular matrix
        }

        if (pivot != i) {
            std::swap(aug[i], aug[pivot]);
        }

        double diag = aug[i][i];
        for (int c = 0; c < 2 * K; ++c) {
            aug[i][c] /= diag;
        }

        for (int r = 0; r < K; ++r) {
            if (r != i) {
                double factor = aug[r][i];
                for (int c = 0; c < 2 * K; ++c) {
                    aug[r][c] -= factor * aug[i][c];
                }
            }
        }
    }

    inv.resize(K, std::vector<double>(K, 0.0));
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < K; ++j) {
            inv[i][j] = aug[i][j + K];
        }
    }
    return true;
}

/**
 * @brief Performs the Augmented Dickey-Fuller (ADF) test on a time series.
 */
ADF_API ADFResult calculate_adf_test(const double* series, int n, int lags) {
    ADFResult result;
    result.test_statistic = 0.0;
    result.crit_1pct = -3.433553;
    result.crit_5pct = -2.862955;
    result.crit_10pct = -2.567523;
    result.lags_used = (lags < 0) ? 1 : lags;
    result.is_stationary = 0;

    int p = result.lags_used;
    // Total observations needed: at least p + 5 points for regression
    if (series == nullptr || n < (p + 10)) {
        return result;
    }

    // 1. Calculate first differences: Delta y_t = y_t - y_{t-1}
    std::vector<double> dy(n - 1);
    for (int i = 0; i < n - 1; ++i) {
        dy[i] = series[i + 1] - series[i];
    }

    // Number of regression observations: M
    int M = (n - 1) - p;
    // Number of regressors: K = 1 (constant) + 1 (lagged level y_{t-1}) + p (lagged differences)
    int K = 2 + p;

    if (M <= K) {
        return result;
    }

    // 2. Build Dependent Variable Y (Delta y_t) and Design Matrix X
    // Model: Delta y_t = alpha + gamma * y_{t-1} + delta_1 * Delta y_{t-1} + ... + delta_p * Delta y_{t-p} + e_t
    std::vector<double> Y(M);
    std::vector<std::vector<double>> X(M, std::vector<double>(K, 0.0));

    for (int i = 0; i < M; ++i) {
        int t = i + p + 1; // Index in original series (1-based from start)
        Y[i] = dy[i + p];  // Delta y_t

        X[i][0] = 1.0;                  // Constant term alpha
        X[i][1] = series[t - 1];        // Lagged level y_{t-1}

        for (int lag = 0; lag < p; ++lag) {
            X[i][2 + lag] = dy[i + p - 1 - lag]; // Lagged difference Delta y_{t - 1 - lag}
        }
    }

    // 3. Compute (X^T * X) and (X^T * Y)
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

    // 4. Invert (X^T * X)
    std::vector<std::vector<double>> XtX_inv;
    if (!invert_matrix(XtX, XtX_inv, K)) {
        return result; // Linear dependence or singular matrix
    }

    // 5. Solve for OLS beta coefficients: beta = (X^T * X)^(-1) * (X^T * Y)
    std::vector<double> beta(K, 0.0);
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < K; ++j) {
            beta[i] += XtX_inv[i][j] * XtY[j];
        }
    }

    // 6. Compute Residual Sum of Squares (RSS)
    double RSS = 0.0;
    for (int i = 0; i < M; ++i) {
        double y_hat = 0.0;
        for (int j = 0; j < K; ++j) {
            y_hat += X[i][j] * beta[j];
        }
        double residual = Y[i] - y_hat;
        RSS += residual * residual;
    }

    // 7. Calculate Standard Error and t-statistic for gamma (beta[1])
    int df = M - K;
    double variance = RSS / static_cast<double>(df);
    double se_gamma = std::sqrt(variance * XtX_inv[1][1]);

    if (se_gamma < 1e-12) {
        return result;
    }

    double t_stat = beta[1] / se_gamma;
    result.test_statistic = t_stat;

    // Reject null hypothesis if test statistic is strictly less than critical value (more negative)
    result.is_stationary = (t_stat < result.crit_5pct) ? 1 : 0;

    return result;
}
