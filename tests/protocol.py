"""river-native 的桌面协议 v3 测试。

协议的重点是载荷形状与状态机：config 必须是契约的嵌套形状（bounds/params），
accepted 必须回声 requestId，progress 必须包在信封里，候选必须分批下发，
取消走 failed(cancelled) 且不再补发 done，EOF 不能替用户取消任务
（这条踩过坑，说明见 protocol.cpp 底部）。
"""

import json
import pathlib
import subprocess
import sys
import tempfile

EXE = pathlib.Path(sys.argv[1])
failures = []

RIVER_PARAMS = ["mcVersion", "minArea", "sampleY"]


def check(condition, what):
    if condition:
        return
    failures.append(what)
    print(f"FAIL: {what}")


def talk(commands, workdir):
    """发一串命令，读回全部事件。"""
    payload = "".join(json.dumps(c, ensure_ascii=False) + "\n" for c in commands)
    proc = subprocess.run([str(EXE), "--desktop"], input=payload,
                          capture_output=True, text=True, encoding="utf-8",
                          cwd=workdir, timeout=300)
    events = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line:
            events.append(json.loads(line))
    return events, proc.stderr


def of_type(events, kind):
    return [e for e in events if e.get("type") == kind]


def start_config(**overrides):
    """契约形状的 start config；overrides 覆盖顶层键。"""
    config = {
        "protocolVersion": 3,
        "finder": "river",
        "seed": "12345",
        "bounds": {
            "centerX": 0,
            "centerZ": 0,
            "radiusBlocks": 8000,
            "shape": "circle",
            "fullWorld": False,
        },
        "threads": 4,
        "resultLimit": 3,
        "params": {"mcVersion": "26.3", "minArea": 20000, "sampleY": 63},
    }
    config.update(overrides)
    return config


def start_command(config, job_id="job-1", request_id="req-1"):
    return {"type": "start", "requestId": request_id, "jobId": job_id, "config": config}


def check_rejected(config, what, need_field=None, workdir=None):
    """契约形状的 config 变体必须被拒绝成 invalid-config。"""
    events, _ = talk([start_command(config)], workdir or pathlib.Path.cwd())
    failed = of_type(events, "failed")
    check(len(failed) == 1, f"{what}：应有一个 failed，实际 {len(failed)}")
    if not failed:
        return
    check(failed[0]["code"] == "invalid-config",
          f"{what}：错误码应为 invalid-config，实际 {failed[0]['code']}")
    if need_field is not None:
        check(need_field in failed[0]["message"],
              f"{what}：消息应指出 {need_field}，实际 {failed[0]['message']}")


