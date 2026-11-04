// C3 大坑链路诊断：分别统计 A0 种子、A 链极值、B 链峰与大陆性过滤后的数量。
#define lerp cubiomes_lerp
#include <cubiomes/rng.h>
#undef lerp
#include "core/pit/pit_search.hpp"

#include <cubiomes/generator.h>

#include <cstdio>

int main(int argc, char **argv) {
    const uint64_t seed = argc > 1 ? strtoull(argv[1], nullptr, 10) : 12345;
    Generator generator;
    setupGenerator(&generator, MC_26_3, 0);
    applySeed(&generator, DIM_OVERWORLD, seed);
    const DoublePerlinNoise *weird = &generator.bn.climate[NP_WEIRDNESS];
    wcore::pit::Progress progress;
    auto seeds = wcore::pit::detail::scanA0(weird, &progress, 8);
    std::printf("seed=%llu A0 seeds=%zu\n", (unsigned long long)seed, seeds.size());
    auto extremes = wcore::pit::detail::refineAChain(weird, seeds, &progress);
    std::printf("A-chain extremes=%zu\n", extremes.size());
    auto peaks = wcore::pit::detail::scanBChain(weird, extremes, &progress, 8);
    std::printf("B-chain peaks=%zu\n", peaks.size());
    int inside = 0;
    for (const auto &p : peaks)
        if (p.bx >= -131072 && p.bx <= 131072 && p.bz >= -131072 && p.bz <= 131072) ++inside;
    std::printf("inside 131k box=%d\n", inside);
    int cont = 0;
    for (const auto &p : peaks)
        if (wcore::passesContinentalOctaves(&generator.bn, nullptr, p.bx, p.bz, 0.55, true)) ++cont;
    std::printf("continental pass=%d\n", cont);
    return 0;
}
