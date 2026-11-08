#pragma once

#include "nexusmatch/types.hpp"
#include "tests/test_utils.hpp"
#include <vector>
#include <cstdint>

namespace nexusmatch::bench {

struct WorkloadConfig {
    std::size_t num_orders{1'000'000};
    std::uint64_t seed{42};
    std::uint32_t price_tick_min{100};
    std::uint32_t price_tick_max{500};
    double limit_ratio{0.60};
    double cancel_ratio{0.30};
    double market_ratio{0.10};
    std::uint32_t max_qty{100};
    std::uint32_t owner_count{100};
};

class WorkloadGenerator {
public:
    [[nodiscard]] static std::vector<OrderCommand> generate_mixed(const WorkloadConfig& cfg) {
        std::vector<OrderCommand> commands;
        commands.reserve(cfg.num_orders);

        test::DeterministicRng rng(cfg.seed);
        std::vector<std::uint64_t> active_ids;
        active_ids.reserve(cfg.num_orders / 2);

        std::uint64_t next_id = 1;

        for (std::size_t i = 0; i < cfg.num_orders; ++i) {
            double r = rng.next_double();
            if (active_ids.empty() || r < cfg.limit_ratio) {
                // Limit Order
                Side side = (rng.next_double() < 0.5) ? Side::Buy : Side::Sell;
                std::uint32_t price = rng.uniform_u32(cfg.price_tick_min, cfg.price_tick_max);
                std::uint32_t qty = rng.uniform_u32(1, cfg.max_qty);
                std::uint32_t owner = rng.uniform_u32(1, cfg.owner_count);
                std::uint64_t id = next_id++;

                commands.push_back(OrderCommand::limit(side, id, owner, price, qty));
                active_ids.push_back(id);
            } else if (r < cfg.limit_ratio + cfg.cancel_ratio) {
                // Cancel Order
                std::size_t idx = rng.uniform_u32(0, static_cast<std::uint32_t>(active_ids.size() - 1));
                std::uint64_t id = active_ids[idx];
                commands.push_back(OrderCommand::cancel(id));

                active_ids[idx] = active_ids.back();
                active_ids.pop_back();
            } else {
                // Market Order
                Side side = (rng.next_double() < 0.5) ? Side::Buy : Side::Sell;
                std::uint32_t qty = rng.uniform_u32(1, cfg.max_qty);
                std::uint32_t owner = rng.uniform_u32(1, cfg.owner_count);
                std::uint64_t id = next_id++;

                commands.push_back(OrderCommand::market(side, id, owner, qty));
            }
        }

        return commands;
    }

    [[nodiscard]] static std::vector<OrderCommand> generate_cancel_heavy(std::size_t count, std::uint64_t seed = 123) {
        WorkloadConfig cfg{
            .num_orders = count,
            .seed = seed,
            .price_tick_min = 200,
            .price_tick_max = 300,
            .limit_ratio = 0.50,
            .cancel_ratio = 0.50,
            .market_ratio = 0.0,
            .max_qty = 50,
            .owner_count = 50
        };
        return generate_mixed(cfg);
    }

    [[nodiscard]] static std::vector<OrderCommand> generate_trade_heavy(std::size_t count, std::uint64_t seed = 456) {
        std::vector<OrderCommand> commands;
        commands.reserve(count);
        test::DeterministicRng rng(seed);

        std::uint64_t next_id = 1;
        // Half resting makers around price 100, half aggressive crosses
        for (std::size_t i = 0; i < count; ++i) {
            if (i < count / 2) {
                // Resting Sell makers at price 100
                commands.push_back(OrderCommand::limit(Side::Sell, next_id++, 1, 100, 10));
            } else {
                // Aggressive crossing Buy takers at price 100
                commands.push_back(OrderCommand::limit(Side::Buy, next_id++, 2, 100, 10));
            }
        }
        return commands;
    }
};

} // namespace nexusmatch::bench
