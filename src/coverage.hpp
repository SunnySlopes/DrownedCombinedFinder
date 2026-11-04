#pragma once
#include "engine.hpp"
#include "spawn_rules.hpp"
#include <array>
#include <map>
#include <tuple>
#include <utility>

namespace riverfinder_native {
using ExactScore = __int128;

ExactScore scoreKey(const Hit &hit, const Config &cfg);
ExactScore scoreKey(const Hit &hit);
bool meetsMinimum(const Hit &hit, const Config &cfg);
int floorDiv(int value, int divisor);
int64_t pureRiverUpperBound(const Config &cfg);
bool naturalChunk(int cx, int cz, int x, int z, int simulationDistance);
/// 与 Config 相关的精确评分（含 caveConversion），保留完整精度。
double scoreExact(const Hit &hit, const Config &cfg);

struct GeometryRow {
    int dz, outer, inner;
};

// 属于搜索工作单元，不依赖平台线程局部对象的析构顺序。
struct GeometryCache {
    std::map<std::tuple<int,int,bool,bool>,std::vector<GeometryRow>> kernels;
    std::map<std::tuple<int,int,int>,std::array<std::pair<int,int>,17>> naturalRows;
};

// 数据为逐层Z/X排列的群系编号；同一采样窗口由所有站位和挂机Y共享。
class BiomeWindow {
    int x_, z_, width_, height_, layers_, scale_;
    std::vector<uint32_t> river_, cave_, riverProjected_, caveProjected_;
    std::vector<uint64_t> riverIntegral_, caveIntegral_;
    int64_t rowSum(const std::vector<uint32_t> &prefix, int layer, int z, int left, int right) const;
    int64_t rectangle(const std::vector<uint64_t> &prefix, int left, int top, int right, int bottom) const;
    void buildIntegrals();

  public:
    BiomeWindow(int x, int z, int width, int height, int layers, const int *biomes, int scale = 1);
    // 已合并垂直权重的粗筛窗口，只用于投影计分，不能用于最终逐层球壳计分。
    BiomeWindow(int x, int z, int width, int height, const uint16_t *river, const uint16_t *cave, int scale);
    bool hasTargets(int x, int z, int radius) const;
    Hit evaluate(const Config &cfg, int x, int z, int playerY, bool upperBound = false, GeometryCache *cache = nullptr) const;
    // 平台解析解：所有层都在玩家 ±24 内时总分与挂机 Y 无关，不再逐 Y 穷举。
    Hit bestHeight(const Config &cfg, int x, int z, Control *control = nullptr, GeometryCache *cache = nullptr) const;
};

std::vector<Hit> selectHotspots(std::vector<Hit> hits, const Config &cfg);
}
