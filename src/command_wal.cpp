#include "nexusmatch/command_wal.hpp"
#include <array>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

namespace nexusmatch {

namespace {

// Precomputed CRC32C table (Castagnoli 0x82F63B78)
constexpr auto generate_crc32c_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0x82F63B78 : 0);
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto kCrc32cTable = generate_crc32c_table();

void write_u16_le(std::uint8_t* dst, std::uint16_t val) noexcept {
    dst[0] = static_cast<std::uint8_t>(val);
    dst[1] = static_cast<std::uint8_t>(val >> 8);
}

void write_u32_le(std::uint8_t* dst, std::uint32_t val) noexcept {
    dst[0] = static_cast<std::uint8_t>(val);
    dst[1] = static_cast<std::uint8_t>(val >> 8);
    dst[2] = static_cast<std::uint8_t>(val >> 16);
    dst[3] = static_cast<std::uint8_t>(val >> 24);
}

void write_u64_le(std::uint8_t* dst, std::uint64_t val) noexcept {
    for (int i = 0; i < 8; ++i) {
        dst[i] = static_cast<std::uint8_t>(val >> (i * 8));
    }
}

std::uint16_t read_u16_le(const std::uint8_t* src) noexcept {
    return static_cast<std::uint16_t>(src[0]) |
           (static_cast<std::uint16_t>(src[1]) << 8);
}

std::uint32_t read_u32_le(const std::uint8_t* src) noexcept {
    return static_cast<std::uint32_t>(src[0]) |
           (static_cast<std::uint32_t>(src[1]) << 8) |
           (static_cast<std::uint32_t>(src[2]) << 16) |
           (static_cast<std::uint32_t>(src[3]) << 24);
}

std::uint64_t read_u64_le(const std::uint8_t* src) noexcept {
    std::uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val |= (static_cast<std::uint64_t>(src[i]) << (i * 8));
    }
    return val;
}

} // namespace

std::uint32_t Crc32c::calculate(const void* data, std::size_t length) noexcept {
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < length; ++i) {
        crc = (crc >> 8) ^ kCrc32cTable[(crc ^ p[i]) & 0xFF];
    }
    return ~crc;
}

CommandWalWriter::CommandWalWriter(const std::string& filepath)
    : filepath_(filepath),
      file_(filepath, std::ios::binary | std::ios::trunc)
{
    if (!file_.is_open()) {
        throw std::runtime_error("Failed to open WAL file for writing: " + filepath);
    }

    // Write file header: 16 bytes
    std::uint8_t header[16]{};
    write_u32_le(header + 0, kWaLMagic);
    write_u16_le(header + 4, kWaLVersion);
    write_u16_le(header + 6, 16); // Header size
    write_u64_le(header + 8, 0);  // Reserved

    file_.write(reinterpret_cast<const char*>(header), sizeof(header));
    file_.flush();
}

CommandWalWriter::~CommandWalWriter() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

bool CommandWalWriter::append(const OrderCommand& cmd) {
    if (!file_.is_open() || file_.fail()) return false;

    // Record layout:
    // 2 bytes: payload length = 32 bytes
    // Payload (32 bytes):
    //   8 bytes: seq
    //   1 byte: kind
    //   1 byte: side
    //   2 bytes: reserved
    //   4 bytes: owner_id
    //   8 bytes: order_id
    //   4 bytes: price_tick
    //   4 bytes: quantity
    // 4 bytes: CRC32C over the 32 bytes payload
    // Total record length = 2 + 32 + 4 = 38 bytes
    constexpr std::uint16_t kPayloadLen = 32;
    std::uint8_t buf[38]{};

    write_u16_le(buf + 0, kPayloadLen);

    std::uint8_t* payload = buf + 2;
    write_u64_le(payload + 0, seq_++);
    payload[8] = static_cast<std::uint8_t>(cmd.kind);
    payload[9] = static_cast<std::uint8_t>(cmd.side);
    write_u16_le(payload + 10, cmd.reserved);
    write_u32_le(payload + 12, cmd.owner_id);
    write_u64_le(payload + 16, cmd.order_id);
    write_u32_le(payload + 24, cmd.price_tick);
    write_u32_le(payload + 28, cmd.quantity);

    std::uint32_t crc = Crc32c::calculate(payload, kPayloadLen);
    write_u32_le(buf + 34, crc);

    file_.write(reinterpret_cast<const char*>(buf), sizeof(buf));
    return file_.good();
}

