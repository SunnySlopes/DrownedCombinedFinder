"""river-native 的 CLI 集成测试。

覆盖真实链路：参数解析 → 搜索 → 导出。重点验证 CLI 层不会悄悄改参数，
以及排名口径在真实结果里成立。
"""

import json
import pathlib
import subprocess
import sys
import tempfile

EXE = pathlib.Path(sys.argv[1])
failures = []


def check(condition, what):
    if condition:
        return
    failures.append(what)
    print(f"FAIL: {what}")


def run(args, workdir):
    proc = subprocess.run([str(EXE)] + [str(a) for a in args],
                          capture_output=True, text=True, encoding="utf-8", cwd=workdir)
    return proc


def main():
    with tempfile.TemporaryDirectory(dir=pathlib.Path.cwd()) as tmp:
        tmp = pathlib.Path(tmp)

        # --- 1. --versions 输出完整版本表 ---
        proc = run(["--versions"], tmp)
        check(proc.returncode == 0, "--versions 应成功")
        table = json.loads(proc.stdout)["versions"]
        check(len(table) == 32, f"--versions 应有 32 个版本，实际 {len(table)}")
        labels = {row["version"] for row in table}
        for expected in ("1.18", "1.18.1", "1.18.2", "1.19.3", "1.20.5", "26.1.2", "26.3", "1.21.11"):
            check(expected in labels, f"--versions 应包含 {expected}")

        # --- 2. 实际搜索并检查排名口径 ---
        out = tmp / "out1"
        proc = run(["--seed", "12345", "--version", "26.3",
                    "--radius", "8000", "--limit", "8", "--output", out], tmp)
        check(proc.returncode == 0, f"搜索应成功: {proc.stderr}")
        result = json.loads(proc.stdout)
        check(result["status"] == "completed", f"状态应为 completed，实际 {result['status']}")
        check(result["verified"] is False, "verified 必须为 false")
        check(result["searchApproximate"] is True, "searchApproximate 必须为 true")
        check(result["rankingRuleVersion"] == 3, "排名规则版本应为 3")
        check(result["rankingRule"] == "riverarea-desc-distance-asc-x-asc-z-asc",
              "排名规则字符串")

        candidates = result["candidates"]
        check(len(candidates) > 0, "该范围应有候选")

        # 主指标必须是面积降序
        areas = [c["riverArea"] for c in candidates]
        check(areas == sorted(areas, reverse=True),
              f"面积应降序，实际 {areas}")

        # 同面积时距离必须升序（用户明确要求的次序）
        for i in range(len(candidates) - 1):
            a, b = candidates[i], candidates[i + 1]
            if a["riverArea"] == b["riverArea"]:
                check(a["distanceSq"] <= b["distanceSq"],
                      f"同面积时距离应升序：{a['distanceSq']} vs {b['distanceSq']}")

        # rank / id 必须是 1..N 连续
        check([c["rank"] for c in candidates] == list(range(1, len(candidates) + 1)),
              "rank 应从 1 连续递增")

        # --- 3. 导出文件与 stdout 一致 ---
        exports = result["exports"]
        for kind in ("csv", "txt", "json"):
            check(pathlib.Path(exports[kind]).is_file(), f"{kind} 文件应存在")
        saved = json.loads(pathlib.Path(exports["json"]).read_text(encoding="utf-8"))
        check(len(saved["candidates"]) == len(candidates), "导出 JSON 候选数与 stdout 一致")

        # --- 4. 半径过小必须显式报错，不能静默给空结果 ---
        proc = run(["--seed", "12345", "--radius", "99"], tmp)
        check(proc.returncode != 0, "radius=99 应报错")
        check("128" in proc.stderr, f"报错应提到 128: {proc.stderr}")

        # --- 5. 64 位种子边界值 ---
        for seed in ("-9223372036854775808", "9223372036854775807"):
            proc = run(["--seed", seed, "--radius", "1000", "--limit", "2",
                        "--output", tmp / "big"], tmp)
            check(proc.returncode == 0, f"64位种子 {seed} 应能运行: {proc.stderr}")

        # --- 6. 未知参数必须报错 ---
        proc = run(["--seed", "1", "--radius", "1000", "--nonsense", "1"], tmp)
        check(proc.returncode != 0, "未知参数应报错")

        # --- 7. --config 读文件，且命令行能覆盖 ---
        cfg = tmp / "finder.json"
        cfg.write_text(json.dumps({
            "seed": "12345", "version": "26.3", "radius": 2000,
            "min-area": 1000, "limit": 3, "output": str(tmp / "cfgout"),
        }, ensure_ascii=False), encoding="utf-8")
        proc = run(["--config", cfg], tmp)
        check(proc.returncode == 0, f"--config 应成功: {proc.stderr}")
        from_file = json.loads(proc.stdout)
        check(from_file["config"]["radius"] == 2000, "配置文件的半径应生效")

        proc = run(["--config", cfg, "--radius", "3000"], tmp)
        check(proc.returncode == 0, "命令行覆盖应成功")
        if proc.returncode == 0:
            check(json.loads(proc.stdout)["config"]["radius"] == 3000,
                  "命令行应覆盖配置文件")

    if failures:
        print(f"\n失败 {len(failures)} 项")
        return 1
    print("CLI 集成测试全部通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
