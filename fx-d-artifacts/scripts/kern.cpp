// Kernel-only timings on pre-faulted 590 MB buffers, same flags as Vespa: isolates compute from first-touch.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
using u8 = uint8_t;
__attribute__((noinline)) u8 k_current(const u8* __restrict s, u8* __restrict d, size_t n, uint32_t diff) {
    const u8 c = std::min(diff, 255u); u8 m = 0;
    for (size_t i = 0; i < n; ++i) { u8 r = s[i] - std::min(s[i], c); d[i] = r; m = std::max(m, r); }
    return m;
}
#include <span>
__attribute__((noinline)) uint8_t
shipped(std::span<const int8_t> sources, std::span<int8_t> result, uint32_t diff) noexcept
{
    const auto clampedDiff = static_cast<uint8_t>(std::min(diff, 255u));
    const auto * __restrict src = reinterpret_cast<const uint8_t *>(sources.data());
    auto * __restrict dst = reinterpret_cast<uint8_t *>(result.data());
    const size_t numDocs = sources.size();
    uint8_t maxSource = 0;
    for (size_t docId = 0; docId < numDocs; ++docId) {
        uint8_t source = src[docId];
        uint8_t newSource = source - std::min(source, clampedDiff);
        dst[docId] = newSource;
        maxSource = std::max(maxSource, newSource);
    }
    return maxSource;
}


__attribute__((noinline)) uint8_t lanes64(std::span<const int8_t> sources, std::span<int8_t> result, uint32_t diff) noexcept
{
    const auto clampedDiff = static_cast<uint8_t>(std::min(diff, 255u));
    const auto * __restrict src = reinterpret_cast<const uint8_t *>(sources.data());
    auto * __restrict dst = reinterpret_cast<uint8_t *>(result.data());
    const size_t numDocs = sources.size();
    constexpr size_t lanes = 64;
    uint8_t laneMax[lanes] = {};
    size_t docId = 0;
    for (; docId + lanes <= numDocs; docId += lanes) {
        for (size_t lane = 0; lane < lanes; ++lane) {
            uint8_t source = src[docId + lane];
            uint8_t newSource = source - std::min(source, clampedDiff);
            dst[docId + lane] = newSource;
            laneMax[lane] = std::max(laneMax[lane], newSource);
        }
    }
    uint8_t maxSource = 0;
    for (; docId < numDocs; ++docId) {
        uint8_t source = src[docId];
        uint8_t newSource = source - std::min(source, clampedDiff);
        dst[docId] = newSource;
        maxSource = std::max(maxSource, newSource);
    }
    for (uint8_t value : laneMax) {
        maxSource = std::max(maxSource, value);
    }
    return maxSource;
}

__attribute__((noinline)) void k_nomax(const u8* __restrict s, u8* __restrict d, size_t n, uint32_t diff) {
    const u8 c = std::min(diff, 255u);
    for (size_t i = 0; i < n; ++i) { d[i] = s[i] - std::min(s[i], c); }
}
__attribute__((noinline)) u8 k_blocked(const u8* __restrict s, u8* __restrict d, size_t n, uint32_t diff) {
    // 4 independent max accumulators via 64-byte blocks
    const u8 c = std::min(diff, 255u); u8 m0 = 0, m1 = 0, m2 = 0, m3 = 0; size_t i = 0;
    for (; i + 64 <= n; i += 64) {
        for (size_t j = 0; j < 16; ++j) { u8 r = s[i+j] - std::min(s[i+j], c); d[i+j] = r; m0 = std::max(m0, r); }
        for (size_t j = 16; j < 32; ++j) { u8 r = s[i+j] - std::min(s[i+j], c); d[i+j] = r; m1 = std::max(m1, r); }
        for (size_t j = 32; j < 48; ++j) { u8 r = s[i+j] - std::min(s[i+j], c); d[i+j] = r; m2 = std::max(m2, r); }
        for (size_t j = 48; j < 64; ++j) { u8 r = s[i+j] - std::min(s[i+j], c); d[i+j] = r; m3 = std::max(m3, r); }
    }
    for (; i < n; ++i) { u8 r = s[i] - std::min(s[i], c); d[i] = r; m0 = std::max(m0, r); }
    return std::max(std::max(m0, m1), std::max(m2, m3));
}
int main(int argc, char** argv) {
    size_t n = 590000000; int reps = 10;
    u8* s = (u8*)mmap(nullptr, n, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    u8* d = (u8*)mmap(nullptr, n, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    madvise(s, n, MADV_HUGEPAGE); madvise(d, n, MADV_HUGEPAGE);
    uint64_t x = 42; for (size_t i = 0; i < n; ++i) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; s[i] = x % 8; }
    memset(d, 1, n);
    volatile u8 sink = 0;
    const char* names[] = {"memcpy", "shipped", "nomax", "lanes64"};
    for (int r = 0; r < reps; ++r) {
        for (int k = 0; k < 4; ++k) {
            auto t0 = std::chrono::steady_clock::now();
            if (k == 0) memcpy(d, s, n);
            else if (k == 1) sink = shipped(std::span<const int8_t>((const int8_t*)s, n), std::span<int8_t>((int8_t*)d, n), 1);
            else if (k == 2) k_nomax(s, d, n, 1);
            else sink = lanes64(std::span<const int8_t>((const int8_t*)s, n), std::span<int8_t>((int8_t*)d, n), 1);
            double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            printf("rep=%d kernel=%s wall_ms=%.3f GBps_rw=%.1f d_last=%u\n", r, names[k], sec*1e3, 2.0*n/sec/1e9, (unsigned)d[n-1]);
        }
    }
    return sink;
}
