#include "coverage.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <cubiomes/biomes.h>

namespace riverfinder_native {
int floorDiv(int value, int divisor) {
    return value / divisor - (value % divisor < 0);
}

// S1：caveConversion 参与计分。
// 溶洞体积按 cfg.caveConversion（默认 4880/7011）折算成"等效河流格"。
// 为避免浮点误差，用有理数 num/den 放大：权重 = kRiverWeight * num / den。
// 默认值 (4880, 7011) 时走快路径，结果与旧版逐位一致。
static void caveConversionRatio(const Config &cfg, int64_t &num, int64_t &den) {
    constexpr int64_t kScale = 1000000;   // 小数部分保留 6 位
    if (cfg.caveConversion == kDefaultCaveConversion) {
        num = kCaveWeight;                // 7011
        den = kRiverWeight;               // 4880  → 7011/4880
        return;
    }
    double v = cfg.caveConversion;
    if (!(v >= 0.0)) v = 0.0;             // NaN / 负数一律按 0
    num = std::llround(v * double(kScale));
    den = kScale;
}

ExactScore scoreKey(const Hit &hit, const Config &cfg) {
    int64_t num = 0, den = 1;
    caveConversionRatio(cfg, num, den);
    // scoreKey ≡ (kRiverWeight*den*riverVolume + kRiverWeight*num*caveVolume) / den
    return ExactScore(kRiverWeight) * den * hit.riverVolume
         + ExactScore(kRiverWeight) * num * hit.caveVolume;
}

ExactScore scoreKey(const Hit &hit) {
    return ExactScore(kRiverWeight) * hit.riverVolume + ExactScore(kCaveWeight) * hit.caveVolume;
}

int scoreDenominator(const Config &cfg) {
    return kRiverWeight * layersOf(cfg);
}

double score(const Hit &hit, const Config &cfg) {
    return double(scoreKey(hit)) / scoreDenominator(cfg);
}

// S1：等效溶洞面积 = round(caveVolume * caveConversion / layers)
int64_t equivalentCaveArea(const Hit &hit, const Config &cfg) {
    const int layers = layersOf(cfg) > 0 ? layersOf(cfg) : 1;
    if (!cfg.includeCaves || hit.caveVolume <= 0) return 0;
    const double v = double(hit.caveVolume) * cfg.caveConversion / double(layers);
    return int64_t(std::llround(v));
}

// 与 Config 相关的分子文本（含 caveConversion），用于结果 JSON 的无损表达。
std::string scoreNumerator(const Hit &hit, const Config &cfg) {
    int64_t num = 0, den = 1;
    caveConversionRatio(cfg, num, den);
    auto value = scoreKey(hit, cfg);
    if (!value) return "0";
    std::string text;
    while (value) { text.push_back(char('0' + int(value % 10))); value /= 10; }
    std::reverse(text.begin(), text.end());
    // 分母 = scoreDenominator * den，调用方需要知道放大倍数
    return text + "/" + std::to_string(int64_t(scoreDenominator(cfg)) * den);
}
// 与 Config 相关的精确评分（含 caveConversion）。分母同步乘 den 以保持量纲。
double scoreExact(const Hit &hit, const Config &cfg) {
    int64_t num = 0, den = 1;
    caveConversionRatio(cfg, num, den);
    return double(scoreKey(hit, cfg)) / (double(scoreDenominator(cfg)) * double(den));
}

std::string scoreNumerator(const Hit &hit) {
    auto value = scoreKey(hit);
    if (!value)
        return "0";
    std::string text;
    while (value) {
        text.push_back(char('0' + value % 10));
        value /= 10;
    }
    std::reverse(text.begin(), text.end());
    return text;
}

bool meetsMinimum(const Hit &hit, const Config &cfg) {
    int64_t num = 0, den = 1;
    caveConversionRatio(cfg, num, den);
    // 两侧同乘 den，保持与 scoreKey(hit, cfg) 同口径
    return scoreKey(hit, cfg) >= ExactScore(cfg.minArea) * scoreDenominator(cfg) * den;
}


bool naturalChunk(int cx, int cz, int x, int z, int simulationDistance) {
    if (std::abs(cx - floorDiv(x, 16)) > simulationDistance ||
        std::abs(cz - floorDiv(z, 16)) > simulationDistance)
        return false;
    int64_t dx = int64_t(cx)*32 + 16 - (int64_t(x)*2 + 1);
    int64_t dz = int64_t(cz)*32 + 16 - (int64_t(z)*2 + 1);
    // 原版先按区块中心到玩家的水平距离严格小于128筛自然刷怪区块。
    return dx*dx + dz*dz < 65536;
}

namespace {
int root(int64_t value) {
    if (value < 0)
        return -1;
    int n = int(std::sqrt(double(value)));
    while (int64_t(n+1)*(n+1) <= value)
        ++n;
    while (int64_t(n)*n > value)
        --n;
    return n;
}

// 沿用参考工具的圆环行跨度，推广为任意半径及各层球壳。
// 核只依赖水平形状和dy，线程内复用，不包含任何群系或种子状态。
const std::array<std::pair<int,int>,17> &naturalRows(GeometryCache &cache, int x, int z, int simulationDistance) {
    using Key = std::tuple<int,int,int>;
    auto &cached = cache.naturalRows;
    int phaseX = x-floorDiv(x,16)*16, phaseZ = z-floorDiv(z,16)*16;
    auto [it,inserted] = cached.try_emplace(Key{phaseX,phaseZ,simulationDistance});
    if (inserted) {
        // 自然刷怪区块形状只取决于玩家在区块内的偏移，避免每次查询重算17×17格。
        for (int cz = -8; cz <= 8; ++cz) {
            int lo = std::numeric_limits<int>::max(), hi = std::numeric_limits<int>::min();
            for (int cx = -8; cx <= 8; ++cx) {
                if (naturalChunk(cx,cz,phaseX,phaseZ,simulationDistance)) {
                    lo = std::min(lo,cx*16);
                    hi = std::max(hi,cx*16+15);
                }
            }
            it->second[cz+8] = {lo,hi};
        }
    }
    return it->second;
}

const std::vector<GeometryRow> &kernel(GeometryCache &cache, int radius, int dy, bool game, bool square) {
    using Key = std::tuple<int, int, bool, bool>;
    auto &kernels = cache.kernels;
    Key key{radius, dy, game, square};
    auto [it, inserted] = kernels.try_emplace(key);
    if (inserted) {
        int64_t vertical = game ? int64_t(dy)*dy : 0;
        int reach = root(int64_t(radius)*radius - vertical);
        for (int dz = -reach; dz <= reach; ++dz) {
            int outer = square ? radius : root(int64_t(radius)*radius - vertical - int64_t(dz)*dz);
            int inner = game ? root(576 - vertical - int64_t(dz)*dz) : -1;
            it->second.push_back({dz, outer, inner});
        }
    }
    return it->second;
}
}


int64_t pureRiverUpperBound(const Config &cfg) {
    GeometryCache cache;
    int radius = cfg.coverage == "game" ? 128 : cfg.innerRadius;
    if (cfg.coverage == "square" || radius > 512)
        return int64_t(2*radius+1)*(2*radius+1);
    if (cfg.coverage == "circle") {
        int64_t count = 0;
        for (const auto &row : kernel(cache,radius,0,false,false))
            count += 2*row.outer+1;
        return count;
    }
    if (cfg.coverage == "game" && cfg.optimizePlayerY) {
        // 兼容遗留请求：仍给出对任意挂机 Y 都成立的保守上界。
        int64_t best = 0;
        for (int dy = 0; dy <= 128; ++dy) {
            int64_t count = 0;
            for (const auto &row : kernel(cache,128,dy,true,false))
                count += 2*row.outer+1-(row.inner >= 0 ? 2*row.inner+1 : 0);
            best = std::max(best,count);
        }
        return best;
    }
    int layers = layersOf(cfg);
    int64_t best = 0;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            const auto &allowed = naturalRows(cache,x,z,cfg.simulationDistanceChunks);
            int64_t count = 0;
            for (int y = 0; y < layers; ++y)
                for (const auto &row : kernel(cache,128,layerY(cfg,y)-cfg.playerY,true,false)) {
                    int index = floorDiv(z+row.dz,16)+8;
                    if (index < 0 || index >= 17)
                        continue;
                    auto [lo,hi] = allowed[index];
                    if (lo > hi)
                        continue;
                    int left = std::max(x-row.outer,lo), right = std::min(x+row.outer,hi);
                    count += std::max(0,right-left+1);
                    if (row.inner >= 0)
                        count -= std::max(0,std::min(right,x+row.inner)-std::max(left,x-row.inner)+1);
                }
            best = std::max(best,count);
        }
    return (best+layers-1)/layers;
}

