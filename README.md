# river-native

Minecraft Java版低Y溺尸农场搜索后端，支持1.18至26.3的32个正式版本。输入种子和外范围，寻找指定内范围内预计收益较高的独立热点；不启动Java或Minecraft。

引擎0.4.0把共享的漏斗／A表／梯度表／大坑／bench 提取到 MIT 的 `core/`（与低Y溶洞搜索器同源），新增自定义层范围、自由矩形、三字段门槛、纯河流32格 w 门和 `--bench` 口径。模型版本为 `drowned-weighted-area-v2`。

## 构建与运行

```powershell
pwsh -File build.ps1
pwsh -File build.ps1 -Test
.\run.bat --seed 12345 --radius 100000 --limit 100
.\run.bat --seed 12345 --include-caves --coverage game --simulation-distance 10
.\run.bat --seed 12345 --include-caves --y-min -60 --y-max -52
.\run.bat --seed 12345 --min-x -10000 --max-x 10000 --min-z -5000 --max-z 5000
```

`run.bat`读取本目录`finder.json`，命令行覆盖配置；原生程序仅显式`--config`时读文件。配置键是CLI参数去掉`--`，种子必须为字符串。

## 参数

| 参数 | 说明 | 默认 |
|---|---|---|
| `--seed`、`--version` | 十进制64位种子、正式版入口 | `0`、`26.3` |
| `--center-x/z`、`--radius`、`--shape` | 外范围；圆`circle`或方`square` | 原点、100000格、圆 |
| `--min-x/--max-x/--min-z/--max-z` | 自由矩形外范围，与中心/半径互斥 | 关闭 |
| `--include-caves` | 按 `yMin..yMax` 逐层统计河流与滴水石洞穴折算 | 关闭，仅Y=-63普通河流 |
| `--y-min`、`--y-max` | 自定义层范围；挂机点走平台解析解 | -63、-51 |
| `--coverage` | 内范围`game/circle/square` | `game` |
| `--inner-radius` | 自定义圆／方的水平半径 | 128格 |
| `--simulation-distance` | 游戏模式模拟距离 | 10区块 |
| `--player-y`、`--optimize-player-y` | 固定挂机Y（未勾选“计算最佳Y”时生效）；或计算最佳Y（平台解析解） | **yMin+23**、关闭 |
| `--min-area` | 主评分下限（等效河流格/层） | 34748 |
| `--min-river-area`、`--min-cave-area` | 三字段门槛：`minArea = R + 1.43668·C` | 未启用 |
| `--min-distance` | 输出热点最小水平间距 | 256格 |
| `--candidate-budget` | 粗候选池容量；0通常为max(8192,64×limit)，大范围洞穴至少16384 | 0 |
| `--approximate-gate` | 气候门总开关：**10 个八度分级**（>0.575 直接接受 / 0.525~0.575 才补算剩余 8 个八度 / ≤0.525 判否）+ weird 早退 | 开启 |
| `--pure-river-cache` | 纯河流A表 `auto/on/off` | `auto`（半径≥20万建轻量表） |
| `--pure-river-mask` | 纯河流 w 掩膜步长 `32/64`；32 = 对齐 A 表 32 格晶格（分辨率×4） | **64（改动前行为）** |
| `--coarse-y` | 粗筛召回高度；粗筛气候门与 Y 无关，仅为协议/CLI 兼容而携带 | -51 |
| `--pit` | 大坑逃课路径（只计溶洞体积） | 关闭 |
| `--bench` | 输出 ns/格、A表命中率、早退率、峰值工作集 | 关闭 |
| `--threads`、`--limit` | 1–32线程、最多100000条 | 8、100 |
| `--output`、`--jsonl` | 结果目录、CLI进度 | `results`、关闭 |

距离支持对应的`--radius-unit`、`--inner-radius-unit`、`--min-distance-unit`和`--simulation-distance-unit`，值为`blocks/chunks`；模拟距离必须精确换算为整数区块。`--full-world`展开世界边界方形；`--versions`列出正式版入口。

## 点位诊断

这些参数替代搜索，只算一个点（与最终精排同一套 1:1 层采样与几何，`coverage`/`simulationDistance` 全部生效），适合复现结果、核对 pit 锚点与验证挂机 Y 平台：

| 参数 | 说明 |
|---|---|
| `--score-point <x> <z>` | 给单点打分，输出 score / riverArea / riverVolume / caveVolume / playerY |
| `--sweep-afk-y <a> <b>` | 只采样一次群系，扫描 `[a,b]` 内每个挂机 Y，输出最佳 Y |
| `--repeat <n>` | 单点计时重复次数，默认 5 |

