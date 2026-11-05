#include "engine.hpp"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

#include "coverage.hpp"

#include "coarse_mask.hpp"

#include <algorithm>

#include <cmath>

#include <atomic>

#include <exception>

#include <memory>

#include <queue>

#include <set>

#include <map>

#include <limits>

#include <tuple>

#include <thread>

#include <unordered_map>

#include "riverfinder/climate_gate.hpp"

#include "dripstone/climate_gate.hpp"

#include "dripstone/octave_cache.hpp"

#include "core/bench/bench.hpp"

#include "core/gates/early_exit.hpp"

#include "core/gates/octave_cache.hpp"

#include "core/pit/pit_search.hpp"

#include "core/pit/weirdness_y_offset.hpp"

#include "core/platform/player_y.hpp"



namespace riverfinder_native {

namespace {

struct ParsedVersion {

    int major = 0;

    int minor = 0;

    int patch = 0;

};



bool parseVersionLabel(const std::string &label, ParsedVersion &out) {

    int parts[3] = {0, 0, 0};

    int count = 0;

    size_t i = 0;

    while (i < label.size() && count < 3) {

        if (label[i] < '0' || label[i] > '9') return false;

        int value = 0;

        while (i < label.size() && label[i] >= '0' && label[i] <= '9') {

            value = value * 10 + (label[i] - '0');

            ++i;

        }

        parts[count++] = value;

        if (i < label.size() && label[i] == '.') ++i;

        else break;

    }

    if (count < 2) return false;

    out.major = parts[0];

    out.minor = parts[1];

    out.patch = parts[2];

    return true;

}



/* 版本序比较：>= (major, minor, patch) */

bool versionAtLeast(const ParsedVersion &v, int major, int minor, int patch) {

    if (v.major != major) return v.major > major;

    if (v.minor != minor) return v.minor > minor;

    return v.patch >= patch;

}



int cubiomesMcOf(const ParsedVersion &v) {

// 26.x 是新编号体系（26.1 / 26.2 / 26.3），与 1.x 分开判断

    if (v.major >= 26) {

        if (versionAtLeast(v, 26, 3, 0)) return MC_26_3;

        if (versionAtLeast(v, 26, 2, 0)) return MC_26_2;

        return MC_26_1;

    }

    if (versionAtLeast(v, 1, 21, 11)) return MC_1_21_11;

    if (versionAtLeast(v, 1, 21, 9)) return MC_1_21_9;

    if (versionAtLeast(v, 1, 21, 6)) return MC_1_21_6;

    if (versionAtLeast(v, 1, 21, 5)) return MC_1_21_5;

    if (versionAtLeast(v, 1, 21, 4)) return MC_1_21_4;

    if (versionAtLeast(v, 1, 21, 2)) return MC_1_21_3;

    if (versionAtLeast(v, 1, 21, 0)) return MC_1_21_1;

    if (versionAtLeast(v, 1, 20, 0)) return MC_1_20_6;

    if (versionAtLeast(v, 1, 19, 3)) return MC_1_19_4;

    if (versionAtLeast(v, 1, 19, 0)) return MC_1_19_2;

    return MC_1_18_2;

}



const std::vector<std::string> kVersions = {

    "1.18",

    "1.18.1",

    "1.18.2",

    "1.19",

    "1.19.1",

    "1.19.2",

    "1.19.3",

    "1.19.4",

    "1.20",

    "1.20.1",

    "1.20.2",

    "1.20.3",

    "1.20.4",

    "1.20.5",

    "1.20.6",

    "1.21",

    "1.21.1",

    "1.21.2",

    "1.21.3",

    "1.21.4",

    "1.21.5",

    "1.21.6",

    "1.21.7",

    "1.21.8",

    "1.21.9",

    "1.21.10",

    "1.21.11",

    "26.1",

    "26.1.1",

    "26.1.2",

    "26.2",

    "26.3",

};



}  // namespace



const std::vector<std::string> &versions() { return kVersions; }



VersionInfo versionInfo(const std::string &label) {

    ParsedVersion parsed;

    if (!parseVersionLabel(label, parsed))

        throw std::invalid_argument("无法解析游戏版本: " + label);



/* 只接受 versions() 里列出的入口。

     *

* 这一条不能省：如果不校验，`1.17` 会一路走到 cubiomesMcOf 的兜底

* 分支被悄悄当成 1.18.2 跑起来——用户以为在搜 1.17，实际给了另一组

* 参数的答案。宁可显式报错，也不静默换版本。 */

    bool listed = false;

    for (const auto &known : kVersions) listed = listed || known == label;

    if (!listed)

        throw std::invalid_argument("不支持的游戏版本: " + label +

"（用 --versions 查看支持列表）");



    VersionInfo info;

    info.cubiomesMc = cubiomesMcOf(parsed);

// 版本类名只用于显示；真正的分支判据是 cubiomesMc。

    if (parsed.major >= 26) {

        info.kernelName = "26.x";

    } else if (versionAtLeast(parsed, 1, 21, 9)) {

        info.kernelName = "1.21.9+";

    } else if (versionAtLeast(parsed, 1, 19, 0)) {

        info.kernelName = "1.19~1.21.8";

    } else {

        info.kernelName = "1.18.x";

    }

    return info;

}



uint64_t parseSeed(const std::string &text) {

    if (text.empty()) throw std::invalid_argument("种子不能为空");

// 种子始终按十进制 64 位整数解析；不接受文本种子，与神殿口径一致。

    size_t used = 0;

    long long value = 0;

    try {

        value = std::stoll(text, &used, 10);

    } catch (const std::exception &) {

throw std::invalid_argument("种子必须是十进制 64 位整数: " + text);

    }

    if (used != text.size())

throw std::invalid_argument("种子必须是十进制 64 位整数: " + text);

    return static_cast<uint64_t>(value);

}



int64_t distanceSquared(const Config &cfg, int x, int z) {

    int64_t dx = int64_t(x)-cfg.centerX, dz = int64_t(z)-cfg.centerZ;

    return dx*dx+dz*dz;

}



bool inside(const Config &cfg, int x, int z) {

    if (x < kWorldMin || x > kWorldMax || z < kWorldMin || z > kWorldMax)

        return false;

    if (cfg.rectMode)

        return x >= cfg.minX && x <= cfg.maxX && z >= cfg.minZ && z <= cfg.maxZ;

    if (cfg.fullWorld)

        return true;

    int64_t dx = int64_t(x)-cfg.centerX, dz = int64_t(z)-cfg.centerZ;

    return cfg.shape == "square" ? std::abs(dx) <= cfg.radius && std::abs(dz) <= cfg.radius :

        dx*dx+dz*dz <= int64_t(cfg.radius)*cfg.radius;

}



bool better(const Hit &a, const Hit &b, const Config &cfg) {

    if (scoreKey(a) != scoreKey(b))

        return scoreKey(a) > scoreKey(b);

    return std::tuple(distanceSquared(cfg,a.x,a.z),a.x,a.z) <

           std::tuple(distanceSquared(cfg,b.x,b.z),b.x,b.z);

}



void validate(Config &cfg) {
    cfg.warnings.clear();

// C3：pit 路径只计溶洞体积，必须按洞穴层范围统计。
    if (cfg.pitMode)
        cfg.includeCaves = true;

    if (cfg.rectMode) {
        cfg.fullWorld = false;
        if (cfg.minX > cfg.maxX || cfg.minZ > cfg.maxZ)
            throw std::invalid_argument("矩形的min不得大于max");
        if (cfg.minX < kWorldMin || cfg.maxX > kWorldMax ||
            cfg.minZ < kWorldMin || cfg.maxZ > kWorldMax)
            throw std::invalid_argument("矩形范围必须在世界边界内");
// 排名 tie-break 与分块都以矩形中心为准。
        cfg.centerX = (cfg.minX + cfg.maxX) / 2;
        cfg.centerZ = (cfg.minZ + cfg.maxZ) / 2;
        cfg.radius = std::max(cfg.maxX - cfg.minX, cfg.maxZ - cfg.minZ) / 2 + 1;
        cfg.shape = "square";
    } else if (cfg.fullWorld) {
        cfg.centerX = cfg.centerZ = 0;
        cfg.radius = 30000000;
        cfg.shape = "square";
    }

    if (!cfg.rectMode && cfg.shape != "circle" && cfg.shape != "square")
throw std::invalid_argument("shape只能为circle或square");

    if (cfg.coverage != "circle" && cfg.coverage != "square" && cfg.coverage != "game")
throw std::invalid_argument("coverage只能为circle、square或game");

    if (cfg.radius <= 0 || cfg.radius > 60000000)
throw std::invalid_argument("外半径须在1至60000000格内");

    if (cfg.innerRadius <= 0 || cfg.innerRadius > 30000000)
throw std::invalid_argument("内半径须在1至30000000格内");

    if (cfg.centerX < kWorldMin || cfg.centerX > kWorldMax || cfg.centerZ < kWorldMin || cfg.centerZ > kWorldMax)
throw std::invalid_argument("搜索中心必须在世界边界内");

    if (cfg.threads < 1 || cfg.threads > kMaxWorkerThreads || cfg.top < 1 || cfg.top > 100000)
throw std::invalid_argument("threads须为1至" + std::to_string(kMaxWorkerThreads) +
                            "，limit须为1至100000");

    if (cfg.minArea < 0 || cfg.minDistance < 0)
throw std::invalid_argument("面积下限与热点间距不能为负");

    if (cfg.simulationDistanceChunks < 1 || cfg.simulationDistanceChunks > 32)
throw std::invalid_argument("模拟距离须为1至32整数区块");

// ---- 自定义 Y 范围（C6） ----
    if (cfg.includeCaves) {
        if (cfg.yMin > cfg.yMax)
            throw std::invalid_argument("yMin不得大于yMax");
        if (cfg.yMin < -64 || cfg.yMax > 320)
throw std::invalid_argument("层范围须在-64至320内");
        const int span = cfg.yMax - cfg.yMin + 1;
        if (span > 320)
throw std::invalid_argument("层范围过大");
        if (span > 24)
cfg.warnings.push_back("高度范围超过24格，可能导致搜索变慢，是否继续？");
        if (cfg.yMin > -58)
cfg.warnings.push_back("最低高度高于-58，可能不满足低Y农场前提，是否继续？");
    } else {
// 纯河流只统计 Y=-63 一层（计划 §15）。
        cfg.yMin = cfg.yMax = -63;
    }
    // 挂机 Y：默认 = yMin + 23（球壳平台内、给最低层留一格余量，与 B 一致）；
    // --optimize-player-y 仍走平台解析解（所有层都在 ±24 内，总分与 Y 无关）。
    if (cfg.optimizePlayerY) {
        cfg.playerY = cfg.includeCaves ? wcore::platform::playerY(cfg.yMin, cfg.yMax) : -63;
        cfg.playerYDerived = false;
    } else if (cfg.playerY == kPlayerYAuto) {
        cfg.playerY = cfg.yMin + 23;
        cfg.playerYDerived = true;
    }
    if (cfg.playerY < -64 || cfg.playerY > 320)
        throw std::invalid_argument("挂机Y须在-64至320内");
    if (cfg.includeCaves && !wcore::platform::onPlatform(cfg.yMin, cfg.yMax, cfg.playerY))
        cfg.warnings.push_back("固定挂机Y不在层范围±24内，部分层可能不计入刷怪球；可改用--optimize-player-y");
    if (!cfg.includeCaves && std::abs(cfg.playerY + 63) > 24)
        cfg.warnings.push_back("固定挂机Y距Y=-63超过24格，纯河流层可能不在刷怪球内");

    if (cfg.pureRiverCache != "auto" && cfg.pureRiverCache != "on" && cfg.pureRiverCache != "off")
throw std::invalid_argument("pure-river-cache只能为auto、on或off");

    // ---- 纯河流 w 掩膜步长（C2b）：默认 64 保持旧行为，可显式改 32 ----
    if (cfg.pureRiverMaskStep != 32 && cfg.pureRiverMaskStep != 64)
        throw std::invalid_argument("pure-river-mask只能为32或64");

    // 粗筛召回高度只为协议兼容而携带（粗筛气候门与 Y 无关），但仍做范围校验。
    if (cfg.coarseY < -64 || cfg.coarseY > 320)
        throw std::invalid_argument("粗筛高度须在-64至320内");

    // ---- 三字段门槛（C7）：minArea = R + 1.43668·C ----
    const bool threeField = cfg.minRiverArea >= 0 || cfg.minCaveArea >= 0;
    if (threeField) {
        int64_t river = std::max(cfg.minRiverArea, 0);
        int64_t cave = std::max(cfg.minCaveArea, 0);
        if (!cfg.includeCaves) {
            cave = 0;
            cfg.minCaveArea = 0;
        }
        if (!cfg.includeCaves && river > 49640)
throw std::invalid_argument("纯河流等效面积上限为49640，超出请缩小门槛或开启溶洞折算");
// 等效河流格 = 4880·河 + 7011·洞；换算为每层等效河流格并向上取整。
        const int64_t numerator = int64_t(kCaveWeight) * cave;
        const int64_t equivalent = (numerator + kRiverWeight - 1) / kRiverWeight;
        const int64_t area = river + equivalent;
        if (area > std::numeric_limits<int>::max())
throw std::invalid_argument("面积门槛超出整数范围");
        if (area < 24820)
throw std::invalid_argument("面积下限不得低于24820（纯河流上限49640的50%），再低没有意义");
        cfg.minArea = int(area);
        if (cfg.includeCaves && cave < 29784)
cfg.warnings.push_back("溶洞等效面积低于29784（49640的60%），建议改用纯河流搜索");
    } else if (!cfg.includeCaves && cfg.minArea > 49640) {
throw std::invalid_argument("纯河流等效面积上限为49640，超出请缩小门槛或开启溶洞折算");
    }

    if (cfg.candidateBudget < 0)
throw std::invalid_argument("候选预算不能为负");

    if (!cfg.candidateBudget)
        cfg.candidateBudget = std::max(cfg.includeCaves && cfg.radius >= 500000 ? 16384 : 8192,64*cfg.top);

    if (cfg.candidateBudget < cfg.top)
throw std::invalid_argument("候选预算不能小于结果数量");

    // 精确预算（优化 3）：0=自动；>0 时必须不小结果数量，也不超过候选预算。
    if (cfg.exactBudget < 0)
throw std::invalid_argument("精算预算不能为负");
    if (cfg.exactBudget > 0 && cfg.exactBudget < cfg.top)
throw std::invalid_argument("精算预算不能小于结果数量");

    versionInfo(cfg.version);

    parseSeed(cfg.seed);

}



namespace {

double milliseconds(Clock::time_point start) {

    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();

}



template<class Work>

void parallel(uint64_t count, int threads, Control &control, Work work) {

    std::atomic<uint64_t> next{0};

    std::exception_ptr failure;

    std::mutex mutex;

    std::vector<std::thread> workers;

    // 线程数多于此处的任务数没有意义：每个空转线程仍要真实创建/销毁一次。
    // 高核数机器默认线程数很大时，精排等小任务阶段会因此少建大量无用线程。
    int spawn = int(std::min<uint64_t>(uint64_t(std::max(threads,1)),std::max<uint64_t>(count,1)));

    workers.reserve(size_t(spawn));

    for (int worker = 0; worker < spawn; ++worker) {

        workers.emplace_back([&,worker] {

            try {

                for (;;) {

                    if (!control.poll())

                        break;

                    uint64_t index = next.fetch_add(1);

                    if (index >= count)

                        break;

                    work(index,worker);

                }

            } catch (...) {

                std::lock_guard lock(mutex);

                if (!failure)

                    failure = std::current_exception();

                control.cancel();

            }

            // --bench 线程局部计数在线程退出前并入全局快照。
            wcore::bench::flush();

        });

    }

    for (auto &worker : workers)

        worker.join();

    if (failure)

        std::rethrow_exception(failure);

}





std::unique_ptr<dripstone::OctaveCache> buildOctaveCache(const Config &cfg, Control &control, bool focused, bool weirdOnly) {

    wcore::bench::Scope benchScope(wcore::bench::STAGE_SETUP);

    auto cache = std::make_unique<dripstone::OctaveCache>();

    auto generator = std::make_unique<Generator>();

    setupGenerator(generator.get(),versionInfo(cfg.version).cubiomesMc,0);

    applySeed(generator.get(),DIM_OVERWORLD,parseSeed(cfg.seed));

    auto build = [&](auto &fields,int parameter,int step,int phaseX,int phaseZ) {

        int count = std::min(int(fields.size()),generator->bn.climate[parameter].octA.octcnt);

        for (int i = 0; i < count; ++i) {

            auto &field = fields[i];

            field.step = step;

            field.phaseX = phaseX;

            field.phaseZ = phaseZ;

            double frequency = generator->bn.climate[parameter].octA.octaves[i].lacunarity;

            field.side = int(std::llround(256.0/frequency))/step;

            field.values.resize(size_t(field.side)*field.side);

            parallel(field.side,cfg.threads,control,[&](uint64_t row,int) {

                for (int x = 0; x < field.side; ++x) {

                    if (x%256 == 0 && !control.poll())

                        return;

                    field.values[size_t(row)*field.side+x] = dripstone::OctaveCache::live(

                        &generator->bn,parameter,i,false,x*step+phaseX,int(row)*step+phaseZ);

                }

            });

        }

    };

    if (cfg.includeCaves && !weirdOnly)

        build(cache->continental,NP_CONTINENTALNESS,32,16,16);

    int radius = cfg.coverage == "game" ? 128 : cfg.innerRadius;

    int step = (focused || !cfg.includeCaves) && radius >= 128 ? 32 : 16;

    while (radius/step > 16) step *= 2;

    int halo = (radius+step-1)/step+2, spacing = std::max(cfg.includeCaves ? 64 : 32,step)/4;

    int minX = int(std::max(int64_t(kWorldMin),int64_t(cfg.centerX)-cfg.radius));

    int minZ = int(std::max(int64_t(kWorldMin),int64_t(cfg.centerZ)-cfg.radius));

    int qx = floorDiv(minX-halo*step+step/2,4), qz = floorDiv(minZ-halo*step+step/2,4);

    int phaseX = (qx%spacing+spacing)%spacing, phaseZ = (qz%spacing+spacing)%spacing;

    build(cache->weirdness,NP_WEIRDNESS,spacing,phaseX,phaseZ);

    return cache;

}



struct Local {

