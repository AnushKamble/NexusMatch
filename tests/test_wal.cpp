#include "nexusmatch/command_wal.hpp"
#include "nexusmatch/matching_engine.hpp"
#include "tests/test_utils.hpp"
#include <iostream>
#include <filesystem>

using namespace nexusmatch;
using namespace nexusmatch::test;

void test_wal_roundtrip() {
    const std::string wal_path = "test_wal_roundtrip.wal";
    std::filesystem::remove(wal_path);

    std::vector<OrderCommand> original_cmds = {
        OrderCommand::limit(Side::Buy, 101, 1, 100, 50),
        OrderCommand::limit(Side::Sell, 102, 2, 105, 30),
        OrderCommand::market(Side::Buy, 103, 3, 20),
        OrderCommand::cancel(101)
    };

    // Write WAL
    {
        CommandWalWriter writer(wal_path);
        for (const auto& cmd : original_cmds) {
            TEST_ASSERT(writer.append(cmd));
        }
        writer.flush();
    }

    // Read WAL
    {
        CommandWalReader reader(wal_path);
        TEST_ASSERT(reader.open_and_validate_header() == WalReadStatus::Success);

        WalRecord rec;
        std::size_t count = 0;
        while (reader.read_next(rec) == WalReadStatus::Success) {
            TEST_ASSERT(rec.command_seq == count + 1);
            const auto& orig = original_cmds[count];
            TEST_ASSERT(rec.command.kind == orig.kind);
            TEST_ASSERT(rec.command.side == orig.side);
            TEST_ASSERT(rec.command.order_id == orig.order_id);
            TEST_ASSERT(rec.command.owner_id == orig.owner_id);
            TEST_ASSERT(rec.command.price_tick == orig.price_tick);
            TEST_ASSERT(rec.command.quantity == orig.quantity);
            ++count;
        }
        TEST_ASSERT(count == original_cmds.size());
    }

    std::filesystem::remove(wal_path);
}

void test_wal_corruption_and_truncation() {
    const std::string wal_path = "test_wal_corrupt.wal";
    std::filesystem::remove(wal_path);

    // 1. Invalid Magic
    {
        std::ofstream f(wal_path, std::ios::binary);
        std::uint32_t bad_magic = 0xDEADBEEF;
        f.write(reinterpret_cast<const char*>(&bad_magic), 4);
        char pad[12]{};
        f.write(pad, 12);
    }
    {
        CommandWalReader reader(wal_path);
        TEST_ASSERT(reader.open_and_validate_header() == WalReadStatus::InvalidMagic);
    }

    // 2. Truncated Header (< 16 bytes)
    {
        std::ofstream f(wal_path, std::ios::binary | std::ios::trunc);
        std::uint32_t magic = kWaLMagic;
        f.write(reinterpret_cast<const char*>(&magic), 4);
    }
    {
        CommandWalReader reader(wal_path);
        TEST_ASSERT(reader.open_and_validate_header() == WalReadStatus::TruncatedHeader);
    }

    // 3. Checksum Corruption
    {
        CommandWalWriter writer(wal_path);
        writer.append(OrderCommand::limit(Side::Buy, 101, 1, 100, 50));
        writer.flush();
    }
    // Flip a byte in the payload
    {
        std::fstream f(wal_path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(20); // inside record payload
        char b = 0;
        f.read(&b, 1);
        b = static_cast<char>(b ^ 0x55); // corrupt
        f.seekp(20);
        f.write(&b, 1);
    }
    {
        CommandWalReader reader(wal_path);
        TEST_ASSERT(reader.open_and_validate_header() == WalReadStatus::Success);
        WalRecord rec;
        TEST_ASSERT(reader.read_next(rec) == WalReadStatus::ChecksumMismatch);
    }

    std::filesystem::remove(wal_path);
}

void test_wal_replay_equivalence() {
    const std::string wal_path = "test_wal_replay.wal";
    std::filesystem::remove(wal_path);

    EngineConfig config{
        .price_tick_count = 1000,
        .max_live_orders = 5000,
        .reject_tick_zero = true
    };

    MatchingEngine live_engine(config);
    VectorEventSink live_sink;

    CommandWalWriter writer(wal_path);

    DeterministicRng rng(8888);
    std::vector<std::uint64_t> active_ids;
    std::uint64_t next_id = 1;

    for (int i = 0; i < 2000; ++i) {
        OrderCommand cmd{};
        double r = rng.next_double();
        if (r < 0.6 || active_ids.empty()) {
            Side side = (rng.next_double() < 0.5) ? Side::Buy : Side::Sell;
            std::uint32_t price = rng.uniform_u32(1, 999);
            std::uint32_t qty = rng.uniform_u32(1, 100);
            std::uint32_t owner = rng.uniform_u32(1, 10);
            std::uint64_t id = next_id++;
            cmd = OrderCommand::limit(side, id, owner, price, qty);
            active_ids.push_back(id);
        } else if (r < 0.85) {
            std::size_t idx = rng.uniform_u32(0, static_cast<std::uint32_t>(active_ids.size() - 1));
            std::uint64_t id = active_ids[idx];
            cmd = OrderCommand::cancel(id);
            active_ids[idx] = active_ids.back();
            active_ids.pop_back();
        } else {
            Side side = (rng.next_double() < 0.5) ? Side::Buy : Side::Sell;
            std::uint32_t qty = rng.uniform_u32(1, 150);
            std::uint32_t owner = rng.uniform_u32(1, 10);
            std::uint64_t id = next_id++;
            cmd = OrderCommand::market(side, id, owner, qty);
        }

        writer.append(cmd);
        live_engine.process(cmd, live_sink);
    }
    writer.flush();

    // Replay from WAL into fresh engine
    MatchingEngine replay_engine(config);
    VectorEventSink replay_sink;

    CommandWalReader reader(wal_path);
    TEST_ASSERT(reader.open_and_validate_header() == WalReadStatus::Success);

    WalRecord rec;
    while (reader.read_next(rec) == WalReadStatus::Success) {
        replay_engine.process(rec.command, replay_sink);
    }

    // Compare events
    TEST_ASSERT(live_sink.events.size() == replay_sink.events.size());
    for (std::size_t i = 0; i < live_sink.events.size(); ++i) {
        TEST_ASSERT(events_equal(live_sink.events[i], replay_sink.events[i]));
    }

    // Compare BBO
    Bbo lb = live_engine.bbo();
    Bbo rb = replay_engine.bbo();
    TEST_ASSERT(lb.bid.valid == rb.bid.valid);
    TEST_ASSERT(lb.ask.valid == rb.ask.valid);
    if (lb.bid.valid) {
        TEST_ASSERT(lb.bid.price_tick == rb.bid.price_tick);
        TEST_ASSERT(lb.bid.quantity == rb.bid.quantity);
    }
    if (lb.ask.valid) {
        TEST_ASSERT(lb.ask.price_tick == rb.ask.price_tick);
        TEST_ASSERT(lb.ask.quantity == rb.ask.quantity);
    }

    // Compare State Hash
    TEST_ASSERT(live_engine.state_hash() == replay_engine.state_hash());
    std::cout << "Replay Equivalence Verified! State hash: " << live_engine.state_hash() << std::endl;

    std::filesystem::remove(wal_path);
}

int main() {
    std::cout << "[NexusMatch WAL & Deterministic Replay Tests] Running..." << std::endl;
    test_wal_roundtrip();
    test_wal_corruption_and_truncation();
    test_wal_replay_equivalence();
    std::cout << "[NexusMatch WAL & Deterministic Replay Tests] All passed successfully!" << std::endl;
    return 0;
}
