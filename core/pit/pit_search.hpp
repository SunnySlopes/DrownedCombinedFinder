#pragma once
// 大坑搜索：从 LowYDripstoneCaveFinderBase/LargePitSearch.cpp（MIT）提取。
//
// 三段式：
//   ① A 表极值扫描：0A≥0.21 → 0A+1A≥0.55 → 0A+1A+2A≥0.6
//   ② B 链：A+0B≥1 → A+0B+1B≥1.4，再精修到 |W| > 1.6
//   ③ 大陆性漏斗踢掉 ~2/3，剩下交给调用方的精算回调
//
// 与 B 的差异只有接口：这里不依赖 SearchConfig / Progress / Res，精算步骤交给
// 调用方；线程池用最小实现，停止用原子布尔。

#include "../bench/bench.hpp"
#include "../gates/early_exit.hpp"
#include "../gates/perlin_table.hpp"

#include <cubiomes/biomenoise.h>
#include <cubiomes/noise.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wcore::pit {

struct Peak {
    int bx = 0;   // 方块坐标（不是 climate 坐标）
    int bz = 0;
    double w = 0; // 完整 weirdness
};

struct Options {
    int minX = -29999999, maxX = 29999999;
    int minZ = -29999999, maxZ = 29999999;
    int threads = 1;
    /** 最终 |W| 下限；B 的 pit 口径是 > 1.6。 */
    double minAbsW = 1.6;
    /** 大陆性漏斗终值（B 的 CONT_PARTIAL_FINAL_THRESHOLD）。 */
    double continentThreshold = 0.55;
    /** 只保留能覆盖搜索框的峰；0 表示不裁。 */
    int clipMargin = 0;
};

struct Progress {
    std::atomic<int> phase{0};
    std::atomic<int> current{0};
    std::atomic<int> total{0};
    std::atomic<bool> stop{false};
};

namespace detail {

constexpr double kDpnF = 337.0 / 331.0;
constexpr int kStep1Min = -65536;
constexpr int kStep1Max = 65536;
constexpr int kStep1Stride = 128;
constexpr int kStep1N = (kStep1Max - kStep1Min) / kStep1Stride;
constexpr double kA0Min = 0.21;
constexpr int kStep2Radius = 128;
constexpr int kStep2Stride = 64;
constexpr double kA01Min = 0.55;
constexpr int kStep3Radius = 64;
constexpr int kStep3Stride = 16;
constexpr double kA012Min = 0.6;
constexpr int kStep3Dedup = 192;
constexpr int kPeriod = 131072;
constexpr double kB0Min = 1.0;
constexpr double kB1Min = 1.4;
constexpr int kWCoarseRadius = 128;
constexpr int kWCoarseStride = 64;
constexpr int kWFineRadius = 40;
constexpr int kWFineStride = 4;
constexpr int kWorldLimit = 30000000;
constexpr int kPeriodKMax = (kWorldLimit + 65536 + kPeriod - 1) / kPeriod;
constexpr int kPeakDedup = 256;
constexpr int kMaxSeeds = kStep1N * kStep1N;

struct Seed { int bx, bz, sign; };
struct Extreme { int bx, bz; double a012; int sign; };

inline double rawOctave(const PerlinNoise *p, double x, double z) {
    const double lf = p->lacunarity;
    return p->amplitude * samplePerlinTable(p, x * lf, 0.0, z * lf);
}

inline double scaledOctave(const DoublePerlinNoise *dpn, bool bSide, int idx,
                           double climateX, double climateZ) {
    const OctaveNoise *oct = bSide ? &dpn->octB : &dpn->octA;
    if (idx < 0 || idx >= oct->octcnt) return 0.0;
    const double x = bSide ? climateX * kDpnF : climateX;
    const double z = bSide ? climateZ * kDpnF : climateZ;
    return dpn->amplitude * rawOctave(oct->octaves + idx, x, z);
}

inline double fullWeirdness(const DoublePerlinNoise *dpn, int blockX, int blockZ) {
    const double x = blockX * 0.25, z = blockZ * 0.25;
    double sum = 0.0;
    const int n = std::max(dpn->octA.octcnt, dpn->octB.octcnt);
    for (int i = 0; i < n; ++i) {
        sum += scaledOctave(dpn, false, i, x, z);
        sum += scaledOctave(dpn, true, i, x, z);
    }
    return sum;
}

inline double aSum(const DoublePerlinNoise *dpn, int blockX, int blockZ, int lastIdx) {
    const double x = blockX * 0.25, z = blockZ * 0.25;
    double sum = 0.0;
    for (int i = 0; i <= lastIdx; ++i) sum += scaledOctave(dpn, false, i, x, z);
    return sum;
}

inline bool insideWorld(int x, int z) {
    return x >= -kWorldLimit && x <= kWorldLimit && z >= -kWorldLimit && z <= kWorldLimit;
}

inline bool chebyshevWithin(int ax, int az, int bx, int bz, int limit) {
    return std::abs(ax - bx) <= limit && std::abs(az - bz) <= limit;
}

/** 极简并行 for；work(i) 捕获自己的下标。 */
template <class Work>
inline void parallelFor(int count, int threads, Work work) {
    threads = std::max(1, threads);
    std::atomic<int> next{0};
    if (threads == 1) {
        for (int i = 0; i < count; ++i) work(i);
        return;
    }
    std::vector<std::thread> pool;
    pool.reserve(size_t(threads));
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            for (;;) {
                const int i = next.fetch_add(1);
                if (i >= count) return;
                work(i);
            }
        });
    for (auto &th : pool) th.join();
}

