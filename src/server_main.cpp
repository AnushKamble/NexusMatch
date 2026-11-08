#include "nexusmatch/matching_engine.hpp"
#include "nexusmatch/config.hpp"
#include "nexusmatch/types.hpp"
#include "nexusmatch/event.hpp"
#include "nexusmatch/event_sink.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <sstream>
#include <array>
#include <random>
#include <cstdint>

namespace nexusmatch {

class CollectingSink : public IEventSink {
public:
    std::vector<EngineEvent> events;

    void emit(const EngineEvent& event) noexcept override {
        events.push_back(event);
    }

    void clear() noexcept {
        events.clear();
    }
};

// Minimal JSON value extractor for key: value
static std::string_view extract_string(std::string_view json, std::string_view key) {
    std::string needle = "\"" + std::string(key) + "\"";
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) return {};
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return {};
    if (json[pos] == '"') {
        pos++;
        auto end = json.find('"', pos);
        if (end == std::string_view::npos) return {};
        return json.substr(pos, end - pos);
    } else {
        auto end = json.find_first_of(",} \t\r\n", pos);
        if (end == std::string_view::npos) end = json.size();
        return json.substr(pos, end - pos);
    }
}

static std::uint64_t extract_uint64(std::string_view json, std::string_view key, std::uint64_t default_val = 0) {
    auto val = extract_string(json, key);
    if (val.empty()) return default_val;
    try {
        return std::stoull(std::string(val));
    } catch (...) {
        return default_val;
    }
}

static std::uint32_t extract_uint32(std::string_view json, std::string_view key, std::uint32_t default_val = 0) {
    return static_cast<std::uint32_t>(extract_uint64(json, key, default_val));
}

static Side parse_side(std::string_view side_str) {
    if (side_str == "buy" || side_str == "BUY" || side_str == "b" || side_str == "1") {
        return Side::Buy;
    }
    return Side::Sell;
}

static std::string event_to_json(const EngineEvent& ev) {
    std::ostringstream ss;
    switch (ev.type) {
        case EventType::Trade:
            ss << "{\"type\":\"Trade\","
               << "\"trade_seq\":" << ev.trade.trade_seq << ","
               << "\"maker_order_id\":" << ev.trade.maker_order_id << ","
               << "\"taker_order_id\":" << ev.trade.taker_order_id << ","
               << "\"price_tick\":" << ev.trade.price_tick << ","
               << "\"quantity\":" << ev.trade.quantity << ","
               << "\"maker_side\":\"" << (ev.trade.maker_side == Side::Buy ? "buy" : "sell") << "\"}";
            break;
        case EventType::OrderRested:
            ss << "{\"type\":\"OrderRested\","
               << "\"order_id\":" << ev.order_rested.order_id << ","
               << "\"owner_id\":" << ev.order_rested.owner_id << ","
               << "\"price_tick\":" << ev.order_rested.price_tick << ","
               << "\"remaining_qty\":" << ev.order_rested.remaining_qty << ","
               << "\"side\":\"" << (ev.order_rested.side == Side::Buy ? "buy" : "sell") << "\"}";
            break;
        case EventType::OrderCancelled:
            ss << "{\"type\":\"OrderCancelled\","
               << "\"order_id\":" << ev.order_cancelled.order_id << ","
               << "\"price_tick\":" << ev.order_cancelled.price_tick << ","
               << "\"cancelled_qty\":" << ev.order_cancelled.cancelled_qty << ","
               << "\"side\":\"" << (ev.order_cancelled.side == Side::Buy ? "buy" : "sell") << "\"}";
            break;
        case EventType::CancelRejected:
            ss << "{\"type\":\"CancelRejected\","
               << "\"order_id\":" << ev.cancel_rejected.order_id << ","
               << "\"reason\":\"" << (ev.cancel_rejected.reason == CancelRejectReason::OrderNotFound ? "OrderNotFound" : "ZeroOrderId") << "\"}";
            break;
        case EventType::OrderRejected:
            ss << "{\"type\":\"OrderRejected\","
               << "\"order_id\":" << ev.order_rejected.order_id << ","
               << "\"reason\":\"OrderRejected\"}";
            break;
        case EventType::ResidualCancelled:
            ss << "{\"type\":\"ResidualCancelled\","
               << "\"order_id\":" << ev.residual_cancelled.order_id << ","
               << "\"cancelled_qty\":" << ev.residual_cancelled.cancelled_qty << ","
               << "\"side\":\"" << (ev.residual_cancelled.side == Side::Buy ? "buy" : "sell") << "\"}";
            break;
        case EventType::SelfTradePrevented:
            ss << "{\"type\":\"SelfTradePrevented\","
               << "\"taker_order_id\":" << ev.self_trade_prevented.taker_order_id << ","
               << "\"maker_order_id\":" << ev.self_trade_prevented.maker_order_id << ","
               << "\"cancelled_qty\":" << ev.self_trade_prevented.cancelled_qty << ","
               << "\"side\":\"" << (ev.self_trade_prevented.side == Side::Buy ? "buy" : "sell") << "\"}";
            break;
        case EventType::CapacityExhausted:
            ss << "{\"type\":\"CapacityExhausted\","
               << "\"order_id\":" << ev.capacity_exhausted.order_id << ","
               << "\"unrested_qty\":" << ev.capacity_exhausted.unrested_qty << "}";
            break;
    }
    return ss.str();
}

