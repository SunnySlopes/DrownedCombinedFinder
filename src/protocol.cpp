/* JSONL沿用v3信封与单任务生命周期；农场参数由本引擎0.3.0定义，桌面另行适配。
 * 粗筛和精排分别报告阶段进度，计数单位不能冒充全世界区块数。
 */
#include "protocol.hpp"
#include "range_units.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace riverfinder_native {
namespace {

// 取消与写盘失败要映射成不同的 failed.code，用独立异常类型区分。
struct Cancelled {};
struct IoError {};

double wallMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// 阶段百分比按契约保留一位小数；后续 70ms 节流会丢弃中间帧。
double percent(uint64_t done, uint64_t total) {
    if (!total)
        return 0;
    return std::round(1000.0 * double(done) / double(total)) / 10;
}

// 契约 v3 之外的字段一律拒绝，避免前端字段拼写错误被静默忽略。
void requireKnownFields(const Json &value, std::initializer_list<const char *> allowed, const char *owner) {
    if (!value.is_object())
        throw std::invalid_argument(std::string(owner) + "必须是JSON对象");
    for (auto it = value.begin(); it != value.end(); ++it) {
        bool known = false;
        for (const char *key : allowed)
            known = known || it.key() == key;
        if (!known)
            throw std::invalid_argument(std::string(owner) + "包含未知字段: " + it.key());
    }
}

// 契约里的必填键缺失时给中文报错，不把 nlohmann 的英文异常文本直接抛给用户。
const Json &requirePresent(const Json &owner, const char *key) {
    if (!owner.contains(key))
        throw std::invalid_argument(std::string("缺少必填字段: ") + key);
    return owner.at(key);
}

// 桌面契约有比 CLI 更窄的上限；必须在 configFromJson 之前检查，
// 因为 validate() 会把部分越界值报成另一条语义模糊的错误。
int64_t requireInteger(const Json &owner, const char *key, int64_t low, int64_t high) {
    const Json &field = requirePresent(owner, key);
    if (!field.is_number_integer())
        throw std::invalid_argument(std::string(key) + "必须为整数");
    if (field.is_number_unsigned() && field.get<uint64_t>() > uint64_t(high))
        throw std::invalid_argument(std::string(key) + "超出允许范围");
    int64_t number = field.get<int64_t>();
    if (number < low || number > high)
        throw std::invalid_argument(std::string(key) + "超出允许范围");
    return number;
}

/* 契约 SearchResult。字段名与 desktop/contracts/finder-contract.ts 一致，
 * 不沿用引擎内部的 config/statistics 结构——那是 CLI 的载荷，不是桌面载荷。 */
Json desktopResult(const SearchResult &result, const std::string &jobId, double pausedMs, bool fullWorld) {
    auto value = resultJson(result);
    const auto &cfg = result.config;
    Json params = configJson(cfg);
    for (const char *key : {"seed","version","centerX","centerZ","radius","shape","fullWorld","threads","top"})
        params.erase(key);
    params["mcVersion"] = cfg.version;
    Json bounds = {{"centerX",cfg.centerX},{"centerZ",cfg.centerZ},{"radiusBlocks",cfg.radius},
                   {"shape",cfg.shape},{"fullWorld",fullWorld}};
    if (cfg.rectMode)
        bounds["rect"] = {{"minX",cfg.minX},{"maxX",cfg.maxX},
                          {"minZ",cfg.minZ},{"maxZ",cfg.maxZ}};
    value.update(Json{{"jobId",jobId},{"seed",cfg.seed},{"finderGeneration",cfg.version},
        {"bounds",bounds},
        {"params",params},{"threads",cfg.threads},{"device","cpu"},{"requestedLimit",cfg.top},
        {"returnedCount",result.hits.size()},{"elapsedMs",result.statistics.activeMs},
        {"timing",{{"searchMs",result.statistics.activeMs},{"pausedMs",pausedMs},
                   {"resumedMs",0},{"totalMs",0.0}}},
        {"stats",statisticsJson(result.statistics)},{"output",Json::object()}});
    return value;
}

Json progressFrame(const char *phase, const char *unit, uint64_t current, uint64_t total, double elapsed,
                   uint64_t found, const char *message, bool withOverall) {
    Json frame = {{"phase", phase},
        {"current", current},
        {"total", total},
        {"unit", unit},
        {"percentage", percent(current, total)},
        {"speedPerSecond", 0},
        {"elapsedMs", elapsed},
        {"etaMs", 0},
        {"foundCandidates", found},
        {"state", "running"},
        {"message", message}};
    // 各阶段百分比独立，粗筛结束不能冒充整体搜索完成。
    if (withOverall)
        frame["overallPercentage"] = percent(current, total);
    return frame;
}

std::string writeResultFile(const std::filesystem::path &path, const std::string &content) {
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(content.data(), std::streamsize(content.size())) || !out.flush())
        throw IoError{};
    return pathUtf8(std::filesystem::absolute(path));
}

}  // namespace

