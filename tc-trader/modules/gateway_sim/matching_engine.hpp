/**
 * @file matching_engine.hpp
 * @brief High-performance in-memory simulation matching engine.
 *
 * Simulates an exchange matching engine with zero heap allocations on the hot path.
 * Evaluates market, limit, stop, and bracket/OCA orders against incoming bar OHLCV extremes,
 * applying realistic slippage models and exchange commission schedules.
 */

#ifndef TC_GATEWAY_MATCHING_ENGINE_HPP
#define TC_GATEWAY_MATCHING_ENGINE_HPP

#include "tc/tc_types.h"
#include "tc/tc_abi.h"
#include "tc/gateway/tc_gateway.h"

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <array>
#include <mutex>

namespace tc::gateway {

inline void copy_symbol(char* dest, size_t dest_size, const char* src) noexcept {
    if (!dest || dest_size == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t len = std::strlen(src);
    if (len >= dest_size) len = dest_size - 1;
    std::memcpy(dest, src, len);
    dest[len] = '\0';
}

struct SimOrderSlot {
    TcOrder  order{};
    bool     active{false};
    int64_t  filled_qty{0};
    TcPrice  avg_fill_px{0};
    int64_t  placement_ts_ns{0};
};

class MatchingEngine {
public:
    static constexpr size_t MAX_WORKING_ORDERS = 1024;

    MatchingEngine() noexcept {
        reset();
    }

    void set_config(const TcGatewayConfig& config) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
    }

    void set_order_event_sink(TcOrderEventSink sink, void* user_data) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        order_event_sink_ = sink;
        order_event_user_data_ = user_data;
    }

