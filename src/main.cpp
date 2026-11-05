/* river-native 的命令行入口。
 *
 * 与另外四个引擎同一套外部约定：build.ps1 构建、run.bat 读取本目录 finder.json，
 * 原生程序只在显式 --config 时读配置；配置键是 CLI 参数去掉 `--`，
 * 命令行覆盖配置，seed 为字符串，--output 是结果目录。
 */
#include "storage.hpp"
#include "cli_config.hpp"
#include "protocol.hpp"
#include "range_units.hpp"
#include "core/pit/weirdness_y_offset.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace riverfinder_native {

namespace {

void emit(const Json &value) { std::cout << value.dump() << '\n' << std::flush; }

void help() {
    std::cout <<
R"(river-native —— 低Y溺尸农场搜索后端
用法: river-finder --seed <十进制64位> [选项]
      river-finder --desktop
      river-finder --versions
      river-finder --weirdness-y-offset <种子>
外范围: --center-x 0 --center-z 0 --radius 100000 --shape circle|square
        --radius-unit blocks|chunks --full-world
        自由矩形: --min-x --max-x --min-z --max-z（与中心/半径互斥）
内范围: --coverage game|circle|square --inner-radius 128 --inner-radius-unit blocks|chunks
        --simulation-distance 10 --simulation-distance-unit chunks|blocks
        --y-min -63 --y-max -51（自定义层范围）
        --player-y -40（固定挂机Y；默认 yMin+23 =-40）
        --optimize-player-y（计算最佳挂机Y，平台解析解）
折算:   --include-caves（关闭时仅Y=-63普通河流，开启后按yMin..yMax逐层统计）
筛选:   --min-area 34748 --min-distance 256 --candidate-budget 0
        --exact-budget 0（0=自动：max(768, 8*top, candidate-budget/16)）
        三字段: --min-river-area R --min-cave-area C（门槛 = R + 1.43668·C）
        --approximate-gate / --no-approximate-gate（八度早退与 weird 早退同开关）
        --pure-river-cache auto|on|off --pure-river-mask 32|64 --pit --bench
        --coarse-y -51（粗筛召回高度；粗筛门与 Y 无关，仅为协议/CLI 兼容）
诊断:   --score-point <x> <z>（单点 1:1 打分，与最终精排同一几何）
        --sweep-afk-y <a> <b>（同一份群系采样下扫描挂机 Y）
        --repeat <n>（单点计时重复次数，默认5）
输出:   --limit 100 --output results --jsonl
线程:   --threads <n>（默认=当前机器硬件并发度，多核机器吃满全部核心）
配置:   --config finder.json；键为不带--的参数名，命令行覆盖。
版本:   --versions列出1.18至26.3全部正式版入口。
结果为群系与几何模型预计值，不是实际每小时产量。
)";
}

