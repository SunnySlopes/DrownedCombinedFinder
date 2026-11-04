// weird 早退余量标定：漏点率 vs 早退率（计划 §5.3）。
// 26.3 是 float 交错累加，pre-26.3 是 double 分组累加，两条路径分别标定。
#define lerp cubiomes_lerp
#include <cubiomes/rng.h>
#undef lerp
#include "core/gates/early_exit.hpp"
#include "core/gates/octave_cache.hpp"

#include <cubiomes/generator.h>

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

struct Prefix {
    double partial = 0;  // 第 2 对八度之后
    double full = 0;
};

Prefix measure(const BiomeNoise *bn, int x, int z, int pairs) {
    const auto &src = bn->climate[NP_WEIRDNESS];
    Prefix out;
    const bool modern = bn->mc >= MC_26_3;
    const int count = src.octA.octcnt > src.octB.octcnt ? src.octA.octcnt : src.octB.octcnt;
    float sum = 0;
    double a = 0, b = 0;
    for (int i = 0; i < count; ++i) {
        if (i < src.octA.octcnt) {
            const double v = wcore::OctaveCache::live(bn, NP_WEIRDNESS, i, false, x, z);
            if (modern) sum += float(v); else a += v;
        }
        if (i < src.octB.octcnt) {
            const double v = wcore::OctaveCache::live(bn, NP_WEIRDNESS, i, true, x, z);
            if (modern) sum += float(v); else b += v;
        }
        if (i + 1 == pairs)
            out.partial = modern ? double(sum) : (a + b) * src.amplitude;
    }
    out.full = modern ? double(sum) : (a + b) * src.amplitude;
    return out;
}

}  // namespace

int main() {
    constexpr int points = 300000;
    const uint64_t seeds[] = {0, 12345, UINT64_MAX};
    const int versions[] = {MC_26_3, MC_1_21_1, MC_1_18_2};
    for (int mc : versions) {
        Generator generator;
        setupGenerator(&generator, mc, 0);
        int bothMiss[9] = {}, lowMiss[9] = {}, bothExit[9] = {}, lowExit[9] = {};
        double margins[9];
        for (int i = 0; i < 9; ++i) margins[i] = 0.14 + 0.02 * i;
        uint64_t random = 20261103 + uint64_t(mc) * 7919;
        for (uint64_t seed : seeds) {
            applySeed(&generator, DIM_OVERWORLD, seed);
            for (int i = 0; i < points; ++i) {
                random = random * UINT64_C(6364136223846793005) + 1;
                const int x = int(random % 15000000) - 7500000;
                random = random * UINT64_C(6364136223846793005) + 1;
                const int z = int(random % 15000000) - 7500000;
                const Prefix p = measure(&generator.bn, x, z, 2);
                int64_t wi = (int64_t)(10000.0F * p.full);
                if (wi < 0) wi = -wi;
                const double part = std::fabs(p.partial);
                const bool lowTail = wi < 500;
                const bool bothTail = lowTail || wi > 12800;
                for (int m = 0; m < 9; ++m) {
                    const double margin = margins[m];
                    if (part > 0.05 + margin) {
                        ++lowExit[m];
                        if (lowTail) ++lowMiss[m];
                        if (part < 1.28 - margin) {
                            ++bothExit[m];
                            if (bothTail) ++bothMiss[m];
                        }
                    }
                }
            }
        }
        const double total = double(points) * (sizeof(seeds) / sizeof(seeds[0]));
        std::printf("mc=%d points=%.0f\n", mc, total);
        for (int m = 0; m < 9; ++m) {
            std::printf("  m=%.2f  low_miss=%.4f%% low_exit=%.2f%%  both_miss=%.4f%% both_exit=%.2f%%\n",
                        margins[m], lowMiss[m] / total * 100.0, lowExit[m] / total * 100.0,
                        bothMiss[m] / total * 100.0, bothExit[m] / total * 100.0);
        }
    }
    return 0;
}