    void set_fill_sink(TcFillSink sink, void* user_data) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        fill_sink_ = sink;
        fill_user_data_ = user_data;
    }

    TcStatus place_order(const TcOrder* order, uint64_t* out_client_order_id, int64_t current_ts_ns) noexcept {
        if (!order) return TC_ERR_INVALID_ARG;
        if (order->qty <= 0 || order->symbol[0] == '\0') {
            emit_order_event(order->client_order_id, 0, order->symbol,
                             TC_ORD_REJECTED, 0, order->qty, 0, 0.0, current_ts_ns, 1001);
            stats_.orders_rejected++;
            return TC_ERR_INVALID_ARG;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Find empty slot
        int free_idx = -1;
        for (size_t i = 0; i < MAX_WORKING_ORDERS; ++i) {
            if (!orders_[i].active) {
                free_idx = static_cast<int>(i);
                break;
            }
        }

        if (free_idx < 0) {
            emit_order_event(order->client_order_id, 0, order->symbol,
                             TC_ORD_REJECTED, 0, order->qty, 0, 0.0, current_ts_ns, 1002);
            stats_.orders_rejected++;
            return TC_ERR_QUEUE_FULL;
        }

        SimOrderSlot& slot = orders_[free_idx];
        slot.order = *order;
        slot.active = true;
        slot.filled_qty = 0;
        slot.avg_fill_px = 0;
        slot.placement_ts_ns = current_ts_ns;

        if (out_client_order_id) {
            *out_client_order_id = order->client_order_id;
        }

        stats_.orders_placed++;

        // Emit ACK event
        emit_order_event(order->client_order_id, static_cast<int64_t>(order->client_order_id),
                         order->symbol, TC_ORD_ACK, 0, order->qty, 0, 0.0, current_ts_ns, 0);

        return TC_OK;
    }

    TcStatus cancel_order(uint64_t client_order_id, int64_t current_ts_ns) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);

        for (size_t i = 0; i < MAX_WORKING_ORDERS; ++i) {
            if (orders_[i].active && orders_[i].order.client_order_id == client_order_id) {
                orders_[i].active = false;
                int64_t rem = orders_[i].order.qty - orders_[i].filled_qty;
                emit_order_event(client_order_id, static_cast<int64_t>(client_order_id),
                                 orders_[i].order.symbol, TC_ORD_CANCELLED,
                                 orders_[i].filled_qty, rem,
                                 orders_[i].avg_fill_px, 0.0, current_ts_ns, 0);
                stats_.orders_cancelled++;
                return TC_OK;
            }
        }
        return TC_ERR_NOT_FOUND;
    }

    TcStatus cancel_all_orders(const char* symbol, int64_t current_ts_ns) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);

        for (size_t i = 0; i < MAX_WORKING_ORDERS; ++i) {
            if (orders_[i].active) {
                if (!symbol || symbol[0] == '\0' || std::strcmp(orders_[i].order.symbol, symbol) == 0) {
                    orders_[i].active = false;
                    int64_t rem = orders_[i].order.qty - orders_[i].filled_qty;
                    emit_order_event(orders_[i].order.client_order_id,
                                     static_cast<int64_t>(orders_[i].order.client_order_id),
                                     orders_[i].order.symbol, TC_ORD_CANCELLED,
                                     orders_[i].filled_qty, rem,
                                     orders_[i].avg_fill_px, 0.0, current_ts_ns, 0);
                    stats_.orders_cancelled++;
                }
            }
        }
        return TC_OK;
    }

    /**
     * @brief Match working orders against current bar prices.
     */
    void process_bar(const TcBar& bar) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);

        for (size_t i = 0; i < MAX_WORKING_ORDERS; ++i) {
            if (!orders_[i].active) continue;
            if (std::strcmp(orders_[i].order.symbol, bar.symbol) != 0) continue;

            TcOrder& ord = orders_[i].order;
            bool matched = false;
            TcPrice exec_px = 0;

            TcPrice slippage = static_cast<TcPrice>(bar.open * config_.slippage_pct);

            switch (ord.order_type) {
                case TC_GW_ORDER_MKT: {
                    matched = true;
                    if (ord.side > 0) { // BUY
                        exec_px = bar.open + slippage;
                    } else { // SELL
                        exec_px = (bar.open > slippage) ? (bar.open - slippage) : bar.open;
                    }
                    break;
                }

                case TC_GW_ORDER_LMT: {
                    if (ord.side > 0) { // BUY limit
                        if (bar.low <= ord.limit_px) {
                            matched = true;
                            exec_px = std::min(bar.open, ord.limit_px);
                        }
                    } else { // SELL limit
                        if (bar.high >= ord.limit_px) {
                            matched = true;
                            exec_px = std::max(bar.open, ord.limit_px);
                        }
                    }
                    break;
                }

                case TC_GW_ORDER_STP: {
                    if (ord.side > 0) { // BUY stop
                        if (bar.high >= ord.stop_px) {
                            matched = true;
                            exec_px = std::max(bar.open, ord.stop_px) + slippage;
                        }
                    } else { // SELL stop
                        if (bar.low <= ord.stop_px) {
                            matched = true;
                            TcPrice base_px = std::min(bar.open, ord.stop_px);
                            exec_px = (base_px > slippage) ? (base_px - slippage) : base_px;
                        }
                    }
                    break;
                }

                case TC_GW_ORDER_STP_LMT: {
                    if (ord.side > 0) { // BUY stop limit
                        if (bar.high >= ord.stop_px && bar.low <= ord.limit_px) {
                            matched = true;
                            exec_px = std::min(bar.open, ord.limit_px);
                        }
                    } else { // SELL stop limit
                        if (bar.low <= ord.stop_px && bar.high >= ord.limit_px) {
                            matched = true;
                            exec_px = std::max(bar.open, ord.limit_px);
                        }
                    }
                    break;
                }

                default:
                    break;
            }

            if (matched) {
                execute_fill(i, bar, exec_px);
            }
        }
    }

    void get_stats(TcGatewayStats* out_stats) const noexcept {
        if (!out_stats) return;
        std::lock_guard<std::mutex> lock(mutex_);
        *out_stats = stats_;
    }

    void reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& slot : orders_) {
            slot.active = false;
            slot.filled_qty = 0;
            slot.avg_fill_px = 0;
            slot.placement_ts_ns = 0;
        }

        std::memset(&stats_, 0, sizeof(stats_));
        stats_.struct_size = sizeof(TcGatewayStats);
        stats_.version = 1;
    }

