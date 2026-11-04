#pragma once
// 参数化气候门：八度早退漏斗 + weird 早退。
//
// 来源：LowYDripstoneCaveFinderBase/BiomeSampler.cpp 的
//   passContinentalnessPartialThr（大陆性 8 级）与 passCaveWeirdnessFast（weird 双边）。
//
// 数值红线（计划 §4.4）：漏斗的阈值比较必须与完整判定在同一条累加序列上做。
// 因此这里统一用 Acc 复刻 OctaveCache::noise() 的累加：
//   26.3      ：float 交错累加，amplitude 已含在 term() 内；
//   pre-26.3 ：double 分别累加 A 组、B 组，最后乘总 amplitude。
// approximate=false 时完全走全量分支，结果与改造前逐格一致。

#include "octave_cache.hpp"
#include "../bench/bench.hpp"

#include <cmath>
#include <cstdint>

namespace wcore {

/** B 实测的 8 级阈值；最后一级 0.55 只是漏斗终值。 */
inline constexpr double kContinentThresholds[8] = {
    -0.2, -0.1, 0.0, 0.13, 0.3, 0.37, 0.44, 0.55
};
inline constexpr int kContinentThresholdCount = 8;
inline constexpr double kContinentDefaultThreshold = 0.55;
/**
 * C1 分级门（10 个八度算完后）：
 *   v > 0.575          → 剩余 8 个八度最多只能再移动 ~0.05，必然越过 0.525/0.55 门，直接接受；
 *   0.525 < v ≤ 0.575  → 不确定，补算剩余八度后按 threshold 终判；
 *   v ≤ 0.525          → 按漏斗的有损预算直接判否。
 * 0.575 = 0.525 + 0.05 与“剩余 8 个八度造成 >0.025 波动的概率约万分之一”同源
 * （留一倍余量）。
 */
inline constexpr double kContinentBandLow = 0.525;
inline constexpr double kContinentFastAccept = 0.575;

/** weird 门：|w| < 0.05 或 |w| > 1.28。 */
inline constexpr double kWeirdLow = 0.05;
inline constexpr double kWeirdHigh = 1.28;
/** 早退余量默认值。B 在 double 路径用 0.14（58.2% 退出、0.11‰ 漏点）；
 * C 的 26.3 float 交错累加在 1.35M 点（3 版本 × 3 种子 × 30 万）上重标定后取
 * 0.20：漏点 0.0002%～0.0004%，早退约 48%，满足 ≤0.11‰ 的预算。 */
inline constexpr double kWeirdMargin = 0.20;
/** 26.3 float 路径与 pre-26.3 double 路径分别标定（计划 §5.3）。 */
inline constexpr double kWeirdMarginFloat = 0.20;
inline constexpr double kWeirdMarginLegacy = 0.20;

inline double weirdMargin(const BiomeNoise *bn) {
    return bn->mc >= MC_26_3 ? kWeirdMarginFloat : kWeirdMarginLegacy;
}

namespace detail {

inline double component(const BiomeNoise *bn, const OctaveCache *cache, int parameter,
                        int index, bool second, int x, int z) {
    return cache ? cache->term(bn, parameter, index, second, x, z)
                 : OctaveCache::live(bn, parameter, index, second, x, z);
}

/** 复刻 OctaveCache::noise() 的累加器，并在每个阶段暴露同一个值。 */
struct Acc {
    const BiomeNoise *bn;
    const OctaveCache *cache;
    int parameter;
    int x, z;
    bool modern;
    double amplitude;
    int octA, octB;
    float sum = 0;        // modern
    double a = 0, b = 0;  // legacy

    Acc(const BiomeNoise *noise, const OctaveCache *octaves, int parameter_, int x_, int z_)
        : bn(noise), cache(octaves), parameter(parameter_), x(x_), z(z_),
          modern(noise->mc >= MC_26_3),
          amplitude(noise->climate[parameter_].amplitude),
          octA(noise->climate[parameter_].octA.octcnt),
          octB(noise->climate[parameter_].octB.octcnt) {}

    int count() const { return octA > octB ? octA : octB; }

    void addSide(int i, bool second) {
        if (second) {
            if (i < octB) {
                const double v = component(bn, cache, parameter, i, true, x, z);
                if (modern) sum += float(v); else b += v;
            }
        } else if (i < octA) {
            const double v = component(bn, cache, parameter, i, false, x, z);
            if (modern) sum += float(v); else a += v;
        }
    }

    void addPair(int i) {
        addSide(i, false);
        addSide(i, true);
    }

    /** 与 OctaveCache::noise() 相同的值。 */
    float value() const { return modern ? sum : float((a + b) * amplitude); }

    /** 中间阈值比较用：26.3 就是 float 累加值，pre-26.3 保留 double 再乘振幅。 */
    double valueDouble() const { return modern ? double(sum) : (a + b) * amplitude; }

