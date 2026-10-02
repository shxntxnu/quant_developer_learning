#ifndef TC_INDICATORS_CORE_HPP
#define TC_INDICATORS_CORE_HPP

/**
 * @file tc_indicators_core.hpp
 * @brief Header-only, zero-heap, O(1) incremental technical indicators core.
 *
 * Implements circular buffer algorithms for SMA, EMA, RSI, MACD, ATR, Bollinger Bands,
 * and ADX. Designed to be inlined directly into tc_strategy on the hot path for zero-call overhead.
 */

#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <string>
#include <mutex>

#include "tc/tc_types.h"
#include "tc/indicators/tc_indicators.h"

namespace tc {

/**
 * Fixed-capacity circular ring buffer with running sum tracking.
 * Zero dynamic heap allocation.
 */
template <typename T, size_t MaxCapacity>
class CircularRing {
public:
    CircularRing() : head_(0), count_(0), sum_(0.0) {}

    void reset() noexcept {
        head_ = 0;
        count_ = 0;
        sum_ = 0.0;
    }

    void push(T value, size_t active_period) noexcept {
        if (count_ < active_period) {
            buffer_[head_] = value;
            sum_ += value;
            head_ = (head_ + 1) % active_period;
            count_++;
        } else {
            sum_ -= buffer_[head_];
            buffer_[head_] = value;
            sum_ += value;
            head_ = (head_ + 1) % active_period;
        }
    }

    [[nodiscard]] bool is_ready(size_t period) const noexcept {
        return count_ >= period;
    }

    [[nodiscard]] double sum() const noexcept {
        return sum_;
    }

    [[nodiscard]] size_t count() const noexcept {
        return count_;
    }

    [[nodiscard]] const T* data() const noexcept {
        return buffer_.data();
    }

private:
    std::array<T, MaxCapacity> buffer_{};
    size_t head_{0};
    size_t count_{0};
    double sum_{0.0};
};

// -----------------------------------------------------------------------------
// Incremental Simple Moving Average (SMA)
// -----------------------------------------------------------------------------
class IncrementalSMA {
public:
    explicit IncrementalSMA(size_t period = 20) : period_(period) {}

    void reset() noexcept {
        ring_.reset();
        current_val_ = 0.0;
    }

    void update(double price) noexcept {
        ring_.push(price, period_);
        if (ring_.is_ready(period_)) {
            current_val_ = ring_.sum() / static_cast<double>(period_);
        }
    }

    [[nodiscard]] bool is_ready() const noexcept { return ring_.is_ready(period_); }
    [[nodiscard]] double value() const noexcept { return current_val_; }
    [[nodiscard]] size_t period() const noexcept { return period_; }

private:
    size_t period_;
    CircularRing<double, 128> ring_;
    double current_val_{0.0};
};

// -----------------------------------------------------------------------------
// Incremental Exponential Moving Average (EMA)
// -----------------------------------------------------------------------------
class IncrementalEMA {
public:
    explicit IncrementalEMA(size_t period = 20)
        : period_(period),
          alpha_(2.0 / static_cast<double>(period + 1)) {}

    void reset() noexcept {
        count_ = 0;
        sum_init_ = 0.0;
        current_val_ = 0.0;
        initialized_ = false;
    }

    void update(double price) noexcept {
        if (!initialized_) {
            sum_init_ += price;
            count_++;
            if (count_ >= period_) {
                current_val_ = sum_init_ / static_cast<double>(period_);
                initialized_ = true;
            }
        } else {
            current_val_ = (price * alpha_) + (current_val_ * (1.0 - alpha_));
        }
    }

    [[nodiscard]] bool is_ready() const noexcept { return initialized_; }
    [[nodiscard]] double value() const noexcept { return current_val_; }

private:
    size_t period_;
    double alpha_;
    size_t count_{0};
    double sum_init_{0.0};
    double current_val_{0.0};
    bool initialized_{false};
};

// -----------------------------------------------------------------------------
// Incremental Relative Strength Index (RSI - 14 Periods Wilder's Smoothing)
// -----------------------------------------------------------------------------
class IncrementalRSI {
public:
    explicit IncrementalRSI(size_t period = 14) : period_(period) {}

    void reset() noexcept {
        prev_close_ = 0.0;
        count_ = 0;
        avg_gain_ = 0.0;
        avg_loss_ = 0.0;
        current_rsi_ = 50.0;
        initialized_ = false;
        has_prev_ = false;
    }