static std::string build_response(const MatchingEngine& engine,
                                  const std::vector<EngineEvent>& events,
                                  std::uint64_t latency_ns,
                                  std::size_t max_depth = 20) {
    std::ostringstream ss;
    ss << "{\"status\":\"ok\",\"latency_ns\":" << latency_ns << ",";

    // Events
    ss << "\"events\":[";
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (i > 0) ss << ",";
        ss << event_to_json(events[i]);
    }
    ss << "],";

    // BBO
    const auto bbo = engine.bbo();
    ss << "\"bbo\":{";
    ss << "\"bid\":{\"price\":" << (bbo.bid.valid ? bbo.bid.price_tick : 0)
       << ",\"qty\":" << (bbo.bid.valid ? bbo.bid.quantity : 0)
       << ",\"valid\":" << (bbo.bid.valid ? "true" : "false") << "},";
    ss << "\"ask\":{\"price\":" << (bbo.ask.valid ? bbo.ask.price_tick : 0)
       << ",\"qty\":" << (bbo.ask.valid ? bbo.ask.quantity : 0)
       << ",\"valid\":" << (bbo.ask.valid ? "true" : "false") << "},";
    if (bbo.bid.valid && bbo.ask.valid && bbo.ask.price_tick >= bbo.bid.price_tick) {
        ss << "\"spread\":" << (bbo.ask.price_tick - bbo.bid.price_tick);
    } else {
        ss << "\"spread\":0";
    }
    ss << "},";

    // Depth
    std::array<BookLevelSnapshot, 50> bid_levels;
    std::array<BookLevelSnapshot, 50> ask_levels;
    std::size_t n_bids = engine.book().copy_depth_snapshot(Side::Buy, std::span(bid_levels.data(), std::min(max_depth, bid_levels.size())));
    std::size_t n_asks = engine.book().copy_depth_snapshot(Side::Sell, std::span(ask_levels.data(), std::min(max_depth, ask_levels.size())));

    ss << "\"depth\":{\"bids\":[";
    for (std::size_t i = 0; i < n_bids; ++i) {
        if (i > 0) ss << ",";
        ss << "{\"price\":" << bid_levels[i].price_tick
           << ",\"qty\":" << bid_levels[i].total_quantity
           << ",\"orders\":" << bid_levels[i].order_count << "}";
    }
    ss << "],\"asks\":[";
    for (std::size_t i = 0; i < n_asks; ++i) {
        if (i > 0) ss << ",";
        ss << "{\"price\":" << ask_levels[i].price_tick
           << ",\"qty\":" << ask_levels[i].total_quantity
           << ",\"orders\":" << ask_levels[i].order_count << "}";
    }
    ss << "]},";

    // Stats
    const auto& stats = engine.stats();
    ss << "\"stats\":{";
    ss << "\"total_trades\":" << stats.total_trades << ",";
    ss << "\"total_accepted\":" << stats.total_orders_accepted << ",";
    ss << "\"total_rejected\":" << stats.total_orders_rejected << ",";
    ss << "\"total_cancels\":" << stats.total_cancels_processed << ",";
    ss << "\"total_self_trades_prevented\":" << stats.total_self_trades_prevented << ",";
    ss << "\"active_orders\":" << engine.book().active_orders_count() << ",";
    ss << "\"arena_capacity\":" << engine.book().arena().capacity() << ",";
    ss << "\"state_hash\":\"" << engine.state_hash() << "\",";
    ss << "\"invariants_valid\":" << (engine.validate_invariants() ? "true" : "false");
    ss << "}}";

    return ss.str();
}

} // namespace nexusmatch