    std::unique_ptr<Generator> generator;

    const dripstone::OctaveCache *octaves = nullptr;

    double biomeMs = 0, geometryMs = 0;

    uint64_t biomeCells = 0, prunedColumns = 0, prunedTiles = 0;

    std::vector<Hit> hits;

    GeometryCache geometry;

    // 精算窗口缓存已移除（优化 2 作废）。窗口改为以 best 为中心后，
    // 每个 finalist 的 (best.x,best.z) 在进入 refineExact 前已按 XZ 去重，
    // 缓存键不可能重复命中；保留空缓存只会白占内存（实测 12 槽 × 32 线程
    // 把峰值工作集从约 1.3 GB 推到约 4.7 GB）。

    std::unordered_map<uint64_t, std::array<float, NP_MAX>> refinementClimate;

    uint64_t quartCells = 0;

    double quartMs = 0, preciseMs = 0;

    double continentMs = 0, coarseSamplingMs = 0;

};



std::vector<int> quartVolume(Local &local, Range range, Control &control) {

    std::vector<int> out(getMinCacheSize(local.generator.get(),4,range.sx,range.sy,range.sz));

    if (!control.poll())

        return {};

    if (genBiomes(local.generator.get(),out.data(),range))

        throw std::runtime_error("四分群系批量生成失败");

    local.quartCells += uint64_t(range.sx)*range.sy*range.sz;

    return out;

}





bool caveBiome(int id) {

    return id == dripstone_caves || id == lush_caves || id == deep_dark || id == sulfur_caves;

}



void approximateColumn(const BiomeNoise *bn, const float climate[NP_MAX], int y0, int count,

                       int *out, uint64_t &cache) {

    int64_t quantized[NP_MAX];

    for (int n = 0; n < NP_MAX; ++n)

        quantized[n] = int64_t(10000.0F*climate[n]);

    auto query = [&](int y) {

        float depth = sampleClimateDepth(bn,climate[NP_CONTINENTALNESS],climate[NP_EROSION],

                                         climate[NP_WEIRDNESS],(y0+y)*4);

        quantized[NP_DEPTH] = int64_t(10000.0F*depth);

        auto parameters = reinterpret_cast<const uint64_t*>(quantized);

        int id = climateCaveBiome(bn->mc,parameters);

        return id != none ? id : climateToBiome(bn->mc,parameters,&cache);

    };

    out[0] = query(0);

    if (count == 1)

        return;

    out[count-1] = query(count-1);

    bool uniform = out[0] == out[count-1] && caveBiome(out[0]);

    for (int y = 1; y < count-1; ++y)

        out[y] = uniform ? out[0] : query(y);

}



std::vector<int> approximateQuartVolume(Local &local, Range range, int scale, Control &control) {

// 仅用于候选筛选：16格气候网格插值，最终输出仍重新生成精确群系。

    constexpr int spacing = 16;

    int x0 = floorDiv(range.x*4,spacing)*spacing, z0 = floorDiv(range.z*4,spacing)*spacing;

    int w = (range.x*4+(range.sx-1)*scale-x0)/spacing+2;

    int h = (range.z*4+(range.sz-1)*scale-z0)/spacing+2;

    std::vector<std::array<float,NP_MAX>> climate(size_t(w)*h);

    auto *bn = &local.generator->bn;

// 同一工作线程的相邻候选共享16格气候节点；种子和版本随Local固定，清空只影响命中率。
    if (local.refinementClimate.size() > 8192)
        local.refinementClimate.clear();

    for (int z = 0; z < h; ++z) {

        if (!control.poll())

            return {};

        for (int x = 0; x < w; ++x) {
            int wx = x0+x*spacing, wz = z0+z*spacing;
            uint64_t key = (uint64_t(uint32_t(wx)) << 32) | uint32_t(wz);
            auto &value = climate[size_t(z)*w+x];
            auto cached = local.refinementClimate.find(key);
            if (cached != local.refinementClimate.end()) {
                value = cached->second;
            } else {
                sampleBiomeClimate(bn,value.data(),wx,0,wz,SAMPLE_NO_DEPTH);
                local.refinementClimate.emplace(key,value);
            }
        }

    }

    std::vector<int> out(size_t(range.sx)*range.sz*range.sy);

// 近似候选允许群系同距时沿用上一叶；最终群系生成不启用这个缓存。

    uint64_t climateCache = 0;

    std::vector<int> column(size_t(range.sy));

    for (int z = 0; z < range.sz; ++z) {

        if (!control.poll())

            return {};

        int rz = range.z*4+z*scale-z0, iz = rz/spacing;

        float fz = float(rz%spacing)/spacing;

        for (int x = 0; x < range.sx; ++x) {

            int rx = range.x*4+x*scale-x0, ix = rx/spacing;

            float fx = float(rx%spacing)/spacing;

            float values[NP_MAX];

            for (int n = 0; n < NP_MAX; ++n) {

                float a = climate[size_t(iz)*w+ix][n], b = climate[size_t(iz)*w+ix+1][n];

                float c = climate[size_t(iz+1)*w+ix][n], d = climate[size_t(iz+1)*w+ix+1][n];

                float top = a+fx*(b-a), bottom = c+fx*(d-c);

                values[n] = top+fz*(bottom-top);

            }

            approximateColumn(bn,values,range.y,range.sy,column.data(),climateCache);

            for (int y = 0; y < range.sy; ++y)

                out[(size_t(y)*range.sz+z)*range.sx+x] = column[y];

        }

    }

    return out;

}



std::shared_ptr<BiomeWindow> sampleWindow(Local &local, int x, int z, int width, int height,

                                         const Config &cfg, int scale, Control &control) {

    if (!control.poll())

        return {};

    auto started = Clock::now();
    wcore::bench::Scope preciseScope(scale == 1 ? wcore::bench::STAGE_PRECISE :
                                                  wcore::bench::STAGE_COARSE);

    int layers = layersOf(cfg);

    int x0 = std::max(x,kWorldMin), z0 = std::max(z,kWorldMin);

    int x1 = std::min(int64_t(x)+width-1,int64_t(kWorldMax));

    int z1 = std::min(int64_t(z)+height-1,int64_t(kWorldMax));

    if (scale > 1) {

        x0 = floorDiv(x0,scale)*scale;

        z0 = floorDiv(z0,scale)*scale;

    }

    int w = (x1-x0)/scale+1, h = (z1-z0)/scale+1;

    if (w <= 0 || h <= 0)

        return {};

    int y0 = scale == 1 ? cfg.yMin : floorDiv(cfg.yMin,4);

    int sy = scale == 1 ? layers : floorDiv(cfg.yMax,4)-y0+1;

    Range range{scale,scale == 1 ? x0 : floorDiv(x0,4),scale == 1 ? z0 : floorDiv(z0,4),

                w,h,y0,sy};

    std::vector<int> data;

    if (scale == 1) {

        Range source = getVoronoiSrcRange(range);

        auto quart = quartVolume(local,source,control);

        if (quart.empty())

            return {};

// 最终只关心河流/滴水石/其他三类；同类边界无须额外求Voronoi，深暗之域仍为零。

        for (auto &id : quart)

            if (id != river && (!cfg.includeCaves || id != dripstone_caves))

                id = none;

        data.resize(size_t(w)*h*sy);

        int bandRows = 32;

        std::vector<int> band(size_t(w)*std::min(h,bandRows)*sy);

        for (int row = 0; row < h; row += bandRows) {

            if (!control.poll())

                return {};

            int count = std::min(bandRows,h-row);

            mapVoronoiVolume(local.generator->sha,band.data(),quart.data(),

                range.x,range.y,range.z+row,w,sy,count,

                source.x,source.y,source.z,source.sx,source.sy,source.sz);

            for (int y = 0; y < sy; ++y)

                std::copy_n(band.data()+size_t(y)*count*w,size_t(count)*w,

                            data.data()+(size_t(y)*h+row)*w);

        }

    } else {

        data = approximateQuartVolume(local,range,scale,control);

        if (data.empty())

            return {};

    }

    local.biomeCells += uint64_t(w)*h*sy;

    std::vector<int> expanded;

    const int *ids = data.data();

    if (scale > 1 && layers > 1) {

        expanded.resize(size_t(w)*h*layers);

        for (int y = 0; y < layers; ++y) {

            int sourceY = floorDiv(cfg.yMin+y,4)-y0;

            std::copy_n(data.data()+size_t(sourceY)*w*h,size_t(w)*h,

                        expanded.data()+size_t(y)*w*h);

        }

        ids = expanded.data();

    }

    auto result = std::make_shared<BiomeWindow>(x0,z0,w,h,layers,ids,scale);

    double elapsed = milliseconds(started);

    if (scale == 1)
        wcore::bench::count(wcore::bench::counters().preciseCells, uint64_t(w)*h*sy);

    local.biomeMs += elapsed;

    (scale == 1 ? local.preciseMs : local.quartMs) += elapsed;

    return result;

}



std::vector<Hit> coarseTile(Local &local, const Config &cfg, int tx, int tz, int width, int height,
                             Control &control, bool cavesOnly, int64_t cutoff) {
// 混合路径从16格起步；纯河流和洞穴聚焦从32格起步，最终候选仍完整逐格生成。
    int radius = cfg.coverage == "game" ? 128 : cfg.innerRadius;
    int step = (cavesOnly || !cfg.includeCaves) && radius >= 128 ? 32 : 16;
    while (radius/step > 16)
        step *= 2;
    int halo = (radius+step-1)/step + 2;
    int w = (width+step-1)/step+2*halo, h = (height+step-1)/step+2*halo;
    int startX = tx-halo*step+step/2, startZ = tz-halo*step+step/2;
// C2b：纯河流 w 采样格对齐 A 表晶格（32 世界格），消除 ~50% 落空。
    if (!cfg.includeCaves && local.octaves && local.octaves->weirdness[0].side) {
        const auto &field = local.octaves->weirdness[0];
        const int stepClimate = field.step > 0 ? field.step : 8;
        auto alignOffset = [&](int origin) {
            const int q = floorDiv(origin, 4);
            const int off = ((q - field.phaseX) % stepClimate + stepClimate) % stepClimate;
            return off * 4;
        };
        auto alignOffsetZ = [&](int origin) {
            const int q = floorDiv(origin, 4);
            const int off = ((q - field.phaseZ) % stepClimate + stepClimate) % stepClimate;
            return off * 4;
        };
        const int shiftX = alignOffset(startX), shiftZ = alignOffsetZ(startZ);
        if (shiftX) { startX -= shiftX; w += 1; }
        if (shiftZ) { startZ -= shiftZ; h += 1; }
    }
    size_t plane = size_t(w)*h;
    int layers = layersOf(cfg);
    std::vector<uint16_t> rivers(plane), caves(cfg.includeCaves ? plane : 0);
    auto *bn = &local.generator->bn;
    // C2b：w 掩膜步长默认 64（改动前行为）；纯河流可用 --pure-river-mask 32
// 把采样格对齐到 A 表的 32 世界格晶格（分辨率 ×4、成本基本持平）。
    int maskStep = std::max(64, step);
    if (!cfg.includeCaves && cfg.pureRiverMaskStep < maskStep)
        maskStep = cfg.pureRiverMaskStep;
    int maskStride = maskStep/step;
    int mw = (w-1)/maskStride+2, mh = (h-1)/maskStride+2;
    std::vector<float> weirdness(size_t(mw)*mh);
    std::vector<uint8_t> weirdReady(weirdness.size());
    auto weirdAt = [&](int x,int z) {
        size_t index = size_t(z)*mw+x;
        if (!weirdReady[index]) {
            int qx = floorDiv(startX+x*maskStep,4), qz = floorDiv(startZ+z*maskStep,4);
            weirdness[index] = local.octaves ? local.octaves->noise(bn,NP_WEIRDNESS,qx,qz) :
                sampleClimateNoise(bn,NP_WEIRDNESS,qx,0,qz);
            weirdReady[index] = 1;
        }
        return weirdness[index];
    };
    auto estimateW = [&](int x,int z) {
        int ix = x/maskStride, iz = z/maskStride;
        float fx = float(x%maskStride)/maskStride, fz = float(z%maskStride)/maskStride;
        float a = weirdAt(ix,iz), b = weirdAt(ix+1,iz);
        float c = weirdAt(ix,iz+1), d = weirdAt(ix+1,iz+1);
        return (a+fx*(b-a))*(1-fz)+(c+fx*(d-c))*fz;
    };
    auto continentalStarted = Clock::now();
    int cw = (w*step+127)/128+4, ch = (h*step+127)/128+4;
    int contX = floorDiv(startX,128)-1, contZ = floorDiv(startZ,128)-1;
    std::vector<uint8_t> cont(cfg.includeCaves ? size_t(cw)*ch : 0);
    std::vector<std::array<float,NP_MAX>> climateGrid(cfg.includeCaves ? size_t(cw)*ch : 0);
    std::vector<uint8_t> thermalReady(climateGrid.size());
    {
    wcore::bench::Scope continentScope(wcore::bench::STAGE_CONTINENT);
    if (cfg.includeCaves) {
        for (int z = 0; z < ch; ++z) {
            if (!control.poll())
                return {};
            for (int x = 0; x < cw; ++x) {
                int wx = (contX+x)*128+64, wz = (contZ+z)*128+64;
                // C1a：approximate 时先过 8 级八度早退漏斗，只给通过者求精确值；
                // approximate=false 时始终求精确值，保证与改造前逐格一致
                // （climateGrid 的 C 还要供 32 格气候插值使用，不能跳过）。
                float c = 0;
                bool pass = false;
                if (cfg.approximateGate) {
                    pass = dripstone::passContinentalness(bn, local.octaves,
                                                          wx/4, wz/4, true);
                    if (pass) {
                        c = local.octaves ? local.octaves->noise(bn,NP_CONTINENTALNESS,wx/4,wz/4) :
                            sampleClimateNoise(bn,NP_CONTINENTALNESS,wx/4,0,wz/4);
                    }
                } else {
                    c = local.octaves ? local.octaves->noise(bn,NP_CONTINENTALNESS,wx/4,wz/4) :
                        sampleClimateNoise(bn,NP_CONTINENTALNESS,wx/4,0,wz/4);
                    pass = c > dripstone::continentalnessThreshold;
                }
                climateGrid[size_t(z)*cw+x][NP_CONTINENTALNESS] = c;
                if (pass) {
                    int dilation = cavesOnly ? (radius+127)/128+2 : 1;
                    for (int dz = -dilation; dz <= dilation; ++dz)
                        for (int dx = -dilation; dx <= dilation; ++dx)
                            if (x+dx >= 0 && z+dz >= 0 && x+dx < cw && z+dz < ch) {
                                auto &slot = cont[size_t(z+dz)*cw+x+dx];
// 差分膨胀：已经写过的格子只计数不再写。
                                if (!slot) slot = 1;
                                else wcore::bench::count(wcore::bench::counters().dilationWritesSaved);
                            }
                }
            }
        }
    }
    }
    local.continentMs += milliseconds(continentalStarted);
    wcore::bench::count(wcore::bench::counters().continentCells, size_t(cw)*ch);
    auto samplingStarted = Clock::now();
// 纯河流只跑 w 门与河流群系判定，单列为 weird 阶段。
    wcore::bench::Scope sampleScope(!cfg.includeCaves ? wcore::bench::STAGE_WEIRD :
                                                       wcore::bench::STAGE_COARSE);
    int stride = std::max(64,step);
    std::vector<uint8_t> potential, retained;
    std::vector<float> potentialW;
    std::unique_ptr<CoarseMask> needed;
    if ((cavesOnly && step >= 32) || !cfg.includeCaves) {
        potential.resize(plane);
        potentialW.resize(plane);
        retained.resize(plane);
        for (int z = 0; z < h; ++z) {
            if (!control.poll())
                return {};
            int wz = startZ+z*step;
            for (int x = 0; x < w; ++x) {
                int wx = startX+x*step;
                if (wx < kWorldMin || wx > kWorldMax || wz < kWorldMin || wz > kWorldMax)
                    continue;
                if (cavesOnly) {
                    int cx = floorDiv(wx,128)-contX, cz = floorDiv(wz,128)-contZ;
                    if (!cont[size_t(cz)*cw+cx])
                        continue;
                }
                float estimate = estimateW(x,z);
                int wi = int(10000.0F*estimate);
                if (wi < 0) wi = -wi;
                bool possible = cavesOnly ? wi < 500 || wi > 12800 : std::abs(estimate) <= 0.18F;
                if (possible) {
                    potential[size_t(z)*w+x] = 1;
                    potentialW[size_t(z)*w+x] = estimate;
                }
            }
        }
        needed = std::make_unique<CoarseMask>(w,h,potential);
        int64_t threshold = std::max(cutoff,int64_t(cfg.minArea)*scoreDenominator(cfg)/2);
        std::vector<int> diskWidths;
        if (cavesOnly && cfg.coverage != "square") {
            int reach = radius/step;
            for (int dz = -reach; dz <= reach; ++dz) {
                int64_t remaining = int64_t(radius)*radius-int64_t(dz*step)*(dz*step);
                int dx = int(std::sqrt(double(remaining)))/step;
                while (int64_t((dx+1)*step)*((dx+1)*step) <= remaining)
                    ++dx;
                while (int64_t(dx*step)*(dx*step) > remaining)
                    --dx;
                diskWidths.push_back(dx);
            }
        }
        bool any = false;
        for (int z = tz; z < tz+height; z += stride)
            for (int x = tx; x < tx+width; x += stride) {
                int px = x+step/2, pz = z+step/2;
                if (!inside(cfg,px,pz))
                    continue;
                int gx = (px-startX)/step, gz = (pz-startZ)/step;
// 假定所有潜在列13层全是高权重洞穴，且用方形包住圆形；同分不能剪掉。
                int weight = cfg.includeCaves ? kCaveWeight : kRiverWeight;
                ExactScore upper = ExactScore(needed->count(gx,gz,radius/step))*layers*weight*step*step;
                if (upper < threshold)
                    continue;
// 方形上界通过后再收紧到圆盘；游戏模式忽略禁刷球和区块裁剪仍是保守上界。
                if (!diskWidths.empty() &&
                    ExactScore(needed->countDisk(gx,gz,diskWidths))*layers*weight*step*step < threshold)
                    continue;
                retained[size_t(gz)*w+gx] = 1;
                needed->include(gx,gz,radius/step);
                any = true;
            }
        needed->finish();
        for (int z = 0; z < h; ++z)
            for (int x = 0; x < w; ++x)
                local.prunedColumns += potential[size_t(z)*w+x] && !needed->needs(x,z);
        if (!any) {
            ++local.prunedTiles;
            local.coarseSamplingMs += milliseconds(samplingStarted);
            return {};
        }
    }
// 混合路径保留32格W/E实算；洞穴聚焦从64格W及128格C/T/H/E网格插值。
    auto climateAt = [&](int x,int z) -> const std::array<float,NP_MAX> & {
        size_t index = size_t(z)*cw+x;
        auto &value = climateGrid[index];
        if (!thermalReady[index]) {
            int qx = (contX+x)*32+16, qz = (contZ+z)*32+16;
            value[NP_TEMPERATURE] = sampleClimateNoise(bn,NP_TEMPERATURE,qx,0,qz);
            value[NP_HUMIDITY] = sampleClimateNoise(bn,NP_HUMIDITY,qx,0,qz);
            if (cavesOnly)
                value[NP_EROSION] = sampleClimateNoise(bn,NP_EROSION,qx,0,qz);
            thermalReady[index] = 1;
        }
        return value;
    };
    struct CaveColumn {
        bool sampled = false;
        std::vector<int> biomes;
    };
// C6：四分高度档按 cfg.yMin/yMax 自动生成，不再写死 4 档与 {3,4,4,2}。
    const int quartY0 = floorDiv(cfg.yMin,4);
    const int quartCount = floorDiv(cfg.yMax,4)-quartY0+1;
    std::vector<int> weights(size_t(quartCount),0);
    for (int y = cfg.yMin; y <= cfg.yMax; ++y)
        ++weights[size_t(floorDiv(y,4)-quartY0)];
    int columnX = floorDiv(startX,32), columnZ = floorDiv(startZ,32);
    int columnWidth = (w*step+31)/32+1, columnHeight = (h*step+31)/32+1;
    std::vector<CaveColumn> caveColumns(cfg.includeCaves ? size_t(columnWidth)*columnHeight : 0);
    for (int z = 0; z < h; ++z) {
        if (!control.poll())
            return {};
        int wz = startZ+z*step;
        for (int x = 0; x < w; ++x) {
            int wx = startX+x*step;
            if (needed && (!potential[size_t(z)*w+x] || !needed->needs(x,z)))
                continue;
            if (wx < kWorldMin || wx > kWorldMax || wz < kWorldMin || wz > kWorldMax)
                continue;
            int cx = floorDiv(wx,128)-contX, cz = floorDiv(wz,128)-contZ;
            bool nearCave = cfg.includeCaves && cx >= 0 && cz >= 0 && cx < cw && cz < ch &&
                            cont[size_t(cz)*cw+cx];
            if (cavesOnly && !nearCave)
                continue;
            float estimate = needed ? potentialW[size_t(z)*w+x] : estimateW(x,z);
// 河流原门为|W|约0.05；另留0.13误差余量。这仍是近似门，不是严格上界。
            int id = none;
            if (!cavesOnly && std::abs(estimate) <= 0.18F) {
                int qx = floorDiv(wx,4), qz = floorDiv(wz,4);
                if (!cfg.includeCaves) {
// C2b：低尾单边早退 + A 表；未早退时 knownW 仍是完整值。
                    float knownW = 0;
                    if (wcore::passesWeirdLowTail(bn,local.octaves,qx,qz,cfg.approximateGate,&knownW))
                        id = riverfinder::riverBiomeIdAt(bn,qx,-16,qz,&knownW);
                } else {
                    id = riverfinder::riverBiomeIdAt(bn,qx,-16,qz,nullptr);
                }
            }
            size_t index = size_t(z)*w+x;
            if (id == river)
                rivers[index] = layers;
            if (!nearCave)
                continue;
            int qx = floorDiv(wx,32), qz = floorDiv(wz,32);
            auto &column = caveColumns[size_t(qz-columnZ)*columnWidth+qx-columnX];
            if (!column.sampled) {
                column.sampled = true;
                column.biomes.assign(size_t(quartCount),0);
                float climate[NP_MAX]{};
                bool passed = false;
                if (cavesOnly && (std::abs(estimate) <= 0.18F || std::abs(estimate) >= 1.1F)) {
// 大范围候选直接使用64格W网格插值；后续8格气候细排和最终逐格群系均重新求值。
                    float w = estimate;
                    climate[NP_WEIRDNESS] = w;
                    int wi = int(10000.0F*w);
                    if (wi < 0) wi = -wi;
                    passed = wi < 500 || wi > 12800;
                } else if (!cavesOnly) {
// C2：大陆性 + weird 同开关、同执行；查表 + 早退。
                    passed = dripstone::passCoarseCaveCell(bn,local.octaves,qx*8+4,qz*8+4,
                        cfg.approximateGate,&climate[NP_WEIRDNESS],&climate[NP_EROSION]);
                }
                if (passed) {
                    int rx = qx*8+4-(contX*32+16), rz = qz*8+4-(contZ*32+16);
                    int ix = floorDiv(rx,32), iz = floorDiv(rz,32);
                    float fx = float(rx-ix*32)/32, fz = float(rz-iz*32)/32;
                    const auto &a = climateAt(ix,iz), &b = climateAt(ix+1,iz);
                    const auto &c = climateAt(ix,iz+1), &d = climateAt(ix+1,iz+1);
                    for (int n : {NP_TEMPERATURE,NP_HUMIDITY,NP_CONTINENTALNESS,NP_EROSION}) {
                        if (n == NP_EROSION && !cavesOnly)
                            continue;
                        float top = a[n]+fx*(b[n]-a[n]), bottom = c[n]+fx*(d[n]-c[n]);
                        climate[n] = top+fz*(bottom-top);
                    }
                    if (cavesOnly && climate[NP_EROSION] < -0.425F)
                        continue;
                    uint64_t columnCache = 0;
                    approximateColumn(bn,climate,quartY0,quartCount,column.biomes.data(),columnCache);
                }
            }
            for (int y = 0; y < quartCount; ++y) {
                int candidate = column.biomes[size_t(y)];
                if (candidate == dripstone_caves) {
                    caves[index] += weights[y];
                    if (id == river)
                        rivers[index] -= weights[y];
                } else if (cavesOnly && candidate == river) {
                    rivers[index] += weights[y];
                }
            }
        }
    }
    local.coarseSamplingMs += milliseconds(samplingStarted);
    wcore::bench::count(!cfg.includeCaves ? wcore::bench::counters().weirdCells :
                                            wcore::bench::counters().coarseCells, size_t(w)*h);
    BiomeWindow window(startX,startZ,w,h,rivers.data(),cfg.includeCaves ? caves.data() : nullptr,step);
    std::map<std::pair<int,int>,Hit> peaks;
    for (int z = tz; z < tz+height; z += stride) {
        for (int x = tx; x < tx+width; x += stride) {
            int px = x+step/2, pz = z+step/2;
            if (!inside(cfg,px,pz))
                continue;
            if (needed && !retained[size_t((pz-startZ)/step)*w+(px-startX)/step])
                continue;
            if (!window.hasTargets(px,pz,radius))
                continue;
            auto hit = window.evaluate(cfg,px,pz,cfg.playerY,true,&local.geometry);
            if (scoreKey(hit) == 0 || score(hit,cfg) < cfg.minArea*0.5)
                continue;
            auto key = std::pair(floorDiv(px,256),floorDiv(pz,256));
            auto it = peaks.find(key);
            if (it == peaks.end() || better(hit,it->second,cfg))
                peaks[key] = hit;
        }
    }
    std::vector<Hit> result;
    for (auto &[key,hit] : peaks)
        result.push_back(hit);
    return result;
}
Hit refine(Local &local, const Config &cfg, const Hit &seed, Control &control) {

    int radius = cfg.coverage == "game" ? 128 : cfg.innerRadius;

    auto window = sampleWindow(local,seed.x-radius-52,seed.z-radius-52,

                               2*radius+105,2*radius+105,cfg,8,control);

    if (!window)

        return {};

    auto time = Clock::now();

    Hit best;

    bool found = false;

    for (int dz = -48; dz <= 48; dz += 8) {

        if (!control.poll())

            return {};

        for (int dx = -48; dx <= 48; dx += 8) {

            int x = seed.x+dx, z = seed.z+dz;

            if (!inside(cfg,x,z))

                continue;

// 自动Y粗排用无禁刷球的投影上界，避免只保留固定高度占优的位置。

// 平台解析解使挂机 Y 固定；粗排不再需要自动 Y 的投影上界。
            auto hit = window->evaluate(cfg,x,z,cfg.playerY,false,&local.geometry);

            if (!found || better(hit,best,cfg)) {

                best = hit;

                found = true;

            }

        }

    }

    local.geometryMs += milliseconds(time);

    if (!found || scoreKey(best) == 0)

        return {};

    return best;

}



/* 精算窗口几何（优化 1）：窗口以 best 为中心，而不是以 64 格桶原点为中心。
 *
 * refineExact 要评估的站位是 best ± kReach，再对最终站位评估 ±kFine，
 * 因此访问到的圆心集合是 best ± (kReach + kFine) = best ± 27；
 * 每个圆心要取半径 radius 的水平球壳切片，所以窗口必须覆盖
 *   [best - 27 - radius, best + 27 + radius]
 * 每边 kPreciseWindowMargin = 27 = kReach + kFine 刚好够用，
 * side = 2*(radius + 27) 在 radius=128 时是 311 格，
 * 比原来以桶原点为中心的 376 格少算 31.6% 面积。
 * 原来必须留 120 余量，是因为圆心最多偏离桶原点 63 格（±27 之外再叠桶偏移）。
 * 窗口起点按 best 对齐到 scale=1 的整数格即可；getVoronoiSrcRange 内部会
 * 自行右移 2 位对齐到 4 格网格，不要求这里预先对齐。 */
constexpr int kRefineExactReach = 24;
constexpr int kRefineExactFine = 3;
constexpr int kPreciseWindowMargin = kRefineExactReach + kRefineExactFine;
int kPreciseWindowSide(int radius) {
    return 2*(radius + kPreciseWindowMargin) + 1;
}

Hit refineExact(Local &local, const Config &cfg, const Hit &best, Control &control) {

    int radius = cfg.coverage == "game" ? 128 : cfg.innerRadius;

    // 窗口以 best 为中心；每个 finalist 的 XZ 在调用前已去重，无需再查缓存。
    int side = kPreciseWindowSide(radius);

    auto precise = sampleWindow(local,best.x-radius-kPreciseWindowMargin,
                                best.z-radius-kPreciseWindowMargin,

                                side,side,cfg,1,control);

    if (!precise)

        return {};

    auto time = Clock::now();

    Hit final;

    bool found = false;

    auto examine = [&](int x,int z) {

        if (!inside(cfg,x,z))

            return;

// §1.2 的上界剪枝：实测在 refineExact 的 ±24 小窗口内收益为负
// （100k 洞穴对照：precise 线程耗时 118s → 129s，候选逐条一致），
// 因此只在旧的 optimizePlayerY 兼容路径上保留；默认走平台解析解。
        if (cfg.optimizePlayerY && found &&
            scoreKey(precise->evaluate(cfg,x,z,cfg.playerY,true,&local.geometry)) < scoreKey(final))
            return;

        auto hit = precise->bestHeight(cfg,x,z,&control,&local.geometry);

        if (!found || better(hit,final,cfg)) {

            final = hit;

            found = true;

        }

    };

// 稀采样只确定附近热点；扩大真实细化范围，纠正插值造成的站位偏移。

    for (int dz = -24; dz <= 24; dz += 4) {

        if (!control.poll())

            return {};

        for (int dx = -24; dx <= 24; dx += 4)

            examine(best.x+dx,best.z+dz);

    }

    if (found) {

        int x0 = final.x, z0 = final.z;

        for (int dz = -3; dz <= 3; ++dz) {

            if (!control.poll())

                return {};

            for (int dx = -3; dx <= 3; ++dx)

                examine(x0+dx,z0+dz);

        }

    }

    local.geometryMs += milliseconds(time);

    return final;

}

}



static SearchResult searchPass(Config cfg, Control &control, const ProgressCallback &progress, bool cavesOnly,

                               const dripstone::OctaveCache *cache) {

    validate(cfg);

    SearchResult result;

    result.config = cfg;

    result.statistics.workerThreads = cfg.threads;

    result.statistics.caveFocused = cavesOnly;

    uint64_t seed = parseSeed(cfg.seed);

    int mc = versionInfo(cfg.version).cubiomesMc;

    std::vector<Local> workers(cfg.threads);

    for (auto &worker : workers) {

        worker.octaves = cache;

        worker.generator = std::make_unique<Generator>();

        setupGenerator(worker.generator.get(),mc,0);

        applySeed(worker.generator.get(),DIM_OVERWORLD,seed);

    }

    int minX, maxX, minZ, maxZ;

    if (cfg.rectMode) {

        minX = cfg.minX; maxX = cfg.maxX;

        minZ = cfg.minZ; maxZ = cfg.maxZ;

    } else {

        minX = int(std::max(int64_t(kWorldMin),int64_t(cfg.centerX)-cfg.radius));

        minZ = int(std::max(int64_t(kWorldMin),int64_t(cfg.centerZ)-cfg.radius));

        maxX = int(std::min(int64_t(kWorldMax),int64_t(cfg.centerX)+cfg.radius));

        maxZ = int(std::min(int64_t(kWorldMax),int64_t(cfg.centerZ)+cfg.radius));

    }

    int tile = cfg.radius >= 500000 ? 8192 : 2048;

    uint64_t nx = (maxX-minX)/tile+1, nz = (maxZ-minZ)/tile+1;

    /* 每线程本地 top-K：候选池不再共享，避免每块都在全局锁下重排
     * （大半径下这是主要串行点；作者实测 1e6 半径由“卡死”降到分钟级）。 */
    std::vector<std::vector<Hit>> local(workers.size());

    std::atomic<int64_t> coarseCutoff{0};

    std::atomic<int64_t> candidates{0};

    std::atomic<int64_t> scanned{0};

    std::mutex mutex;

    auto started = Clock::now();

    auto lastEmit = started-std::chrono::seconds(1);

    result.statistics.totalChunks = nx*nz;

    /* better 越小越优，大顶堆顶就是本线程当前最差的一条。 */
    auto heapPush = [&](std::vector<Hit> &heap,const Hit &hit) {
        auto compare = [&](const Hit &a,const Hit &b) { return better(a,b,cfg); };

        size_t capacity = size_t(cfg.candidateBudget);

        if (heap.size() < capacity) {
            heap.push_back(hit);
            std::push_heap(heap.begin(),heap.end(),compare);
            return;
        }

        if (better(hit,heap.front(),cfg)) {
            std::pop_heap(heap.begin(),heap.end(),compare);
            heap.back() = hit;
            std::push_heap(heap.begin(),heap.end(),compare);
        }
    };

    parallel(nx*nz,cfg.threads,control,[&](uint64_t index,int worker) {

        int x = minX+int(index%nx)*tile, z = minZ+int(index/nx)*tile;

        bool intersects = true;

        if (cfg.shape == "circle") {

            int px = std::clamp(cfg.centerX,x,std::min(x+tile-1,maxX));

            int pz = std::clamp(cfg.centerZ,z,std::min(z+tile-1,maxZ));

            intersects = distanceSquared(cfg,px,pz) <= int64_t(cfg.radius)*cfg.radius;

        }

        auto values = intersects ? coarseTile(workers[worker],cfg,x,z,

            std::min(tile,maxX-x+1),std::min(tile,maxZ-z+1),control,cavesOnly,

            coarseCutoff.load(std::memory_order_relaxed)) : std::vector<Hit>{};

        auto &heap = local[size_t(worker)];

        for (const auto &hit : values)

            heapPush(heap,hit);

        candidates.fetch_add(int64_t(values.size()),std::memory_order_relaxed);

        if (heap.size() == size_t(cfg.candidateBudget)) {

            ExactScore key = std::min(scoreKey(heap.front()),ExactScore(std::numeric_limits<int64_t>::max()));

            int64_t previous = coarseCutoff.load(std::memory_order_relaxed);

            while (int64_t(key) > previous &&

                   !coarseCutoff.compare_exchange_weak(previous,int64_t(key),std::memory_order_relaxed)) {

            }

        }

        scanned.fetch_add(1,std::memory_order_relaxed);

        if (progress && Clock::now()-lastEmit >= std::chrono::milliseconds(70)) {

            std::lock_guard lock(mutex);

            result.statistics.coarseCandidates = candidates.load(std::memory_order_relaxed);

            result.statistics.scannedChunks = scanned.load(std::memory_order_relaxed);

            result.statistics.activeMs = control.activeMilliseconds();

            progress(result.statistics);

            lastEmit = Clock::now();

        }

    });

    result.statistics.coarseCandidates = candidates.load(std::memory_order_relaxed);

    result.statistics.scannedChunks = scanned.load(std::memory_order_relaxed);

    if (progress) {

        result.statistics.activeMs = control.activeMilliseconds();

        progress(result.statistics);

    }

    result.statistics.coarseMs = milliseconds(started);

    std::vector<Hit> seeds;

    for (auto &heap : local)

        seeds.insert(seeds.end(),heap.begin(),heap.end());

    if (seeds.size() > size_t(cfg.candidateBudget)) {

        std::partial_sort(seeds.begin(),seeds.begin()+std::ptrdiff_t(cfg.candidateBudget),seeds.end(),

                          [&](const Hit &a,const Hit &b) { return better(a,b,cfg); });

        seeds.resize(size_t(cfg.candidateBudget));

    }

    if (cfg.radius <= 2048 && seeds.size() < size_t(cfg.candidateBudget)) {

        Hit center;

        center.x = cfg.centerX;

        center.z = cfg.centerZ;

        seeds.push_back(center);

    }

    std::sort(seeds.begin(),seeds.end(),[](const Hit &a,const Hit &b) {

        return std::pair(a.x,a.z) < std::pair(b.x,b.z);

    });

    result.statistics.phase = "refine";

    result.statistics.refineTotal = seeds.size()+std::min(seeds.size(),size_t(cfg.exactBudget > 0 ? cfg.exactBudget : std::max({kDefaultExactBudget,8*cfg.top,cfg.candidateBudget/16})));

    uint64_t done = 0;

    parallel(seeds.size(),cfg.threads,control,[&](uint64_t index,int worker) {

        auto hit = refine(workers[worker],cfg,seeds[index],control);

        if (inside(cfg,hit.x,hit.z) && scoreKey(hit) > 0)

            workers[worker].hits.push_back(hit);

        std::lock_guard lock(mutex);

        ++done;

        result.statistics.refinedCandidates = done;

        if (progress && Clock::now()-lastEmit >= std::chrono::milliseconds(70)) {

            result.statistics.activeMs = control.activeMilliseconds();

            progress(result.statistics);

            lastEmit = Clock::now();

        }

    });

    std::vector<Hit> finalists;

    for (auto &worker : workers) {

        finalists.insert(finalists.end(),worker.hits.begin(),worker.hits.end());

        worker.hits.clear();

    }

    std::sort(finalists.begin(),finalists.end(),[&](const Hit &a,const Hit &b) { return better(a,b,cfg); });

// 完全相同XZ会得到同一精排窗口和站位搜索；只占一份预算，给其他热点留下名额。

    std::set<std::pair<int,int>> positions;

    finalists.erase(std::remove_if(finalists.begin(),finalists.end(),[&](const Hit &hit) {

        return !positions.emplace(hit.x,hit.z).second;

    }),finalists.end());

// 精算预算（优化 3）：默认 768 个或 8 倍 Top K，给插值误差和热点归并留余量；
// 不是只重排近似榜单前 100。cfg.exactBudget > 0 时改用该值（--exact-budget）。
    size_t exactBudget = cfg.exactBudget > 0 ? size_t(cfg.exactBudget) :
        size_t(std::max({kDefaultExactBudget,8*cfg.top,cfg.candidateBudget/16}));

    exactBudget = std::min(finalists.size(),exactBudget);

    finalists.resize(exactBudget);

    result.statistics.screenedCandidates = seeds.size();

    result.statistics.preciseCandidates = finalists.size();

    result.statistics.refineTotal = done+finalists.size();

    parallel(finalists.size(),cfg.threads,control,[&](uint64_t index,int worker) {

        auto hit = refineExact(workers[worker],cfg,finalists[index],control);

        if (inside(cfg,hit.x,hit.z) && meetsMinimum(hit,cfg) && scoreKey(hit) > 0)

            workers[worker].hits.push_back(hit);

        std::lock_guard lock(mutex);

        result.statistics.refinedCandidates = ++done;

        if (progress && Clock::now()-lastEmit >= std::chrono::milliseconds(70)) {

            result.statistics.activeMs = control.activeMilliseconds();

            progress(result.statistics);

            lastEmit = Clock::now();

        }

    });

    for (auto &worker : workers) {

        result.hits.insert(result.hits.end(),worker.hits.begin(),worker.hits.end());

        result.statistics.biomeMs += worker.biomeMs;

        result.statistics.geometryMs += worker.geometryMs;

        result.statistics.biomeCells += worker.biomeCells;

        result.statistics.quartCells += worker.quartCells;

        result.statistics.quartMs += worker.quartMs;

        result.statistics.preciseMs += worker.preciseMs;

        result.statistics.continentMs += worker.continentMs;

        result.statistics.coarseSamplingMs += worker.coarseSamplingMs;

        result.statistics.prunedCoarseColumns += worker.prunedColumns;

        result.statistics.prunedCoarseTiles += worker.prunedTiles;

    }

    result.hits = selectHotspots(std::move(result.hits),cfg);

    result.statistics.candidates = result.hits.size();

    result.statistics.activeMs = control.activeMilliseconds();

    result.statistics.pausedMs = control.pausedMilliseconds();

    result.status = control.cancelled ? "cancelled" : "completed";

    result.statistics.phase = "refine";

    if (progress)

        progress(result.statistics);

    return result;

}





namespace {

uint64_t peakWorkingSetBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters)))
        return counters.PeakWorkingSetSize;
    return 0;