DesktopRequest parseDesktopRequest(const Json &config) {
    requireKnownFields(config,
        {"protocolVersion", "finder", "seed", "bounds", "threads", "resultLimit", "params", "deferExport"},
        "config");
    if (requirePresent(config, "protocolVersion") != kProtocolVersion)
        throw std::invalid_argument("桌面协议版本必须为3");
    if (requirePresent(config, "finder") != kFinder)
        throw std::invalid_argument("finder必须为river");
    const Json &bounds = requirePresent(config, "bounds");
    requireKnownFields(bounds, {"centerX", "centerZ", "radiusBlocks", "radius", "shape", "fullWorld",
                                "rect", "minX", "maxX", "minZ", "maxZ"}, "bounds");
    const Json &params = requirePresent(config, "params");
    requireKnownFields(params, {"mcVersion","minArea","includeCaves","coverage","innerRadius",
        "simulationDistanceChunks","playerY","optimizePlayerY","coarseY",
        "minDistance","candidateBudget","yMin","yMax","approximateGate",
        "minRiverArea","minCaveArea","caveConversion",
        "pureRiverCache","pureRiverMaskStep","pitMode","bench"}, "params");

    DesktopRequest request;
    request.deferExport = config.value("deferExport", false);
    request.fullWorld = bounds.value("fullWorld", false);
    requireInteger(config, "threads", 1, 32);
    requireInteger(config, "resultLimit", 1, 200);

    Json plain = Json::object();
    plain["seed"] = requirePresent(config, "seed");
    // C5：bounds 支持 rect 变体（{minX,maxX,minZ,maxZ}）；与中心/半径互斥。
    if (bounds.contains("rect")) {
        if (request.fullWorld)
            throw std::invalid_argument("rect不能与fullWorld同时使用");
        const Json &rect = bounds.at("rect");
        if (!rect.is_object())
            throw std::invalid_argument("bounds.rect必须是对象");
        requireKnownFields(rect, {"minX", "maxX", "minZ", "maxZ"}, "bounds.rect");
        plain["rectMode"] = true;
        plain["minX"] = requireInteger(rect, "minX", kWorldMin, kWorldMax);
        plain["maxX"] = requireInteger(rect, "maxX", kWorldMin, kWorldMax);
        plain["minZ"] = requireInteger(rect, "minZ", kWorldMin, kWorldMax);
        plain["maxZ"] = requireInteger(rect, "maxZ", kWorldMin, kWorldMax);
        plain["centerX"] = (plain["minX"].get<int>() + plain["maxX"].get<int>()) / 2;
        plain["centerZ"] = (plain["minZ"].get<int>() + plain["maxZ"].get<int>()) / 2;
        plain["radius"] = std::max(plain["maxX"].get<int>() - plain["minX"].get<int>(),
                                   plain["maxZ"].get<int>() - plain["minZ"].get<int>()) / 2 + 1;
        plain["shape"] = "square";
    } else {
        plain["centerX"] = requirePresent(bounds, "centerX");
        plain["centerZ"] = requirePresent(bounds, "centerZ");
        // bounds.radius 支持 {value,unit} 对象，但只能与 radiusBlocks 二选一。
        plain["radius"] = range_units::field(bounds, "radius", "radiusBlocks", 100000);
        plain["shape"] = requirePresent(bounds, "shape");
    }
    /* fullWorld 只是简写：展开成世界边界的方形范围后再交给引擎，
     * 结果回显展开后的实际 bounds（与神殿同一写法）。 */
    if (request.fullWorld) {
        plain["centerX"] = 0;
        plain["centerZ"] = 0;
        plain["radius"] = 30000000;
        plain["shape"] = "square";
    }
    plain["fullWorld"] = request.fullWorld;
    plain["threads"] = config.at("threads");
    plain["top"] = config.at("resultLimit");
    plain["version"] = requirePresent(params, "mcVersion");
    for (auto it = params.begin(); it != params.end(); ++it)
        if (it.key() != "mcVersion")
            plain[it.key()] = it.value();
    // configFromJson 继续负责未知字段、整数类型、文本种子与引擎范围校验。
    request.config = configFromJson(plain);
    return request;
}

