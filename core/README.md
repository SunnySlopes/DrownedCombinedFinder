# core —— 两个搜索器共享的 MIT 代码

本目录只放从 `LowYDripstoneCaveFinderBase`（MIT）提取并参数化的实现，
供 river-native（GPL）单向依赖。**不得**把 river-native 的 `Hit` / `Config`
或任何 GPL 实现搬进来；若本目录混入 GPL，B 的分发会被迫整体转 GPL。

## 内容

| 路径 | 来源 | 说明 |
|---|---|---|
| `gates/perlin_table.hpp` | B `PerlinTable.h` + C `noise.c` 26.3 路径 | 16×3 梯度表，double 版与 float 版，逐位等同 `samplePerlin` / `samplePerlinFloat` |
| `gates/octave_cache.hpp` | B `OctaveFieldCache` + C `dripstone/octave_cache.hpp` | A 组周期表；26.3 float 交错累加，pre-26.3 double 分组累加 |
| `gates/early_exit.hpp` | B `BiomeSampler.cpp` 的漏斗 | 参数化气候门：大陆性 8 级、weird 双边、weird 单边 |
| `bench/bench.hpp` | B `Bench.h` | 分阶段计时与命中率计数，`--bench` 关闭时只花一次 relaxed 原子读 |
| `platform/player_y.hpp` | 本计划 §1.2 | 挂机点平台解析解 |
| `pit/` | B `LargePitSearch.cpp` + `lowydripstonecavefinder.cpp` | 大坑搜索，精算抽成回调；Java RNG 的 weirdness Y 偏移 |

## 数值红线

- `perlin_table.hpp` 的求和顺序固定为 `(gx*a + gy*b) + gz*c`，插值顺序与
  cubiomes 相同；不要重排。
- `octave_cache.hpp` 的 26.3 路径必须 `sum += A; sum += B;` 交错累加；
  pre-26.3 路径必须先求 A 组、B 组，最后乘总振幅。
- 早退漏斗与全量判定必须在**同一条累加序列**上比较，否则浮点边界会翻转。

## 许可证

本目录为 MIT（见 `LICENSE`），源自 `LowYDripstoneCaveFinderBase`。
