#pragma once
// 洞穴粗筛气候门：core/gates/early_exit.hpp 的参数化包装。
//
// 原实现从 LowYDripstoneCaveFinderGUI 的 BiomeSampler.cpp / SearchConfig.h 取来，
// 现在只保留阈值常量与调用方需要的函数形态；算法在 core/ 里只有一份。

#include "../../core/gates/early_exit.hpp"
#include "../../core/gates/octave_cache.hpp"

#include <cubiomes/biomenoise.h>

namespace dripstone {

// 仅用于候选预筛；精排不使用它裁掉深暗之域边界附近的有效格位。
constexpr int continentalnessScale = 128, weirdnessScale = 32;
constexpr double continentalnessThreshold = 0.525;

/** 无周期表的兼容入口（小范围搜索与自检用）。 */
inline bool passContinentalnessPartialThr(const BiomeNoise *bn, int bx, int bz,
                                          double threshold = continentalnessThreshold) {
    return wcore::passesContinentalOctaves(bn, nullptr, bx, bz, threshold, true);
}

/** 有周期表时走 8 级八度早退漏斗；approximate=false 时与完整判定逐格一致。 */
inline bool passContinentalness(const BiomeNoise *bn, const wcore::OctaveCache *cache,
                                int bx, int bz, bool approximate) {
    return wcore::passesContinentalOctaves(bn, cache, bx, bz,
                                           continentalnessThreshold, approximate);
}

/** 洞穴 weird 双边门（|w| < 0.05 或 |w| > 1.28）；带早退与周期表。 */
inline bool passCaveWeirdness(const BiomeNoise *bn, const wcore::OctaveCache *cache,
                              int bx, int bz, bool approximate, float *weirdness = nullptr) {
    return wcore::passesWeirdBothTails(bn, cache, bx, bz, approximate, weirdness);
}

inline bool passCaveWeirdness(const BiomeNoise *bn, int bx, int bz) {
    int wi = int(10000.0F * sampleClimateNoise(bn, NP_WEIRDNESS, bx, 0, bz));
    if (wi < 0) wi = -wi;
    return wi < 500 || wi > 12800;
}

/**
 * 洞穴粗筛单元：weird → 侵蚀。
 * weirdness/erosion 作为 out 参数回填，供后续 32 格气候插值复用。
 *
 * 这里刻意不再做大陆性检查：混合路径的候选列已经由 128 格大陆性掩膜（带 1 格
 * 膨胀）筛过，再按精确格位查一次会剪掉掩膜边缘的候选，破坏改造前的逐格一致。
 * 洞穴聚焦路径的大陆性漏斗在掩膜处执行（见 search.cpp 的 coarseTile）。
 */
inline bool passCoarseCaveCell(const BiomeNoise *bn, const wcore::OctaveCache *cache,
                               int bx, int bz, bool approximate,
                               float *weirdness = nullptr, float *erosion = nullptr) {
    float w = 0;
    if (!passCaveWeirdness(bn, cache, bx, bz, approximate, &w))
        return false;
    const float e = wcore::sampleClimateNoiseTable(bn, NP_EROSION, bx, 0, bz);
    if (weirdness) *weirdness = w;
    if (erosion) *erosion = e;
    return int(10000.0F * e) > -3750;
}

inline bool passCoarseCaveCell(const BiomeNoise *bn, int bx, int bz,
                               float *weirdness = nullptr, float *erosion = nullptr) {
    return passCoarseCaveCell(bn, nullptr, bx, bz, true, weirdness, erosion);
}

}  // namespace dripstone