#else
    return 0;
#endif
}

double ratio(uint64_t part, uint64_t total) {
    return total ? double(part) / double(total) : 0.0;
}

}  // namespace

/** 把 --bench 计数写进统计；关闭时全部为 0。 */
static void applyBenchStatistics(SearchResult &result) {
    wcore::bench::flush();
    const auto &counters = wcore::bench::aggregate();
    auto &stats = result.statistics;
    stats.coarseNsPerCell = counters.coarseCells ?
        double(counters.ns[wcore::bench::STAGE_COARSE]) / double(counters.coarseCells) : 0;
    stats.weirdNsPerCell = counters.weirdCells ?
        double(counters.ns[wcore::bench::STAGE_WEIRD]) / double(counters.weirdCells) : 0;
    stats.continentNsPerCell = counters.continentCells ?
        double(counters.ns[wcore::bench::STAGE_CONTINENT]) / double(counters.continentCells) : 0;
    stats.aTableHitRateRiver = ratio(counters.riverTableHits, counters.riverTableSamples);
    stats.aTableHitRateCave = ratio(counters.caveTableHits, counters.caveTableSamples);
    stats.earlyExitRateContinent = ratio(counters.continentEarlyExits, counters.continentSamples);
    stats.fastAcceptRateContinent = ratio(counters.continentFastAccepts, counters.continentSamples);
    stats.earlyExitRateWeird = ratio(counters.weirdEarlyExits, counters.weirdSamples);
    stats.latticeMissRate = ratio(counters.latticeMisses, counters.latticeSamples);
    stats.dilationWritesSaved = counters.dilationWritesSaved;
    stats.peakWorkingSetBytes = peakWorkingSetBytes();
}