inline std::vector<Seed> scanA0(const DoublePerlinNoise *dpn, Progress *progress, int threads) {
    std::vector<uint8_t> hits(size_t(kStep1N) * kStep1N, 0);
    if (progress) {
        progress->phase.store(1);
        progress->current.store(0);
        progress->total.store(kStep1N);
    }
    std::atomic<int> rowsDone{0};
    const int band = std::max(1, kStep1N / (std::max(1, threads) * 4));
    std::vector<std::pair<int,int>> bands;
    for (int r0 = 0; r0 < kStep1N; r0 += band) bands.push_back({r0, std::min(kStep1N, r0 + band)});

    parallelFor(int(bands.size()), threads, [&](int b) {
        const int r0 = bands[size_t(b)].first, r1 = bands[size_t(b)].second;
        for (int ix = r0; ix < r1; ++ix) {
            if (progress && progress->stop.load(std::memory_order_relaxed)) return;
            const int bx = kStep1Min + ix * kStep1Stride;
            for (int iz = 0; iz < kStep1N; ++iz) {
                const int bz = kStep1Min + iz * kStep1Stride;
                const double o0 = scaledOctave(dpn, false, 0, bx * 0.25, bz * 0.25);
                if (o0 > kA0Min) hits[size_t(ix) * kStep1N + size_t(iz)] = 1;
                else if (o0 < -kA0Min) hits[size_t(ix) * kStep1N + size_t(iz)] = 2;
            }
        }
        const int done = rowsDone.fetch_add(r1 - r0) + (r1 - r0);
        if (progress) progress->current.store(std::min(done, kStep1N));
    });

    std::vector<Seed> seeds;
    seeds.reserve(4096);
    for (int ix = 0; ix < kStep1N; ++ix)
        for (int iz = 0; iz < kStep1N; ++iz) {
            const uint8_t mark = hits[size_t(ix) * kStep1N + size_t(iz)];
            if (!mark) continue;
            if (int(seeds.size()) >= kMaxSeeds) return seeds;
            seeds.push_back(Seed{kStep1Min + ix * kStep1Stride,
                                 kStep1Min + iz * kStep1Stride, mark == 1 ? 1 : -1});
        }
    return seeds;
}