int main() {
    using namespace nexusmatch;
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    EngineConfig config{
        .price_tick_count = 100000,
        .max_live_orders = 262144, // 256K active orders
        .reject_tick_zero = true
    };

    MatchingEngine engine(config);
    CollectingSink sink;
    std::mt19937_64 rng(1337);
    std::uint64_t auto_order_id = 1000000;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        auto action = extract_string(line, "action");
        sink.clear();

        if (action == "limit") {
            auto id = extract_uint64(line, "order_id");
            auto owner = extract_uint32(line, "owner_id", 1);
            auto side = parse_side(extract_string(line, "side"));
            auto price = extract_uint32(line, "price");
            auto qty = extract_uint32(line, "quantity");

            OrderCommand cmd = OrderCommand::limit(side, id, owner, price, qty);
            auto t0 = std::chrono::high_resolution_clock::now();
            engine.process(cmd, sink);
            auto t1 = std::chrono::high_resolution_clock::now();
            auto lat = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

            std::cout << build_response(engine, sink.events, static_cast<std::uint64_t>(lat)) << "\n" << std::flush;
        } else if (action == "market") {
            auto id = extract_uint64(line, "order_id");
            auto owner = extract_uint32(line, "owner_id", 1);
            auto side = parse_side(extract_string(line, "side"));
            auto qty = extract_uint32(line, "quantity");

            OrderCommand cmd = OrderCommand::market(side, id, owner, qty);
            auto t0 = std::chrono::high_resolution_clock::now();
            engine.process(cmd, sink);
            auto t1 = std::chrono::high_resolution_clock::now();
            auto lat = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

            std::cout << build_response(engine, sink.events, static_cast<std::uint64_t>(lat)) << "\n" << std::flush;
        } else if (action == "cancel") {
            auto id = extract_uint64(line, "order_id");
            OrderCommand cmd = OrderCommand::cancel(id);

            auto t0 = std::chrono::high_resolution_clock::now();
            engine.process(cmd, sink);
            auto t1 = std::chrono::high_resolution_clock::now();
            auto lat = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

            std::cout << build_response(engine, sink.events, static_cast<std::uint64_t>(lat)) << "\n" << std::flush;
        } else if (action == "seed") {
            // Seed a realistic spread around mid_price
            auto mid = extract_uint32(line, "mid_price", 100);
            auto levels = extract_uint32(line, "levels", 8);
            auto qty_base = extract_uint32(line, "qty", 50);

            auto t0 = std::chrono::high_resolution_clock::now();
            for (std::uint32_t i = 1; i <= levels; ++i) {
                // Bid
                if (mid > i) {
                    std::uint32_t bid_price = mid - i;
                    std::uint32_t bid_qty = qty_base + (i * 10);
                    OrderCommand bcmd = OrderCommand::limit(Side::Buy, ++auto_order_id, 1000 + i, bid_price, bid_qty);
                    engine.process(bcmd, sink);
                }
                // Ask
                std::uint32_t ask_price = mid + i;
                std::uint32_t ask_qty = qty_base + (i * 10);
                OrderCommand acmd = OrderCommand::limit(Side::Sell, ++auto_order_id, 2000 + i, ask_price, ask_qty);
                engine.process(acmd, sink);
            }
            auto t1 = std::chrono::high_resolution_clock::now();
            auto lat = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

            std::cout << build_response(engine, sink.events, static_cast<std::uint64_t>(lat)) << "\n" << std::flush;
        } else if (action == "simulate") {
            // Generate synthetic market activity
            auto count = extract_uint32(line, "count", 25);
            auto mid = extract_uint32(line, "mid_price", 100);

            auto t0 = std::chrono::high_resolution_clock::now();
            std::uniform_int_distribution<int> kind_dist(0, 99);
            std::uniform_int_distribution<uint32_t> side_dist(0, 1);
            std::uniform_int_distribution<uint32_t> offset_dist(1, 5);
            std::uniform_int_distribution<uint32_t> qty_dist(5, 50);
            std::uniform_int_distribution<uint32_t> owner_dist(10, 50);

            for (std::uint32_t i = 0; i < count; ++i) {
                int kind = kind_dist(rng);
                Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
                uint32_t owner = owner_dist(rng);
                uint32_t qty = qty_dist(rng);

                if (kind < 70) {
                    // Limit order near mid
                    uint32_t offset = offset_dist(rng);
                    uint32_t price = (side == Side::Buy) ? (mid > offset ? mid - offset : 1) : (mid + offset);
                    OrderCommand cmd = OrderCommand::limit(side, ++auto_order_id, owner, price, qty);
                    engine.process(cmd, sink);
                } else if (kind < 85) {
                    // Market order crossing book
                    OrderCommand cmd = OrderCommand::market(side, ++auto_order_id, owner, qty);
                    engine.process(cmd, sink);
                } else {
                    // Quick cancel of an auto-generated order if any exist
                    if (auto_order_id > 1000005) {
                        uint64_t target_id = auto_order_id - (rng() % 10 + 1);
                        OrderCommand cmd = OrderCommand::cancel(target_id);
                        engine.process(cmd, sink);
                    }
                }
            }
            auto t1 = std::chrono::high_resolution_clock::now();
            auto lat = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

            std::cout << build_response(engine, sink.events, static_cast<std::uint64_t>(lat)) << "\n" << std::flush;
        } else if (action == "reset") {
            // Re-instantiate engine
            engine = MatchingEngine(config);
            auto_order_id = 1000000;
            std::cout << build_response(engine, sink.events, 0) << "\n" << std::flush;
        } else if (action == "snapshot") {
            auto depth = extract_uint32(line, "depth", 20);
            std::cout << build_response(engine, sink.events, 0, depth) << "\n" << std::flush;
        } else if (action == "ping") {
            std::cout << "{\"status\":\"ok\",\"pong\":true}\n" << std::flush;
        } else {
            std::cout << "{\"status\":\"error\",\"message\":\"unknown action\"}\n" << std::flush;
        }
    }

    return 0;
}
