/* 精度验证：把参考实现的原始算法单独跑一遍，作为对照基准。
 *
 * 目的：回答「把参考实现接进项目骨架后，结果有没有被改坏」。
 * 这里只产出**参考侧**的权威候选集合，本引擎的结果由 precision.py
 * 通过 CLI 拿到后比对。两边用完全相同的 (种子, 范围, 采样高度, 面积下限)。
 *
 * ⚠️ include 顺序是硬要求，改错就编不过：
 *   · 先 <cmath>（让 std::lerp 先进全局名字空间）
 *   · 再 river_finder.cpp（它在首次 include rng.h 前把 lerp 宏改成 cubiomes_lerp）
 *   · 最后才 include cubiomes 其余头文件
 *   顺序反了会报 "std::lerp conflicts with a previous declaration"。
 *   本文件刻意不在顶部直接 include <cubiomes/*>，靠 river_finder.cpp 带进来。
 *
 * 编译：
 *   g++ -O3 -ffp-contract=off -std=c++20 -I third_party -I third_party/riverfinder \
 *       tools/reference_dump.cpp -o build/prec/reference_dump.exe build/libriver_core.a -static -lm
 */

#include <cmath>

#include "../third_party/riverfinder/river_finder.cpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <tuple>
#include <vector>

namespace {

std::set<std::tuple<int, int, int>> runReference(long long seed, int mc, int startX, int startZ,
                                                 int width, int height, int sampleY, int minArea,
                                                 int threads) {
    ::Generator generator{};
    setupGenerator(&generator, mc, FORCE_OCEAN_VARIANTS);
    applySeed(&generator, DIM_OVERWORLD, seed);

    ::riverfinder::Progress progress{};
    ::riverfinder::ThreadSafeResults<::riverfinder::Res> collected;
    ::riverfinder::findBiggestRiverParallelPool(collected, &generator, startX, startZ,
                                                width, height, sampleY, minArea, 0.0,
                                                &progress, threads);
    std::set<std::tuple<int, int, int>> out;
    for (const auto &item : collected.getAllResults())
        out.insert({item.point.x, item.point.y, item.riverArea});
    return out;
}

}  // namespace

int main(int argc, char **argv) {
    const long long seed = (argc > 1) ? std::atoll(argv[1]) : 12345;
    const int radius = (argc > 2) ? std::atoi(argv[2]) : 8000;
    const int sampleY = (argc > 3) ? std::atoi(argv[3]) : 63;
    const int minArea = (argc > 4) ? std::atoi(argv[4]) : 20000;

    std::printf("种子=%lld 半径=%d 采样高度=%d 面积下限=%d\n", seed, radius, sampleY, minArea);

    const auto reference = runReference(seed, MC_NEWEST, -radius, -radius, radius * 2, radius * 2,
                                        sampleY, minArea, 8);

    std::printf("参考实现候选数: %zu\n", reference.size());
    for (const auto &[x, z, area] : reference)
        std::printf("REF\t%d\t%d\t%d\n", x, z, area);
    return 0;
}
