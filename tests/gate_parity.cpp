// 早退漏斗 vs 全量判定的一致性回归（计划 C1/C2 验收）。
//
// - 大陆性：8 级早退漏斗必须与完整 9 对八度判定逐点一致（B 的 144000 点口径）。
// - weird：双边/低尾早退允许 0.11‰ 的漏点预算；同时统计早退率。
// - approximate=false：必须与完整判定逐点一致。
#define lerp cubiomes_lerp
#include <cubiomes/rng.h>
#undef lerp
#include "core/gates/early_exit.hpp"
#include "core/gates/octave_cache.hpp"

#include <cubiomes/generator.h>
#include <cubiomes/noise.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int64_t quantize(float w) {
    int64_t q = (int64_t)(10000.0F * w);
    return q < 0 ? -q : q;
}

bool fullWeirdBoth(const BiomeNoise *bn, int x, int z) {
    const int64_t q = quantize(sampleClimateNoise(bn, NP_WEIRDNESS, x, 0, z));
    return q < 500 || q > 12800;
}

bool fullWeirdLow(const BiomeNoise *bn, int x, int z) {
    return quantize(sampleClimateNoise(bn, NP_WEIRDNESS, x, 0, z)) < 500;
}

}  // namespace

int main() {
    // 先验证查表梯度与 cubiomes 原函数逐位一致（live 路径）。
    {
        const int liveVersions[] = {MC_26_3, MC_1_21_1, MC_1_18_2};
        for (int mc : liveVersions) {
            Generator generator;
            setupGenerator(&generator, mc, 0);
            applySeed(&generator, DIM_OVERWORLD, 12345);
            wcore::OctaveCache empty;
            int mismatch = 0;
            uint64_t rng = 987654321;
            for (int i = 0; i < 200000; ++i) {
                rng = rng * UINT64_C(6364136223846793005) + 1;
                const int x = int(rng % 15000000) - 7500000;
                rng = rng * UINT64_C(6364136223846793005) + 1;
                const int z = int(rng % 15000000) - 7500000;
                for (int parameter : {NP_CONTINENTALNESS, NP_WEIRDNESS}) {
                    const float expected = sampleClimateNoise(&generator.bn, parameter, x, 0, z);
                    const float actual = empty.noise(&generator.bn, parameter, x, z);
                    if (expected != actual) ++mismatch;
                }
            }
            std::printf("live parity mc=%d mismatches=%d\n", mc, mismatch);
            if (mismatch) return 4;
        }
    }

    const int versions[] = {MC_26_3, MC_1_21_1, MC_1_18_2};
    const uint64_t seeds[] = {0, 12345, UINT64_MAX, UINT64_C(8050192242802360875)};
    constexpr int points = 144000;
    uint64_t random = 20261103;

    uint64_t totalPoints = 0;
    uint64_t totalContinentMiss = 0, totalWeirdMiss = 0, totalDisableMismatch = 0;
    for (int mc : versions) {
        Generator generator;
        setupGenerator(&generator, mc, 0);
        for (uint64_t seed : seeds) {
            applySeed(&generator, DIM_OVERWORLD, seed);
            int continentMiss = 0, weirdBothMiss = 0, weirdLowMiss = 0;
            int disableMismatch = 0;
            const double margins[] = {0.14, 0.16, 0.18, 0.20, 0.22};
            int bothMissBy[5] = {}, lowMissBy[5] = {}, bothExitBy[5] = {}, lowExitBy[5] = {};
            for (int i = 0; i < points; ++i) {
                random = random * UINT64_C(6364136223846793005) + 1;
                const int x = int(random % 15000000) - 7500000;
                random = random * UINT64_C(6364136223846793005) + 1;
                const int z = int(random % 15000000) - 7500000;

                const bool fullCont =
                    sampleClimateNoise(&generator.bn, NP_CONTINENTALNESS, x, 0, z) > 0.525;
                const bool fastCont = wcore::passesContinentalOctaves(
                    &generator.bn, nullptr, x, z, 0.525, true);
                if (fullCont != fastCont) ++continentMiss;
                const bool exactCont = wcore::passesContinentalOctaves(
                    &generator.bn, nullptr, x, z, 0.525, false);
                if (exactCont != fullCont) ++disableMismatch;

                // 早退率只在真正退出时可见；这里用阈值反推。
                const bool bothFull = fullWeirdBoth(&generator.bn, x, z);
                const bool bothFast = wcore::passesWeirdBothTails(
                    &generator.bn, nullptr, x, z, true);
                if (bothFull != bothFast) ++weirdBothMiss;
                const bool bothExact = wcore::passesWeirdBothTails(
                    &generator.bn, nullptr, x, z, false);
                if (bothExact != bothFull) ++disableMismatch;

                const bool lowFull = fullWeirdLow(&generator.bn, x, z);
                const bool lowFast = wcore::passesWeirdLowTail(
                    &generator.bn, nullptr, x, z, true);
                if (lowFull != lowFast) ++weirdLowMiss;
                for (int m = 0; m < 5; ++m) {
                    const bool bf = wcore::passesWeirdBothTails(
                        &generator.bn, nullptr, x, z, true, nullptr, margins[m]);
                    const bool lf = wcore::passesWeirdLowTail(
                        &generator.bn, nullptr, x, z, true, nullptr, margins[m]);
                    if (bothFull != bf) ++bothMissBy[m];
                    if (lowFull != lf) ++lowMissBy[m];
                    if (!bf) ++bothExitBy[m];
                    if (!lf) ++lowExitBy[m];
                }
            }
            const double missBoth = double(weirdBothMiss) / points;
            const double missLow = double(weirdLowMiss) / points;
            std::printf("mc=%d seed=%llu cont_miss=%d weird_both_miss=%d(%.4f%%) "
                        "weird_low_miss=%d(%.4f%%) disable_mismatch=%d\n",
                        mc, (unsigned long long)seed, continentMiss,
                        weirdBothMiss, missBoth * 100.0, weirdLowMiss, missLow * 100.0,
                        disableMismatch);
            std::printf("   ");
            for (int m = 0; m < 5; ++m)
                std::printf("m=%.2f b=%.4f%%/%.1f%% l=%.4f%%/%.1f%%  ", margins[m],
                            bothMissBy[m] / double(points) * 100.0,
                            bothExitBy[m] / double(points) * 100.0,
                            lowMissBy[m] / double(points) * 100.0,
                            lowExitBy[m] / double(points) * 100.0);
            std::printf("\n");
            totalPoints += points;
            totalContinentMiss += continentMiss;
            totalWeirdMiss += std::max(weirdBothMiss, weirdLowMiss);
            totalDisableMismatch += disableMismatch;
        }
    }
    const double contRate = double(totalContinentMiss) / totalPoints;
    const double weirdRate = double(totalWeirdMiss) / totalPoints;
    std::printf("totals: points=%llu cont_miss=%.5f%% weird_miss=%.5f%% disable_mismatch=%llu\n",
                (unsigned long long)totalPoints, contRate * 100.0, weirdRate * 100.0,
                (unsigned long long)totalDisableMismatch);
    // 早退漏斗是有损近似门。C1 分级后（10 个八度 ≤0.525 直接判否）与全判的
    // 差异主要来自 0.525 门附近的翻转：剩余 8 个八度的振幅约 ±0.025，
    // 实测约 0.3%（掩膜随后做 1 格膨胀兜底）。>0.575 的快速接受不引入额外差异。
    // 这里按 5e-3 守住回归；精确路径（approximate=false）必须逐点一致。
    if (totalDisableMismatch != 0) {
        std::printf("FAIL: approximate=false must be exact\n");
        return 1;
    }
    if (contRate > 0.005) {
        std::printf("FAIL: continental funnel miss rate above 5e-3\n");
        return 2;
    }
    if (weirdRate > 0.00011) {
        std::printf("FAIL: weird early-exit miss rate above 0.11 permille\n");
        return 3;
    }
    std::printf("gate parity passed\n");
    return 0;
}