    void update(double close) noexcept {
        if (!has_prev_) {
            prev_close_ = close;
            has_prev_ = true;
            return;
        }

        const double change = close - prev_close_;
        prev_close_ = close;
        const double gain = change > 0.0 ? change : 0.0;
        const double loss = change < 0.0 ? -change : 0.0;

        if (!initialized_) {
            avg_gain_ += gain;
            avg_loss_ += loss;
            count_++;
            if (count_ >= period_) {
                avg_gain_ /= static_cast<double>(period_);
                avg_loss_ /= static_cast<double>(period_);
                calculate_rsi();
                initialized_ = true;
            }
        } else {
            // Wilder's smoothing
            const double p = static_cast<double>(period_);
            avg_gain_ = ((avg_gain_ * (p - 1.0)) + gain) / p;
            avg_loss_ = ((avg_loss_ * (p - 1.0)) + loss) / p;
            calculate_rsi();
        }
    }

    [[nodiscard]] bool is_ready() const noexcept { return initialized_; }
    [[nodiscard]] double value() const noexcept { return current_rsi_; }

private:
    void calculate_rsi() noexcept {
        if (avg_loss_ <= 1e-12) {
            current_rsi_ = 100.0;
        } else {
            const double rs = avg_gain_ / avg_loss_;
            current_rsi_ = 100.0 - (100.0 / (1.0 + rs));
        }
    }

    size_t period_;
    double prev_close_{0.0};
    size_t count_{0};
    double avg_gain_{0.0};
    double avg_loss_{0.0};
    double current_rsi_{50.0};
    bool initialized_{false};
    bool has_prev_{false};
};

// -----------------------------------------------------------------------------
// Incremental Moving Average Convergence Divergence (MACD 12, 26, 9)
// -----------------------------------------------------------------------------
class IncrementalMACD {
public:
    IncrementalMACD(size_t fast_period = 12, size_t slow_period = 26, size_t signal_period = 9)
        : ema_fast_(fast_period),
          ema_slow_(slow_period),
          ema_signal_(signal_period) {}

    void reset() noexcept {
        ema_fast_.reset();
        ema_slow_.reset();
        ema_signal_.reset();
        macd_line_ = 0.0;
        signal_line_ = 0.0;
        histogram_ = 0.0;
    }

    void update(double close) noexcept {
        ema_fast_.update(close);
        ema_slow_.update(close);

        if (ema_slow_.is_ready()) {
            macd_line_ = ema_fast_.value() - ema_slow_.value();
            ema_signal_.update(macd_line_);

            if (ema_signal_.is_ready()) {
                signal_line_ = ema_signal_.value();
                histogram_ = macd_line_ - signal_line_;
            }
        }
    }

    [[nodiscard]] bool is_ready() const noexcept { return ema_signal_.is_ready(); }
    [[nodiscard]] double macd() const noexcept { return macd_line_; }
    [[nodiscard]] double signal() const noexcept { return signal_line_; }
    [[nodiscard]] double hist() const noexcept { return histogram_; }

private:
    IncrementalEMA ema_fast_;
    IncrementalEMA ema_slow_;
    IncrementalEMA ema_signal_;
    double macd_line_{0.0};
    double signal_line_{0.0};
    double histogram_{0.0};
};

// -----------------------------------------------------------------------------
// Incremental Average True Range (ATR - 14 Periods Wilder's Smoothing)
// -----------------------------------------------------------------------------
class IncrementalATR {
public:
    explicit IncrementalATR(size_t period = 14) : period_(period) {}

    void reset() noexcept {
        prev_close_ = 0.0;
        count_ = 0;
        sum_tr_ = 0.0;
        current_atr_ = 0.0;
        initialized_ = false;
        has_prev_ = false;
    }

    void update(double high, double low, double close) noexcept {
        double tr = high - low;
        if (has_prev_) {
            const double c1 = std::abs(high - prev_close_);
            const double c2 = std::abs(low - prev_close_);
            tr = std::max({tr, c1, c2});
        }
        prev_close_ = close;
        has_prev_ = true;

        if (!initialized_) {
            sum_tr_ += tr;
            count_++;
            if (count_ >= period_) {
                current_atr_ = sum_tr_ / static_cast<double>(period_);
                initialized_ = true;
            }
        } else {
            const double p = static_cast<double>(period_);
            current_atr_ = ((current_atr_ * (p - 1.0)) + tr) / p;
        }
    }

