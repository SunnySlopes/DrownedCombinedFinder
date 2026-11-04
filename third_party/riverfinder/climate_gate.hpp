#pragma once
// 从 RiverFinder-JNI 的 river_finder.cpp 复用气候快速门；许可见相邻 LICENSE。
// 精确阶段始终用维护版 Cubiomes，不能把这道近似门当作最终群系。
//
// C1b：全版本查表梯度。26.3 的 cubiomes 路径本身已是 16×3 表 + float 交错累加；
// pre-26.3 的 sampleDoublePerlin / sampleClimateNoise 换成 core 的查表版本，
// 逐位等同原函数（求和顺序、maintainPrecision、总振幅都保持）。
#define lerp cubiomes_lerp
#include <cubiomes/rng.h>
#undef lerp
#include <cubiomes/generator.h>
#include <cubiomes/biomes.h>

#include "../../core/gates/octave_cache.hpp"

namespace riverfinder {
static inline int sampleRiverBiomeNoise(const BiomeNoise *bn, int x, int y, int z,
                                        uint32_t sample_flags, const float *knownWeirdness = nullptr)
{
#ifdef RIVER_STRICT_BIOME
    (void)bn;
    return 0; /* unused; the call site uses sampleBiomeNoise() instead */
#else
    if (bn->nptype >= 0)
    {
        int64_t id = (int64_t) (10000.0 * sampleClimatePara(bn, nullptr, x, z));
        return (int) id;
    }

    float t = 0, h = 0, c = 0, e = 0, d = 0, w = 0;
    double px = x, pz = z;
    if (!(sample_flags & SAMPLE_NO_SHIFT))
    {
        if (bn->mc >= MC_26_3) {
            px += sampleClimateNoise(bn, NP_SHIFT, x, 0, z) * 4.0F;
            pz += sampleClimateNoise(bn, NP_SHIFT, z, x, 0) * 4.0F;
        } else {
            px += wcore::sampleDoublePerlinTable(&bn->climate[NP_SHIFT], x, 0, z) * 4.0;
            pz += wcore::sampleDoublePerlinTable(&bn->climate[NP_SHIFT], z, x, 0) * 4.0;
        }
    }

    // 已知值必须来自相同采样坐标；纯河流无shift粗筛用它复用周期表。
    w = knownWeirdness ? *knownWeirdness : wcore::sampleClimateNoiseTable(bn, NP_WEIRDNESS, px, 0, pz);
    // 群系树先乘10000再向零截断；直接比较浮点会漏掉-500等有效边界。
    const int64_t qw = (int64_t)(10000.0F * w);
    if (qw > 500 || qw < -500) return 0;
    c = wcore::sampleClimateNoiseTable(bn, NP_CONTINENTALNESS, px, 0, pz);
    const int64_t qc = (int64_t)(10000.0F * c);
    if (qc < -1900) return 0;
    e = wcore::sampleClimateNoiseTable(bn, NP_EROSION, px, 0, pz);
    const int64_t qe = (int64_t)(10000.0F * e);
    if (qc > 300 && qe < -3750) return 0;
    t = wcore::sampleClimateNoiseTable(bn, NP_TEMPERATURE, px, 0, pz);
    if ((int64_t)(10000.0F * t) < -4500) return 0;
    if (qe > 5500 && qc >= -1100) return 0;

    if (!(sample_flags & SAMPLE_NO_DEPTH))
    {
        d = sampleClimateDepth(bn, c, e, w, y*4);
    }

    h = wcore::sampleClimateNoiseTable(bn, NP_HUMIDITY, px, 0, pz);

    int64_t l_np[6];
    l_np[0] = (int64_t) (10000.0F * t);
    l_np[1] = (int64_t) (10000.0F * h);
    l_np[2] = (int64_t) (10000.0F * c);
    l_np[3] = (int64_t) (10000.0F * e);
    l_np[4] = (int64_t) (10000.0F * d);
    l_np[5] = (int64_t) (10000.0F * w);

    if (sample_flags & SAMPLE_NO_BIOME)
        return none;
    return climateToBiome(bn->mc, (const uint64_t *) l_np, nullptr);
#endif
}

static inline int riverBiomeIdAt(const BiomeNoise *bn, int x, int y, int z,
                                 const float *knownWeirdness = nullptr)
{
#ifdef RIVER_STRICT_BIOME
    return sampleBiomeNoise(bn, nullptr, x, y, z, nullptr, SAMPLE_NO_SHIFT);
#else
    return sampleRiverBiomeNoise(bn, x, y, z, SAMPLE_NO_SHIFT, knownWeirdness);
#endif
}

}
