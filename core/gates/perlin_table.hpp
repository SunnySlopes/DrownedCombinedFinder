#pragma once
// 16×3 梯度表版 Perlin，逐位等同 cubiomes 的 switch 版本。
//
// 来源：LowYDripstoneCaveFinderBase/PerlinTable.h（MIT），float 版按
// third_party/cubiomes/noise.c 的 samplePerlinFloatRaw 逐句搬运。放在此处而不是
// cubiomes/ 里，因为 vendored cubiomes 由 cubiomes-lock.json 的逐文件 SHA256 固定，
// 不能打本地补丁。
//
// 求和顺序必须保持 (gx*a + gy*b) + gz*c，插值顺序与 cubiomes 相同。

#include <cubiomes/noise.h>
#include <cmath>
#include <cstdint>

namespace wcore {

/** 与 cubiomes lerp(): from + part * (to - from) 相同。 */
inline double perlinLerp(double part, double from, double to) {
    return from + part * (to - from);
}

/** indexedLerp() 的十六个梯度方向（12..15 重复）。 */
inline double gradientTable(uint8_t index, double a, double b, double c) {
    static const double g[16][3] = {
        { 1.0,  1.0,  0.0}, {-1.0,  1.0,  0.0},
        { 1.0, -1.0,  0.0}, {-1.0, -1.0,  0.0},
        { 1.0,  0.0,  1.0}, {-1.0,  0.0,  1.0},
        { 1.0,  0.0, -1.0}, {-1.0,  0.0, -1.0},
        { 0.0,  1.0,  1.0}, { 0.0, -1.0,  1.0},
        { 0.0,  1.0, -1.0}, { 0.0, -1.0, -1.0},
        { 1.0,  1.0,  0.0}, { 0.0, -1.0,  1.0},
        {-1.0,  1.0,  0.0}, { 0.0, -1.0, -1.0}
    };
    const double *v = g[index & 15];
    return (v[0] * a + v[1] * b) + v[2] * c;
}

/**
 * 与 samplePerlin(noise, d1, 0.0, d3, 0, 0) 逐位相同。
 * yamp/ymax 地形参数不支持：本库所有调用点都在 y = 0 采样气候。
 */
inline double samplePerlinTable(const PerlinNoise *noise, double d1, double d2, double d3) {
    uint8_t h1, h2, h3;
    double t1, t2, t3;

    // y == 0：复用每八度预计算偏移，与 samplePerlin() 的 d2 == 0.0 分支一致。
    if (d2 == 0.0) {
        d2 = noise->d2;
        h2 = noise->h2;
        t2 = noise->t2;
    } else {
        d2 += noise->b;
        const double i2 = std::floor(d2);
        d2 -= i2;
        h2 = (uint8_t)(int)i2;
        t2 = d2*d2*d2 * (d2 * (d2*6.0-15.0) + 10.0);
    }

    d1 += noise->a;
    d3 += noise->c;

    const double i1 = std::floor(d1);
    const double i3 = std::floor(d3);
    d1 -= i1;
    d3 -= i3;

    h1 = (uint8_t)(int)i1;
    h3 = (uint8_t)(int)i3;

    t1 = d1*d1*d1 * (d1 * (d1*6.0-15.0) + 10.0);
    t3 = d3*d3*d3 * (d3 * (d3*6.0-15.0) + 10.0);

    const uint8_t *idx = noise->d;

    // 与 samplePerlin() 相同的哈希链；+1 取值依赖 idx[256] == idx[0]。
    const uint8_t a1 = (uint8_t)(idx[h1]      + h2);
    const uint8_t b1 = (uint8_t)(idx[h1 + 1]  + h2);
    const uint8_t a2 = (uint8_t)(idx[a1]      + h3);
    const uint8_t b2 = (uint8_t)(idx[a1 + 1]  + h3);
    const uint8_t c2 = (uint8_t)(idx[b1]      + h3);
    const uint8_t d2h = (uint8_t)(idx[b1 + 1] + h3);

    const uint8_t e1 = idx[a2],  e2 = idx[a2 + 1];
    const uint8_t e3 = idx[b2],  e4 = idx[b2 + 1];
    const uint8_t e5 = idx[c2],  e6 = idx[c2 + 1];
    const uint8_t e7 = idx[d2h], e8 = idx[d2h + 1];

    double l1 = gradientTable(e1, d1,   d2,   d3);
    double l5 = gradientTable(e2, d1,   d2,   d3-1);
    double l2 = gradientTable(e5, d1-1, d2,   d3);
    double l6 = gradientTable(e6, d1-1, d2,   d3-1);
    double l3 = gradientTable(e3, d1,   d2-1, d3);
    double l7 = gradientTable(e4, d1,   d2-1, d3-1);
    double l4 = gradientTable(e7, d1-1, d2-1, d3);
    double l8 = gradientTable(e8, d1-1, d2-1, d3-1);

    l1 = perlinLerp(t1, l1, l2);
    l3 = perlinLerp(t1, l3, l4);
    l5 = perlinLerp(t1, l5, l6);
    l7 = perlinLerp(t1, l7, l8);

    l1 = perlinLerp(t2, l1, l3);
    l5 = perlinLerp(t2, l5, l7);

    return perlinLerp(t3, l1, l5);
}

// ---------------------------------------------------------------------------
// float 版：逐句对应 cubiomes samplePerlinFloatRaw(..., smear = 0)。
// 26.3 的坐标与偏移仍是 double，格内坐标、梯度、插值逐步舍入为 float。
// ---------------------------------------------------------------------------

inline float gradientTableFloat(uint8_t index, float a, float b, float c) {
    static const float g[16][3] = {
        {1,1,0}, {-1,1,0}, {1,-1,0}, {-1,-1,0},
        {1,0,1}, {-1,0,1}, {1,0,-1}, {-1,0,-1},
        {0,1,1}, {0,-1,1}, {0,1,-1}, {0,-1,-1},
        {1,1,0}, {0,-1,1}, {-1,1,0}, {0,-1,-1}
    };
    const float *v = g[index & 15];
    return (v[0]*a + v[1]*b) + v[2]*c;
}

inline float lerpFloat(float t, float a, float b) {
    return a + t * (b - a);
}

inline double wrapFloatNoise(double x) {
    return x - std::floor(x / 33554432.0 + 0.5) * 33554432.0;
}

/** 与 samplePerlinFloat(noise, x, y, z) 逐位相同（smear = 0）。 */
inline float samplePerlinTableFloat(const PerlinNoise *noise, double x, double y, double z) {
    x = wrapFloatNoise(x) + noise->a;
    y = wrapFloatNoise(y) + noise->b;
    z = wrapFloatNoise(z) + noise->c;
    const int ix = (int)std::floor(x), iy = (int)std::floor(y), iz = (int)std::floor(z);
    const float dx = (float)(x - ix), dy = (float)(y - iy), dz = (float)(z - iz);
    const float tx = dx*dx*dx * (dx * (dx*6.0F - 15.0F) + 10.0F);
    const float ty = dy*dy*dy * (dy * (dy*6.0F - 15.0F) + 10.0F);
    const float tz = dz*dz*dz * (dz * (dz*6.0F - 15.0F) + 10.0F);
    const uint8_t *permutation = noise->d;
    float corners[2][2][2];
    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b)
            for (int c = 0; c < 2; ++c) {
                const int h = permutation[(permutation[(permutation[(ix+a)&255]+iy+b)&255]+iz+c)&255];
                corners[a][b][c] = gradientTableFloat(uint8_t(h), dx-a, dy-b, dz-c);
            }
    return lerpFloat(tz,
        lerpFloat(ty, lerpFloat(tx, corners[0][0][0], corners[1][0][0]),
                      lerpFloat(tx, corners[0][1][0], corners[1][1][0])),
        lerpFloat(ty, lerpFloat(tx, corners[0][0][1], corners[1][0][1]),
                      lerpFloat(tx, corners[0][1][1], corners[1][1][1])));
}

}  // namespace wcore