    [[nodiscard]] bool is_ready() const noexcept { return initialized_; }
    [[nodiscard]] double value() const noexcept { return current_atr_; }

private:
    size_t period_;
    double prev_close_{0.0};
    size_t count_{0};
    double sum_tr_{0.0};
    double current_atr_{0.0};
    bool initialized_{false};
    bool has_prev_{false};
};

// -----------------------------------------------------------------------------
// Incremental Bollinger Bands (20 Periods, +/- 2.0 Std Dev)
// -----------------------------------------------------------------------------
class IncrementalBollinger {
public:
    IncrementalBollinger(size_t period = 20, double num_std_dev = 2.0)
        : period_(period), num_std_dev_(num_std_dev) {}

    void reset() noexcept {
        ring_.reset();
        ring_sq_.reset();
        middle_ = 0.0;
        upper_ = 0.0;
        lower_ = 0.0;
    }

    void update(double close) noexcept {
        ring_.push(close, period_);
        ring_sq_.push(close * close, period_);

        if (ring_.is_ready(period_)) {
            const double n = static_cast<double>(period_);
            middle_ = ring_.sum() / n;
            const double mean_sq = ring_sq_.sum() / n;
            const double variance = std::max(0.0, mean_sq - (middle_ * middle_));
            const double std_dev = std::sqrt(variance);

            upper_ = middle_ + (num_std_dev_ * std_dev);
            lower_ = middle_ - (num_std_dev_ * std_dev);
        }
    }

    [[nodiscard]] bool is_ready() const noexcept { return ring_.is_ready(period_); }
    [[nodiscard]] double upper() const noexcept { return upper_; }
    [[nodiscard]] double middle() const noexcept { return middle_; }
    [[nodiscard]] double lower() const noexcept { return lower_; }

private:
    size_t period_;
    double num_std_dev_;
    CircularRing<double, 64> ring_;
    CircularRing<double, 64> ring_sq_;
    double middle_{0.0};
    double upper_{0.0};
    double lower_{0.0};
};

// -----------------------------------------------------------------------------
// Incremental Average Directional Index (ADX - 14 Periods)
// -----------------------------------------------------------------------------
class IncrementalADX {
public:
    explicit IncrementalADX(size_t period = 14) : period_(period) {}

    void reset() noexcept {
        prev_high_ = 0.0;
        prev_low_ = 0.0;
        prev_close_ = 0.0;
        count_ = 0;
        adx_count_ = 0;
        smoothed_tr_ = 0.0;
        smoothed_plus_dm_ = 0.0;
        smoothed_minus_dm_ = 0.0;
        current_adx_ = 0.0;
        adx_init_sum_ = 0.0;
        has_prev_ = false;
        dm_ready_ = false;
        adx_ready_ = false;
    }

    void update(double high, double low, double close) noexcept {
        if (!has_prev_) {
            prev_high_ = high;
            prev_low_ = low;
            prev_close_ = close;
            has_prev_ = true;
            return;
        }

        // 1. True Range
        const double tr1 = high - low;
        const double tr2 = std::abs(high - prev_close_);
        const double tr3 = std::abs(low - prev_close_);
        const double tr = std::max({tr1, tr2, tr3});

        // 2. Directional Movement
        const double up_move = high - prev_high_;
        const double down_move = prev_low_ - low;
        double plus_dm = 0.0;
        double minus_dm = 0.0;

        if (up_move > down_move && up_move > 0.0) {
            plus_dm = up_move;
        }
        if (down_move > up_move && down_move > 0.0) {
            minus_dm = down_move;
        }

        prev_high_ = high;
        prev_low_ = low;
        prev_close_ = close;

        const double p = static_cast<double>(period_);

        if (!dm_ready_) {
            smoothed_tr_ += tr;
            smoothed_plus_dm_ += plus_dm;
            smoothed_minus_dm_ += minus_dm;
            count_++;

            if (count_ >= period_) {
                dm_ready_ = true;
                calculate_dx();
            }
        } else {
            smoothed_tr_ = smoothed_tr_ - (smoothed_tr_ / p) + tr;
            smoothed_plus_dm_ = smoothed_plus_dm_ - (smoothed_plus_dm_ / p) + plus_dm;
            smoothed_minus_dm_ = smoothed_minus_dm_ - (smoothed_minus_dm_ / p) + minus_dm;
            calculate_dx();
        }
    }

