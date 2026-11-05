#pragma once


#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace riverfinder_native {
using Clock = std::chrono::steady_clock;
constexpr int kWorldMin = -30000000, kWorldMax = 29999999;
/** caveConversion 的默认值；用于"未传 → 走旧口径快路径"的判定。 */
constexpr double kDefaultCaveConversion = 4880.0 / 7011.0;

/**
 * 固定挂机 Y 的“自动”哨兵（对齐 B 的 AFK_Y_AUTO）。
 * validate() 解析为 yMin + 23：所有采样层都落在玩家 ±24 内，球壳截面积恒定，
 * 总分与具体 Y 无关；+23 贴着平台下沿，给最低层留一格余量。
 */
constexpr int kPlayerYAuto = std::numeric_limits<int>::min();

/**
 * 自动精算预算的下限（优化 3：原为 1024；A 项再降到 256）。
 * 实测：r8000/r20000、多条种子下，256~300 与 800 的 Top50 逐位一致，
 * 只有 51~100 名尾部顺序会漂移。top≥86 时由 kExactBudgetPerTop*top 接管。
 */
constexpr int kDefaultExactBudget = 256;

/**
 * 自动精算预算的「每个 top 名额配多少精算候选」。
 * 原为 8：top=100 时自动值 800。实测降到 3（300）时 Top50 逐位一致（含分数与顺序），
 * 而精算耗时与预算近似线性，因此默认取 3。需要更稳的尾部时用 --exact-budget 显式调回。
 */
constexpr int kExactBudgetPerTop = 3;

/**
 * 自动精算预算中由候选池规模决定的一项：candidateBudget / 该值。
 * 原为 16（8192 → 512），会把上面的 3*top=300 顶回去，故放到 32（8192 → 256）。
 */
constexpr int kExactBudgetPerCandidate = 32;

/**
 * 并行工作线程数的校验上限。默认线程数 = 当前机器的硬件并发度，超过 32 核同样吃满，
 * 不再有「32 就是天花板」的隐含约束。
 *
 * 这里保留一个上限只是为了拦截明显荒谬的输入（例如 --threads 100000 会尝试创建
 * 十万个线程）；它不是性能或稳定性边界，仓库里没有「线程数到某个值就出问题」的实测依据。
 * 取 1024 是为了覆盖当前及可预见的机器（含多路服务器），真实硬件不会触到。
 *
 * 放开线程数时要注意：峰值内存与线程数近似线性。每个工作线程在精算阶段会同时持有
 * 一个 1:1 窗口（半径 128 时约 12 MB），32 线程实测峰值工作集约 0.4 GB，
 * 因此 128 线程约 1.5 GB。多核机器放开前应结合本机内存评估。
 */
constexpr int kMaxWorkerThreads = 1024;

/**
 * 默认线程数 = 当前机器的硬件并发度 std::thread::hardware_concurrency()。
 * - 该函数允许返回 0 表示“无法确定”，此时退回 16；
 * - 结果按 kMaxWorkerThreads 封顶，保证默认值一定能通过 validate()，
 *   否则核数超过上限的机器会在默认配置下直接报错。
 * 只求值一次并缓存：Config 在多处被默认构造（如 configJson 的字段模板）。
 */
inline int defaultThreads() {
    static const int cached = [] {
        unsigned count = std::thread::hardware_concurrency();
        if (count == 0)
            count = 16;
        return int(std::min(count, unsigned(kMaxWorkerThreads)));
    }();
    return cached;
}

// 信封及主指标降序沿用v3；0.3.0的参数和计分模型已升级，旧sampleY请求不兼容。
constexpr int kProtocolVersion = 3;
constexpr int kRankingRuleVersion = 3;
constexpr const char *kRankingRule = "score-desc-distance-asc-x-asc-z-asc";
constexpr const char *kFinder = "river";
constexpr const char *kEngine = "river-native";