namespace {

/** 诊断用：以 (x,z) 为中心采样一个完整的球壳窗口（1:1）。 */
std::shared_ptr<BiomeWindow> samplePointWindow(Local &local, const Config &cfg,
                                               int x, int z, Control &control) {
    const int radius = cfg.coverage == "game" ? 128 : cfg.innerRadius;
    return sampleWindow(local, x - radius, z - radius, 2 * radius + 1, 2 * radius + 1,
                        cfg, 1, control);
}

/** 诊断用：按 cfg 建 Generator（调用方已 validate）。 */
void prepareLocal(Local &local, const Config &cfg) {
    local.generator = std::make_unique<Generator>();
    setupGenerator(local.generator.get(), versionInfo(cfg.version).cubiomesMc, 0);
    applySeed(local.generator.get(), DIM_OVERWORLD, parseSeed(cfg.seed));
}

}  // namespace

PointScore scorePoint(const Config &cfgIn, int x, int z, Control &control) {
    Config cfg = cfgIn;
    validate(cfg);

    Local local;
    prepareLocal(local, cfg);
    PointScore out;
    out.x = x;
    out.z = z;
    out.playerY = cfg.playerY;
    auto window = samplePointWindow(local, cfg, x, z, control);
    if (!window)
        return out;
    const Hit hit = window->evaluate(cfg, x, z, cfg.playerY, false, &local.geometry);
    out.riverArea = hit.riverArea;
    out.riverVolume = hit.riverVolume;
    out.caveVolume = hit.caveVolume;
    out.score = score(hit, cfg);
    return out;
}