inline std::vector<Extreme> refineAChain(const DoublePerlinNoise *dpn,
                                         const std::vector<Seed> &seeds, Progress *progress) {
    if (progress) {
        progress->phase.store(1);
        progress->current.store(0);
        progress->total.store(std::max(1, int(seeds.size())));
    }
    std::vector<Extreme> scratch;
    scratch.reserve(seeds.size() / 8 + 1);
    std::unordered_set<uint64_t> seen;
    seen.reserve(seeds.size() / 8 + 1);

    int index = 0;
    for (const Seed &seed : seeds) {
        if (progress && progress->stop.load(std::memory_order_relaxed)) return {};
        if (progress) progress->current.store(index);
        ++index;

        const int sign = seed.sign;
        int bestX = seed.bx, bestZ = seed.bz;
        double best = sign > 0 ? -1e300 : 1e300;
        auto moreExtreme = [sign](double s, double bestSoFar) {
            return sign > 0 ? s > bestSoFar : s < bestSoFar;
        };
        for (int rx = seed.bx - kStep2Radius; rx <= seed.bx + kStep2Radius; rx += kStep2Stride)
            for (int rz = seed.bz - kStep2Radius; rz <= seed.bz + kStep2Radius; rz += kStep2Stride) {
                const double s = aSum(dpn, rx, rz, 1);
                if (moreExtreme(s, best)) { best = s; bestX = rx; bestZ = rz; }
            }
        if (sign > 0 ? !(best > kA01Min) : !(best < -kA01Min)) continue;

        const uint64_t key2 = (uint64_t(uint32_t(bestX)) << 32) | uint32_t(bestZ) |
                              (sign < 0 ? 0x8000000000000000ULL : 0);
        if (seen.contains(key2)) continue;

        best = sign > 0 ? -1e300 : 1e300;
        double a012 = 0.0;
        for (int rx = bestX - kStep3Radius; rx <= bestX + kStep3Radius; rx += kStep3Stride)
            for (int rz = bestZ - kStep3Radius; rz <= bestZ + kStep3Radius; rz += kStep3Stride) {
                const double s = aSum(dpn, rx, rz, 2);
                if (moreExtreme(s, best)) { best = s; bestX = rx; bestZ = rz; a012 = s; }
            }
        if (sign > 0 ? !(a012 > kA012Min) : !(a012 < -kA012Min)) continue;
        const uint64_t key3 = (uint64_t(uint32_t(bestX)) << 32) | uint32_t(bestZ) |
                              (sign < 0 ? 0x8000000000000000ULL : 0);
        if (!seen.insert(key3).second) continue;
        scratch.push_back(Extreme{bestX, bestZ, a012, sign});
    }

    std::sort(scratch.begin(), scratch.end(), [](const Extreme &a, const Extreme &b) {
        return std::fabs(a.a012) > std::fabs(b.a012);
    });
    std::vector<Extreme> kept;
    kept.reserve(scratch.size());
    for (const Extreme &e : scratch) {
        bool dominated = false;
        for (const Extreme &k : kept)
            if (e.sign == k.sign && chebyshevWithin(e.bx, e.bz, k.bx, k.bz, kStep3Dedup)) {
                dominated = true;
                break;
            }
        if (!dominated) kept.push_back(e);
    }
    if (progress) progress->current.store(index);
    return kept;
}

/** 保留最极端的带符号 W；|W| > 1.6 全部保留（包括 > 1.9）。 */
inline bool refineSignedW(const DoublePerlinNoise *dpn, int cx, int cz, int sign,
                          int &outX, int &outZ, double &outW) {
    auto windowExt = [&](int centerX, int centerZ, int radius, int stride,
                         int &bx, int &bz, double &best) {
        best = sign > 0 ? -1e300 : 1e300;
        bx = centerX;
        bz = centerZ;
        for (int rx = centerX - radius; rx <= centerX + radius; rx += stride)
            for (int rz = centerZ - radius; rz <= centerZ + radius; rz += stride) {
                const double w = fullWeirdness(dpn, rx, rz);
                if (sign > 0 ? w > best : w < best) { best = w; bx = rx; bz = rz; }
            }
    };
    int cx2 = cx, cz2 = cz;
    double w1 = 0.0;
    windowExt(cx, cz, kWCoarseRadius, kWCoarseStride, cx2, cz2, w1);
    windowExt(cx2, cz2, kWFineRadius, kWFineStride, outX, outZ, outW);
    if (!(std::fabs(outW) > 1.599999)) return false;
    return insideWorld(outX, outZ);
}

inline std::vector<Peak> dedupPeaks(std::vector<Peak> peaks) {
    std::sort(peaks.begin(), peaks.end(), [](const Peak &a, const Peak &b) {
        const double aw = std::fabs(a.w), bw = std::fabs(b.w);
        if (aw != bw) return aw > bw;
        if (a.bx != b.bx) return a.bx < b.bx;
        return a.bz < b.bz;
    });
    auto cellOf = [](int v) { return v >= 0 ? v / kPeakDedup : (v - kPeakDedup + 1) / kPeakDedup; };
    auto bucketKey = [](int cx, int cz) {
        return (uint64_t(uint32_t(cx)) << 32) | uint32_t(cz);
    };
    std::unordered_map<uint64_t, std::vector<Peak>> buckets;
    std::vector<Peak> kept;
    kept.reserve(peaks.size());
    for (const Peak &p : peaks) {
        if (!(std::fabs(p.w) > 1.599999)) continue;
        const int cx = cellOf(p.bx), cz = cellOf(p.bz);
        bool alreadyNear = false;
        for (int dx = -1; dx <= 1 && !alreadyNear; ++dx)
            for (int dz = -1; dz <= 1 && !alreadyNear; ++dz) {
                const auto it = buckets.find(bucketKey(cx + dx, cz + dz));
                if (it == buckets.end()) continue;
                for (const Peak &k : it->second)
                    if (chebyshevWithin(p.bx, p.bz, k.bx, k.bz, kPeakDedup)) { alreadyNear = true; break; }
            }
        if (alreadyNear) continue;
        buckets[bucketKey(cx, cz)].push_back(p);
        kept.push_back(p);
    }
    return kept;
}