BiomeWindow::BiomeWindow(int x, int z, int width, int height, int layers, const int *biomes, int scale)
    : x_(x), z_(z), width_(width), height_(height), layers_(layers), scale_(scale) {
    size_t plane = size_t(width+1)*height;
    river_.resize(plane*layers);
    if (layers > 1)
        cave_.resize(plane*layers);
    riverProjected_.resize(plane);
    if (layers > 1)
        caveProjected_.resize(plane);
    for (int y = 0; y < layers; ++y) {
        for (int z0 = 0; z0 < height; ++z0) {
            size_t row = size_t(z0)*(width+1);
            for (int x0 = 0; x0 < width; ++x0) {
                int id = biomes[(size_t(y)*height+z0)*width+x0];
                size_t at = size_t(y)*plane + row + x0;
                river_[at+1] = river_[at] + (id == river);
                if (layers > 1)
                    cave_[at+1] = cave_[at] + (id == dripstone_caves);
                // 行前缀的逐层累加：rowSum/rectangle 都按“前缀数组”语义差分，
                // 逐层相加后差值就是该 x 区间全部层的格数。
                riverProjected_[row+x0+1] += river_[at+1];
                if (layers > 1)
                    caveProjected_[row+x0+1] += cave_[at+1];
            }
        }
    }
    buildIntegrals();
}

