#pragma once
#include <numeric>

namespace riverfinder_native {
// 原版表审计见tests/drowned-rules-metadata.json。系数按选择概率、组尝试数和准入概率推导，
// 不包含高度抽样、组内移动和实体上限，因此只作为预计收益权重。
struct SpawnRules {
    int riverTotal = 615, riverWeight = 100, riverGroup = 1, riverChance = 15;
    int caveTotal = 610, caveWeight = 95, caveGroup = 4, caveChance = 40;
};
constexpr SpawnRules kSpawnRules;
constexpr int kUnreducedCave = kSpawnRules.caveWeight*kSpawnRules.caveGroup*
    kSpawnRules.riverTotal*kSpawnRules.riverChance;
constexpr int kUnreducedRiver = kSpawnRules.caveTotal*kSpawnRules.caveChance*
    kSpawnRules.riverWeight*kSpawnRules.riverGroup;
constexpr int kCoefficientGcd = std::gcd(kUnreducedCave,kUnreducedRiver);
constexpr int kRiverWeight = kUnreducedRiver/kCoefficientGcd;
constexpr int kCaveWeight = kUnreducedCave/kCoefficientGcd;
static_assert(kRiverWeight == 4880 && kCaveWeight == 7011);
}
