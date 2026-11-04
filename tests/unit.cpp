/* river-native 的本地单元测试。
 *
 * 只测本仓库维护的外壳（版本映射、排名、配置校验、导出），不测 cubiomes
 * 与参考实现的算法本体——那部分由第三方自己的行为决定。
 *
 * 排名口径是本引擎最容易出错的地方，所以重点覆盖：
 *   · 面积降序优先
 *   · 面积相同 → 距离升序
 *   · 距离也相同 → X、Z 升序（保证确定性）
 */
#include "../src/storage.hpp"

#include <cubiomes/biomes.h>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string &what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what.c_str());
    ++failures;
}

template <typename Fn>
void throws(Fn fn, const std::string &what) {
    try {
        fn();
    } catch (const std::exception &) {
        return;
    }
    std::printf("FAIL: 期望抛异常但没抛 —— %s\n", what.c_str());
    ++failures;
}

void testVersions() {
    using namespace riverfinder_native;
    // 已发布的32个版本入口；只接受明确列出的版本。
    check(versions().size() == 32, "版本数量应为 32");

    // 版本按序映射到 cubiomes 常量，不能枚举字面量 + else 兜底
    const VersionInfo v1182 = versionInfo("1.18.2");
    check(v1182.kernelName == "1.18.x", "1.18.2 版本类名");

    check(versionInfo("1.18").cubiomesMc == MC_1_18_2, "1.18采用已对拍的群系类");
    check(versionInfo("1.18.1").cubiomesMc == MC_1_18_2, "1.18.1采用已对拍的群系类");
    check(versionInfo("1.19.3").cubiomesMc == MC_1_19_4, "1.19.3使用6分支群系树");
    check(versionInfo("1.20.2").cubiomesMc == MC_1_20_6, "1.20.2群系类");
    check(versionInfo("1.21.2").cubiomesMc == MC_1_21_3, "1.21.2不能落到1.21.1");
    check(versionInfo("26.1.2").cubiomesMc == MC_26_1, "26.1.2群系类");
    const VersionInfo v1218 = versionInfo("1.21.6");
    check(v1218.kernelName == "1.19~1.21.8", "1.21.6 属于 1.19~1.21.8");

    const VersionInfo v1219 = versionInfo("1.21.9");
    check(v1219.kernelName == "1.21.9+", "1.21.9 属于 1.21.9+");

    // 关键回归：1.21.11 必须比 1.21.9 更靠后，不能被错归
    const VersionInfo v12111 = versionInfo("1.21.11");
    check(v12111.cubiomesMc == MC_1_21_11, "1.21.11 映射到 MC_1_21_11");

    const VersionInfo v263 = versionInfo("26.3");
    check(v263.cubiomesMc == MC_26_3, "26.3 映射到 MC_26_3");
    check(v263.kernelName == "26.x", "26.3 版本类名");

    const VersionInfo v261 = versionInfo("26.1");
    check(v261.cubiomesMc == MC_26_1, "26.1 映射到 MC_26_1");

    throws([] { versionInfo("1.17"); }, "1.17 不在支持列表里应抛异常");
    throws([] { versionInfo("abc"); }, "非版本字符串应抛异常");
}

void testSeedParsing() {
    using namespace riverfinder_native;
    check(parseSeed("0") == 0, "种子 0");
    check(parseSeed("-1") == static_cast<uint64_t>(-1), "种子 -1 按 64 位回绕");
    // 64 位边界值必须能解析（前端用字符串传，不能用 Number）
    check(parseSeed("-9223372036854775808") == 9223372036854775808ULL, "int64 下界");
    check(parseSeed("9223372036854775807") == 9223372036854775807ULL, "int64 上界");

    throws([] { parseSeed(""); }, "空种子应抛异常");
    throws([] { parseSeed("12a"); }, "非数字种子应抛异常");
}

