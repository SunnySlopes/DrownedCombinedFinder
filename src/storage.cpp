#include "storage.hpp"
#include "coverage.hpp"
#include "range_units.hpp"
#include "../core/platform/player_y.hpp"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace riverfinder_native {
Json configJson(const Config &cfg) {
    return Json{{"seed",cfg.seed},{"version",cfg.version},{"centerX",cfg.centerX},{"centerZ",cfg.centerZ},
        {"radius",cfg.radius},{"shape",cfg.shape},{"fullWorld",cfg.fullWorld},{"threads",cfg.threads},
        {"top",cfg.top},{"minArea",cfg.minArea},{"includeCaves",cfg.includeCaves},{"coverage",cfg.coverage},
        {"innerRadius",cfg.innerRadius},{"simulationDistanceChunks",cfg.simulationDistanceChunks},
        {"playerY",cfg.playerY},{"optimizePlayerY",cfg.optimizePlayerY},{"minDistance",cfg.minDistance},
        {"coarseY",cfg.coarseY},
        {"candidateBudget",cfg.candidateBudget},{"yMin",cfg.yMin},{"yMax",cfg.yMax},
        {"approximateGate",cfg.approximateGate},{"rectMode",cfg.rectMode},
        {"minX",cfg.minX},{"maxX",cfg.maxX},{"minZ",cfg.minZ},{"maxZ",cfg.maxZ},
        {"minRiverArea",cfg.minRiverArea},{"minCaveArea",cfg.minCaveArea},
        {"caveConversion",cfg.caveConversion},{"pureRiverCache",cfg.pureRiverCache},
        {"pureRiverMaskStep",cfg.pureRiverMaskStep},
        {"pitMode",cfg.pitMode},{"bench",cfg.bench}};
}

Config configFromJson(const Json &value) {
    if (!value.is_object())
        throw std::invalid_argument("配置必须是JSON对象");
    Config cfg;
    Json known = configJson(cfg);
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (it.key() == "sampleY" || it.key() == "caveFactor")
            throw std::invalid_argument("sampleY/caveFactor已移除；使用includeCaves和coverage，刷怪脚部从Y=-63起");
        if (!known.contains(it.key()))
            throw std::invalid_argument("未知配置字段: "+it.key());
        const auto &expected = known.at(it.key());
        if (expected.is_number_integer() && it.value().is_number_integer()) {
            long double n = it.value().get<long double>();
            if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max())
                throw std::invalid_argument("整数配置超出允许范围: "+it.key());
        }
        bool distance = it.key() == "radius" || it.key() == "innerRadius" ||
            it.key() == "simulationDistanceChunks" || it.key() == "minDistance";
        if (!distance && ((expected.is_number_integer() && !it.value().is_number_integer()) ||
            (expected.is_boolean() && !it.value().is_boolean()) ||
            (expected.is_string() && !it.value().is_string())))
            throw std::invalid_argument("配置字段类型不匹配: "+it.key());
    }
    cfg.seed = value.value("seed",cfg.seed);
    cfg.version = value.value("version",cfg.version);
    cfg.centerX = value.value("centerX",cfg.centerX);
    cfg.centerZ = value.value("centerZ",cfg.centerZ);
    cfg.shape = value.value("shape",cfg.shape);
    cfg.coverage = value.value("coverage",cfg.coverage);
    cfg.fullWorld = value.value("fullWorld",cfg.fullWorld);
    cfg.threads = value.value("threads",cfg.threads);
    cfg.top = value.value("top",cfg.top);
    cfg.minArea = value.value("minArea",cfg.minArea);
    cfg.includeCaves = value.value("includeCaves",cfg.includeCaves);
    cfg.playerY = value.value("playerY",cfg.playerY);
    cfg.optimizePlayerY = value.value("optimizePlayerY",cfg.optimizePlayerY);
    cfg.coarseY = value.value("coarseY",cfg.coarseY);
    cfg.candidateBudget = value.value("candidateBudget",cfg.candidateBudget);
    cfg.yMin = value.value("yMin",cfg.yMin);
    cfg.yMax = value.value("yMax",cfg.yMax);
    cfg.approximateGate = value.value("approximateGate",cfg.approximateGate);
    cfg.rectMode = value.value("rectMode",cfg.rectMode);
    cfg.minX = value.value("minX",cfg.minX);
    cfg.maxX = value.value("maxX",cfg.maxX);
    cfg.minZ = value.value("minZ",cfg.minZ);
    cfg.maxZ = value.value("maxZ",cfg.maxZ);
    cfg.minRiverArea = value.value("minRiverArea",cfg.minRiverArea);
    cfg.minCaveArea = value.value("minCaveArea",cfg.minCaveArea);
    cfg.caveConversion = value.value("caveConversion",cfg.caveConversion);
    cfg.pureRiverCache = value.value("pureRiverCache",cfg.pureRiverCache);
    cfg.pureRiverMaskStep = value.value("pureRiverMaskStep",cfg.pureRiverMaskStep);
    cfg.pitMode = value.value("pitMode",cfg.pitMode);
    cfg.bench = value.value("bench",cfg.bench);
    if (value.contains("radius"))
        cfg.radius = range_units::read(value.at("radius"));
    if (value.contains("innerRadius"))
        cfg.innerRadius = range_units::read(value.at("innerRadius"));
    if (value.contains("minDistance"))
        cfg.minDistance = range_units::read(value.at("minDistance"));
    if (value.contains("simulationDistanceChunks"))
        cfg.simulationDistanceChunks = range_units::read(value.at("simulationDistanceChunks"),true);
    validate(cfg);
    return cfg;
}