    [[nodiscard]] bool is_ready() const noexcept { return adx_ready_; }
    [[nodiscard]] double value() const noexcept { return current_adx_; }

private:
    void calculate_dx() noexcept {
        if (smoothed_tr_ <= 1e-12) return;

        const double plus_di = 100.0 * (smoothed_plus_dm_ / smoothed_tr_);
        const double minus_di = 100.0 * (smoothed_minus_dm_ / smoothed_tr_);
        const double di_sum = plus_di + minus_di;

        double dx = 0.0;
        if (di_sum > 1e-12) {
            dx = 100.0 * (std::abs(plus_di - minus_di) / di_sum);
        }

        if (!adx_ready_) {
            adx_init_sum_ += dx;
            adx_count_++;
            if (adx_count_ >= period_) {
                current_adx_ = adx_init_sum_ / static_cast<double>(period_);
                adx_ready_ = true;
            }
        } else {
            const double p = static_cast<double>(period_);
            current_adx_ = ((current_adx_ * (p - 1.0)) + dx) / p;
        }
    }

    size_t period_;
    double prev_high_{0.0};
    double prev_low_{0.0};
    double prev_close_{0.0};
    size_t count_{0};
    size_t adx_count_{0};
    double smoothed_tr_{0.0};
    double smoothed_plus_dm_{0.0};
    double smoothed_minus_dm_{0.0};
    double current_adx_{0.0};
    double adx_init_sum_{0.0};
    bool has_prev_{false};
    bool dm_ready_{false};
    bool adx_ready_{false};
};

// -----------------------------------------------------------------------------
// Composite Symbol Indicators Manager
// -----------------------------------------------------------------------------
class SymbolIndicatorSet {
public:
    explicit SymbolIndicatorSet(const TcIndicatorSpec& spec)
        : sma_fast_(spec.sma_fast_period > 0 ? spec.sma_fast_period : 20),
          sma_slow_(spec.sma_slow_period > 0 ? spec.sma_slow_period : 50),
          ema_(spec.ema_period > 0 ? spec.ema_period : 20),
          rsi_(spec.rsi_period > 0 ? spec.rsi_period : 14),
          macd_(spec.macd_fast_period > 0 ? spec.macd_fast_period : 12,
                spec.macd_slow_period > 0 ? spec.macd_slow_period : 26,
                spec.macd_signal_period > 0 ? spec.macd_signal_period : 9),
          atr_(spec.atr_period > 0 ? spec.atr_period : 14),
          bb_(spec.bb_period > 0 ? spec.bb_period : 20,
              spec.bb_std_dev > 0.0 ? spec.bb_std_dev : 2.0),
          adx_(spec.adx_period > 0 ? spec.adx_period : 14) {}

    void reset() noexcept {
        sma_fast_.reset();
        sma_slow_.reset();
        ema_.reset();
        rsi_.reset();
        macd_.reset();
        atr_.reset();
        bb_.reset();
        adx_.reset();
        bars_received_ = 0;
    }

    void update_bar(const TcBar& bar) noexcept {
        const double high = TC_PRICE_TO_DOUBLE(bar.high);
        const double low = TC_PRICE_TO_DOUBLE(bar.low);
        const double close = TC_PRICE_TO_DOUBLE(bar.close);

        sma_fast_.update(close);
        sma_slow_.update(close);
        ema_.update(close);
        rsi_.update(close);
        macd_.update(close);
        atr_.update(high, low, close);
        bb_.update(close);
        adx_.update(high, low, close);

        last_ts_ns_ = bar.ts_ns;
        bars_received_++;
    }