struct Config {
    std::string seed = "0";
    std::string version = "26.3";
    int centerX = 0;
    int centerZ = 0;
    int radius = 100000;
    std::string shape = "circle";
    bool fullWorld = false;
    /* 默认线程数 = 当前机器的硬件并发度（封顶 kMaxWorkerThreads）。
     * 原为固定值，那只是当时开发机的取值，换机器就浪费或超订；
     * 现在小机器不超订、大机器能吃满，显式 --threads 仍可覆盖。 */
    int threads = defaultThreads();
    int top = 100;
    // 三字段门槛（C7）：minArea = R + 1.43668·C。minArea 是低层入口，
    // 三字段给定时由 minRiverArea/minCaveArea 推导。默认 = 河流 70% 上限。
    int minArea = 34748;
    bool includeCaves = false;
    std::string coverage = "game";
    int innerRadius = 128;
    int simulationDistanceChunks = 10;
    /** kPlayerYAuto = 由 validate() 解析为 yMin + 23（与 B 一致）。 */
    int playerY = kPlayerYAuto;
    // 勾选“计算最佳Y”时走平台解析解；未勾选且 playerY 为自动哨兵时用 yMin + 23。
    bool optimizePlayerY = false;
    /** playerY 是否由 yMin + 23 推导（结果里区分 auto / fixed / platform）。 */
    bool playerYDerived = false;
    /**
     * 粗筛召回高度。本引擎的粗筛气候门与 Y 无关（同 B），所以该字段只为
     * 桌面协议/CLI 兼容而携带，不改变扫描。
     */
    int coarseY = -51;
    // 自定义 Y 范围（C6）。includeCaves=false 时归一化为单层 Y=-63。
    int yMin = -63;
    int yMax = -51;
    // 早退漏斗总开关（C1/C2）：大陆性与 weird 早退同开同关。
    bool approximateGate = true;
    // 自由矩形（C5）：与 centerX/centerZ/radius/shape 互斥。
    bool rectMode = false;
    int minX = -100000, maxX = 100000, minZ = -100000, maxZ = 100000;
    // C7 三字段：<0 表示未启用，minArea 由 minArea 原样使用。
    int minRiverArea = -1;
    int minCaveArea = -1;
    double caveConversion = 4880.0 / 7011.0;
    // 纯河流 A 表：auto|on|off（C2b 第 3 步）。
    std::string pureRiverCache = "auto";
    /**
     * 纯河流 w 掩膜步长（C2b）：默认 64 = 改动前的行为；设 32 时把采样格对齐到
     * A 表的 32 世界格晶格（分辨率 ×4、成本基本持平），按需显式开启。
     */
    int pureRiverMaskStep = 64;
    // 大坑逃课路径（C3）。
    bool pitMode = false;
    // --bench：分阶段 ns/格、命中率、早退率（C4）。
    bool bench = false;
    std::vector<std::string> warnings;
    int minDistance = 256;
    // 0按Top K展开为max(8192,64*top)，不是返回条数。
    int candidateBudget = 0;
    /**
     * 精算预算（优化 3）：进入 refineExact 最终逐格精算的候选数。
     * 0 = 自动，取 max(kDefaultExactBudget, kExactBudgetPerTop*top, candidateBudget/kExactBudgetPerCandidate)；
     * >0 时直接用该值（--exact-budget），便于按种子权衡耗时与榜单覆盖。
     */
    int exactBudget = 0;
};

/** 统计层数：纯河流只算 Y=-63 一层。 */
inline int layersOf(const Config &cfg) {
    return cfg.includeCaves ? cfg.yMax - cfg.yMin + 1 : 1;
}

/** 第 layer 层的世界 Y。 */
inline int layerY(const Config &cfg, int layer) {
    return cfg.includeCaves ? cfg.yMin + layer : -63;
}

struct Hit {
    int x = 0;          // 河流区域中心方块 X
    int z = 0;          // 河流区域中心方块 Z
    int y = -63;
    int64_t riverArea = 0;  // 有效范围在Y=-63的普通河流格数。
    int64_t riverVolume = 0;
    int64_t caveVolume = 0;
    int radius = 0;     // 该候选的统计半径（方块）
    int regionX = 0;    // 所属统计分块
    int regionZ = 0;
};

/** 单点打分结果（与最终精排同一套 1:1 采样与几何）。 */
struct PointScore {
    int x = 0;
    int z = 0;
    int playerY = 0;
    int64_t riverArea = 0;    // yMin 层的有效河流格数
    int64_t riverVolume = 0;  // 全部层的河流格位
    int64_t caveVolume = 0;
    double score = 0;         // 等效河流格/层（与 search() 同一公式）
};

