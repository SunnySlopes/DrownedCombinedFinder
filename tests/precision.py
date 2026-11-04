"""精度对照：本引擎 vs 参考实现的原始算法。

这是本仓库最重要的一条测试——它证明「把参考实现接进项目骨架后，结果没被改坏」。

做法：同一批参数分别跑
  · 参考实现（tools/reference_dump.cpp，直接调 riverfinder 的原始入口）
  · 本引擎（CLI）
比对候选集合（x, z, riverArea）是否**逐项相同**。

⚠️ 必须用 --shape square：参考实现的扫描范围是方形（startX-r .. startX+r），
   而本引擎的 --shape circle 会在最终筛选中按圆形裁剪。用 circle 比对会
   「少几个」，那是口径不同，不是算法错。
"""

import json
import pathlib
import subprocess
import sys
import tempfile

EXE = pathlib.Path(sys.argv[1])
HERE = pathlib.Path(__file__).resolve().parent.parent
REF_DUMP = HERE / "build" / "prec" / "reference_dump.exe"

failures = []


def check(condition, what):
    if condition:
        return
    failures.append(what)
    print(f"FAIL: {what}")


def reference(seed, radius, sample_y, min_area):
    proc = subprocess.run(
        [str(REF_DUMP), str(seed), str(radius), str(sample_y), str(min_area)],
        capture_output=True, text=True, encoding="utf-8", check=True)
    out = set()
    for line in proc.stdout.splitlines():
        if line.startswith("REF\t"):
            _, x, z, area = line.split("\t")
            out.add((int(x), int(z), int(area)))
    return out


def engine(seed, radius, sample_y, min_area, workdir):
    proc = subprocess.run(
        [str(EXE), "--seed", str(seed), "--version", "26.3", "--radius", str(radius),
         "--shape", "square", "--min-area", str(min_area), "--sample-y", str(sample_y),
         "--limit", "1000", "--output", str(workdir)],
        capture_output=True, text=True, encoding="utf-8", cwd=workdir)
    if proc.returncode != 0:
        raise RuntimeError(f"引擎失败: {proc.stderr}")
    data = json.loads(proc.stdout)
    return {(c["x"], c["z"], c["riverArea"]) for c in data["candidates"]}


def main():
    if not REF_DUMP.is_file():
        print(f"跳过：找不到 {REF_DUMP}（需先按 README 的精度验证步骤编译）")
        return 0

    cases = [
        ("种子12345 r8000 y63", 12345, 8000, 63, 20000),
        ("种子0 r4000 y63", 0, 4000, 63, 20000),
        ("高面积下限", 12345, 8000, 63, 30000),
        ("不同采样高度 y80", 12345, 8000, 80, 20000),
    ]

    with tempfile.TemporaryDirectory(dir=pathlib.Path.cwd()) as tmp:
        tmp = pathlib.Path(tmp)
        for label, seed, radius, sample_y, min_area in cases:
            ref = reference(seed, radius, sample_y, min_area)
            mine = engine(seed, radius, sample_y, min_area, tmp)

            if ref == mine:
                print(f"  {label}: 一致（{len(ref)} 个候选）")
                continue

            only_ref = sorted(ref - mine)
            only_mine = sorted(mine - ref)
            print(f"  {label}: 不一致！参考 {len(ref)} 本引擎 {len(mine)}")
            if only_ref:
                print(f"    参考独有: {only_ref[:5]}")
            if only_mine:
                print(f"    本引擎独有: {only_mine[:5]}")
            check(False, f"{label} 候选集合应逐项相同")

    if failures:
        print(f"\n失败 {len(failures)} 项")
        return 1
    print("精度对照全部通过：本引擎与参考实现逐项一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())