void CommandWalWriter::flush() {
    if (file_.is_open()) {
        file_.flush();
    }
}

void CommandWalWriter::sync() {
    flush();
#if defined(__linux__) || defined(__unix__)
    int fd = ::open(filepath_.c_str(), O_WRONLY);
    if (fd >= 0) {
        ::fdatasync(fd);
        ::close(fd);
    }
#endif
}

CommandWalReader::CommandWalReader(const std::string& filepath)
    : filepath_(filepath),
      file_(filepath, std::ios::binary)
{
}

CommandWalReader::~CommandWalReader() {
    close();
}

void CommandWalReader::close() {
    if (file_.is_open()) {
        file_.close();
    }
}

WalReadStatus CommandWalReader::open_and_validate_header() {
    if (!file_.is_open()) {
        return WalReadStatus::IoError;
    }

    std::uint8_t header[16]{};
    file_.read(reinterpret_cast<char*>(header), sizeof(header));
    if (file_.gcount() < 16) {
        return WalReadStatus::TruncatedHeader;
    }

    std::uint32_t magic = read_u32_le(header + 0);
    if (magic != kWaLMagic) {
        return WalReadStatus::InvalidMagic;
    }

    std::uint16_t version = read_u16_le(header + 4);
    if (version != kWaLVersion) {
        return WalReadStatus::UnsupportedVersion;
    }

    header_read_ = true;
    expected_seq_ = 1;
    return WalReadStatus::Success;
}

WalReadStatus CommandWalReader::read_next(WalRecord& out_record) {
    if (!header_read_) {
        WalReadStatus s = open_and_validate_header();
        if (s != WalReadStatus::Success) return s;
    }

    std::uint8_t len_buf[2]{};
    file_.read(reinterpret_cast<char*>(len_buf), 2);
    if (file_.gcount() == 0) {
        return WalReadStatus::EndOfFile;
    }
    if (file_.gcount() < 2) {
        return WalReadStatus::TruncatedRecord;
    }

    std::uint16_t payload_len = read_u16_le(len_buf);
    if (payload_len != 32) {
        return WalReadStatus::TruncatedRecord;
    }

    std::vector<std::uint8_t> record_buf(payload_len + 4); // 32 payload + 4 crc
    file_.read(reinterpret_cast<char*>(record_buf.data()), record_buf.size());
    if (file_.gcount() < static_cast<std::streamsize>(record_buf.size())) {
        return WalReadStatus::TruncatedRecord;
    }

    std::uint32_t expected_crc = read_u32_le(record_buf.data() + payload_len);
    std::uint32_t actual_crc = Crc32c::calculate(record_buf.data(), payload_len);
    if (actual_crc != expected_crc) {
        return WalReadStatus::ChecksumMismatch;
    }

    const std::uint8_t* p = record_buf.data();
    out_record.command_seq = read_u64_le(p + 0);
    out_record.command.kind = static_cast<OrderKind>(p[8]);
    out_record.command.side = static_cast<Side>(p[9]);
    out_record.command.reserved = read_u16_le(p + 10);
    out_record.command.owner_id = read_u32_le(p + 12);
    out_record.command.order_id = read_u64_le(p + 16);
    out_record.command.price_tick = read_u32_le(p + 24);
    out_record.command.quantity = read_u32_le(p + 28);

    return WalReadStatus::Success;
}

} // namespace nexusmatch