int desktop_protocol() {
    std::mutex outputMutex, frameMutex;
    std::thread worker;
    std::shared_ptr<Control> control;
    std::atomic<bool> active{false};
    std::string jobId;
    uint64_t sequence = 0;
    bool started = false;
    Json current;
    auto write = [](const Json &event) { std::cout << event.dump() << std::endl; };
    auto emit = [&](const Json &event) {
        std::lock_guard lock(outputMutex);
        write(event);
    };
    for (std::string line; std::getline(std::cin, line);) {
        if (line.empty())
            continue;
        try {
            Json command;
            try {
                command = Json::parse(line);
            } catch (const std::exception &) {
                emit({{"type", "failed"}, {"jobId", jobId}, {"code", "invalid-config"},
                    {"message", "命令必须是合法JSON"}});
                continue;
            }

            const std::string type = command.value("type", std::string{});
            if (type == "start") {
                // 一个进程只跑一个任务：桌面端每次搜索都新起进程。
                // 拒绝码用 invalid-command（契约的 EngineErrorCode 里没有 invalid-state）。
                if (started) {
                    emit({{"type", "failed"}, {"jobId", jobId}, {"code", "invalid-command"},
                        {"message", "本进程只接受一次start"}});
                    continue;
                }
                started = true;
                // jobId 由引擎生成；命令里的 jobId 不可信，不能拿来当任务 id。
                jobId = std::string(kFinder) + "-" + std::to_string(++sequence);
                const std::string id = jobId;
                const std::string requestId = command.value("requestId", std::string{});
                // 契约要求先回声 accepted 再校验配置：配置不合法时前端也能把 failed 关联回来。
                emit({{"type", "accepted"}, {"requestId", requestId}, {"jobId", id}});
                DesktopRequest request;
                try {
                    if (!command.contains("config"))
                        throw std::invalid_argument("start命令缺少config");
                    request = parseDesktopRequest(command.at("config"));
                } catch (const std::exception &error) {
                    emit({{"type", "failed"}, {"jobId", id}, {"code", "invalid-config"},
                        {"message", error.what()}});
                    continue;
                }
                /* startedAt 必须在创建 Control **之前**取。
                 * Control 的计时原点就是它自己的构造时刻，searchMs 从那里算起；
                 * 若这里晚于它取原点，短任务会出现 totalMs < searchMs 的倒挂
                 * （实测 radius=8000 时偶发差 1ms），违反「totalMs 包住 searchMs」。 */
                const auto startedAt = Clock::now();
                auto token = std::make_shared<Control>();
                control = token;
                active = true;
                worker = std::thread([&, request, token, id, startedAt] {
                    auto last = startedAt - std::chrono::seconds(1);
                    uint64_t lastScanned = 0;
                    auto lastSample = startedAt;
                    // 总体百分比单调不回退：阶段切换后剩余时间重估会让瞬时值抖动。
                    double lastOverall = 0.0;
                    try {
                        // 进度帧统一在这里节流；暂停/取消状态帧由命令处理发送。
                        auto publish = [&](const Json &frame, bool force) {
                            {
                                std::lock_guard lock(frameMutex);
                                current = frame;
                            }
                            const auto now = Clock::now();
                            if (!force && now - last < std::chrono::milliseconds(70))
                                return;
                            std::lock_guard lock(outputMutex);
                            // 等锁期间可能已发出暂停/取消帧，必须重新确认状态。
                            if (token->paused || token->cancelled)
                                return;
                            write(Json{{"type", "progress"}, {"jobId", id}, {"progress", frame}});
                            last = now;
                        };
                        publish(progressFrame("prepare", "阶段", 0, 1, 0, 0, "准备搜索", false), true);
                        auto progress = [&](const Statistics &stats) {
                            const auto now = Clock::now();
                            const double span =
                                std::chrono::duration<double, std::milli>(now - lastSample).count();
                            const uint64_t current = stats.phase == "refine" ?
                                stats.refinedCandidates : stats.scannedChunks;
                            const uint64_t advance = current >= lastScanned ? current-lastScanned : 0;
                            // 粗筛吞吐按相邻进度帧的区块增量计算，暂停期间自然归零。
                            const int64_t speed = span > 0 ? int64_t(double(advance) * 1000.0 / span) : 0;
                            lastScanned = current;
                            lastSample = now;
                            bool refine = stats.phase == "refine";
                            const uint64_t shown = refine ? stats.refinedCandidates : stats.scannedChunks;
                            const uint64_t shown_total = refine ? stats.refineTotal : stats.totalChunks;
                            Json frame = progressFrame(stats.phase.c_str(), refine ? "候选" : "分块",
                                shown, shown_total,
                                double(token->activeMilliseconds()), stats.candidates,
                                "低Y群系搜索与几何精排", false);
                            frame["speedPerSecond"] = speed;
                            // 剩余按本轮实测吞吐外推；吞吐为 0（刚启动/暂停）时如实报未知。
                            const int64_t eta = speed > 0 && shown_total > shown ?
                                int64_t(double(shown_total - shown) * 1000.0 / double(speed)) : 0;
                            frame["etaMs"] = eta;
                            // 总体按「已用 / (已用 + 剩余)」外推，末段留 0.1% 给收尾，避免假完成。
                            const double used = double(token->activeMilliseconds());
                            double overall = eta > 0 ? 100.0 * used / (used + double(eta)) : 100.0;
                            if (overall < lastOverall)
                                overall = lastOverall;
                            if (overall > 99.9)
                                overall = 99.9;
                            lastOverall = overall;
                            frame["overallPercentage"] = overall;
                            // 已扫描区块只在粗筛阶段增长，精排阶段保持不变。
                            frame["scannedChunks"] = int64_t(stats.scannedChunks);
                            // 阶段末帧必须落地：否则最后不足 70ms 的进度会被节流丢掉，界面停在半路。
                            publish(frame, shown_total > 0 && shown >= shown_total);
                        };
                        SearchResult found = search(request.config, *token, progress);
                        if (token->cancelled)
                            throw Cancelled{};
                        // 小任务也补发精排终态，阶段内100%不代表任务已经完成。
                        Json finalFrame = progressFrame("refine", "候选", found.statistics.refinedCandidates,
                            found.statistics.refineTotal, double(found.statistics.activeMs),
                            found.statistics.candidates, "低Y群系搜索与几何精排", false);
                        finalFrame["scannedChunks"] = int64_t(found.statistics.scannedChunks);
                        publish(finalFrame, true);
                        Json result =
                            desktopResult(found, id, double(token->pausedMilliseconds()), request.fullWorld);
                        // 按最终榜单分批下发候选；批间隔让界面能看到结果流入。
                        const Json &candidates = result["candidates"];
                        for (size_t first = 0; first < candidates.size(); first += 100) {
                            if (token->cancelled)
                                throw Cancelled{};
                            if (first)
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            Json batch = Json::array();
                            for (size_t i = first; i < std::min(first + 100, candidates.size()); ++i)
                                batch.push_back(candidates[i]);
                            emit({{"type", "candidates"}, {"jobId", id}, {"batch", batch},
                                {"totalFound", candidates.size()}});
                        }
                        // 导出与 done 只在暂停结束后发送；取消后不再补发正常完成。
                        std::unique_lock lock(outputMutex);
                        while (token->paused) {
                            lock.unlock();
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            if (token->cancelled)
                                throw Cancelled{};
                            lock.lock();
                        }
                        if (token->cancelled)
                            throw Cancelled{};
                        Json exports = Json::object();
                        // 正式 CSV/TXT 内容与 CLI 共用同一份生成函数，保证榜单一致。
                        const ExportText files = exportText(found);
                        result["timing"]["totalMs"] = wallMs(startedAt);
                        if (request.deferExport) {
                            exports = {{"csv", files.csv}, {"txt", files.txt},
                                {"json", result.dump(2) + "\n"}};
                        } else {
                            const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();
                            const auto prefix =
                                utf8Path("results") / utf8Path("results-river-" + std::to_string(stamp));
                            auto named = [&](const char *suffix) {
                                auto path = prefix;
                                path += suffix;
                                return path;
                            };
                            const std::string csvPath = writeResultFile(named(".csv"), files.csv);
                            const std::string txtPath = writeResultFile(named(".txt"), files.txt);
                            const std::string jsonPath =
                                pathUtf8(std::filesystem::absolute(named(".json")));
                            // 先回填绝对路径再落盘 JSON，保证文件内容与 done.result 完全一致。
                            result["output"] = {{"csvPath", csvPath}, {"txtPath", txtPath},
                                {"jsonPath", jsonPath}};
                            writeResultFile(named(".json"), result.dump(2) + "\n");
                        }
                        Json report = progressFrame("report", "文件", 1, 1,
                            double(found.statistics.activeMs), found.statistics.candidates,
                            "整理搜索结果", true);
                        {
                            std::lock_guard frameLock(frameMutex);
                            current = report;
                        }
                        write(Json{{"type", "progress"}, {"jobId", id}, {"progress", report}});
                        Json finished = report;
                        finished["phase"] = "done";
                        finished["state"] = "done";
                        finished["percentage"] = 100;
                        finished["overallPercentage"] = 100;
                        finished["message"] = "完成";
                        write(Json{{"type", "progress"}, {"jobId", id}, {"progress", finished}});
                        write(Json{{"type", "done"}, {"jobId", id}, {"result", result},
                            {"exports", exports}});
                        active = false;
                    } catch (const Cancelled &) {
                        active = false;
                        emit({{"type", "failed"}, {"jobId", id}, {"code", "cancelled"},
                            {"message", "搜索已取消"}});
                    } catch (const IoError &) {
                        active = false;
                        emit({{"type", "failed"}, {"jobId", id}, {"code", "io-error"},
                            {"message", "结果文件写入失败"}});
                    } catch (const std::bad_alloc &) {
                        active = false;
                        emit({{"type", "failed"}, {"jobId", id}, {"code", "out-of-memory"},
                            {"message", "内存不足，请减少线程或搜索范围"}});
                    } catch (const std::exception &error) {
                        active = false;
                        emit({{"type", "failed"}, {"jobId", id}, {"code", "engine-crashed"},
                            {"message", error.what()}});
                    }
                });
            } else if (type == "pause" || type == "resume" || type == "cancel") {
                if (!control || !active) {
                    std::cerr << "忽略控制命令：当前没有运行中的任务" << std::endl;
                    continue;
                }
                // 迟到的旧任务命令不能作用于新任务；jobId 不匹配就忽略。
                if (command.value("jobId", std::string{}) != jobId) {
                    std::cerr << "忽略控制命令：jobId不匹配" << std::endl;
                    continue;
                }
                std::lock_guard lock(outputMutex);
                // 等锁期间任务可能已经发出 done/failed，必须重新确认。
                if (!active)
                    continue;
                if (type == "pause")
                    control->pause();
                else if (type == "resume")
                    control->resume();
                else
                    control->cancel();
                Json frame;
                {
                    std::lock_guard frameLock(frameMutex);
                    frame = current;
                }
                if (!frame.is_object())
                    frame = progressFrame("prepare", "阶段", 0, 1, 0, 0, "准备搜索", false);
                frame["state"] = type == "pause" ? "paused" : type == "resume" ? "running" : "cancelling";
                {
                    std::lock_guard frameLock(frameMutex);
                    current = frame;
                }
                write(Json{{"type", "progress"}, {"jobId", jobId}, {"progress", frame}});
            } else if (type == "export") {
                std::cerr << "结果已随done事件返回，无需再次导出" << std::endl;
            } else {
                emit({{"type", "failed"}, {"jobId", jobId}, {"code", "invalid-command"},
                    {"message", "未知命令: " + type}});
            }
        } catch (const std::exception &error) {
            emit({{"type", "failed"}, {"jobId", jobId}, {"code", "invalid-config"},
                {"message", error.what()}});
        }
    }

    /* stdin 关闭后**不能**立刻取消正在跑的任务。
     *
     * 这里踩过一个坑：早期写成「stdin 一关闭就 control.cancel()」，导致
     * 单行 start 输入（stdin 立即 EOF）的任务刚跑完就被判成 cancelled，
     * done 帧里的 status 永远是 cancelled，桌面端会把它当失败。
     *
     * 正确语义：
     *   · 桌面端在任务结束前一直持有 stdin，不会提前 EOF；
     *   · 如果 stdin 提前 EOF（CLI 管道用法），应等任务**自然跑完**，
     *     而不是替用户取消——取消是显式命令，不能由 EOF 隐式触发。
     * 只有已经收到 cancel 命令时，才不需要额外动作。 */
    if (worker.joinable())
        worker.join();
    return 0;
}

}  // namespace riverfinder_native