private:
    void execute_fill(size_t slot_idx, const TcBar& bar, TcPrice exec_px) noexcept {
        SimOrderSlot& slot = orders_[slot_idx];
        int64_t fill_qty = slot.order.qty - slot.filled_qty;
        if (fill_qty <= 0) {
            slot.active = false;
            return;
        }

        double commission = std::max(config_.min_commission,
                                     static_cast<double>(fill_qty) * config_.commission_per_share);

        slot.filled_qty += fill_qty;
        slot.avg_fill_px = exec_px;
        slot.active = false; // Completely filled

        stats_.orders_filled++;
        stats_.total_commissions += commission;

        // Calculate slippage PnL impact relative to bar.open
        int64_t slippage_delta = (slot.order.side > 0) ? (exec_px - bar.open) : (bar.open - exec_px);
        stats_.total_slippage_pnl += slippage_delta * fill_qty;

        // Emit TcFill
        if (fill_sink_) {
            TcFill fill{};
            fill.struct_size = sizeof(TcFill);
            fill.version = 1;
            fill.side = slot.order.side;
            fill.reserved = 0;
            copy_symbol(fill.symbol, sizeof(fill.symbol), slot.order.symbol);
            fill.ts_ns = bar.ts_ns;
            fill.client_order_id = slot.order.client_order_id;
            fill.fill_qty = fill_qty;
            fill.fill_px = exec_px;
            fill.commission = commission;

            fill_sink_(&fill, fill_user_data_);
        }

        // Emit TcOrderEvent (FILLED)
        emit_order_event(slot.order.client_order_id,
                         static_cast<int64_t>(slot.order.client_order_id),
                         slot.order.symbol,
                         TC_ORD_FILLED,
                         slot.filled_qty,
                         0,
                         slot.avg_fill_px,
                         commission,
                         bar.ts_ns,
                         0);

        // One-Cancels-All (OCA) bracket logic:
        // If this order has a parent_order_id, cancel sibling orders sharing the same parent_order_id
        if (slot.order.parent_order_id != 0) {
            uint64_t parent_id = slot.order.parent_order_id;
            uint64_t filled_id = slot.order.client_order_id;

            for (size_t j = 0; j < MAX_WORKING_ORDERS; ++j) {
                if (orders_[j].active &&
                    orders_[j].order.parent_order_id == parent_id &&
                    orders_[j].order.client_order_id != filled_id) {
                    
                    orders_[j].active = false;
                    int64_t rem = orders_[j].order.qty - orders_[j].filled_qty;
                    emit_order_event(orders_[j].order.client_order_id,
                                     static_cast<int64_t>(orders_[j].order.client_order_id),
                                     orders_[j].order.symbol,
                                     TC_ORD_CANCELLED,
                                     orders_[j].filled_qty,
                                     rem,
                                     orders_[j].avg_fill_px,
                                     0.0,
                                     bar.ts_ns,
                                     0);
                    stats_.orders_cancelled++;
                }
            }
        }
    }

    void emit_order_event(uint64_t client_order_id, int64_t broker_order_id,
                          const char* symbol, TcOrderStatus status,
                          int64_t filled_qty, int64_t rem_qty,
                          TcPrice avg_fill_px, double commission,
                          int64_t ts_ns, int32_t error_code) noexcept {
        if (!order_event_sink_) return;

        TcOrderEvent evt{};
        evt.struct_size = sizeof(TcOrderEvent);
        evt.version = 1;
        evt.status = static_cast<uint8_t>(status);
        evt.reserved1 = 0;
        copy_symbol(evt.symbol, sizeof(evt.symbol), symbol);
        evt.ts_ns = ts_ns;
        evt.client_order_id = client_order_id;
        evt.broker_order_id = broker_order_id;
        evt.filled_qty = filled_qty;
        evt.remaining_qty = rem_qty;
        evt.avg_fill_px = avg_fill_px;
        evt.commission = commission;
        evt.error_code = error_code;
        evt.reserved2 = 0;

        order_event_sink_(&evt, order_event_user_data_);
    }

    mutable std::mutex mutex_;
    TcGatewayConfig config_{};
    TcGatewayStats  stats_{};

    TcOrderEventSink order_event_sink_{nullptr};
    void*            order_event_user_data_{nullptr};

    TcFillSink       fill_sink_{nullptr};
    void*            fill_user_data_{nullptr};

    std::array<SimOrderSlot, MAX_WORKING_ORDERS> orders_{};
};

} // namespace tc::gateway

#endif /* TC_GATEWAY_MATCHING_ENGINE_HPP */