```powershell
.\run.bat --seed 12345 --include-caves --score-point -87 -236
.\run.bat --seed 12345 --include-caves --score-point -87 -236 --sweep-afk-y -64 0
```

`--score-point` 的 `coverage` 取 `circle` 时等价于 B 的“cylinder”对照模型（每层水平环、忽略球壳的 dy）。

## 模型与排名

假设挖空、供水、遮光并排除其他刷怪点，不预测天然地形与水体。关闭折算时查脚部Y=-63；开启后逐层统计`yMin..yMax`，深暗之域等非目标群系不计入。层数`L=yMax-yMin+1`参与分母，层范围一变分数即不可比，结果用`layers`与`spawnYMin/spawnYMax`回显。

综合分为`(4880×河流体积 + 7011×溶洞体积)/(4880×L)`。系数来自原版生成表与准入概率，是理论比较权重，不是每小时产量。圆／方忽略Y，不扣24格；游戏模式使用三维24–128格球壳与自然刷怪区块、模拟距离的交集。固定挂机Y默认**yMin+23**（默认窗口即 -40，与 B 一致），可用`--player-y`覆盖；勾选`--optimize-player-y`时改用平台解析解（所有层都在玩家±24格内时每层覆盖面积相同，不再逐Y穷举）。

结果里的`fullShellArea`是**显示分母**：把`yMin..yMax`每层 24–128 球壳格数求和后除以层数（默认 -63..-51 配 playerY=-40 时恰为 **49659**）。它由几何精确推导而非拟合；`coverage=game` 还会按自然刷怪区块裁剪，所以它只是上界。

**已知有损点**：`--approximate-gate` 开启时，大陆性用 10/18 八度分级（>0.575 直接接受、0.525~0.575 才补算剩余八度、≤0.525 判否），与 B 的截断同源；0.525 门附近约 0.3% 的点会与全判翻转，掩膜随后做 1 格膨胀兜底。`--no-approximate-gate` 退回全量判定，结果与改造前逐格一致。

**pit 验证配方**：正常情况下约 80% 的种子在 pit 路径下没有结果，不适合对拍。用种子 **6900457**：它会比普通种子慢数倍，但会返回相当多的结果，其中包含满面积点位 `16043916 15695385`。**不要尝试现场计算该点位**，只把它当作对拍时的已知锚点。

正式排名为**未舍入主评分降序 → 距搜索中心距离升序 → X → Z**。热点去重和Top K在后端完成，CSV/TXT/JSON使用同一榜单。

## 独立接口

公开C++入口为`<minecraft_finders/river.hpp>`，CMake目标`MinecraftFinders::River`。`examples/basic.cpp`演示直接调用。嵌入默认关闭CLI，普通构建不需要tests、tools、Java、Git、网络或相邻维护库。

`--desktop`保留JSONL信封v3，后端源码为0.4.0，使用低Y农场参数；旧`sampleY/caveFactor`请求会报错。`bounds` 新增 `rect:{minX,maxX,minZ,maxZ}` 变体，`params` 新增 `yMin/yMax/approximateGate/minRiverArea/minCaveArea/pureRiverCache/pureRiverMaskStep/coarseY/pitMode/bench`。**本轮未适配桌面、未更新dist或发行包**。新字段和例子见使用说明。

## 精度与依赖

候选搜索仍为近似，预算和粗网格可能漏掉热点；候选内计分经过独立逐格重算。结果固定`verified:false`、`searchApproximate:true`，不保证全地图真实前100。早退漏斗是有损近似门（约1e-4量级），`--no-approximate-gate` 可退回逐格全判。

Cubiomes通用柱求值、洞穴距离剪枝和Voronoi批量优化维护在集中仓库，当前河流使用带内容哈希的未提交开发快照，`cubiomesSourceId`标识实际源码；其他引擎不自动升级。没有共享DLL或运行时下载。生成表、原版低Y抽样、测试及未测项见开发记录。

## 目录与说明

`src/`为农场模型、几何、候选搜索及适配层；`core/`为与低Y溶洞搜索器共享的 MIT 代码（气候门、A表、梯度表、大坑、bench、平台公式）；`third_party/`保留复用的河流／溶洞代码及Cubiomes最小源码闭包。`tests/`、`tools/`、`docs/记录/`仅本地保留，不参与普通构建。

[使用说明](docs/说明/使用说明.md) · [开发记录](docs/记录/开发记录.md) · [第三方来源](THIRD_PARTY.md)

## 许可证

本项目采用[GPL-3.0-only](LICENSE)。`core/` 为 MIT（见 `core/LICENSE`），C 单向依赖它；外部代码保留原许可及来源，见[第三方来源](THIRD_PARTY.md)。
