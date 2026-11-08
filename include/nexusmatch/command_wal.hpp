#pragma once

#include "nexusmatch/types.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstddef>
#include <string_view>

namespace nexusmatch {

inline constexpr std::uint32_t kWaLMagic = 0x4E584D57; // "NXMW"
inline constexpr std::uint16_t kWaLVersion = 1;

enum class WalReadStatus : std::uint8_t {
    Success = 0,
    EndOfFile,
    InvalidMagic,
    UnsupportedVersion,
    TruncatedHeader,
    TruncatedRecord,
    ChecksumMismatch,
    IoError
};

[[nodiscard]] constexpr std::string_view to_string(WalReadStatus status) noexcept {
    switch (status) {
        case WalReadStatus::Success: return "SUCCESS";
        case WalReadStatus::EndOfFile: return "END_OF_FILE";
        case WalReadStatus::InvalidMagic: return "INVALID_MAGIC";
        case WalReadStatus::UnsupportedVersion: return "UNSUPPORTED_VERSION";
        case WalReadStatus::TruncatedHeader: return "TRUNCATED_HEADER";
        case WalReadStatus::TruncatedRecord: return "TRUNCATED_RECORD";
        case WalReadStatus::ChecksumMismatch: return "CHECKSUM_MISMATCH";
        case WalReadStatus::IoError: return "IO_ERROR";
    }
    return "UNKNOWN";
}

struct WalRecord {
    std::uint64_t command_seq{0};
    OrderCommand command{};
};

class CommandWalWriter {
public:
    explicit CommandWalWriter(const std::string& filepath);
    ~CommandWalWriter();

    CommandWalWriter(const CommandWalWriter&) = delete;
    CommandWalWriter& operator=(const CommandWalWriter&) = delete;

    bool append(const OrderCommand& cmd);
    void flush();
    void sync(); // fsync
    [[nodiscard]] std::uint64_t next_seq() const noexcept { return seq_; }

private:
    std::string filepath_;
    std::ofstream file_;
    std::uint64_t seq_{1};
};

class CommandWalReader {
public:
    explicit CommandWalReader(const std::string& filepath);
    ~CommandWalReader();

    CommandWalReader(const CommandWalReader&) = delete;
    CommandWalReader& operator=(const CommandWalReader&) = delete;

    [[nodiscard]] WalReadStatus open_and_validate_header();
    [[nodiscard]] WalReadStatus read_next(WalRecord& out_record);
    void close();

private:
    std::string filepath_;
    std::ifstream file_;
    std::uint64_t expected_seq_{1};
    bool header_read_{false};
};

// CRC32C (Castagnoli) checksum utility
class Crc32c {
public:
    [[nodiscard]] static std::uint32_t calculate(const void* data, std::size_t length) noexcept;
};

} // namespace nexusmatch