Json statisticsJson(const Statistics &s) {
    return Json{{"scannedTiles",s.scannedChunks},{"totalTiles",s.totalChunks},{"candidates",s.candidates},
        {"coarseCandidates",s.coarseCandidates},{"refinedCandidates",s.refinedCandidates},{"refineTotal",s.refineTotal},
        {"caveFocused",s.caveFocused},{"riverFallback",s.riverFallback},
        {"prunedCoarseColumns",s.prunedCoarseColumns},{"prunedCoarseTiles",s.prunedCoarseTiles},
        {"cacheBuildMs",s.cacheBuildMs},{"cacheBytes",s.cacheBytes},
        {"continentWorkerMs",s.continentMs},{"coarseSamplingWorkerMs",s.coarseSamplingMs},
        {"pureRiverCeiling",s.pureRiverCeiling},{"focusedCutoff",s.focusedCutoff},
        {"screenedCandidates",s.screenedCandidates},{"preciseCandidates",s.preciseCandidates},
        {"quartCells",s.quartCells},{"quartWorkerMs",s.quartMs},{"preciseWorkerMs",s.preciseMs},
        {"biomeCells",s.biomeCells},{"activeMs",s.activeMs},{"pausedMs",s.pausedMs},{"wallMs",s.wallMs},
        {"workerThreads",s.workerThreads},{"phase",s.phase},{"coarseMs",s.coarseMs},
        {"biomeWorkerMs",s.biomeMs},{"geometryWorkerMs",s.geometryMs},{"exportMs",s.exportMs},
        {"coarseNsPerCell",s.coarseNsPerCell},{"weirdNsPerCell",s.weirdNsPerCell},
        {"continentNsPerCell",s.continentNsPerCell},
        {"aTableHitRateRiver",s.aTableHitRateRiver},{"aTableHitRateCave",s.aTableHitRateCave},
        {"earlyExitRateContinent",s.earlyExitRateContinent},{"earlyExitRateWeird",s.earlyExitRateWeird},
        {"fastAcceptRateContinent",s.fastAcceptRateContinent},
        {"latticeMissRate",s.latticeMissRate},{"dilationWritesSaved",s.dilationWritesSaved},
        {"peakWorkingSetBytes",s.peakWorkingSetBytes},
        {"pitPeaks",s.pitPeaks},{"pitMs",s.pitMs}};
}

Json hitJson(const Hit &hit) {
    return Json{{"x",hit.x},{"y",hit.y},{"z",hit.z},{"riverArea",hit.riverArea},
        {"riverVolume",hit.riverVolume},{"caveVolume",hit.caveVolume},{"radius",hit.radius},
        {"regionX",hit.regionX},{"regionZ",hit.regionZ}};
}

