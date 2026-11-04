#pragma once
// 挂机点平台解析解（计划 §1.2），替代逐 Y 穷举的 optimizePlayerY。
//
// 三维球壳 576 < dx² + dy² + dz² ≤ 16384 在固定层高 dy 处的水平截面积：
//   |dy| ≤ 24 : π[(16384-dy²) - (576-dy²)] = π·15808   恒定
//   |dy| >  24 : π(16384-dy²)                          单调下降
// 所以只要所有层都落在玩家 ±24 格内，每层覆盖面积相同，总分与挂机 Y 无关。

namespace wcore::platform {

inline constexpr int kShellInnerSq = 576;      // 24²：禁刷球
inline constexpr int kShellOuterSq = 16384;    // 128²：模拟距离外沿
inline constexpr int kMaxHalfSpan = 24;

/** 层范围的安全挂机 Y。
 *
 * 平台条件：所有层都落在玩家 ±24 内。满足它的 Y 有很多个，选哪一个只差离散
 * 格点计数的零头；这里优先取 yMin，使默认 -63..-51 的逐层 dy 仍是 0..12，
 * 与旧固定挂机 Y=-63 的整数面积逐位一致（计划 §12 的 C6 验收）。
 *   span ≤ 25 : yMin                       （dy = 0..span-1 ≤ 24）
 *   span ≤ 48 : yMin + 24                  （dy = -24..span-25）
 *   span > 48 : (yMin + yMax) / 2          （必有层超出 ±24，取中点）
 */
inline int playerY(int yMin, int yMax) {
    const int span = yMax - yMin + 1;
    if (span <= kMaxHalfSpan + 1) return yMin;
    if (span <= 2 * kMaxHalfSpan + 1) return yMin + kMaxHalfSpan;
    return (yMin + yMax) / 2;
}

/** 该挂机 Y 是否让所有层都落在 ±24 内。 */
inline bool onPlatform(int yMin, int yMax, int playerY) {
    return playerY - yMin <= kMaxHalfSpan && yMax - playerY <= kMaxHalfSpan;
}

/** dy 层的球壳面积（以格点计的上界，仅用于诊断与文档）。 */
inline double shellArea(int dy) {
    const double vertical = double(dy) * dy;
    const double outer = 16384.0 - vertical;
    const double inner = 576.0 - vertical;
    if (inner <= 0) return outer > 0 ? 3.14159265358979323846 * outer : 0.0;
    return 3.14159265358979323846 * (outer - inner);
}

/** 整数平方根（floor）；constexpr 让显示分母可以编译期定型。 */
constexpr int isqrtFloor(long long value) {
    if (value <= 0) return 0;
    long long lo = 0, hi = 1;
    while (hi * hi <= value) hi *= 2;
    while (lo + 1 < hi) {
        const long long mid = (lo + hi) / 2;
        if (mid * mid <= value) lo = mid; else hi = mid;
    }
    return int(lo);
}

/**
 * dy 层的 24..128 球壳格点数（与运行时求和完全一致）：
 *   每行 outer = floor(sqrt(128² - dy² - dz²))，inner = floor(sqrt(24² - dy² - dz²))，
 *   行贡献 2·outer+1，被禁刷球切到时再减 2·inner+1；|dy| > 128 返回 0。
 *
 * 注意：这是**不裁剪**的球壳格数。coverage=game 还会按自然刷怪区块与模拟距离
 * 进一步裁剪，所以它只是显示用上界，不是 game 模式下可达的满分。
 */
constexpr long long shellLayerCells(int dy) {
    const long long dy2 = (long long) dy * (long long) dy;
    if (dy2 > (long long) kShellOuterSq) return 0;
    long long total = 0;
    const int outerRadius = 128;
    for (int dz = -outerRadius; dz <= outerRadius; ++dz) {
        const long long dz2 = (long long) dz * (long long) dz;
        const long long out2 = (long long) kShellOuterSq - dy2 - dz2;
        if (out2 < 0) continue;
        total += 2 * (long long) isqrtFloor(out2) + 1;
        const long long in2 = (long long) kShellInnerSq - dy2 - dz2;
        if (in2 >= 0) total -= 2 * (long long) isqrtFloor(in2) + 1;
    }
    return total;
}

/**
 * 当前模型的显示分母（与 B 的 RIVER_AREA_FULL 同源）：
 * 把 [yMin, yMax] 每层球壳格数求和后除以层数并四舍五入。
 * 默认 -63..-51 配 playerY=-40 时等于 49659。
 */
constexpr int fullShellArea(int yMin, int yMax, int playerY) {
    int layers = yMax - yMin + 1;
    if (layers < 1) layers = 1;
    long long sum = 0;
    for (int layer = 0; layer < layers; ++layer)
        sum += shellLayerCells(yMin + layer - playerY);
    return int((sum + layers / 2) / layers);
}

}  // namespace wcore::platform
