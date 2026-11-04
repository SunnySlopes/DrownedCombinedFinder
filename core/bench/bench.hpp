#pragma once
// 分阶段计时与诊断计数（照搬 B Bench.h 的口径，名额按 C 的阶段重排）。
//
// 关闭 --bench 时唯一的开销是一个 relaxed 原子读；所有加法都守在这个开关后面。
// 计数用线程局部结构累加，线程结束时 merge 到全局，避免每个采样点做跨核原子写
// （那会把 --bench 本身变成要测量的瓶颈）。不在每个采样点里插桩耗时。
//
// 合并点：搜索的 parallel() 工作线程退出时、以及主线程收尾前各调用一次 flush()。

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace wcore::bench {

enum Stage {
    STAGE_SETUP = 0,       // 周期表构建
    STAGE_CONTINENT = 1,   // 大陆性掩膜 / 气候门
    STAGE_WEIRD = 2,       // weird 门与 w 采样
    STAGE_COARSE = 3,      // 粗筛采样
    STAGE_PRECISE = 4,     // 最终逐格窗口
    STAGE_PIT = 5,         // 大坑扫描
    STAGE_COUNT = 6
};

inline const char *stageName(Stage stage) {
    switch (stage) {
    case STAGE_SETUP: return "setup octave tables";
    case STAGE_CONTINENT: return "continent gate";
    case STAGE_WEIRD: return "weird gate";
    case STAGE_COARSE: return "coarse sampling";
    case STAGE_PRECISE: return "precise windows";
    case STAGE_PIT: return "pit scan";
    default: return "?";
    }
}

struct Counters {
    uint64_t ns[STAGE_COUNT]{};
    uint64_t calls[STAGE_COUNT]{};

    // 分母：各阶段实际处理的格数/采样数（只统计，不参与判定）。
    uint64_t continentCells{0};
    uint64_t weirdCells{0};
    uint64_t coarseCells{0};
    uint64_t preciseCells{0};
    uint64_t pitCells{0};

    // 早退漏斗。
    uint64_t continentSamples{0};
    uint64_t continentEarlyExits{0};
    /** C1：10 个八度已过 0.575、无需补算剩余 8 个八度的比例。 */
    uint64_t continentFastAccepts{0};
    uint64_t weirdSamples{0};
    uint64_t weirdEarlyExits{0};

    // A 表命中率：river = 纯河流 weird 门，cave = 洞穴路径的气候门。
    uint64_t riverTableHits{0};
    uint64_t riverTableSamples{0};
    uint64_t caveTableHits{0};
    uint64_t caveTableSamples{0};

    // 对齐诊断：weird 采样落在 A 表晶格外、退回 live 的比例。
    uint64_t latticeMisses{0};
    uint64_t latticeSamples{0};

    // 差分膨胀：真正省下的重复写入次数。
    uint64_t dilationWritesSaved{0};

    void reset() { std::memset(this, 0, sizeof(*this)); }
};

namespace detail {

struct State {
    std::atomic<bool> enabled{false};
    std::mutex mutex;
    Counters aggregate;
};

inline State &state() {
    static State instance;
    return instance;
}

inline Counters &threadCounters() {
    static thread_local Counters localCounters;
    return localCounters;
}

}  // namespace detail

inline Counters &counters() {
    return detail::threadCounters();
}

inline bool enabled() {
    return detail::state().enabled.load(std::memory_order_relaxed);
}

inline void setEnabled(bool on) {
    detail::state().enabled.store(on, std::memory_order_relaxed);
}

inline void reset() {
    detail::state().aggregate.reset();
    detail::threadCounters().reset();
}

/** 把本线程计数并入全局快照。线程退出前必须调用；可重复调用。 */
inline void flush() {
    Counters &local = detail::threadCounters();
    if (!enabled())
        return;
    detail::State &s = detail::state();
    std::lock_guard lock(s.mutex);
    Counters &out = s.aggregate;
    for (int i = 0; i < STAGE_COUNT; ++i) {
        out.ns[i] += local.ns[i];
        out.calls[i] += local.calls[i];
    }
    out.continentCells += local.continentCells;
    out.weirdCells += local.weirdCells;
    out.coarseCells += local.coarseCells;
    out.preciseCells += local.preciseCells;
    out.pitCells += local.pitCells;
    out.continentSamples += local.continentSamples;
    out.continentEarlyExits += local.continentEarlyExits;
    out.continentFastAccepts += local.continentFastAccepts;
    out.weirdSamples += local.weirdSamples;
    out.weirdEarlyExits += local.weirdEarlyExits;
    out.riverTableHits += local.riverTableHits;
    out.riverTableSamples += local.riverTableSamples;
    out.caveTableHits += local.caveTableHits;
    out.caveTableSamples += local.caveTableSamples;
    out.latticeMisses += local.latticeMisses;
    out.latticeSamples += local.latticeSamples;
    out.dilationWritesSaved += local.dilationWritesSaved;
    local.reset();
}

/** 合并后的全局快照（只在 flush 之后读取）。 */
inline const Counters &aggregate() {
    return detail::state().aggregate;
}

inline uint64_t nowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline void add(uint64_t &slot, uint64_t value) {
    if (enabled())
        slot += value;
}

inline void count(uint64_t &slot) {
    if (enabled())
        ++slot;
}

inline void count(uint64_t &slot, uint64_t value) {
    if (enabled() && value)
        slot += value;
}

/** 计一段流水线阶段的耗时；--bench 关闭时只读一次原子开关。 */
class Scope {
  public:
    explicit Scope(Stage stage) : stage_(stage), startNs_(0), active_(enabled()) {
        if (active_) startNs_ = nowNs();
    }
    ~Scope() {
        if (!active_) return;
        Counters &c = counters();
        c.ns[stage_] += nowNs() - startNs_;
        ++c.calls[stage_];
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

  private:
    Stage stage_;
    uint64_t startNs_;
    bool active_;
};

}  // namespace wcore::bench