    void get_snapshot(const char* symbol, TcIndicatorSnapshot& out) const noexcept {
        out.struct_size = sizeof(TcIndicatorSnapshot);
        out.version = 1;
        out.reserved = 0;
        std::strncpy(out.symbol, symbol, sizeof(out.symbol) - 1);
        out.symbol[sizeof(out.symbol) - 1] = '\0';
        out.ts_ns = last_ts_ns_;
        out.valid_mask = 0;
        out.flags = 0;

        if (sma_fast_.is_ready()) {
            out.sma_fast = sma_fast_.value();
            out.valid_mask |= TC_IND_MASK_SMA_FAST;
        } else {
            out.sma_fast = 0.0;
        }

        if (sma_slow_.is_ready()) {
            out.sma_slow = sma_slow_.value();
            out.valid_mask |= TC_IND_MASK_SMA_SLOW;
        } else {
            out.sma_slow = 0.0;
        }

        if (ema_.is_ready()) {
            out.ema = ema_.value();
            out.valid_mask |= TC_IND_MASK_EMA;
        } else {
            out.ema = 0.0;
        }

        if (rsi_.is_ready()) {
            out.rsi = rsi_.value();
            out.valid_mask |= TC_IND_MASK_RSI;
        } else {
            out.rsi = 50.0;
        }

        if (macd_.is_ready()) {
            out.macd = macd_.macd();
            out.macd_signal = macd_.signal();
            out.macd_hist = macd_.hist();
            out.valid_mask |= TC_IND_MASK_MACD;
        } else {
            out.macd = 0.0;
            out.macd_signal = 0.0;
            out.macd_hist = 0.0;
        }

        if (atr_.is_ready()) {
            out.atr = atr_.value();
            out.valid_mask |= TC_IND_MASK_ATR;
        } else {
            out.atr = 0.0;
        }

        if (bb_.is_ready()) {
            out.bb_upper = bb_.upper();
            out.bb_middle = bb_.middle();
            out.bb_lower = bb_.lower();
            out.valid_mask |= TC_IND_MASK_BB;
        } else {
            out.bb_upper = 0.0;
            out.bb_middle = 0.0;
            out.bb_lower = 0.0;
        }

        if (adx_.is_ready()) {
            out.adx = adx_.value();
            out.valid_mask |= TC_IND_MASK_ADX;
        } else {
            out.adx = 0.0;
        }
    }

    [[nodiscard]] size_t bars_received() const noexcept { return bars_received_; }

private:
    IncrementalSMA sma_fast_;
    IncrementalSMA sma_slow_;
    IncrementalEMA ema_;
    IncrementalRSI rsi_;
    IncrementalMACD macd_;
    IncrementalATR atr_;
    IncrementalBollinger bb_;
    IncrementalADX adx_;

    int64_t last_ts_ns_{0};
    size_t bars_received_{0};
};

// -----------------------------------------------------------------------------
// High-Level Thread-Safe Indicators Engine
// -----------------------------------------------------------------------------
class IndicatorEngine {
public:
    explicit IndicatorEngine(TcIndicatorSpec spec = default_spec())
        : spec_(spec) {}

    static TcIndicatorSpec default_spec() {
        TcIndicatorSpec s{};
        s.struct_size = sizeof(TcIndicatorSpec);
        s.version = 1;
        s.sma_fast_period = 20;
        s.sma_slow_period = 50;
        s.ema_period = 20;
        s.rsi_period = 14;
        s.macd_fast_period = 12;
        s.macd_slow_period = 26;
        s.macd_signal_period = 9;
        s.atr_period = 14;
        s.bb_period = 20;
        s.bb_std_dev = 2.0;
        s.adx_period = 14;
        return s;
    }

    TcStatus update_bar(const TcBar& bar) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string sym(bar.symbol);
        auto it = symbols_.find(sym);
        if (it == symbols_.end()) {
            it = symbols_.emplace(sym, SymbolIndicatorSet(spec_)).first;
        }
        it->second.update_bar(bar);
        total_bars_processed_++;
        return TC_OK;
    }

    TcStatus get_snapshot(const char* symbol, TcIndicatorSnapshot& out) const {
        if (!symbol) return TC_ERR_INVALID_ARG;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = symbols_.find(symbol);
        if (it == symbols_.end()) {
            return TC_ERR_NOT_FOUND;
        }

        it->second.get_snapshot(symbol, out);
        return TC_OK;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [_, set] : symbols_) {
            set.reset();
        }
        total_bars_processed_ = 0;
    }

    [[nodiscard]] uint32_t warmup_bars_required() const noexcept {
        // Slowest indicator warmup: Slow SMA (50) or ADX (14 + 14 = 28)
        return std::max<uint32_t>(50, spec_.sma_slow_period);
    }

    [[nodiscard]] uint64_t total_bars_processed() const noexcept {
        return total_bars_processed_;
    }

private:
    TcIndicatorSpec spec_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SymbolIndicatorSet> symbols_;
    uint64_t total_bars_processed_{0};
};

} // namespace tc

#endif /* TC_INDICATORS_CORE_HPP */