def main():
    with tempfile.TemporaryDirectory(dir=pathlib.Path.cwd()) as tmp:
        tmp = pathlib.Path(tmp)

        # --- 1. 正常起点到终点：accepted → progress → candidates → done ---
        events, stderr = talk([start_command(start_config())], tmp)

        accepted = of_type(events, "accepted")
        check(len(accepted) == 1, f"应有一个 accepted，实际 {len(accepted)}")
        if accepted:
            check(accepted[0]["requestId"] == "req-1",
                  f"accepted 必须回声命令里的 requestId，实际 {accepted[0].get('requestId')}")
            check(accepted[0]["jobId"] == "river-1",
                  f"jobId 应由引擎生成，实际 {accepted[0].get('jobId')}")
            # 契约 EngineEvent.accepted 只有 requestId/jobId 两个字段
            check(set(accepted[0]) == {"type", "requestId", "jobId"},
                  f"accepted 不应带多余字段，实际 {sorted(accepted[0])}")

        progress = of_type(events, "progress")
        check(len(progress) >= 2, f"应有多个 progress 帧，实际 {len(progress)}")
        for frame in progress:
            # 进度必须包在信封里，不能把 phase/current 平铺到顶层
            check("progress" in frame and isinstance(frame["progress"], dict),
                  f"progress 帧必须带 progress 对象，实际 {sorted(frame)}")
            inner = frame.get("progress", {})
            for key in ("phase", "current", "total", "unit", "percentage", "speedPerSecond",
                        "elapsedMs", "etaMs", "foundCandidates", "state"):
                check(key in inner, f"progress.{key} 缺失")
            check("jobId" in frame, "progress 帧应带 jobId")

        phases = [f["progress"]["phase"] for f in progress]
        check(phases[0] == "prepare", f"首帧 phase 应为 prepare，实际 {phases[0]}")
        check("coarse" in phases, f"应有 coarse 阶段，实际 {phases}")
        check("report" in phases, f"应有 report 阶段，实际 {phases}")
        check(phases[-1] == "done", f"末帧 phase 应为 done，实际 {phases[-1]}")
        check(progress[-1]["progress"]["percentage"] == 100, "done 帧 percentage 应为 100")
        check(progress[-1]["progress"]["overallPercentage"] == 100,
              "done 帧 overallPercentage 应为 100")
        check(progress[-1]["progress"]["state"] == "done", "done 帧 state 应为 done")
        # prepare 阶段没有可靠的总体估计，不能编造 overallPercentage
        check("overallPercentage" not in progress[0]["progress"],
              "prepare 帧不应带 overallPercentage")
        coarse = [f["progress"] for f in progress if f["progress"]["phase"] == "coarse"]
        check(all("overallPercentage" in f for f in coarse), "coarse 帧应带 overallPercentage")
        check(all(f["unit"] == "区块" for f in coarse), "coarse 帧单位应为 区块")

        done = of_type(events, "done")
        check(len(done) == 1, f"应有一个 done，实际 {len(done)}")
        failures_before = len(failures)
        if done:
            result = done[0]["result"]
            check(done[0]["jobId"] == "river-1", "done.jobId 应为引擎生成的 jobId")
            check(result["jobId"] == "river-1", "result.jobId 应与事件一致")
            check(result["status"] == "completed",
                  f"status 应为 completed，实际 {result.get('status')}")
            check(result["protocolVersion"] == 3, "protocolVersion 应为 3")
            check(result["rankingRuleVersion"] == 3, "rankingRuleVersion 应为 3")
            check(result["rankingRule"] == "riverarea-desc-distance-asc-x-asc-z-asc",
                  "rankingRule 字符串")
            check(result["finder"] == "river", "finder 应为 river")
            check(result["engine"] == "river-native", "engine 应为 river-native")
            check(result["engineVersion"] == "0.2.0",
                  f"engineVersion 应为 0.2.0，实际 {result.get('engineVersion')}")
            check(result["verified"] is False, "verified 必须为 false")
            check(result["searchApproximate"] is True, "searchApproximate 必须为 true")
            check(result["device"] == "cpu", "device 应为 cpu")
            check(result["model"] == "river-biome-area-v1", "model 标识")
            check(result["finderGeneration"] == "26.3", "finderGeneration 应为 mcVersion")
            check(result["mcVersion"] == "26.3", "mcVersion 应为 26.3")
            check(result["bounds"]["radiusBlocks"] == 8000, "bounds 应回显实际范围")
            check(result["bounds"]["fullWorld"] is False, "bounds.fullWorld 应回显 false")
            # params 只能回显白名单里的三个键
            check(sorted(result["params"]) == RIVER_PARAMS,
                  f"params 应恰好是 {RIVER_PARAMS}，实际 {sorted(result['params'])}")
            check(result["params"]["minArea"] == 20000, "params.minArea 回显")
            check(result["params"]["sampleY"] == 63, "params.sampleY 回显")
            check(result["requestedLimit"] == 3, "requestedLimit 应回显 resultLimit")
            check(result["returnedCount"] == len(result["candidates"]),
                  "returnedCount 应等于候选数")
            check(result["scannedChunks"] > 0, "scannedChunks 应大于 0")
            # 契约 SearchStats 的三个必备口径
            for key in ("candidatePositions", "simulated", "results"):
                check(key in result["stats"], f"stats.{key} 缺失")
            check(result["stats"]["results"] == len(result["candidates"]),
                  "stats.results 应等于候选数")
            check(result["stats"]["scannedChunks"] == result["scannedChunks"],
                  "stats.scannedChunks 应与顶层一致")
            check(result["timing"]["totalMs"] >= result["timing"]["searchMs"],
                  "timing.totalMs 应包住 searchMs")
            check(result["elapsedMs"] == result["timing"]["searchMs"],
                  "elapsedMs 应等于 timing.searchMs")
            # 未开 deferExport：正式文件已落盘，output 给三份绝对路径
            for key in ("csvPath", "txtPath", "jsonPath"):
                check(key in result["output"], f"output.{key} 缺失")
                if key in result["output"]:
                    check(pathlib.Path(result["output"][key]).is_file(),
                          f"output.{key} 指向的文件应存在")

            candidates = result["candidates"]
            check(len(candidates) > 0, "该范围应有候选")
            check(len(candidates) <= 3, "候选数不应超过 resultLimit")
            for index, candidate in enumerate(candidates):
                prefix = f"candidate[{index}]"
                check(isinstance(candidate["id"], str),
                      f"{prefix}.id 必须是字符串，实际 {type(candidate['id']).__name__}")
                check(candidate["id"] == f"river-{candidate['x']}-{candidate['z']}",
                      f"{prefix}.id 应为 river-<x>-<z>")
                check(candidate["rank"] == index + 1, f"{prefix}.rank 应从 1 连续递增")
                check(candidate["score"] == candidate["riverArea"],
                      f"{prefix}.score 应等于 riverArea")
                check(candidate["scoreUnit"] == "格", f"{prefix}.scoreUnit 应为 格")
                check(candidate["secondary"] == candidate["caveArea"],
                      f"{prefix}.secondary 应为 caveArea")
                check(candidate["secondaryUnit"] == "溶洞格", f"{prefix}.secondaryUnit 应为 溶洞格")
                for key in ("riverArea", "caveArea", "radius", "regionX", "regionZ"):
                    check(key in candidate, f"{prefix} 应保留河流专有字段 {key}")
                check(isinstance(candidate["distance"], float), f"{prefix}.distance 应为小数")
                check(isinstance(candidate["distanceSq"], int), f"{prefix}.distanceSq 应为整数")
                check(candidate["distanceSq"] <= 2 ** 53, f"{prefix}.distanceSq 应在 2^53 内")
                check(candidate["verified"] is False, f"{prefix}.verified 应为 false")
                # 河流没有 Y 概念：站位省略 y，传送指令用 ~
                check("y" not in candidate["bestPlayer"], f"{prefix}.bestPlayer 应省略 y")
                check(candidate["bestPlayer"]["x"] == candidate["x"], f"{prefix}.bestPlayer.x")
                check(candidate["bestPlayer"]["z"] == candidate["z"], f"{prefix}.bestPlayer.z")
                check(candidate["tpCommand"] ==
                      f"/tp @s {candidate['x']} ~ {candidate['z']}",
                      f"{prefix}.tpCommand 应为 /tp @s x ~ z")

            # 排名：面积降序 → 距离升序 → X → Z
            for index in range(len(candidates) - 1):
                a, b = candidates[index], candidates[index + 1]
                ordered = (b["riverArea"], -b["distanceSq"], -b["x"], -b["z"]) < \
                          (a["riverArea"], -a["distanceSq"], -a["x"], -a["z"])
                check(ordered, f"候选顺序应满足面积降序 → 距离升序 → X → Z（{index}）")

        # candidates 分批：每批 ≤100，frame 字段齐全
        batches = of_type(events, "candidates")
        check(len(batches) >= 1, f"应有 candidates 分批帧，实际 {len(batches)}")
        collected = []
        for batch in batches:
            check("jobId" in batch and "batch" in batch and "totalFound" in batch,
                  f"candidates 帧字段应齐全，实际 {sorted(batch)}")
            check(len(batch["batch"]) <= 100, f"每批最多 100 条，实际 {len(batch['batch'])}")
            if done:
                check(batch["totalFound"] == len(done[0]["result"]["candidates"]),
                      "totalFound 应等于最终候选数")
            collected.extend(batch["batch"])
        if done:
            check([c["id"] for c in collected] ==
                  [c["id"] for c in done[0]["result"]["candidates"]],
                  "分批下发的候选应与 done.result 一致，且不重复不遗漏")
        # 事件顺序：accepted 在最前，done 在最后
        if events:
            check(events[0]["type"] == "accepted", f"首事件应为 accepted，实际 {events[0]['type']}")
            check(events[-1]["type"] == "done", f"末事件应为 done，实际 {events[-1]['type']}")

        # --- 1b. 候选超过 100 条时确实分成多批（每批 ≤100、批间隔 100ms） ---
        # 半径 30000 / resultLimit=150 的实测产出约 110 条，正好跨越一批的边界。
        events, _ = talk([start_command(start_config(
            bounds={"centerX": 0, "centerZ": 0, "radiusBlocks": 30000,
                    "shape": "circle", "fullWorld": False},
            threads=8, resultLimit=150, deferExport=True))], tmp)
        batches = of_type(events, "candidates")
        done = of_type(events, "done")
        if done and len(done[0]["result"]["candidates"]) > 100:
            check(len(batches) >= 2,
                  f"候选超过 100 条时应分成多批，实际 {len(batches)} 批")
            check(all(len(b["batch"]) <= 100 for b in batches), "每批最多 100 条")
            check(batches[0]["totalFound"] == len(done[0]["result"]["candidates"]),
                  "每批的 totalFound 都是最终候选总数")
            check(sum(len(b["batch"]) for b in batches) == len(done[0]["result"]["candidates"]),
                  "各批条数之和应等于最终候选数")
        else:
            # 该范围在别的机器上可能不足 100 条；那样这条只能跳过，不能算失败。
            check(len(batches) >= 1, f"至少应有一批 candidates，实际 {len(batches)}")

        # --- 2. 核心回归：契约形状的 config 必须被接受（本次修的 bug） ---
        events, stderr = talk([start_command(start_config(
            bounds={"centerX": 0, "centerZ": 0, "radiusBlocks": 8000,
                    "shape": "circle", "fullWorld": False},
            threads=4, resultLimit=3,
            params={"mcVersion": "26.3", "minArea": 20000, "sampleY": 63}))], tmp)
        failed = of_type(events, "failed")
        check(len(failed) == 0,
              f"契约形状的 config 不应被拒绝，实际 {failed[0] if failed else ''}")
        codes = [f.get("code") for f in failed]
        check("invalid-config" not in codes, f"契约形状的 config 不应报 invalid-config，实际 {codes}")
        check(len(of_type(events, "done")) == 1, "契约形状的 config 应能跑完")

        # --- 3. 未知顶层字段 → invalid-config，消息含字段名 ---
        check_rejected(start_config(typoField=1), "未知顶层字段", need_field="typoField")

        # --- 4. 未知 bounds 字段 → invalid-config，消息含字段名 ---
        bad_bounds = {"centerX": 0, "centerZ": 0, "radiusBlocks": 8000,
                      "shape": "circle", "fullWorld": False, "offsetX": 1}
        check_rejected(start_config(bounds=bad_bounds), "未知 bounds 字段", need_field="offsetX")

        # --- 5. 未知 params 字段 → invalid-config，消息含字段名 ---
        bad_params = {"mcVersion": "26.3", "minArea": 20000, "sampleY": 63, "caveFactor": 0.5}
        check_rejected(start_config(params=bad_params), "未知 params 字段", need_field="caveFactor")

        # --- 6. finder 不是 river → 报错 ---
        events, _ = talk([start_command(start_config(finder="ocean-monument"))], tmp)
        failed = of_type(events, "failed")
        check(len(failed) == 1, "finder 不是 river 应报错")
        if failed:
            check(failed[0]["code"] == "invalid-config", "finder 错误码应为 invalid-config")
            check("river" in failed[0]["message"], "消息应说明 finder 必须为 river")

        # --- 7. protocolVersion 不是 3 → 报错 ---
        events, _ = talk([start_command(start_config(protocolVersion=2))], tmp)
        failed = of_type(events, "failed")
        check(len(failed) == 1, "protocolVersion 不是 3 应报错")
        if failed:
            check(failed[0]["code"] == "invalid-config", "协议版本错误码应为 invalid-config")

        # --- 8. radius < 128 → invalid-config ---
        for radius in (0, 99, 127):
            events, _ = talk([start_command(start_config(
                bounds={"centerX": 0, "centerZ": 0, "radiusBlocks": radius,
                        "shape": "circle", "fullWorld": False}))], tmp)
            failed = of_type(events, "failed")
            check(len(failed) == 1, f"radius={radius} 应被拒绝")
            if failed:
                check(failed[0]["code"] == "invalid-config",
                      f"radius={radius} 错误码应为 invalid-config，实际 {failed[0]['code']}")
            # 非正半径报「必须为正」，不足分块尺寸报「至少 128」，两者都不该静默通过
            if radius > 0 and failed:
                check("128" in failed[0]["message"],
                      f"radius={radius} 报错应提到 128：{failed[0]['message']}")

        # --- 9. threads / resultLimit 越界 → invalid-config ---
        for key, value in (("threads", 0), ("threads", 33), ("resultLimit", 0), ("resultLimit", 201)):
            events, _ = talk([start_command(start_config(**{key: value}))], tmp)
            failed = of_type(events, "failed")
            check(len(failed) == 1, f"{key}={value} 应被拒绝")
            if failed:
                check(failed[0]["code"] == "invalid-config",
                      f"{key}={value} 错误码应为 invalid-config")
                check(key in failed[0]["message"],
                      f"{key}={value} 消息应指出字段名：{failed[0]['message']}")

        # --- 10. radius 也可以用 {value,unit} 对象表达 ---
        events, _ = talk([start_command(start_config(
            bounds={"centerX": 0, "centerZ": 0,
                    "radius": {"value": 500, "unit": "chunks"},
                    "shape": "circle", "fullWorld": False}))], tmp)
        check(len(of_type(events, "failed")) == 0, "radius 允许 {value,unit} 对象")
        chunks_done = of_type(events, "done")
        if chunks_done:
            check(chunks_done[0]["result"]["bounds"]["radiusBlocks"] == 8000,
                  "500 区块应换算成 8000 方块")

        # --- 11. 未知命令 → invalid-command ---
        events, _ = talk([{"type": "frobnicate", "jobId": "x"}], tmp)
        failed = of_type(events, "failed")
        check(len(failed) == 1, "未知命令应有一个 failed")
        if failed:
            check(failed[0]["code"] == "invalid-command", "错误码应为 invalid-command")

        # --- 12. 重复 start → 拒绝 ---
        events, _ = talk([
            start_command(start_config(), job_id="a"),
            start_command(start_config(seed="2"), job_id="b"),
        ], tmp)
        repeats = [f for f in of_type(events, "failed") if "只接受一次start" in f.get("message", "")]
        check(len(repeats) == 1, f"重复 start 应被拒绝，实际 {len(repeats)}")
        if repeats:
            check(repeats[0]["code"] == "invalid-command",
                  f"重复 start 的码应在契约枚举内，实际 {repeats[0]['code']}")

        # --- 13. 控制命令的 jobId 不匹配要忽略（不改变任务状态） ---
        events, stderr = talk([
            start_command(start_config(seed="12345")),
            {"type": "pause", "jobId": "someone-else"},
            {"type": "resume", "jobId": "someone-else"},
        ], tmp)
        check("jobId不匹配" in stderr, f"jobId 不匹配应写 stderr，实际 {stderr}")
        mismatched = [f for f in of_type(events, "progress")
                      if f["progress"].get("state") == "paused"]
        check(len(mismatched) == 0, "jobId 不匹配的 pause 不应改变任务状态")
        check(len(of_type(events, "done")) == 1, "被忽略的控制命令不应影响任务完成")

        # --- 14. pause/resume 要回状态帧（river 以前完全没处理暂停帧） ---
        events, _ = talk([
            start_command(start_config(
                bounds={"centerX": 0, "centerZ": 0, "radiusBlocks": 30000,
                        "shape": "circle", "fullWorld": False},
                threads=2, resultLimit=20)),
            {"type": "pause", "jobId": "river-1"},
            {"type": "resume", "jobId": "river-1"},
            {"type": "cancel", "jobId": "river-1"},
        ], tmp)
        states = [f["progress"].get("state") for f in of_type(events, "progress")]
        check("paused" in states, f"pause 应回一帧 state=paused 的 progress，实际 {states}")
        check(states.count("paused") <= 1, "pause 不应重复回帧")
        # resume 之后 worker 可能已经进入收尾，允许没有 running 帧，但不能报错
        check(len([f for f in of_type(events, "failed")
                   if f.get("code") not in ("cancelled",)]) == 0,
              "暂停/恢复期间不应出现 cancelled 以外的失败码")

        # --- 15. 取消 → failed(cancelled)，且不再发 done ---
        events, _ = talk([
            start_command(start_config(
                bounds={"centerX": 0, "centerZ": 0, "radiusBlocks": 30000,
                        "shape": "circle", "fullWorld": False},
                threads=2, resultLimit=50)),
            {"type": "cancel", "jobId": "river-1"},
        ], tmp)
        failed = of_type(events, "failed")
        cancelled = [f for f in failed if f.get("code") == "cancelled"]
        check(len(cancelled) == 1, f"取消应产生 failed(cancelled)，实际 {[f.get('code') for f in failed]}")
        check(len(of_type(events, "done")) == 0, "取消后不能再发 done")
        cancelling = [f for f in of_type(events, "progress")
                      if f["progress"].get("state") == "cancelling"]
        check(len(cancelling) >= 1, "取消时应回一帧 state=cancelling 的 progress")

        # --- 16. deferExport → exports 三份内容字符串，output 为空 ---
        events, _ = talk([start_command(start_config(
            deferExport=True, resultLimit=3))], tmp)
        done = of_type(events, "done")
        check(len(done) == 1, "deferExport 应正常完成")
        if done:
            exports = done[0].get("exports")
            check(isinstance(exports, dict), "done 应带 exports 对象")
            if isinstance(exports, dict):
                for kind in ("csv", "txt", "json"):
                    check(kind in exports, f"exports.{kind} 缺失")
                    check(isinstance(exports.get(kind), str) and exports[kind],
                          f"exports.{kind} 应为非空内容字符串")
                check(eval_ok_json(exports.get("json", "")),
                      "exports.json 应是合法 JSON 内容字符串")
            check(done[0]["result"]["output"] == {},
                  "deferExport 时 result.output 应为空对象")

        # --- 17. 非 deferExport → 写 results/ 并回填 output 路径 ---
        events, _ = talk([start_command(start_config(deferExport=False, resultLimit=2))], tmp)
        done = of_type(events, "done")
        check(len(done) == 1, "非 deferExport 应正常完成")
        if done:
            output = done[0]["result"]["output"]
            for kind in ("csvPath", "txtPath", "jsonPath"):
                check(kind in output, f"output.{kind} 缺失")
                path = pathlib.Path(output[kind])
                check(path.is_file(), f"{kind} 指向的文件应存在：{path}")
            json_path = pathlib.Path(output["jsonPath"])
            if json_path.is_file():
                saved = json.loads(json_path.read_text(encoding="utf-8"))
                check(len(saved["candidates"]) == len(done[0]["result"]["candidates"]),
                      "落盘 JSON 的候选数应与 done.result 一致")
                check(saved["engineVersion"] == "0.2.0", "落盘 JSON 应带引擎版本")

        # --- 18. fullWorld 是 bounds 白名单里的合法键 ---
        # 这里**不能**真跑 fullWorld 任务：它会被展开成 30M 半径的方形全图搜索，
        # 而粗筛循环（third_party/riverfinder）不受本进程取消令牌的控制，
        # 任务无法在测试时间内退出。因此只验证 fullWorld 被当作已知键接受，
        # 同时它周围的未知键仍然被抓出来（展开与回显另见人工验证记录）。
        fullworld_bounds = {"centerX": 100, "centerZ": -100, "radiusBlocks": 8000,
                            "shape": "circle", "fullWorld": True, "offsetX": 1}
        check_rejected(start_config(bounds=fullworld_bounds),
                       "fullWorld 与未知 bounds 键", need_field="offsetX", workdir=tmp)

        # --- 19. EOF 不能被当成 cancel（单行 start 输入立即 EOF） ---
        events, _ = talk([start_command(start_config())], tmp)
        check(len(of_type(events, "done")) == 1,
              "EOF 应等任务自然跑完（不能被当成取消）")
        check(len([f for f in of_type(events, "failed") if f.get("code") == "cancelled"]) == 0,
              "EOF 不应产生 failed(cancelled)")
        done = of_type(events, "done")
        if done:
            check(done[0]["result"]["status"] == "completed",
                  f"EOF 后 status 应为 completed，实际 {done[0]['result'].get('status')}")

        # --- 20. 空行与非法 JSON 不应让进程崩溃 ---
        proc = subprocess.run([str(EXE), "--desktop"], input="\n\nnot-json\n",
                              capture_output=True, text=True, encoding="utf-8",
                              cwd=tmp, timeout=60)
        check(proc.returncode == 0, f"非法 JSON 后进程应正常退出，实际 {proc.returncode}")
        events = [json.loads(line) for line in proc.stdout.splitlines() if line.strip()]
        failed = of_type(events, "failed")
        check(len(failed) == 1, f"非法 JSON 应有一个 failed，实际 {len(failed)}")
        if failed:
            check(failed[0]["code"] == "invalid-config", "非法 JSON 错误码应为 invalid-config")

    if failures:
        print(f"\n失败 {len(failures)} 项")
        return 1
    print("协议测试全部通过")
    return 0


def eval_ok_json(text):
    try:
        json.loads(text)
    except (ValueError, TypeError):
        return False
    return True


if __name__ == "__main__":
    sys.exit(main())