Json candidateJson(const SearchResult &result, size_t index) {
    const auto &hit = result.hits.at(index);
    auto value = hitJson(hit);
    int64_t distanceSq = distanceSquared(result.config,hit.x,hit.z);
    value.update(Json{{"id","river-"+std::to_string(hit.x)+"-"+std::to_string(hit.z)},
        {"rank",index+1},{"score",score(hit,result.config)},{"equivalentArea",score(hit,result.config)},
        {"scoreNumerator",scoreNumerator(hit)},{"scoreDenominator",scoreDenominator(result.config)},
        {"scoreUnit",result.config.includeCaves ? "等效河流格" : "格"},
        {"distance",std::sqrt(double(distanceSq))},{"distanceSq",distanceSq},
        {"secondary",hit.caveVolume},{"secondaryUnit","溶洞格位"},
        {"bestPlayer",{{"x",hit.x},{"y",hit.y},{"z",hit.z}}},
        {"tpCommand","/tp @s "+std::to_string(hit.x)+" "+std::to_string(hit.y)+" "+std::to_string(hit.z)},
        {"verified",false}});
    return value;
}

Json resultJson(const SearchResult &result) {
    Json candidates = Json::array();
    for (size_t i = 0; i < result.hits.size(); ++i)
        candidates.push_back(candidateJson(result,i));
    const Config &cfg = result.config;
    const int layers = layersOf(cfg);
    Json warnings = Json::array();
    for (const auto &warning : cfg.warnings)
        warnings.push_back(warning);
    // 四分权重按 yMin..yMax 自动生成（C6）；纯河流模式为空。
    Json weights = Json::array();
    if (cfg.includeCaves) {
        const int y0 = floorDiv(cfg.yMin,4);
        std::vector<int> quart(size_t(floorDiv(cfg.yMax,4)-y0+1),0);
        for (int y = cfg.yMin; y <= cfg.yMax; ++y)
            ++quart[size_t(floorDiv(y,4)-y0)];
        for (int count : quart) weights.push_back(count);
    }
    return Json{{"finder",kFinder},{"engine",kEngine},{"engineVersion",RIVER_ENGINE_VERSION},
        {"cubiomesSourceId",CUBIOMES_REVISION},{"protocolVersion",kProtocolVersion},
        {"rankingRuleVersion",kRankingRuleVersion},{"rankingRule",kRankingRule},
        {"mcVersion",cfg.version},{"kernelClass",versionInfo(cfg.version).kernelName},
        {"searchMethod",cfg.pitMode ? "pit-escape-v1" : "drowned-multistage-v3"},
        {"model","drowned-weighted-area-v2"},{"verified",false},{"searchApproximate",true},
        {"spawnYMin",cfg.yMin},{"spawnYMax",cfg.yMax},{"layers",layers},
        {"playerYSource",cfg.optimizePlayerY ? "platform" : (cfg.playerYDerived ? "auto" : "fixed")},
        /* 显示分母：Σ各层 24..128 球壳格数 / 层数（与 B 的 RIVER_AREA_FULL 同源）。
         * game 模式还会按自然刷怪区块裁剪，所以它是上界而非可达满分。 */
        {"fullShellArea",wcore::platform::fullShellArea(cfg.yMin,cfg.yMax,cfg.playerY)},
        {"fullShellAreaNote","未按自然刷怪区块/模拟距离裁剪的上界"},
        {"scoreDenominator",scoreDenominator(cfg)},
        {"approximateGate",cfg.approximateGate},
        {"minimumArea",{{"river",cfg.minRiverArea},{"cave",cfg.minCaveArea},
                        {"conversion",cfg.caveConversion},{"threshold",cfg.minArea}}},
        {"quartWeights",weights},
        {"caveWeight",{{"numerator",kCaveWeight},{"denominator",kRiverWeight},
                       {"value",double(kCaveWeight)/kRiverWeight},{"theoretical",true}}},
        {"scoreAssumptions","挖空、供水、遮光、排除其他刷怪点；不估计高度图与实际每小时产量"},
        {"timingScopes",{{"coarseMs","阶段墙钟，含暂停"},{"biomeWorkerMs","采样与缓存构建的线程累计耗时"},
                         {"geometryWorkerMs","几何计算的线程累计耗时"},
                         {"quartWorkerMs","近似候选窗口采样的线程累计耗时"},
                         {"preciseWorkerMs","逐格最终窗口采样的线程累计耗时"},
                         {"coarseNsPerCell","--bench：粗筛线程纳秒/粗格"},
                         {"weirdNsPerCell","--bench：weird 门线程纳秒/采样"},
                         {"continentNsPerCell","--bench：大陆性门线程纳秒/格"}}},
        {"warnings",warnings},
        {"status",result.status},{"config",configJson(result.config)},
        {"statistics",statisticsJson(result.statistics)},{"candidates",candidates}};
}

