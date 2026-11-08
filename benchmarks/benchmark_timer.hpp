#pragma once

#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>
#include <cstdint>
#include <string>
#include <iostream>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#endif

namespace nexusmatch::bench {

class SteadyTimer {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    void start() noexcept {
        start_time_ = Clock::now();
    }

    void stop() noexcept {
        end_time_ = Clock::now();
    }

    [[nodiscard]] double elapsed_seconds() const noexcept {
        return std::chrono::duration<double>(end_time_ - start_time_).count();
    }

    [[nodiscard]] std::uint64_t elapsed_nanoseconds() const noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end_time_ - start_time_).count()
        );
    }

private:
    TimePoint start_time_{};
    TimePoint end_time_{};
};

class HardwareRdtscTimer {
public:
    static inline void calibrate() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
        // Measure overhead
        std::uint64_t sum = 0;
        for (int i = 0; i < 1000; ++i) {
            std::uint64_t t0 = read_start();
            std::uint64_t t1 = read_stop();
            if (t1 > t0) sum += (t1 - t0);
        }
        overhead_cycles_ = sum / 1000;
#endif
    }

    [[nodiscard]] static inline std::uint64_t read_start() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_lfence();
        std::uint64_t tsc = __rdtsc();
        __builtin_ia32_lfence();
        return tsc;
#else
        return static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()
        );
#endif
    }

    [[nodiscard]] static inline std::uint64_t read_stop() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
        unsigned int aux;
        std::uint64_t tsc = __rdtscp(&aux);
        __builtin_ia32_lfence();
        return tsc;
#else
        return static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()
        );
#endif
    }

    [[nodiscard]] static std::uint64_t overhead_cycles() noexcept {
        return overhead_cycles_;
    }

private:
    static inline std::uint64_t overhead_cycles_{0};
};

struct LatencyDistribution {
    double min_ns{0.0};
    double p50_ns{0.0};
    double p90_ns{0.0};
    double p95_ns{0.0};
    double p99_ns{0.0};
    double p999_ns{0.0};
    double max_ns{0.0};
    double mean_ns{0.0};
    std::size_t sample_count{0};

    [[nodiscard]] static LatencyDistribution compute(std::vector<double>& samples) {
        if (samples.empty()) return {};
        std::sort(samples.begin(), samples.end());
        std::size_t n = samples.size();
        double sum = std::accumulate(samples.begin(), samples.end(), 0.0);

        auto percentile = [&](double p) -> double {
            if (n == 0) return 0.0;
            std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(n - 1));
            return samples[std::min(idx, n - 1)];
        };

        return LatencyDistribution{
            .min_ns = samples.front(),
            .p50_ns = percentile(0.50),
            .p90_ns = percentile(0.90),
            .p95_ns = percentile(0.95),
            .p99_ns = percentile(0.99),
            .p999_ns = percentile(0.999),
            .max_ns = samples.back(),
            .mean_ns = sum / static_cast<double>(n),
            .sample_count = n
        };
    }
};

} // namespace nexusmatch::bench