void BiomeWindow::buildIntegrals() {
    auto integral = [&](const auto &rows, auto &out) {
        if (rows.empty())
            return;
        out.resize(size_t(width_+1)*(height_+1));
        for (int z0 = 0; z0 < height_; ++z0)
            for (int x0 = 1; x0 <= width_; ++x0)
                out[size_t(z0+1)*(width_+1)+x0] =
                    out[size_t(z0)*(width_+1)+x0] + rows[size_t(z0)*(width_+1)+x0];
    };
    integral(riverProjected_, riverIntegral_);
    integral(caveProjected_, caveIntegral_);
}


BiomeWindow::BiomeWindow(int x, int z, int width, int height,
                         const uint16_t *river, const uint16_t *cave, int scale)
    : x_(x), z_(z), width_(width), height_(height), layers_(1), scale_(scale) {
    riverProjected_.resize(size_t(width+1)*height);
    if (cave)
        caveProjected_.resize(size_t(width+1)*height);
    for (int z0 = 0; z0 < height; ++z0) {
        uint32_t r = 0, c = 0;
        size_t row = size_t(z0)*(width+1);
        for (int x0 = 0; x0 < width; ++x0) {
            r += river[size_t(z0)*width+x0];
            riverProjected_[row+x0+1] = r;
            if (cave) {
                c += cave[size_t(z0)*width+x0];
                caveProjected_[row+x0+1] = c;
            }
        }
    }
    buildIntegrals();
}

bool BiomeWindow::hasTargets(int x, int z, int radius) const {
    return rectangle(riverIntegral_,x-radius,z-radius,x+radius,z+radius) ||
           rectangle(caveIntegral_,x-radius,z-radius,x+radius,z+radius);
}

int64_t BiomeWindow::rowSum(const std::vector<uint32_t> &prefix, int layer, int z,
                           int left, int right) const {
    if (prefix.empty())
        return 0;
    // 最终逐格窗口不需要坐标缩放，绕过每次前缀查询的通用整数除法。
    int row = scale_ == 1 ? z-z_ : floorDiv(z-z_,scale_);
    if (row < 0 || row >= height_)
        return 0;
    int lo = std::max(0,scale_ == 1 ? left-x_ : floorDiv(left-x_+scale_-1,scale_));
    int hi = std::min(width_-1,scale_ == 1 ? right-x_ : floorDiv(right-x_,scale_));
    if (lo > hi)
        return 0;
    size_t at = (size_t(layer)*height_+row)*(width_+1);
    return int64_t(prefix[at+hi+1] - prefix[at+lo]) * scale_*scale_;
}

int64_t BiomeWindow::rectangle(const std::vector<uint64_t> &prefix, int left, int top,
                              int right, int bottom) const {
    if (prefix.empty())
        return 0;
    int l = std::clamp(floorDiv(left-x_+scale_-1, scale_), 0, width_);
    int r = std::clamp(floorDiv(right-x_, scale_)+1, 0, width_);
    int t = std::clamp(floorDiv(top-z_+scale_-1, scale_), 0, height_);
    int b = std::clamp(floorDiv(bottom-z_, scale_)+1, 0, height_);
    if (l >= r || t >= b)
        return 0;
    int stride = width_+1;
    return int64_t(prefix[size_t(b)*stride+r] - prefix[size_t(b)*stride+l] -
                   prefix[size_t(t)*stride+r] + prefix[size_t(t)*stride+l]) * scale_*scale_;
}