inline std::vector<Peak> scanBChain(const DoublePerlinNoise *dpn,
                                    const std::vector<Extreme> &extremes,
                                    Progress *progress, int threads) {
    if (progress) {
        progress->phase.store(1);
        progress->current.store(0);
        progress->total.store(std::max(1, int(extremes.size())));
    }
    if (extremes.empty()) return {};

    std::vector<std::vector<Peak>> perExtreme(extremes.size());
    parallelFor(int(extremes.size()), threads, [&](int ei) {
        if (progress && progress->stop.load(std::memory_order_relaxed)) return;
        const Extreme &extreme = extremes[size_t(ei)];
        std::vector<Peak> local;
        for (int kx = -kPeriodKMax; kx <= kPeriodKMax; ++kx) {
            if (progress && progress->stop.load(std::memory_order_relaxed)) break;
            for (int kz = -kPeriodKMax; kz <= kPeriodKMax; ++kz) {
                const long long px64 = (long long)extreme.bx + (long long)kx * kPeriod;
                const long long pz64 = (long long)extreme.bz + (long long)kz * kPeriod;
                if (px64 < -kWorldLimit || px64 > kWorldLimit ||
                    pz64 < -kWorldLimit || pz64 > kWorldLimit) continue;
                const int px = (int)px64, pz = (int)pz64;
                const double a012 = aSum(dpn, px, pz, 2);
                const double x = px * 0.25, z = pz * 0.25;
                const double b0 = scaledOctave(dpn, true, 0, x, z);
                if (!(std::fabs(a012 + b0) > kB0Min)) continue;
                const double b1 = scaledOctave(dpn, true, 1, x, z);
                if (!(std::fabs(a012 + b0 + b1) > kB1Min)) continue;
                int hx = px, hz = pz;
                double hw = 0.0;
                if (!refineSignedW(dpn, px, pz, extreme.sign, hx, hz, hw)) continue;
                local.push_back(Peak{hx, hz, hw});
            }
        }
        perExtreme[size_t(ei)] = std::move(local);
        if (progress) progress->current.fetch_add(1);
    });

    std::vector<Peak> peaks;
    for (const auto &part : perExtreme) peaks.insert(peaks.end(), part.begin(), part.end());
    return dedupPeaks(std::move(peaks));
}

}  // namespace detail

/** 扫描全图（受 Options 裁剪）的 |W| > 1.6 峰。 */
inline std::vector<Peak> findWeirdnessPeaks(const BiomeNoise *bn, const Options &options,
                                            Progress *progress = nullptr) {
    wcore::bench::Scope scope(wcore::bench::STAGE_PIT);
    const DoublePerlinNoise *weird = &bn->climate[NP_WEIRDNESS];
    const int threads = std::max(1, options.threads);

    const std::vector<detail::Seed> seeds = detail::scanA0(weird, progress, threads);
    if (progress && progress->stop.load()) return {};
    const std::vector<detail::Extreme> extremes = detail::refineAChain(weird, seeds, progress);
    if (progress && progress->stop.load()) return {};
    std::vector<Peak> peaks = detail::scanBChain(weird, extremes, progress, threads);
    if (progress && progress->stop.load()) return {};

    if (options.clipMargin > 0) {
        peaks.erase(std::remove_if(peaks.begin(), peaks.end(), [&](const Peak &p) {
            return p.bx < options.minX - options.clipMargin || p.bx > options.maxX + options.clipMargin ||
                   p.bz < options.minZ - options.clipMargin || p.bz > options.maxZ + options.clipMargin;
        }), peaks.end());
    }

    if (progress) {
        progress->phase.store(2);
        progress->current.store(0);
        progress->total.store(std::max(1, int(peaks.size())));
    }
    std::vector<Peak> passed;
    passed.reserve(peaks.size());
    int index = 0;
    for (const Peak &p : peaks) {
        if (progress && progress->stop.load()) return {};
        if (wcore::passesContinentalOctaves(bn, nullptr, p.bx, p.bz,
                                            float(options.continentThreshold), true))
            passed.push_back(p);
        if (progress) progress->current.store(++index);
    }
    return passed;
}

}  // namespace wcore::pit
