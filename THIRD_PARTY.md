# 第三方来源与许可

本项目新增代码采用 [GPL-3.0-only](LICENSE)。外部代码保留原始声明，项目许可证不改写其版权或许可。

| 来源 | 使用范围 | 固定版本与许可 |
|---|---|---|
| [LowYDripstoneCaveFinderBase](https://github.com/SunnySlopes/LowYDripstoneCaveFinderGUI)（本地工作区 `LowYDripstoneCaveFinderGUI/LowYDripstoneCaveFinderBase`） | `core/`：气候门早退漏斗、A组周期表、16×3梯度表、大坑搜索、bench 口径、挂机点平台公式与 Java RNG 的 weirdness Y 偏移 | MIT，[原文](core/LICENSE) |
| [RiverFinder-JNI](https://github.com/M33Galaxy/RiverFinder-JNI)，本地工作区 `参考/RiverFinder-JNI` | `third_party/riverfinder/` 的河流搜索内核与线程收集；本地适配公共气候接口，修复量化边界的预筛漏检，并允许纯河流粗筛复用相同坐标的W采样值 | MIT，[原文](third_party/riverfinder/LICENSE) |
| [Cubiomes维护分支](https://github.com/qk-yiyihehe/cubiomes/tree/minecraft-finders)，源于Cubitect/cubiomes与xpple/cubiomes | `third_party/cubiomes/`为所需C源码及本地include闭包；噪声、RNG、群系及调用到的结构能力 | 本地开发快照`dd28bae6bd99bc175fe645c606f356c919a8f1b5c2108e3fb18c612b81c44aaa`，基线`91a0e8b7d122830f7b1a6c3367266d3ae546d0de`，基于xpple提交`aee74239609f3b371747e4573808c04a72518979`；[锁文件](third_party/cubiomes-lock.json)含文件SHA256、来源与补丁；[MIT原文](third_party/cubiomes/LICENSE) |
| [JSON for Modern C++](https://github.com/nlohmann/json)（Niels Lohmann 及贡献者） | `third_party/json.hpp`，配置、JSONL 与结果序列化 | v3.12.0；[MIT 原文](third_party/json-LICENSE.MIT) |

复用LowYDripstoneCaveFinderGUI-master的原生BiomeSampler.cpp/SearchConfig.h预筛阈值、圆环行跨度、多级细化及OctaveFieldCache的A组周期表思路，改写到 `core/`（MIT）并参数化：大陆性 8 级漏斗、weird 双边/单边早退、查表梯度（double + float）、大坑搜索（精算回调化）、bench 计数与平台公式。river-native 单向依赖 `core/`；`core/` 不得反向引用 GPL 代码或本引擎的 Hit/Config。选取的预筛与周期表适配仍保留 `third_party/dripstone/` 兼容层；周期表改为每次搜索独立持有，新增采样晶格相位并保留26.3交错float累加，原目录的MIT许可原文一并保留；没有复制Java界面、JNI包装或该项目旧Cubiomes。河流气候快速门从现有RiverFinder-JNI源码拆出复用，不重复实现。

## 依赖维护

共享修改只回到维护仓库。本轮按用户要求不做提交：通过显式--working-tree同步河流开发快照，锁文件记录基线提交、workingTree=true和完整内容哈希sourceId；这不是正式Git提交或发布依赖包，其他三个引擎保持原固定版本。副本只包含河流编译使用的源码与头文件；维护仓库的上游来源、补丁与手动同步步骤见[维护说明](https://github.com/qk-yiyihehe/cubiomes/tree/minecraft-finders)。普通构建不联网、不依赖相邻仓库；升级后完整重编译，执行本地回归。

集中维护库另加入批量Voronoi、严格洞穴距离下界及单柱洞穴区间优化，保留完整查询作为无法证明时的退路。本次保留Java纵向噪声量化修复；河流气候采样的yamp为0，该补丁不直接改变此路径。支持26.3枚举和群系表不等于完整原版数值已对齐；与参考程序一致不能代替原版验证。

## 许可兼容性说明

`third_party/riverfinder/`、`third_party/cubiomes/` 与 `core/` 都是 MIT，允许在 GPL-3.0-only 项目中分发。保留原作者声明、许可及本地适配说明，不因补丁改变上游代码的许可。`core/` 只汇总 MIT 来源；CI 的 `core-license` 测试会扫描该目录，避免误入 GPL 代码把 B 的分发拖成 GPL。

Minecraft Java Edition 由 Mojang Studios/Microsoft 开发，仅作为结构与游戏规则的行为参照。本项目运行源码不包含游戏 JAR、服务端运行时或世界存档，正式运行不启动游戏。

分发时保留本说明、相关许可证和源文件中的原作者声明。

26.3粗筛使用维护库按版本分派的分量噪声和深度接口，提前返回阈值保留；与完整群系采样器执行相同数值路径。支持1.18至26.3的32个正式版本入口，1.19.3按6分支群系树选择底层类。