ExportText exportText(const SearchResult &result) {
    std::ostringstream csv,txt;
    csv << std::setprecision(15);
    csv << "rank,x,y,z,score,riverArea,riverVolume,caveVolume,distance,scoreNumerator,scoreDenominator\n";
    txt << "种子: " << result.config.seed << "  版本: " << result.config.version << '\n';
    txt << "模型: drowned-weighted-area-v2；预计值，非游戏实测\n";
    txt << "排名: 主评分降序 -> 距搜索中心距离升序 -> X升序 -> Z升序\n";
    txt << "统计: " << result.config.coverage << "  溶洞折算: " << result.config.includeCaves
        << "  洞穴权重: 7011/4880  层范围: Y" << result.config.yMin << ".." << result.config.yMax
        << "  层数: " << layersOf(result.config) << "\n\n";
    for (size_t i = 0; i < result.hits.size(); ++i) {
        const auto &h = result.hits[i];
        csv << i+1 << ',' << h.x << ',' << h.y << ',' << h.z << ',' << score(h,result.config) << ','
            << h.riverArea << ',' << h.riverVolume << ',' << h.caveVolume << ','
            << std::sqrt(double(distanceSquared(result.config,h.x,h.z))) << ','
            << scoreNumerator(h) << ',' << scoreDenominator(result.config) << '\n';
        txt << '#' << i+1 << "  /tp @s " << h.x << ' ' << h.y << ' ' << h.z
            << "  评分=" << std::setprecision(15) << score(h,result.config)
            << "  底层河流=" << h.riverArea << "  河流体积=" << h.riverVolume
            << "  溶洞体积=" << h.caveVolume << '\n';
    }
    return {csv.str(),txt.str()};
}

Json exportResults(const std::filesystem::path &prefix, SearchResult &result) {
    auto started = Clock::now();
    const ExportText text = exportText(result);
    std::filesystem::path dir = prefix.parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir);

    auto csvPath = prefix;
    csvPath += utf8Path(".csv");
    auto txtPath = prefix;
    txtPath += utf8Path(".txt");
    auto jsonPath = prefix;
    jsonPath += utf8Path(".json");
    {
        std::ofstream out(csvPath, std::ios::binary);
        if (!out || !(out << text.csv).flush())
            throw std::runtime_error("CSV写入失败");
    }
    {
        std::ofstream out(txtPath, std::ios::binary);
        if (!out || !(out << text.txt).flush())
            throw std::runtime_error("TXT写入失败");
    }
    result.statistics.exportMs = std::chrono::duration<double,std::milli>(Clock::now()-started).count();
    atomicJson(jsonPath, resultJson(result));

    return Json{{"csv", pathUtf8(csvPath)},
                {"txt", pathUtf8(txtPath)},
                {"json", pathUtf8(jsonPath)}};
}

void atomicJson(const std::filesystem::path &path, const Json &value) {
    std::filesystem::path dir = path.parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir);
    auto temp = path;
    temp += utf8Path(".tmp");
    {
        std::ofstream out(temp, std::ios::binary);
        if (!out || !(out << value.dump(2)).flush())
            throw std::runtime_error("JSON写入失败");
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(temp, path, ec);
    }
    if (ec)
        throw std::filesystem::filesystem_error("结果文件替换失败",temp,path,ec);
}

std::filesystem::path utf8Path(const std::string &value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

std::string pathUtf8(const std::filesystem::path &path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

}  // namespace riverfinder_native
