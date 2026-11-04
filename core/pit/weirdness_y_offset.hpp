#pragma once
// weirdness Y 偏移：Java 侧 lowydripstonecavefinder.cpp:293 的忠实移植（MIT）。
//
// score = |yo0A-0.5| + |yo1A-0.5| + |yo0B-0.5| + |yo1B-0.5|，
// yo = 八度 fork 的高 24 位 × 2⁻²⁴。
// UI 侧只做阈值判断：< 0.75 推荐逃课（pit），> 0.85 建议回普通搜索。

#include <cmath>
#include <cstdint>

namespace wcore::pit {

namespace detail {

constexpr uint64_t kMix1 = 0xbf58476d1ce4e5b9ULL;
constexpr uint64_t kMix2 = 0x94d049bb133111ebULL;
constexpr uint64_t kSilver = 0x6a09e667f3bcc909ULL;
constexpr uint64_t kGolden = 0x9e3779b97f4a7c15ULL;
constexpr uint64_t kFallbackLo = 0x9e3779b97f4a7c15ULL;
constexpr uint64_t kFallbackHi = 0x6a09e667f3bcc909ULL;

// md5("minecraft:weirdness") / md5("octave_-7") / md5("octave_-6")
constexpr uint64_t kHashWLo = 0xefc8ef4d36102b34ULL;
constexpr uint64_t kHashWHi = 0x1beeeb324a0f24eaULL;
constexpr uint64_t kOctM7Lo = 0xf11268128982754fULL;
constexpr uint64_t kOctM7Hi = 0x257a1d670430b0aaULL;
constexpr uint64_t kOctM6Lo = 0xe51c98ce7d1de664ULL;
constexpr uint64_t kOctM6Hi = 0x5f9478a733040c45ULL;

inline uint64_t rol64(uint64_t a, int bits) {
    return (a << bits) | (a >> (64 - bits));
}

inline uint64_t mix64(uint64_t a) {
    a = (a ^ (a >> 30)) * kMix1;
    a = (a ^ (a >> 27)) * kMix2;
    return a ^ (a >> 31);
}

struct JavaRng {
    uint64_t lo, hi;

    explicit JavaRng(uint64_t seed) {
        seed ^= kSilver;
        lo = mix64(seed);
        hi = mix64(seed + kGolden);
        if ((lo | hi) == 0) { lo = kFallbackLo; hi = kFallbackHi; }
    }
    JavaRng(uint64_t lo_, uint64_t hi_) : lo(lo_), hi(hi_) {
        if ((lo | hi) == 0) { lo = kFallbackLo; hi = kFallbackHi; }
    }

    uint64_t next() {
        const uint64_t l = lo, h = hi;
        const uint64_t r = rol64(l + h, 17) + l;
        hi = h ^ l;
        lo = rol64(l, 49) ^ hi ^ (hi << 21);
        hi = rol64(hi, 28);
        return r;
    }
};

inline void forkPair(JavaRng &rng, uint64_t out[2]) {
    out[0] = rng.next();
    out[1] = rng.next();
}

inline void advanceHash(uint64_t lo, uint64_t hi, uint64_t out[2]) {
    JavaRng rng(lo, hi);
    rng.next();
    out[0] = rng.lo;
    out[1] = rng.hi;
}

inline void noiseYoFork(const uint64_t fork[2], uint64_t out[2]) {
    JavaRng rng(fork[0], fork[1]);
    rng.next();
    out[0] = rng.lo;
    out[1] = rng.hi;
}

inline float octaveYo(const uint64_t yo[2], const uint64_t hash[2]) {
    JavaRng rng(yo[0] ^ hash[0], yo[1] ^ hash[1]);
    const int top = static_cast<int>(rng.next() >> 32) & 0xFFFFFF;
    return static_cast<float>(top) * 5.9604645E-8f;
}

}  // namespace detail

/** 与 B 的 weirdnessYOffset() 逐位相同的 C 版。 */
inline float weirdnessYOffset(uint64_t seed) {
    using namespace detail;
    JavaRng world(seed);
    uint64_t noise[2];
    forkPair(world, noise);
    noise[0] ^= kHashWLo;
    noise[1] ^= kHashWHi;

    JavaRng noiseRng(noise[0], noise[1]);
    uint64_t a[2], b[2], fork[2];
    forkPair(noiseRng, fork);
    noiseYoFork(fork, a);
    forkPair(noiseRng, fork);
    noiseYoFork(fork, b);

    uint64_t h0[2], h1[2];
    advanceHash(kOctM7Lo, kOctM7Hi, h0);
    advanceHash(kOctM6Lo, kOctM6Hi, h1);

    const float w0a = octaveYo(a, h0);
    const float w1a = octaveYo(a, h1);
    const float w0b = octaveYo(b, h0);
    const float w1b = octaveYo(b, h1);
    return std::fabs(w0a - 0.5f) + std::fabs(w1a - 0.5f)
         + std::fabs(w0b - 0.5f) + std::fabs(w1b - 0.5f);
}

/** UI 建议：< 0.75 推荐逃课（pit），> 0.85 建议回普通搜索。 */
inline const char *recommendation(float offset) {
    if (offset < 0.75f) return "pit";
    if (offset > 0.85f) return "normal";
    return "either";
}

}  // namespace wcore::pit