struct Statistics {    uint64_t scannedChunks = 0;    // 已扫描的粗筛区块数
    uint64_t totalChunks = 0;      // 本次范围的总区块数
    uint64_t candidates = 0;       // 通过面积下限的候选数
    int64_t activeMs = 0;
    int64_t pausedMs = 0;
    int64_t wallMs = 0;
    int workerThreads = 0;
    uint64_t coarseCandidates = 0;
    uint64_t refinedCandidates = 0;
    uint64_t refineTotal = 0;
    uint64_t biomeCells = 0;
    bool caveFocused = false, riverFallback = false;
    int64_t pureRiverCeiling = 0;
    double focusedCutoff = 0;
    uint64_t screenedCandidates = 0, preciseCandidates = 0;
    uint64_t quartCells = 0;
    double quartMs = 0, preciseMs = 0;
    double continentMs = 0, coarseSamplingMs = 0;
    double cacheBuildMs = 0;
    uint64_t cacheBytes = 0;
    uint64_t prunedCoarseColumns = 0, prunedCoarseTiles = 0;
    double coarseMs = 0;
    double biomeMs = 0;
    double geometryMs = 0;
    double exportMs = 0;
    // --bench 口径（C4）：线程 ms ÷ 对应格数；命中率与早退率均为比例。
    double coarseNsPerCell = 0, weirdNsPerCell = 0, continentNsPerCell = 0;
    double aTableHitRateRiver = 0, aTableHitRateCave = 0;
    double earlyExitRateContinent = 0, earlyExitRateWeird = 0;
    /** C1：10 个八度即过 0.575 的比例（不补算剩余 8 个八度）。 */
    double fastAcceptRateContinent = 0;
    double latticeMissRate = 0;
    uint64_t dilationWritesSaved = 0;
    uint64_t peakWorkingSetBytes = 0;
    uint64_t pitPeaks = 0;
    double pitMs = 0;
    std::string phase = "coarse";
};

struct SearchResult {
    Config config;
    Statistics statistics;
    std::string status;
    std::vector<Hit> hits;
};

class Control {
    std::mutex mutex_;
    std::condition_variable cv_;
    Clock::time_point started_ = Clock::now();
    Clock::time_point pausedAt_{};
    Clock::duration pausedTime_{};

  public:
    std::atomic<bool> cancelled{false};
    std::atomic<bool> paused{false};
    void pause();
    void resume();
    void cancel();
    // 在昂贵循环里协作检查；返回 false 表示应当中止。
    bool poll();
    int64_t activeMilliseconds();
    int64_t pausedMilliseconds();
};

// 版本 → cubiomes 的 MC 常量。参考实现要求显式传 MC_*，不接受版本字符串，
// 这里做一层映射并**按版本序**比较（不枚举字面量，避免新版本落进 else 分支）。
struct VersionInfo {
    int cubiomesMc = 0;      // cubiomes 的 MC_* 枚举值
    std::string kernelName;  // 写进结果的版本类名
};

const std::vector<std::string> &versions();
VersionInfo versionInfo(const std::string &label);
uint64_t parseSeed(const std::string &text);
bool inside(const Config &cfg, int x, int z);
int64_t distanceSquared(const Config &cfg, int x, int z);
// 按未舍入的有理数评分比较，主评分相同后才比较距离和坐标。
bool better(const Hit &a, const Hit &b, const Config &cfg);
double score(const Hit &hit, const Config &cfg);
std::string scoreNumerator(const Hit &hit);
/** 与新口径分子（含 caveConversion），返回 "分子/分母" 文本。 */
std::string scoreNumerator(const Hit &hit, const Config &cfg);
int scoreDenominator(const Config &cfg);
void validate(Config &cfg);

/**
 * 等效溶洞面积（S1）：把 caveVolume 按 caveConversion 折算成"河流层当量格数"。
 * 返回值 = round(caveVolume * caveConversion / layers)，
 * 前端「等效溶洞面积」直接用这个值，与标量 score 同口径。
 */
int64_t equivalentCaveArea(const Hit &hit, const Config &cfg);

/** 单层的河流当量上限（纯河流单层满环 = 49640）。用于 >100% 判断。 */
constexpr int kPureRiverLayerCap = 49640;

using ProgressCallback = std::function<void(const Statistics &)>;
SearchResult search(Config cfg, Control &control, const ProgressCallback &progress = {});

/**
 * 单点诊断：与最终精排同一套 1:1 层采样与几何（coverage/simulationDistance 全部生效）。
 * 不参与搜索，也不会写结果集；用于复现某个点位或比较模型。
 */
PointScore scorePoint(const Config &cfg, int x, int z, Control &control);

/**
 * 只采样一次群系，扫描 [from, to] 内每个挂机 Y 的单点分数
 * （用于验证“平台内每层覆盖面积相同、总分与挂机 Y 无关”）。
 */
std::vector<PointScore> sweepPointPlayerY(const Config &cfg, int x, int z,
                                          int from, int to, Control &control);

}