void testRanking() {
    using namespace riverfinder_native;
    Config cfg;
    cfg.centerX = 0;
    cfg.centerZ = 0;

    // 面积大的优先，与距离无关
    Hit far{};
    far.x = 1000;
    far.z = 0;
    far.riverArea = 50000;
    Hit near{};
    near.x = 10;
    near.z = 0;
    near.riverArea = 40000;
    check(better(far, near, cfg), "面积大的应排在前面，即使更远");

    // 面积相同 → 距离近的优先（用户明确要求）
    Hit near2{};
    near2.x = 100;
    near2.z = 0;
    near2.riverArea = 50000;
    check(better(near2, far, cfg), "面积相同时距离近的优先");

    // 面积、距离都相同 → X 升序
    Hit a{};
    a.x = 10;
    a.z = 0;
    a.riverArea = 50000;
    Hit b{};
    b.x = 20;
    b.z = 0;
    b.riverArea = 50000;
    check(better(a, b, cfg), "同面积同距离时 X 小的优先");

    // 完全对称两点（±x）：X 升序决定，保证结果与排序实现无关
    Hit neg{};
    neg.x = -10;
    neg.z = 0;
    neg.riverArea = 50000;
    Hit pos{};
    pos.x = 10;
    pos.z = 0;
    pos.riverArea = 50000;
    check(better(neg, pos, cfg), "对称点按 X 升序，结果必须确定");

    // 非原点中心：距离要相对实际中心算，不是相对 (0,0)
    Config shifted;
    shifted.centerX = 1000;
    shifted.centerZ = 0;
    Hit atCenter{};
    atCenter.x = 1000;
    atCenter.z = 0;
    atCenter.riverArea = 40000;
    Hit awayFromCenter{};
    awayFromCenter.x = 1500;
    awayFromCenter.z = 0;
    awayFromCenter.riverArea = 40000;
    check(better(atCenter, awayFromCenter, shifted), "距离按实际搜索中心算");
}

void testDistance() {
    using namespace riverfinder_native;
    Config cfg;
    cfg.centerX = 100;
    cfg.centerZ = -50;
    check(distanceSquared(cfg, 100, -50) == 0, "中心点距离为 0");
    check(distanceSquared(cfg, 103, -54) == 25, "3-4-5 勾股");
    // 必须用 64 位，30M 级别的平方会溢出 32 位
    check(distanceSquared(cfg, 30000000, -30000000) > 0, "远距离不溢出");
}

void testValidate() {
    using namespace riverfinder_native;
    Config cfg;
    cfg.radius = 100;
    throws([&] { validate(cfg); }, "radius 小于 128 应拒绝（分块要求）");

    Config good;
    good.radius = 1000;
    validate(good);  // 不应抛

    Config badShape;
    badShape.radius = 1000;
    badShape.shape = "triangle";
    throws([&] { validate(badShape); }, "非法 shape 应拒绝");

    Config badThreads;
    badThreads.radius = 1000;
    badThreads.threads = 0;
    throws([&] { validate(badThreads); }, "threads 为 0 应拒绝");
}

void testConfigRoundTrip() {
    using namespace riverfinder_native;
    Config cfg;
    cfg.seed = "-9223372036854775808";
    cfg.version = "26.3";
    cfg.radius = 5000;
    cfg.minArea = 30000;
    cfg.sampleY = 64;
    cfg.top = 7;

    const Json json = configJson(cfg);
    const Config back = configFromJson(json);
    check(back.seed == cfg.seed, "种子往返一致");
    check(back.radius == cfg.radius, "半径往返一致");
    check(back.minArea == cfg.minArea, "面积下限往返一致");
    check(back.sampleY == cfg.sampleY, "采样高度往返一致");
    check(back.top == cfg.top, "条数往返一致");

    // 未知字段必须被拒绝，避免用户以为改了参数其实没生效
    Json unknown = json;
    unknown["notAField"] = 1;
    throws([&] { configFromJson(unknown); }, "未知字段应拒绝");
}

}  // namespace

int main() {
    testVersions();
    testSeedParsing();
    testRanking();
    testDistance();
    testValidate();
    testConfigRoundTrip();

    if (failures == 0) {
        std::printf("单元测试全部通过\n");
        return 0;
    }
    std::printf("失败 %d 项\n", failures);
    return 1;
}