    bool onTable() const {
        return cache && cache->onLattice(parameter, 0, x, z);
    }
};

inline bool weirdValue(const BiomeNoise *bn, const OctaveCache *cache, int x, int z,
                       bool approximate, bool bothTails, bool riverDomain, float *out,
                       double margin) {
    Acc acc(bn, cache, NP_WEIRDNESS, x, z);
    const int count = acc.count();
    bool early = false;
    for (int i = 0; i < count; ++i) {
        acc.addPair(i);
        if (approximate && i == 1) {
            const double v = std::fabs(acc.valueDouble());
            // 剩余第 3 对八度最多再移动 ~margin：严格落在内部就必然出局。
            const bool outside = bothTails
                ? (v > kWeirdLow + margin && v < kWeirdHigh - margin)
                : (v > kWeirdLow + margin);
            if (outside) {
                early = true;
                break;
            }
        }
    }
    if (wcore::bench::enabled()) {
        auto &c = wcore::bench::counters();
        wcore::bench::add(c.weirdSamples, 1);
        if (early) wcore::bench::add(c.weirdEarlyExits, 1);
        if (riverDomain) {
            wcore::bench::add(c.riverTableSamples, 1);
            if (acc.onTable()) wcore::bench::add(c.riverTableHits, 1);
        } else {
            wcore::bench::add(c.caveTableSamples, 1);
            if (acc.onTable()) wcore::bench::add(c.caveTableHits, 1);
        }
        wcore::bench::add(c.latticeSamples, 1);
        if (cache && !acc.onTable()) wcore::bench::add(c.latticeMisses, 1);
    }
    if (early) return false;
    const float w = acc.value();
    if (out) *out = w;
    const double q = std::fabs(double(w));
    return bothTails ? (q < kWeirdLow || q > kWeirdHigh) : (q < kWeirdLow);
}

}  // namespace detail

/** 大陆性漏斗；approximate=false 时等价于原来的完整判定。 */
inline bool passesContinentalOctaves(const BiomeNoise *bn, const OctaveCache *cache,
                                     int x, int z, double threshold,
                                     bool approximate = true) {
    detail::Acc acc(bn, cache, NP_CONTINENTALNESS, x, z);
    if (!approximate || acc.octA < 5 || acc.octB < 5) {
        const float value = cache ? cache->noise(bn, NP_CONTINENTALNESS, x, z)
                                  : sampleClimateNoiseTable(bn, NP_CONTINENTALNESS, x, 0, z);
        if (wcore::bench::enabled())
            wcore::bench::add(wcore::bench::counters().continentSamples, 1);
        return value > threshold;
    }

    bool early = false;
    if (acc.octA >= 1 || acc.octB >= 1) {
        acc.addPair(0);
        if (acc.valueDouble() < kContinentThresholds[0]) early = true;
    }
    if (!early && (acc.octA >= 2 || acc.octB >= 2)) {
        acc.addSide(1, false);
        if (acc.valueDouble() < kContinentThresholds[1]) early = true;
    }
    if (!early && (acc.octA >= 2 || acc.octB >= 2)) {
        acc.addSide(1, true);
        if (acc.valueDouble() < kContinentThresholds[2]) early = true;
    }
    if (!early && (acc.octA >= 3 || acc.octB >= 3)) {
        acc.addSide(2, false);
        if (acc.valueDouble() < kContinentThresholds[3]) early = true;
    }
    if (!early && (acc.octA >= 3 || acc.octB >= 3)) {
        acc.addSide(2, true);
        if (acc.valueDouble() < kContinentThresholds[4]) early = true;
    }
    if (!early && (acc.octA >= 4 || acc.octB >= 4)) {
        acc.addSide(3, false);
        if (acc.valueDouble() < kContinentThresholds[5]) early = true;
    }
    if (!early && (acc.octA >= 4 || acc.octB >= 4)) {
        acc.addSide(3, true);
        if (acc.valueDouble() < kContinentThresholds[6]) early = true;
    }
    /* C1：10 个八度（0A..4A / 0B..4B）算完后分级，不再一律补算剩余 8 个八度。
     * approximate=false 走上面的全量分支，与改造前逐格一致。 */
    bool fastAccept = false;
    if (!early) {
        const int pairs = acc.count();
        if (pairs > 4)
            acc.addPair(4);
        const double ten = acc.valueDouble();
        if (ten > kContinentFastAccept) {
            // 余下的实测最大振幅远小于 0.05，必然过门。
            fastAccept = true;
        } else if (ten <= kContinentBandLow) {
            early = true;
        } else {
            for (int i = 5; i < pairs; ++i)
                acc.addPair(i);
        }
    }
    if (wcore::bench::enabled()) {
        auto &c = wcore::bench::counters();
        wcore::bench::add(c.continentSamples, 1);
        if (early) wcore::bench::add(c.continentEarlyExits, 1);
        if (fastAccept) wcore::bench::add(c.continentFastAccepts, 1);
        wcore::bench::add(c.caveTableSamples, 1);
        if (acc.onTable()) wcore::bench::add(c.caveTableHits, 1);
    }
    if (early) return false;
    if (fastAccept) return true;
    return acc.value() > threshold;
}

/** weird 双边门：|w| < 0.05 或 |w| > 1.28（洞穴路径）。 */
inline bool passesWeirdBothTails(const BiomeNoise *bn, const OctaveCache *cache, int x, int z,
                                 bool approximate = true, float *out = nullptr,
                                 double margin = -1) {
    return detail::weirdValue(bn, cache, x, z, approximate, true, false, out,
                              margin < 0 ? weirdMargin(bn) : margin);
}

/** weird 单边门：只保留低尾 |w| < 0.05（纯河流）。 */
inline bool passesWeirdLowTail(const BiomeNoise *bn, const OctaveCache *cache, int x, int z,
                               bool approximate = true, float *out = nullptr,
                               double margin = -1) {
    return detail::weirdValue(bn, cache, x, z, approximate, false, true, out,
                              margin < 0 ? weirdMargin(bn) : margin);
}

}  // namespace wcore