int run(const std::vector<std::string> &input) {
    for (const auto &arg : input)
        if (arg == "--desktop") return desktop_protocol();

    auto args = cli_config::arguments(input);
    if (args.empty() || std::find(args.begin(), args.end(), "--help") != args.end()) {
        help();
        return 0;
    }
    if (std::find(args.begin(), args.end(), "--versions") != args.end()) {
        Json table = Json::array();
        for (const auto &version : versions()) {
            const VersionInfo info = versionInfo(version);
            table.push_back(Json{{"version", version}, {"kernelClass", info.kernelName}});
        }
        emit(Json{{"versions", table}});
        return 0;
    }
    // C3：pit 种子的预筛分数（与 B 的 --weirdness-y-offset 同口径）。
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] != "--weirdness-y-offset")
            continue;
        if (++i == args.size())
            throw std::invalid_argument("--weirdness-y-offset需要种子");
        const float offset = wcore::pit::weirdnessYOffset(parseSeed(args[i]));
        emit(Json{{"seed", args[i]}, {"weirdnessYOffset", offset},
                  {"recommendation", wcore::pit::recommendation(offset)}});
        return 0;
    }

    Json config = Json::object();
    auto flag = [&](const std::string &name) {
        for (size_t i = args.size(); i > 1; --i)
            if (args[i - 2] == name) return args[i - 1];
        return std::string{};
    };

    std::string output = "results";
    bool jsonl = false;
    // 单点诊断（与 B 的 --score-point / --sweep-afk-y / --repeat 对应）
    bool pointMode = false;
    int pointX = 0, pointZ = 0;
    bool pointSweep = false;
    int sweepFrom = -64, sweepTo = 320;
    int pointRepeat = 5;
    for (size_t i = 0; i < args.size(); ++i) {
        auto name = args[i];
        auto value = [&]() -> std::string {
            if (++i >= args.size()) throw std::invalid_argument(name + "缺少参数");
            return args[i];
        };
        if (name == "--jsonl") { jsonl = true; continue; }
        if (name == "--radius-unit" || name == "--inner-radius-unit" ||
            name == "--simulation-distance-unit" || name == "--min-distance-unit") {
            value();
            continue;
        }
        if (name == "--sample-y" || name == "--cave-factor")
            throw std::invalid_argument("旧参数已移除；使用--include-caves和--coverage，刷怪脚部从Y=-63起");
        if (name == "--include-caves" || name == "--no-include-caves") {
            config["includeCaves"] = name == "--include-caves";
            continue;
        }
        if (name == "--approximate-gate" || name == "--no-approximate-gate") {
            config["approximateGate"] = name == "--approximate-gate";
            continue;
        }
        if (name == "--pit" || name == "--bench") {
            config[name == "--pit" ? "pitMode" : "bench"] = true;
            continue;
        }
        if (name == "--optimize-player-y" || name == "--no-optimize-player-y") {
            config["optimizePlayerY"] = name == "--optimize-player-y";
            continue;
        }
        if (name == "--full-world") { config["fullWorld"] = true; continue; }
        if (name == "--output") { output = value(); continue; }
        if (name == "--score-point") {
            const auto sx = value();
            const auto sz = value();
            pointX = int(std::stoll(sx));
            pointZ = int(std::stoll(sz));
            pointMode = true;
            continue;
        }
        if (name == "--sweep-afk-y") {
            const auto from = value();
            const auto to = value();
            sweepFrom = int(std::stoll(from));
            sweepTo = int(std::stoll(to));
            pointSweep = true;
            continue;
        }
        if (name == "--repeat") { pointRepeat = int(std::stoll(value())); continue; }
        if (name == "--seed") { config["seed"] = value(); continue; }
        if (name == "--version") { config["version"] = value(); continue; }
        if (name == "--shape") { config["shape"] = value(); continue; }
        if (name == "--coverage") { config["coverage"] = value(); continue; }
        if (name == "--pure-river-cache") { config["pureRiverCache"] = value(); continue; }
        if (name == "--cave-conversion") {
            auto text = value();
            try { config["caveConversion"] = std::stod(text); }
            catch (const std::exception &) { throw std::invalid_argument("--cave-conversion必须是数字"); }
            continue;
        }

        std::string key;
        if (name == "--center-x") key = "centerX";
        else if (name == "--center-z") key = "centerZ";
        else if (name == "--radius") key = "radius";
        else if (name == "--limit" || name == "--top") key = "top";
        else if (name == "--threads") key = "threads";
        else if (name == "--min-area") key = "minArea";
        else if (name == "--inner-radius") key = "innerRadius";
        else if (name == "--simulation-distance") key = "simulationDistanceChunks";
        else if (name == "--player-y") key = "playerY";
        else if (name == "--min-distance") key = "minDistance";
        else if (name == "--candidate-budget") key = "candidateBudget";
        else if (name == "--exact-budget") key = "exactBudget";
        else if (name == "--pure-river-mask") key = "pureRiverMaskStep";
        else if (name == "--y-min") key = "yMin";
        else if (name == "--y-max") key = "yMax";
        else if (name == "--coarse-y") key = "coarseY";
        else if (name == "--min-river-area") key = "minRiverArea";
        else if (name == "--min-cave-area") key = "minCaveArea";
        else if (name == "--min-x") { key = "minX"; config["rectMode"] = true; }
        else if (name == "--max-x") { key = "maxX"; config["rectMode"] = true; }
        else if (name == "--min-z") { key = "minZ"; config["rectMode"] = true; }
        else if (name == "--max-z") { key = "maxZ"; config["rectMode"] = true; }
        else throw std::invalid_argument("未知参数: " + name);

        auto text = value();
        if (name == "--radius" || name == "--inner-radius" ||
            name == "--simulation-distance" || name == "--min-distance") {
            bool chunks = name == "--simulation-distance";
            const auto unit = flag(name+"-unit").empty() ? (chunks ? "chunks" : "blocks") : flag(name+"-unit");
            config[key] = range_units::text(text,unit,chunks);
            continue;
        }
        size_t end = 0;
        long long number = 0;
        try {
            number = std::stoll(text, &end, 10);
        } catch (const std::exception &) {
            throw std::invalid_argument(name + "必须是整数");
        }
        if (end != text.size()) throw std::invalid_argument(name + "必须是整数");
        config[key] = number;
    }

    // pit 逃课路径的默认门槛独立于三字段：0.9 × 49640 × 1.43668 ≈ 64171。
    if (config.value("pitMode", false) && !config.contains("minArea"))
        config["minArea"] = 64171;

    Config cfg = configFromJson(config);
    cfg.top = cfg.top > 0 ? cfg.top : 100;

    // ---- 单点诊断：--score-point <x> <z> [--sweep-afk-y a b] [--repeat n] ----
    if (pointSweep && !pointMode)
        throw std::invalid_argument("--sweep-afk-y需要--score-point");
    if (pointMode) {
        Control control;
        const char *source = cfg.optimizePlayerY ? "platform" :
                             (cfg.playerYDerived ? "auto" : "fixed");
        if (pointSweep) {
            const auto points = sweepPointPlayerY(cfg, pointX, pointZ,
                                                 sweepFrom, sweepTo, control);
            Json rows = Json::array();
            int bestIndex = -1;
            for (size_t i = 0; i < points.size(); ++i) {
                rows.push_back(Json{{"playerY", points[i].playerY},
                                    {"score", points[i].score},
                                    {"riverVolume", points[i].riverVolume},
                                    {"caveVolume", points[i].caveVolume}});
                if (bestIndex < 0 || points[i].score > points[size_t(bestIndex)].score)
                    bestIndex = int(i);
            }
            emit(Json{{"type", "point-sweep"}, {"x", pointX}, {"z", pointZ},
                      {"from", sweepFrom}, {"to", sweepTo}, {"points", rows},
                      {"bestPlayerY", bestIndex >= 0 ? points[size_t(bestIndex)].playerY : 0},
                      {"bestScore", bestIndex >= 0 ? points[size_t(bestIndex)].score : 0.0}});
            return 0;
        }
        const int repeat = pointRepeat > 0 ? pointRepeat : 1;
        PointScore point;
        const auto started = Clock::now();
        for (int i = 0; i < repeat; ++i)
            point = scorePoint(cfg, pointX, pointZ, control);
        const int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - started).count();
        emit(Json{{"type", "point-score"}, {"x", point.x}, {"z", point.z},
                  {"playerY", point.playerY}, {"playerYSource", source},
                  {"score", point.score}, {"riverArea", point.riverArea},
                  {"riverVolume", point.riverVolume}, {"caveVolume", point.caveVolume},
                  {"repeat", repeat}, {"avgMs", double(elapsedMs) / double(repeat)}});
        return 0;
    }

    Control control;
    const auto startedAt = Clock::now();
    auto progress = [&](const Statistics &stats) {
        if (!jsonl) return;
        emit(Json{{"type", "progress"}, {"phase", stats.phase}, {"unit", "tiles"},
                  {"current", stats.phase == "refine" ? stats.refinedCandidates : stats.scannedChunks},
                  {"total", stats.phase == "refine" ? stats.refineTotal : stats.totalChunks}});
    };
    SearchResult result = search(cfg, control, progress);
    result.statistics.wallMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - startedAt).count();

    // 结果文件名带微秒时间戳，避免同一目录下的多次运行互相覆盖
    const auto stamp = std::to_string(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    const std::string stem = "results-" + std::string(kFinder) + "-" + stamp;
    const auto base = std::filesystem::path(utf8Path(output)) / stem;
    const Json paths = exportResults(base, result);
    result.statistics.wallMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-startedAt).count();

    Json out = resultJson(result);
    out["exports"] = paths;
    emit(out);
    std::cerr << "结果已写入 " << output << '\n';
    return 0;
}

}  // namespace
}  // namespace riverfinder_native

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string text(bytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, text.data(), bytes, nullptr, nullptr);
        text.pop_back();
        args.push_back(std::move(text));
    }
#else
int main(int argc, char **argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
#endif
    try {
        return riverfinder_native::run(args);
    } catch (const std::exception &error) {
        std::cerr << "river-finder: " << error.what() << '\n';
        return 2;
    }
}