Hit BiomeWindow::evaluate(const Config &cfg, int x, int z, int playerY, bool upperBound,
                           GeometryCache *cache) const {
    if (!cache) {
        GeometryCache local;
        return evaluate(cfg,x,z,playerY,upperBound,&local);
    }
    Hit hit;
    hit.x = x;
    hit.z = z;
    hit.y = playerY;
    hit.radius = cfg.coverage == "game" ? 128 : cfg.innerRadius;
    hit.regionX = floorDiv(x, 256);
    hit.regionZ = floorDiv(z, 256);
    bool game = cfg.coverage == "game", square = cfg.coverage == "square";
    int radius = hit.radius;
    if (square) {
        hit.riverVolume = rectangle(riverIntegral_, x-radius, z-radius, x+radius, z+radius);
        hit.caveVolume = rectangle(caveIntegral_, x-radius, z-radius, x+radius, z+radius);
        for (int dz = -radius; dz <= radius; dz += scale_)
            hit.riverArea += rowSum(river_, 0, z+dz, x-radius, x+radius);
        return hit;
    }
    const auto *allowed = game ? &naturalRows(*cache,x,z,cfg.simulationDistanceChunks) : nullptr;
    int count = game && !upperBound ? layers_ : 1;
    for (int layer = 0; layer < count; ++layer) {
        int dy = game && !upperBound ? layerY(cfg,layer)-playerY : 0;
        const auto &rows = kernel(*cache,radius,dy,game && !upperBound,false);
        if (rows.empty())
            continue;
        // 稀疏窗口只访问实际采样行；保持原行序，避免遍历整球截面后逐行取模丢弃。
        int offset = z+rows.front().dz-z_;
        int first = floorDiv(offset+scale_-1,scale_)*scale_-offset;
        for (size_t i = size_t(first); i < rows.size(); i += scale_) {
            const auto &row = rows[i];
            int wz = z+row.dz;
            int left = x-row.outer, right = x+row.outer;
            if (game) {
                int index = floorDiv(wz,16)-floorDiv(z,16)+8;
                if (index < 0 || index >= 17)
                    continue;
                auto [lo,hi] = (*allowed)[index];
                if (lo > hi)
                    continue;
                int origin = floorDiv(x,16)*16;
                left = std::max(left,origin+lo);
                right = std::min(right,origin+hi);
            }
            const auto &rp = game && !upperBound ? river_ : riverProjected_;
            const auto &cp = game && !upperBound ? cave_ : caveProjected_;
            int yl = game && !upperBound ? layer : 0;
            auto sum = [&](const auto &data, int y0) {
                int64_t value = rowSum(data,y0,wz,left,right);
                if (row.inner >= 0)
                    value -= rowSum(data,y0,wz,std::max(left,x-row.inner),std::min(right,x+row.inner));
                return value;
            };
            const int64_t rowValue = sum(rp,yl);
            hit.riverVolume += rowValue;
            hit.caveVolume += sum(cp,yl);
            if (layer == 0)
                hit.riverArea += sum(river_,0);
        }
    }
    return hit;
}

Hit BiomeWindow::bestHeight(const Config &cfg, int x, int z, Control *control, GeometryCache *cache) const {
    (void)control;
    // 平台解析解（计划 §1.2）：validate() 已把 playerY 定在平台上，
    // 总分与挂机 Y 无关，删除原来的 141 步穷举。
    return evaluate(cfg,x,z,cfg.playerY,false,cache);
}

std::vector<Hit> selectHotspots(std::vector<Hit> hits, const Config &cfg) {
    std::sort(hits.begin(),hits.end(),[&](const Hit &a,const Hit &b) { return better(a,b,cfg); });
    std::map<std::pair<int,int>,std::vector<std::pair<int,int>>> bins;
    std::vector<Hit> result;
    int spacing = std::max(1,cfg.minDistance);
    for (const auto &hit : hits) {
        bool duplicate = false;
        int bx = floorDiv(hit.x,spacing), bz = floorDiv(hit.z,spacing);
        for (int dz = -1; dz <= 1 && !duplicate; ++dz)
            for (int dx = -1; dx <= 1 && !duplicate; ++dx) {
                auto it = bins.find({bx+dx,bz+dz});
                if (it == bins.end())
                    continue;
                for (auto [x,z] : it->second) {
                    int64_t d = int64_t(x-hit.x)*(x-hit.x) + int64_t(z-hit.z)*(z-hit.z);
                    if ((x == hit.x && z == hit.z) || d < int64_t(cfg.minDistance)*cfg.minDistance) {
                        duplicate = true;
                        break;
                    }
                }
            }
        if (!duplicate) {
            bins[{bx,bz}].push_back({hit.x,hit.z});
            result.push_back(hit);
            if (result.size() == size_t(cfg.top))
                break;
        }
    }
    return result;
}
}
