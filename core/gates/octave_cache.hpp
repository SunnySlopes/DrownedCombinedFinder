#pragma once
// A 组周期表（Cont A0..A8 / Ridge A0..A2）与查表梯度采样。
//
// 来源：LowYDripstoneCaveFinderBase/OctaveFieldCache.{h,cpp}（MIT，建表/对齐逻辑）
// 与 river-native 原 third_party/dripstone/octave_cache.hpp（26.3 float 交错累加）。
// 两版数值语义都在这里固定：
//   26.3      ：sum += A; sum += B;（float 交错累加，amplitude 已在 term() 内）
//   pre-26.3 ：先分别求 A 组、B 组，最后乘总 amplitude（double）
// 早退漏斗只能在同一条累加序列上比较，见 early_exit.hpp。

#include "perlin_table.hpp"

#include <cubiomes/biomenoise.h>
#include <cubiomes/noise.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace wcore {

struct OctaveCache {
    struct Field {
        int step = 0, side = 0, phaseX = 0, phaseZ = 0;
        std::vector<double> values;
    };
    std::array<Field, 9> continental;
    std::array<Field, 3> weirdness;

    /** 单八度分量；命中周期表时直接取值。pre-26.3 不含 amplitude。 */
    static double live(const BiomeNoise *bn, int parameter, int index, bool second, int x, int z) {
        const auto &noise = bn->climate[parameter];
        const auto *p = (second ? noise.octB.octaves : noise.octA.octaves) + index;
        double f = p->lacunarity * (second ? 337.0 / 331.0 : 1.0);
        if (bn->mc >= MC_26_3) {
            float weight = float(noise.amplitude * p->amplitude);
            return weight * samplePerlinTableFloat(p, x * f, 0, z * f);
        }
        return p->amplitude * samplePerlinTable(p, maintainPrecision(x * f), 0,
                                                maintainPrecision(z * f));
    }

    double term(const BiomeNoise *bn, int parameter, int index, bool second, int x, int z) const {
        const Field *field = !second && parameter == NP_CONTINENTALNESS && index < 9 ? &continental[index] :
            !second && parameter == NP_WEIRDNESS && index < 3 ? &weirdness[index] : nullptr;
        if (field && field->side && (x - field->phaseX) % field->step == 0 &&
            (z - field->phaseZ) % field->step == 0) {
            int ix = (x - field->phaseX) / field->step, iz = (z - field->phaseZ) / field->step;
            ix = (ix % field->side + field->side) % field->side;
            iz = (iz % field->side + field->side) % field->side;
            return field->values[size_t(iz) * field->side + ix];
        }
        return live(bn, parameter, index, second, x, z);
    }

    /** 该八度是否命中周期表（只用于 --bench 的命中率统计）。 */
    bool onLattice(int parameter, int index, int x, int z) const {
        if ((parameter != NP_CONTINENTALNESS && parameter != NP_WEIRDNESS) || index < 0)
            return false;
        const Field &field = parameter == NP_CONTINENTALNESS ? continental[index] : weirdness[index];
        return field.side && (x - field.phaseX) % field.step == 0 &&
               (z - field.phaseZ) % field.step == 0;
    }

    /** 完整 18/6 个八度；与 sampleClimateNoise() 逐位相同。 */
    float noise(const BiomeNoise *bn, int parameter, int x, int z) const {
        const auto &source = bn->climate[parameter];
        if (bn->mc >= MC_26_3) {
            float sum = 0;
            int count = std::max(source.octA.octcnt, source.octB.octcnt);
            for (int i = 0; i < count; ++i) {
                if (i < source.octA.octcnt) sum += float(term(bn, parameter, i, false, x, z));
                if (i < source.octB.octcnt) sum += float(term(bn, parameter, i, true, x, z));
            }
            return sum;
        }
        double a = 0, b = 0;
        for (int i = 0; i < source.octA.octcnt; ++i) a += term(bn, parameter, i, false, x, z);
        for (int i = 0; i < source.octB.octcnt; ++i) b += term(bn, parameter, i, true, x, z);
        return float((a + b) * source.amplitude);
    }

    uint64_t bytes() const {
        uint64_t count = 0;
        for (const auto &field : continental) count += field.values.size() * sizeof(double);
        for (const auto &field : weirdness) count += field.values.size() * sizeof(double);
        return count;
    }
};

// ---------------------------------------------------------------------------
// 查表梯度版 climate 采样：pre-26.3 走 16×3 表，逐位等同 sampleDoublePerlin()。
// 26.3 仍用 cubiomes 的 float 路径（它本身已是梯度表，无 switch）。
// ---------------------------------------------------------------------------

inline double sampleOctaveTable(const OctaveNoise *noise, double x, double y, double z) {
    double v = 0;
    for (int i = 0; i < noise->octcnt; ++i) {
        const PerlinNoise *p = noise->octaves + i;
        const double lf = p->lacunarity;
        const double ax = maintainPrecision(x * lf);
        const double ay = maintainPrecision(y * lf);
        const double az = maintainPrecision(z * lf);
        v += p->amplitude * samplePerlinTable(p, ax, ay, az);
    }
    return v;
}

/** 与 sampleDoublePerlin() 逐位相同（pre-26.3 的 double 路径）。 */
inline double sampleDoublePerlinTable(const DoublePerlinNoise *noise,
                                      double x, double y, double z) {
    const double f = 337.0 / 331.0;
    double v = 0;
    v += sampleOctaveTable(&noise->octA, x, y, z);
    v += sampleOctaveTable(&noise->octB, x * f, y * f, z * f);
    return v * noise->amplitude;
}

/** 与 sampleClimateNoise() 逐位相同的查表版本（26.3 委托给 cubiomes 的 float 路径）。 */
inline float sampleClimateNoiseTable(const BiomeNoise *bn, int parameter,
                                     double x, double y, double z) {
    const DoublePerlinNoise *noise = bn->climate + parameter;
    return bn->mc >= MC_26_3 ? sampleClimateNoise(bn, parameter, x, y, z)
                             : (float)sampleDoublePerlinTable(noise, x, y, z);
}

}  // namespace wcore