std::vector<PointScore> sweepPointPlayerY(const Config &cfgIn, int x, int z,
                                          int from, int to, Control &control) {
    Config cfg = cfgIn;
    validate(cfg);

    std::vector<PointScore> out;
    if (from > to)
        return out;

    Local local;
    prepareLocal(local, cfg);
    auto window = samplePointWindow(local, cfg, x, z, control);
    if (!window)
        return out;

    out.reserve(size_t(to - from + 1));
    for (int y = from; y <= to; ++y) {
        if (!control.poll())
            break;
        const Hit hit = window->evaluate(cfg, x, z, y, false, &local.geometry);
        PointScore point{};
        point.x = x;
        point.z = z;
        point.playerY = y;
        point.riverArea = hit.riverArea;
        point.riverVolume = hit.riverVolume;
        point.caveVolume = hit.caveVolume;
        point.score = score(hit, cfg);
        out.push_back(point);
    }
    return out;
}

/** C3：大坑逃课路径。pit 附近必无河流，只计溶洞体积。 */
static SearchResult searchPit(Config cfg, Control &control, const ProgressCallback &progress) {
    SearchResult result;
    result.config = cfg;
    result.statistics.workerThreads = cfg.threads;
    result.statistics.caveFocused = true;
    result.statistics.phase = "pit";

    const uint64_t seed = parseSeed(cfg.seed);
    const int mc = versionInfo(cfg.version).cubiomesMc;

    Local local;
    local.generator = std::make_unique<Generator>();
    setupGenerator(local.generator.get(), mc, 0);
    applySeed(local.generator.get(), DIM_OVERWORLD, seed);

    wcore::pit::Options options;
    options.threads = cfg.threads;
    options.clipMargin = 256;
    if (cfg.rectMode) {
        options.minX = cfg.minX; options.maxX = cfg.maxX;
        options.minZ = cfg.minZ; options.maxZ = cfg.maxZ;
    } else {
        options.minX = int(std::max(int64_t(kWorldMin), int64_t(cfg.centerX) - cfg.radius));
        options.maxX = int(std::min(int64_t(kWorldMax), int64_t(cfg.centerX) + cfg.radius));
        options.minZ = int(std::max(int64_t(kWorldMin), int64_t(cfg.centerZ) - cfg.radius));
        options.maxZ = int(std::min(int64_t(kWorldMax), int64_t(cfg.centerZ) + cfg.radius));
    }

    wcore::pit::Progress pitProgress;
    std::atomic<bool> finished{false};
    std::thread watcher([&] {
        while (!finished.load(std::memory_order_relaxed)) {
            if (!control.poll())
                pitProgress.stop.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    auto started = Clock::now();
    std::vector<wcore::pit::Peak> peaks =
        wcore::pit::findWeirdnessPeaks(&local.generator->bn, options, &pitProgress);
    finished.store(true);
    watcher.join();
    result.statistics.pitMs = milliseconds(started);
    result.statistics.pitPeaks = peaks.size();

    if (control.cancelled) {
        result.status = "cancelled";
        result.statistics.activeMs = control.activeMilliseconds();
        result.statistics.phase = "pit";
        return result;
    }

    std::sort(peaks.begin(), peaks.end(), [](const wcore::pit::Peak &a, const wcore::pit::Peak &b) {
        return std::fabs(a.w) > std::fabs(b.w);
    });

    const int radius = cfg.coverage == "game" ? 128 : cfg.innerRadius;
    // B 的 pit 评分在峰附近 512×512 窗口里找最大区域中心，而不是只算峰点本身。
    constexpr int kPitCenterReach = 128, kPitCenterStride = 16;
    std::vector<Hit> coarse;
    const size_t coarseLimit = std::min(peaks.size(), size_t(std::max(256, 4 * cfg.top)));
    for (size_t i = 0; i < coarseLimit; ++i) {
        const auto &peak = peaks[i];
        if (!inside(cfg, peak.bx, peak.bz))
            continue;
        auto window = sampleWindow(local, peak.bx - radius - 52 - kPitCenterReach,
                                   peak.bz - radius - 52 - kPitCenterReach,
                                   2 * (radius + 52 + kPitCenterReach),
                                   2 * (radius + 52 + kPitCenterReach), cfg, 8, control);
        if (!window)
            continue;
        Hit best = window->evaluate(cfg, peak.bx, peak.bz, cfg.playerY, false, &local.geometry);
        bool found = true;
        for (int dz = -kPitCenterReach; dz <= kPitCenterReach; dz += kPitCenterStride)
            for (int dx = -kPitCenterReach; dx <= kPitCenterReach; dx += kPitCenterStride) {
                const int cx = peak.bx + dx, cz = peak.bz + dz;
                if (!inside(cfg, cx, cz))
                    continue;
                Hit hit = window->evaluate(cfg, cx, cz, cfg.playerY, false, &local.geometry);
                if (!found || better(hit, best, cfg)) {
                    best = hit;
                    found = true;
                }
            }
        // §10.2：pit 附近不计河流面积。
        best.riverArea = 0;
        best.riverVolume = 0;
        if (scoreKey(best) > 0)
            coarse.push_back(best);
        if (progress && (i & 15) == 0) {
            result.statistics.activeMs = control.activeMilliseconds();
            progress(result.statistics);
        }
    }
    std::sort(coarse.begin(), coarse.end(), [&](const Hit &a, const Hit &b) {
        return better(a, b, cfg);
    });
    if (coarse.size() > size_t(std::max(64, 2 * cfg.top)))
        coarse.resize(size_t(std::max(64, 2 * cfg.top)));

    for (const auto &candidate : coarse) {
        if (!control.poll())
            break;
        auto window = sampleWindow(local, candidate.x - radius - 28, candidate.z - radius - 28,
                                   2 * radius + 120, 2 * radius + 120, cfg, 1, control);
        if (!window)
            continue;
        Hit hit = window->evaluate(cfg, candidate.x, candidate.z, cfg.playerY, false, &local.geometry);
        hit.riverArea = 0;
        hit.riverVolume = 0;
        if (meetsMinimum(hit, cfg))
            result.hits.push_back(hit);
    }

    result.hits = selectHotspots(std::move(result.hits), cfg);
    result.statistics.candidates = result.hits.size();
    result.statistics.activeMs = control.activeMilliseconds();
    result.statistics.pausedMs = control.pausedMilliseconds();
    result.status = control.cancelled ? "cancelled" : "completed";
    result.statistics.phase = "refine";
    if (progress)
        progress(result.statistics);
    return result;
}

SearchResult search(Config cfg, Control &control, const ProgressCallback &progress) {

    validate(cfg);

    wcore::bench::setEnabled(cfg.bench);

    if (cfg.bench)

        wcore::bench::reset();

// C3：大坑逃课路径与普通搜索互斥。
    if (cfg.pitMode) {

        SearchResult pit = searchPit(cfg,control,progress);

        applyBenchStatistics(pit);

        return pit;

    }

    int radius = cfg.coverage == "game" ? 128 : cfg.innerRadius;

    bool focused = cfg.includeCaves && cfg.radius >= 500000 && radius <= 512;

// C2b：纯河流可以在更小的范围建轻量表（只建 weirdness 3 oct）。
    int cacheRadius = cfg.coverage == "game" ? 128 : cfg.innerRadius;

    bool wantCache = cfg.radius >= 500000;

    if (!cfg.includeCaves && cfg.pureRiverCache != "off" && cacheRadius >= 128) {

        const int threshold = cfg.pureRiverCache == "on" ? 0 : 200000;

        wantCache = wantCache || cfg.radius >= threshold;

    }

    auto cacheStarted = Clock::now();

    auto cache = wantCache ? buildOctaveCache(cfg,control,focused,!cfg.includeCaves) : nullptr;

    double cacheMs = milliseconds(cacheStarted);

    auto first = searchPass(cfg,control,progress,focused,cache.get());

    first.statistics.cacheBuildMs = cache ? cacheMs : 0;

    first.statistics.cacheBytes = cache ? cache->bytes() : 0;

    if (!focused || control.cancelled) {

        applyBenchStatistics(first);

        return first;

    }

    int64_t pureUpper = pureRiverUpperBound(cfg);

    first.statistics.pureRiverCeiling = pureUpper;

    first.statistics.focusedCutoff = first.hits.empty() ? 0 : score(first.hits.back(),cfg);

// 上限已覆盖区块内全部站位；自动Y用更宽松截面上限。严格超过才能排除同分近点。

    ExactScore upper = ExactScore(pureUpper)*kRiverWeight*layersOf(cfg);

    if (cfg.minArea > pureUpper ||

        (first.hits.size() == size_t(cfg.top) && scoreKey(first.hits.back()) > upper)) {

        applyBenchStatistics(first);

        return first;

    }

// 溶洞不足以压过纯河流时补扫全范围，不能把大范围搜索强行变成只找溶洞。

    auto result = searchPass(cfg,control,progress,false,cache.get());

    result.hits.insert(result.hits.end(),first.hits.begin(),first.hits.end());

    result.hits = selectHotspots(std::move(result.hits),cfg);

    auto &a = result.statistics;

    const auto &b = first.statistics;

    a.cacheBuildMs = first.statistics.cacheBuildMs;

    a.cacheBytes = first.statistics.cacheBytes;

    a.caveFocused = true;

    a.riverFallback = true;

    a.pureRiverCeiling = pureUpper;

    a.focusedCutoff = first.statistics.focusedCutoff;

    a.scannedChunks += b.scannedChunks;

    a.totalChunks += b.totalChunks;

    a.coarseCandidates += b.coarseCandidates;

    a.refinedCandidates += b.refinedCandidates;

    a.refineTotal += b.refineTotal;

    a.screenedCandidates += b.screenedCandidates;

    a.preciseCandidates += b.preciseCandidates;

    a.biomeCells += b.biomeCells;

    a.quartCells += b.quartCells;

    a.coarseMs += b.coarseMs;

    a.biomeMs += b.biomeMs;

    a.geometryMs += b.geometryMs;

    a.quartMs += b.quartMs;

    a.preciseMs += b.preciseMs;

    a.continentMs += b.continentMs;

    a.coarseSamplingMs += b.coarseSamplingMs;

    a.prunedCoarseColumns += b.prunedCoarseColumns;

    a.prunedCoarseTiles += b.prunedCoarseTiles;

    a.candidates = result.hits.size();

    applyBenchStatistics(result);

    return result;

}



void Control::pause() {

    std::lock_guard<std::mutex> lock(mutex_);

    if (paused.exchange(true)) return;

    pausedAt_ = Clock::now();

    cv_.notify_all();

}



void Control::resume() {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!paused.exchange(false)) return;

    pausedTime_ += Clock::now() - pausedAt_;

    cv_.notify_all();

}



void Control::cancel() {

    cancelled.store(true);

    {

        std::lock_guard<std::mutex> lock(mutex_);

        paused.store(false);

    }

    cv_.notify_all();

}



bool Control::poll() {

    if (cancelled.load()) return false;

// 正常计算只读原子状态；高频检查不能让所有工作线程争抢同一把暂停锁。

    if (!paused.load()) return true;

    std::unique_lock<std::mutex> lock(mutex_);

    cv_.wait(lock, [this] { return !paused.load() || cancelled.load(); });

    return !cancelled.load();

}



int64_t Control::activeMilliseconds() {

    std::lock_guard<std::mutex> lock(mutex_);

    auto elapsed = Clock::now() - started_ - pausedTime_;

    if (paused.load()) elapsed -= Clock::now() - pausedAt_;

    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

}



int64_t Control::pausedMilliseconds() {

    std::lock_guard<std::mutex> lock(mutex_);

    auto total = pausedTime_;

    if (paused.load()) total += Clock::now() - pausedAt_;

    return std::chrono::duration_cast<std::chrono::milliseconds>(total).count();

}





}

